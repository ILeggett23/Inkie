#include "source_file.h"
#include "prepare_document_view.h"
#include "library.h"
#include "chrome.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
namespace reader {namespace {
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
QJsonArray rect(pdfview::Rect r){return {r.x,r.y,r.width,r.height};}
QJsonArray matrix(const pdfview::Affine& m){QJsonArray a;for(double v:m.value)a.append(v);return a;}
// Post-render verification must also reject special files without blocking.
void verifySource(const std::filesystem::path& source,const QString& expected){auto checked=reader::source::hashFile(source,library::MaxSourceBytes);need(checked.digest.toHex()==expected.toLatin1(),"pdf-view-source-changed");}
}
underlay::Prepared preparePdfView(const std::filesystem::path& application,const std::filesystem::path& root,int page,const viewstore::ViewSet& immutable,unsigned orientation){
 auto checked=viewstore::readSet(root,immutable.id);need(checked.receipt==immutable.receipt,"pdf-view-receipt-changed");need(orientation==0||orientation==pdfview::Clockwise||orientation==pdfview::Anticlockwise,"pdf-view-turn");
 auto storage=viewstore::turnedStoragePath(root,checked,page,orientation);auto entry=viewstore::pageEntry(checked,page);
 if(!entry&&!orientation)return underlay::prepare(application,storage,page,true,root);
 auto descriptor=library::descriptor(root);auto source=library::sourcePath(root,descriptor);const auto expected=checked.receipt["source_sha"].toString();const auto expectedBytes=expected.toStdString();
 // Backend hashes its bounded immutable memory snapshot before MuPDF parsing. Turned without a stored view: the page's
 // own bounds and the default view.
 const auto bounds=entry?entry->bounds:pdfview::pageBounds(source,page,expectedBytes);const auto view=entry?entry->view:pdfview::View{};
 auto rendered=pdfview::renderPdfRegion(source,page,bounds,view,expectedBytes,orientation);const auto& raster=rendered.raster;const auto& geometry=rendered.geometry;const int W=geometry.width,H=geometry.height;
 need(W>0&&H>0&&geometry.x>=0&&geometry.y>=0&&geometry.x+W<=underlay::Width&&geometry.y+H<=underlay::Height&&raster.pixelSize.width==W&&raster.pixelSize.height==H&&raster.strideBytes==W*4&&raster.rgba.size()==std::size_t(W)*H*4&&(orientation||(W==pdfview::Width&&H==pdfview::Height&&!geometry.x&&!geometry.y)),"pdf-view-raster-contract");
 underlay::Prepared p;p.background.assign(underlay::Pixels,0xffffffffu);p.ink.assign(underlay::Pixels,0xffffffffu);
 for(int y=0;y<H;++y)for(int x=0;x<W;++x){auto n=(std::size_t(y)*W+x)*4;need(raster.rgba[n+3]==255,"pdf-view-raster-alpha");p.background[std::size_t(y+geometry.y)*underlay::Stride+x+geometry.x]=0xff000000u|(std::uint32_t(raster.rgba[n])<<16)|(std::uint32_t(raster.rgba[n+1])<<8)|raster.rgba[n+2];}
 int total=checked.receipt["base_pages"].toInt();const auto settings=orientation?viewstore::viewSettings(checked,page):checked.receipt["page_views"].toObject()[QString::number(page)].toObject()["view"].toObject(); /* portrait: the stored bytes, unchanged */
 p.metadata={{"schema",3},{"generation",1},{"page",page},{"pages",total},{"pdf",expected},{"document",checked.receipt["document"]},{"documentFormat","pdf"},{"layout",checked.receipt["base_layout"]},{"width",underlay::Width},{"height",underlay::Height},{"stride",underlay::Stride*4},{"format","RGB32-LE"},{"raster","qualified-circle-bresenham-v1"},{"render","reader-pdf-view-mupdf1.28.3-rgb32-v1"},{"view_id",orientation?viewstore::turnedConfigId(checked,page,rect(geometry.sourceBounds),orientation):viewstore::pageConfigId(checked,page)},{"view_settings",settings},{"rotation",int(geometry.view.rotation)},{"bounds",rect(geometry.sourceBounds)},{"crop",rect(geometry.crop)},{"pageToSurface",matrix(geometry.forward)},{"surfaceToPage",matrix(geometry.inverse)}};
 if(orientation)p.metadata["orientation"]=int(orientation);else chrome({p.background,underlay::Width,underlay::Height,underlay::Stride},page,total); /* turned: the shell paints the landscape footer */
 p.metadata["backgroundSha256"]=QString::fromLatin1(underlay::digest(QByteArray(reinterpret_cast<const char*>(p.background.data()),underlay::Pixels*4)).toHex());p.binding=underlay::digest(QJsonDocument(p.metadata).toJson(QJsonDocument::Compact));
 verifySource(source,expected);underlay::privateDirectory(storage);underlay::Journal journal(storage/"annotations.ink",p.binding);journal.replay({p.ink,underlay::Width,underlay::Height,underlay::Stride});p.lastId=journal.lastId();p.journalDigest=journal.fingerprint();underlay::savePrepared(storage/"prepared.bin",p);return p;
}
}
