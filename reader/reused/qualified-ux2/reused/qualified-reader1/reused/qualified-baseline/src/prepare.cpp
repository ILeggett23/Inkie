#include "underlay.h"
#include "core/document/MuPdfDocumentEngine.h"
#include "core/document/PageFit.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <sys/stat.h>
#include <cmath>
#include <stdexcept>
namespace underlay {
Prepared prepare(const std::filesystem::path& application,const std::filesystem::path& data){
 auto fixture=inkbound::document::packagedPdf(application);if(!fixture)throw std::runtime_error("fixture-missing-or-size");
 auto source=readBounded(*fixture,1024*1024);if(digest(source).toHex()!=FixtureHash)throw std::runtime_error("fixture-identity");
 inkbound::document::MuPdfDocumentEngine engine;if(!engine.openDocument(*fixture))throw std::runtime_error(engine.lastError());
 auto bounds=engine.pageBounds(0);if(!bounds)throw std::runtime_error("page-bounds");auto scale=inkbound::document::fitPage(*bounds,Width,Height);if(!scale)throw std::runtime_error("page-fit");
 // The allowlisted synthetic page has MediaBox=CropBox600x800, Rotate0.
 if(bounds->x!=0||bounds->y!=0||bounds->width!=600||bounds->height!=800)throw std::runtime_error("fixture-page-geometry");
 auto page=engine.renderPage(0,*scale,true);if(!page)throw std::runtime_error(engine.lastError());
 if(page->pixelSize.width>Width||page->pixelSize.height>Height||page->pixelSize.width<=0||page->pixelSize.height<=0||page->strideBytes!=page->pixelSize.width*4||page->rgba.size()!=std::size_t(page->strideBytes)*page->pixelSize.height)throw std::runtime_error("rendered-buffer");
 const int ox=(Width-page->pixelSize.width)/2,oy=(Height-page->pixelSize.height)/2;
 Prepared p;p.background.assign(Pixels,0xffffffffu);p.ink.assign(Pixels,0xffffffffu);
 for(int y=0;y<page->pixelSize.height;++y)for(int x=0;x<page->pixelSize.width;++x)p.background[std::size_t(y+oy)*Stride+x+ox]=page->rgba[std::size_t(y)*page->strideBytes+x*4]<192?0xff000000u:0xffffffffu;
 auto r=page->renderedDocumentRegion;double tx=ox-r.x*(*scale),ty=oy-r.y*(*scale);
 p.metadata=QJsonObject{{"schema",1},{"generation",1},{"page",0},{"pdf",FixtureHash},{"width",Width},{"height",Height},{"stride",Stride*4},{"format","RGB32-LE"},{"raster","qualified-circle-bresenham-v1"},{"render","mupdf-1.28.3-gray-threshold192-v1"},{"rotation",0},{"bounds",QJsonArray{bounds->x,bounds->y,bounds->width,bounds->height}},{"crop",QJsonArray{0,0,600,800}},{"pageToSurface",QJsonArray{*scale,0,0,*scale,tx,ty}},{"surfaceToPage",QJsonArray{1/(*scale),0,0,1/(*scale),-tx/(*scale),-ty/(*scale)}}};
 p.metadata["backgroundSha256"]=QString::fromLatin1(digest(QByteArray(reinterpret_cast<const char*>(p.background.data()),Pixels*4)).toHex());p.binding=digest(QJsonDocument(p.metadata).toJson(QJsonDocument::Compact));
 privateDirectory(data);
 Journal store(data/"annotations.ink",p.binding);store.replay({p.ink,Width,Height,Stride});p.lastId=store.lastId();p.journalDigest=store.fingerprint();
 savePrepared(data/"prepared.bin",p);engine.closeDocument();if(digest(readBounded(*fixture,1024*1024)).toHex()!=FixtureHash)throw std::runtime_error("source-changed");return p;
}
}
