#include "pdf_viewport.h"
#if !defined(INKBOUND_HAS_MUPDF)
#error "pdf_viewport_backend requires the explicitly configured pinned MuPDF backend"
#endif
#include <cmath>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}
namespace reader::pdfview {namespace {
void need(bool b,Error code,const char* reason){if(!b)throw Failure(code,reason);}
std::vector<unsigned char> readSource(const std::filesystem::path& p){struct FD{int fd;~FD(){if(fd>=0)close(fd);}} file{open(p.c_str(),O_RDONLY|O_NOFOLLOW|O_NONBLOCK|O_CLOEXEC)};need(file.fd>=0,Error::SourceUnavailable,"pdf-source-open");struct stat st{};need(!fstat(file.fd,&st)&&S_ISREG(st.st_mode)&&st.st_size>0,Error::SourceUnavailable,"pdf-source-type");need(std::uint64_t(st.st_size)<=MaxSourceBytes,Error::Capacity,"pdf-source-size");std::vector<unsigned char> bytes;try{bytes.resize(std::size_t(st.st_size));}catch(const std::bad_alloc&){throw Failure(Error::Capacity,"pdf-source-allocation");}std::size_t at=0;while(at<bytes.size()){auto n=read(file.fd,bytes.data()+at,bytes.size()-at);if(n<0&&errno==EINTR)continue;need(n>0,Error::SourceUnavailable,"pdf-source-short-read");at+=std::size_t(n);}unsigned char extra{};need(read(file.fd,&extra,1)==0,Error::SourceUnavailable,"pdf-source-size-changed");return bytes;}
bool sameBounds(Rect a,Rect b){return std::abs(a.x-b.x)<=1e-5&&std::abs(a.y-b.y)<=1e-5&&std::abs(a.width-b.width)<=1e-5&&std::abs(a.height-b.height)<=1e-5;}
}
RenderedViewport renderPdfRegion(const std::filesystem::path& path,int index,Rect bounds,const View& view,std::string_view expectedSourceSha256){
 auto geometry=plan(bounds,view);need(index>=0&&index<1000,Error::InvalidPage,"pdf-page-index");
 auto bytes=readSource(path);
 if(!expectedSourceSha256.empty()){
  need(expectedSourceSha256.size()==64,Error::SourceMismatch,"pdf-source-sha-schema");for(char c:expectedSourceSha256)need((c>='0'&&c<='9')||(c>='a'&&c<='f'),Error::SourceMismatch,"pdf-source-sha-schema");
  fz_sha256 state;unsigned char digest[32];fz_sha256_init(&state);fz_sha256_update(&state,bytes.data(),bytes.size());fz_sha256_final(&state,digest);
  constexpr char hex[]="0123456789abcdef";for(unsigned n=0;n<32;++n)need(expectedSourceSha256[n*2]==hex[digest[n]>>4]&&expectedSourceSha256[n*2+1]==hex[digest[n]&15],Error::SourceMismatch,"pdf-source-sha-mismatch");
 }
 RenderedViewport output;output.geometry=geometry;output.raster.pixelSize={Width,Height};output.raster.strideBytes=Width*4;output.raster.renderedDocumentRegion=geometry.visibleSource;
 try{output.raster.rgba.resize(RasterBytes);}catch(const std::bad_alloc&){throw Failure(Error::Capacity,"pdf-viewport-allocation");}
 fz_context* ctx=fz_new_context(nullptr,nullptr,32*1024*1024);need(ctx,Error::Capacity,"pdf-context-allocation");fz_document* doc=nullptr;fz_stream* stream=nullptr;fz_page* page=nullptr;fz_pixmap* pixmap=nullptr;fz_device* device=nullptr;fz_path* clipping=nullptr;bool failed=false;Error error=Error::RenderFailed;
 fz_var(doc);fz_var(stream);fz_var(page);fz_var(pixmap);fz_var(device);fz_var(clipping);fz_var(failed);fz_var(error);
 // All allocated C++ objects precede the MuPDF longjmp region. The pixmap is
 // exactly the viewport, irrespective of full zoomed page dimensions.
 fz_try(ctx){fz_register_document_handlers(ctx);stream=fz_open_memory(ctx,bytes.data(),bytes.size());doc=fz_open_document_with_stream(ctx,"pdf",stream);
  if(!pdf_specifics(ctx,doc)||fz_needs_password(ctx,doc))fz_throw(ctx,FZ_ERROR_ARGUMENT,"PDF source required");int count=fz_count_pages(ctx,doc);if(index>=count){error=Error::InvalidPage;fz_throw(ctx,FZ_ERROR_ARGUMENT,"PDF page outside source");}
  page=fz_load_page(ctx,doc,index);auto actual=fz_bound_page(ctx,page);Rect actualBounds{actual.x0,actual.y0,double(actual.x1)-actual.x0,double(actual.y1)-actual.y0};if(!sameBounds(bounds,actualBounds)){error=Error::SourceMismatch;fz_throw(ctx,FZ_ERROR_ARGUMENT,"PDF source geometry changed");}
  auto m=geometry.forward.value;fz_matrix matrix{float(m[0]),float(m[1]),float(m[2]),float(m[3]),float(m[4]),float(m[5])};pixmap=fz_new_pixmap_with_bbox(ctx,fz_device_rgb(ctx),{0,0,Width,Height},nullptr,0);fz_clear_pixmap_with_value(ctx,pixmap,255);device=fz_new_draw_device(ctx,fz_identity,pixmap);
  clipping=fz_new_path(ctx);auto crop=geometry.crop;fz_rectto(ctx,clipping,float(crop.x),float(crop.y),float(crop.x+crop.width),float(crop.y+crop.height));fz_clip_path(ctx,device,clipping,0,matrix,fz_infinite_rect);fz_run_page(ctx,page,device,matrix,nullptr);fz_pop_clip(ctx,device);fz_close_device(ctx,device);
  if(pixmap->w!=Width||pixmap->h!=Height||pixmap->n!=3||pixmap->alpha||pixmap->stride<Width*3||pixmap->stride>Width*4||!pixmap->samples)fz_throw(ctx,FZ_ERROR_LIMIT,"PDF viewport pixel contract");
  for(int row=0;row<Height;++row){const auto* from=pixmap->samples+std::size_t(row)*pixmap->stride;auto* to=output.raster.rgba.data()+std::size_t(row)*Width*4;for(int col=0;col<Width;++col){to[col*4]=from[col*3];to[col*4+1]=from[col*3+1];to[col*4+2]=from[col*3+2];to[col*4+3]=255;}}
 }fz_always(ctx){fz_drop_device(ctx,device);fz_drop_path(ctx,clipping);fz_drop_pixmap(ctx,pixmap);fz_drop_page(ctx,page);fz_drop_document(ctx,doc);fz_drop_stream(ctx,stream);}fz_catch(ctx){failed=true;}
 fz_drop_context(ctx);if(failed)throw Failure(error,"pdf-region-render-failed");return output;
}
}
