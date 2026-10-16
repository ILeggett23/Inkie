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
underlay::Prepared preparePdfView(const std::filesystem::path& application,const std::filesystem::path& root,int page,const viewstore::ViewSet& immutable){
 auto checked=viewstore::readSet(root,immutable.id);need(checked.receipt==immutable.receipt,"pdf-view-receipt-changed");auto storage=viewstore::storagePath(root,checked,page);auto entry=viewstore::pageEntry(checked,page);
 if(!entry)return underlay::prepare(application,storage,page,true,root);
 auto descriptor=library::descriptor(root);auto source=library::sourcePath(root,descriptor);const auto expected=checked.receipt["source_sha"].toString();const auto expectedBytes=expected.toStdString();
 // Backend hashes its bounded immutable memory snapshot before MuPDF parsing.
 auto rendered=pdfview::renderPdfRegion(source,page,entry->bounds,entry->view,expectedBytes);const auto& raster=rendered.raster;const auto& geometry=rendered.geometry;
 need(raster.pixelSize.width==pdfview::Width&&raster.pixelSize.height==pdfview::Height&&raster.strideBytes==pdfview::Width*4&&raster.rgba.size()==pdfview::RasterBytes,"pdf-view-raster-contract");
 underlay::Prepared p;p.background.assign(underlay::Pixels,0xffffffffu);p.ink.assign(underlay::Pixels,0xffffffffu);
 for(int y=0;y<pdfview::Height;++y)for(int x=0;x<pdfview::Width;++x){auto n=(std::size_t(y)*pdfview::Width+x)*4;need(raster.rgba[n+3]==255,"pdf-view-raster-alpha");p.background[std::size_t(y)*underlay::Stride+x]=0xff000000u|(std::uint32_t(raster.rgba[n])<<16)|(std::uint32_t(raster.rgba[n+1])<<8)|raster.rgba[n+2];}
 int total=checked.receipt["base_pages"].toInt();auto storedEntry=checked.receipt["page_views"].toObject()[QString::number(page)].toObject();
 p.metadata={{"schema",3},{"generation",1},{"page",page},{"pages",total},{"pdf",expected},{"document",checked.receipt["document"]},{"documentFormat","pdf"},{"layout",checked.receipt["base_layout"]},{"width",underlay::Width},{"height",underlay::Height},{"stride",underlay::Stride*4},{"format","RGB32-LE"},{"raster","qualified-circle-bresenham-v1"},{"render","reader-pdf-view-mupdf1.28.3-rgb32-v1"},{"view_id",viewstore::pageConfigId(checked,page)},{"view_settings",storedEntry["view"]},{"rotation",int(geometry.view.rotation)},{"bounds",rect(geometry.sourceBounds)},{"crop",rect(geometry.crop)},{"pageToSurface",matrix(geometry.forward)},{"surfaceToPage",matrix(geometry.inverse)}};
 chrome({p.background,underlay::Width,underlay::Height,underlay::Stride},page,total);
 p.metadata["backgroundSha256"]=QString::fromLatin1(underlay::digest(QByteArray(reinterpret_cast<const char*>(p.background.data()),underlay::Pixels*4)).toHex());p.binding=underlay::digest(QJsonDocument(p.metadata).toJson(QJsonDocument::Compact));
 verifySource(source,expected);underlay::privateDirectory(storage);underlay::Journal journal(storage/"annotations.ink",p.binding);journal.replay({p.ink,underlay::Width,underlay::Height,underlay::Stride});p.lastId=journal.lastId();p.journalDigest=journal.fingerprint();underlay::savePrepared(storage/"prepared.bin",p);return p;
}
}
