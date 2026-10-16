#include "source_file.h"
// SPDX-License-Identifier: AGPL-3.0-only
#include "page_thumbnails_render.h"
#include "document_validation.h"
#include "document_view_store.h"
#include "epub_reflow.h"
#include "epub_view_store.h"
#include "library.h"
#include "pdf_policy.h"
#include "underlay.h"
#include "core/document/MuPdfDocumentEngine.h"
#include <algorithm>
#include <stdexcept>
namespace thumbs {namespace {
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
}
std::vector<Image> renderBatch(const std::filesystem::path& data,const QString& view,int first,int count){
 need(data.is_absolute()&&first>=0&&count>=1&&count<=MaxBatch,"thumbnail-request");
 const auto descriptor=library::descriptor(data);need(!descriptor.isEmpty(),"thumbnail-document");const auto format=descriptor["format"].toString();
 const auto source=library::sourcePath(data,descriptor);std::vector<Image> out;
 if(format=="epub"){
  // The same receipt and parameters the page helper renders from; a mismatch renders nothing.
  const auto set=reader::epubviews::readSet(data,view);const auto layout=set.receipt["layout"].toObject();
  epub::ReflowDocument doc(source,set.receipt["document"].toString(),set.receipt["source_sha"].toString(),{layout["width"].toInt(),layout["height"].toInt(),layout["fontSize"].toInt(),layout["userCss"].toString()});
  need(doc.pageCount()==set.receipt["pages"].toInt()&&doc.layoutDescriptor()==layout&&(set.id.isEmpty()||doc.layoutId()==set.receipt["layout_id"]),"thumbnail-epub-layout");
  need(first<doc.pageCount(),"thumbnail-range");count=std::min(count,doc.pageCount()-first);
  for(int p=first;p<first+count;++p){auto r=doc.renderPage(p);need(r.rgb32.size()>=std::size_t(r.stride)*r.height,"thumbnail-epub-raster");out.push_back(fromRgb32(p,r.rgb32.data(),r.width,r.height,r.stride));}
  return out;}
 need(format=="pdf","thumbnail-format");
 need(reader::source::hashFile(source,library::MaxSourceBytes).digest.toHex()==descriptor["sha256"].toString().toLatin1(),"thumbnail-source-changed");
 library::validateDocumentContainer(source,"pdf");library::validatePdfPolicy(source);
 inkbound::document::MuPdfDocumentEngine engine;need(engine.openDocument(source),"thumbnail-parser-rejected");const int total=int(engine.locationCount());
 need(total>=1&&total<=1000,"thumbnail-page-count");if(!view.isEmpty())need(reader::viewstore::readSet(data,view).receipt["base_pages"].toInt()==total,"thumbnail-view-pages");
 need(first<total,"thumbnail-range");count=std::min(count,total-first);
 for(int p=first;p<first+count;++p){auto bounds=engine.pageBounds(p);need(bounds&&bounds->width>0&&bounds->height>0,"thumbnail-page-bounds");
  // Render a little larger than the target so the box filter, not MuPDF's rounding, sets the final size.
  const double scale=std::min(2.0*MaxWidth/bounds->width,2.0*MaxHeight/bounds->height);auto pixels=engine.renderPage(p,scale,true);need(pixels.has_value(),"thumbnail-render-rejected");
  out.push_back(fromRgba(p,pixels->rgba.data(),pixels->pixelSize.width,pixels->pixelSize.height,pixels->strideBytes));}
 engine.closeDocument();return out;
}
}
