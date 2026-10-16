#include "underlay.h"
#include "library.h"
#include "source_file.h"
#include "chrome.h"
#include "core/document/MuPdfDocumentEngine.h"
#include "core/document/PageFit.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <sys/stat.h>
#include <cmath>
#include <stdexcept>
#include <cstdio>
#include <memory>
namespace underlay {
namespace {
// D2 page turns (stock-style persistent renderer): a page-server helper keeps the verified source and its open,
// laid-out MuPDF engine between pages, so only the first page pays for hashing and whole-book EPUB layout. The
// file identity (device, inode, size, mtime, ctime) must be unchanged; library sources are replaced atomically,
// so any change is a new identity and is read and verified again. A one-shot helper behaves exactly as before.
struct FileIdentity {dev_t dev{};ino_t ino{};off_t size{};long long ms{},mn{},cs{},cn{};bool operator==(const FileIdentity&)const=default;};
bool fileIdentity(const std::filesystem::path& p,FileIdentity& i){struct stat st{};if(::stat(p.c_str(),&st)||!S_ISREG(st.st_mode))return false;i={st.st_dev,st.st_ino,st.st_size,st.st_mtim.tv_sec,st.st_mtim.tv_nsec,st.st_ctim.tv_sec,st.st_ctim.tv_nsec};return true;}
struct OpenSource {std::filesystem::path path;QString hash;FileIdentity id;std::unique_ptr<inkbound::document::MuPdfDocumentEngine> engine;};
std::unique_ptr<OpenSource> openSource; /* the helper is single-threaded */
}
Prepared prepare(const std::filesystem::path& application,const std::filesystem::path& data,int index,bool cache,const std::filesystem::path& documentData){
 auto descriptor=documentData.empty()?QJsonObject{}:library::descriptor(documentData);bool imported=!descriptor.isEmpty();
 auto fixture=imported?std::optional<std::filesystem::path>(library::sourcePath(documentData,descriptor)):inkbound::document::packagedPdf(application);if(!fixture)throw std::runtime_error("fixture-missing-or-size");
 auto sourceHash=imported?descriptor["sha256"].toString():QString(FixtureHash);FileIdentity before{};const bool known=fileIdentity(*fixture,before);
 if(!(openSource&&known&&openSource->path==*fixture&&openSource->hash==sourceHash&&openSource->id==before)){openSource.reset();
  auto source=reader::source::hashFile(*fixture,library::MaxSourceBytes);if(source.digest.toHex()!=sourceHash.toLatin1())throw std::runtime_error("fixture-identity");
  auto opened=std::make_unique<inkbound::document::MuPdfDocumentEngine>();if(!opened->openDocument(*fixture))throw std::runtime_error(opened->lastError());
  FileIdentity after{};if(!known||!fileIdentity(*fixture,after)||after!=before)throw std::runtime_error("source-changed");
  openSource=std::make_unique<OpenSource>(OpenSource{*fixture,sourceHash,before,std::move(opened)});}
 auto& engine=*openSource->engine;
 int total=int(engine.locationCount());if(index<0||index>=total||total>1000)throw std::runtime_error("page-index");
 auto bounds=engine.pageBounds(index);if(!bounds)throw std::runtime_error("page-bounds");auto scale=inkbound::document::fitPage(*bounds,Width,Height-100);if(!scale)throw std::runtime_error("page-fit");
 // The allowlisted synthetic page has MediaBox=CropBox600x800, Rotate0.
 if(!imported&&(bounds->x!=0||bounds->y!=0||((index==0)&&(bounds->width!=600||bounds->height!=800))||((index==1)&&(bounds->width!=800||bounds->height!=600))))throw std::runtime_error("fixture-page-geometry");
 const auto renderStart=spike::monotonic();auto page=engine.renderPage(index,*scale,!imported);const auto renderEnd=spike::monotonic();if(!page)throw std::runtime_error(engine.lastError());
 std::fprintf(stdout,"PAGE_RENDER page=%d start_ns=%llu end_ns=%llu thread=helper_not_live\n",index,(unsigned long long)renderStart,(unsigned long long)renderEnd);std::fflush(stdout);
 if(page->pixelSize.width>Width||page->pixelSize.height>Height||page->pixelSize.width<=0||page->pixelSize.height<=0||page->strideBytes!=page->pixelSize.width*4||page->rgba.size()!=std::size_t(page->strideBytes)*page->pixelSize.height)throw std::runtime_error("rendered-buffer");
 const int ox=(Width-page->pixelSize.width)/2,oy=(Height-100-page->pixelSize.height)/2;
 Prepared p;p.background.assign(Pixels,0xffffffffu);p.ink.assign(Pixels,0xffffffffu);
 for(int y=0;y<page->pixelSize.height;++y)for(int x=0;x<page->pixelSize.width;++x){auto at=std::size_t(y)*page->strideBytes+x*4;const auto* c=page->rgba.data()+at;if(c[3]!=255)throw std::runtime_error("rendered-alpha");p.background[std::size_t(y+oy)*Stride+x+ox]=imported?(0xff000000u|(std::uint32_t(c[0])<<16)|(std::uint32_t(c[1])<<8)|c[2]):(c[0]<192?0xff000000u:0xffffffffu);}
 auto r=page->renderedDocumentRegion;double tx=ox-r.x*(*scale),ty=oy-r.y*(*scale);
 p.metadata=QJsonObject{{"schema",1},{"generation",1},{"page",index},{"pages",total},{"pdf",FixtureHash},{"width",Width},{"height",Height},{"stride",Stride*4},{"format","RGB32-LE"},{"raster","qualified-circle-bresenham-v1"},{"render","reader-fit-footer100-mupdf1.28.3-threshold192-v1"},{"rotation",0},{"bounds",QJsonArray{bounds->x,bounds->y,bounds->width,bounds->height}},{"crop",QJsonArray{bounds->x,bounds->y,bounds->width,bounds->height}},{"pageToSurface",QJsonArray{*scale,0,0,*scale,tx,ty}},{"surfaceToPage",QJsonArray{1/(*scale),0,0,1/(*scale),-tx/(*scale),-ty/(*scale)}}};
 if(imported){p.metadata["schema"]=2;p.metadata["pdf"]=sourceHash;p.metadata["document"]=descriptor["id"];p.metadata["documentFormat"]=descriptor["format"];p.metadata["layout"]=descriptor["layout"];if(engine.layoutIdentity()!=descriptor["layout"].toString().toStdString())throw std::runtime_error("document-layout-changed");}
 reader::chrome({p.background,Width,Height,Stride},index,total);
 p.metadata["backgroundSha256"]=QString::fromLatin1(digest(QByteArray(reinterpret_cast<const char*>(p.background.data()),Pixels*4)).toHex());if(imported){QJsonObject legacy;
  // A checked cached legacy receipt avoids a second render on subsequent visits. No ink/journal is rewritten.
  try{auto prior=loadPrepared(data/"prepared.bin");auto meta=prior.metadata.contains("legacyMeta")?prior.metadata["legacyMeta"].toObject():prior.metadata;auto expected=p.metadata;expected["backgroundSha256"]=meta["backgroundSha256"];if(meta==expected)legacy=meta;}catch(...){}
  if(legacy.isEmpty()){auto gray=engine.renderPage(index,*scale,true);if(!gray||gray->pixelSize.width!=page->pixelSize.width||gray->pixelSize.height!=page->pixelSize.height)throw std::runtime_error("legacy-color-geometry");std::vector<std::uint32_t> original(Pixels,0xffffffffu);for(int y=0;y<gray->pixelSize.height;++y)for(int x=0;x<gray->pixelSize.width;++x)original[std::size_t(y+oy)*Stride+x+ox]=gray->rgba[std::size_t(y)*gray->strideBytes+x*4]<192?0xff000000u:0xffffffffu;reader::chrome({original,Width,Height,Stride},index,total);legacy=p.metadata;legacy["backgroundSha256"]=QString::fromLatin1(digest(QByteArray(reinterpret_cast<const char*>(original.data()),Pixels*4)).toHex());}
  p.metadata["legacyMeta"]=legacy;p.metadata["render"]="reader-fit-footer100-mupdf1.28.3-rgb32-v2";
 }
 p.binding=preparedBinding(p.metadata);
 privateDirectory(data);
 Journal store(data/"annotations.ink",p.binding);store.replay({p.ink,Width,Height,Stride});p.lastId=store.lastId();p.journalDigest=store.fingerprint();
 if(cache)savePrepared(data/"prepared.bin",p);{FileIdentity now{};if(!fileIdentity(*fixture,now)||now!=openSource->id){openSource.reset();throw std::runtime_error("source-changed");}}return p;
}
}
