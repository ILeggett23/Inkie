#include "pdf_viewport.h"
#include "core/document/MuPdfDocumentEngine.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <algorithm>
#include <cstdlib>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
using namespace reader::pdfview;
static void check(bool v,const char* why){if(!v)throw std::runtime_error(why);}
static bool near(double a,double b){return std::abs(a-b)<1e-7;}
template<class F>void rejects(Error expected,F f){bool rejected=false;try{f();}catch(const Failure& e){check(e.code()==expected,"wrong-rejection-code");rejected=true;}check(rejected,"rejection-required");}
static bool redAt(const RenderedViewport& rendered,Point source){auto p=rendered.geometry.forward.map(source);int x=int(std::floor(p.x)),y=int(std::floor(p.y));check(x>=0&&x<Width&&y>=0&&y<Height,"sample-within-viewport");auto offset=(std::size_t(y)*Width+x)*4;auto& pixels=rendered.raster.rgba;return pixels[offset]>210&&pixels[offset+1]<70&&pixels[offset+2]<70&&pixels[offset+3]==255;}
static void bounded(const RenderedViewport& rendered){check(rendered.raster.pixelSize.width==Width&&rendered.raster.pixelSize.height==Height&&rendered.raster.strideBytes==Width*4&&rendered.raster.rgba.size()==RasterBytes,"fixed-viewport-raster-resource-bound");}
static void fifoRefused(){
 char pattern[]="/tmp/inkbound-pdf-fifo-XXXXXX";char* directory=mkdtemp(pattern);check(directory,"fifo-test-directory");auto fifo=std::filesystem::path(directory)/"source.pdf";
 if(mkfifo(fifo.c_str(),0600)){rmdir(directory);throw std::runtime_error("fifo-test-create");}
 pid_t child=fork();if(child==0){alarm(3);try{rejects(Error::SourceUnavailable,[&]{renderPdfRegion(fifo,0,{0,0,612,792},{});});_exit(0);}catch(...){_exit(2);}}
 int status=0;pid_t waited=child>0?waitpid(child,&status,0):-1;unlink(fifo.c_str());rmdir(directory);
 check(child>0&&waited==child&&WIFEXITED(status)&&WEXITSTATUS(status)==0,"fifo-source-rejected-before-blocking-open");
}
int main(int argc,char**argv){if(argc!=2)return 64;try{
 fifoRefused();
 Rect source{40,70,612,792},crop{90,120,400,500};unsigned plans=0;
 for(unsigned rotation:{0u,90u,180u,270u})for(double zoom:{1.,2.,4.})for(bool cropped:{false,true}){
  View view{zoom,rotation,cropped?std::optional<Rect>(crop):std::nullopt,{}};auto first=plan(source,view);
  for(int edge:{-1,0,1}){view.pan={edge*first.maxPanX,edge*first.maxPanY};auto p=plan(source,view);for(auto point:std::array<Point,3>{{{source.x,source.y},{source.x+source.width,source.y+source.height},{217.25,389.75}}}){auto back=p.inverse.map(p.forward.map(point));check(near(back.x,point.x)&&near(back.y,point.y),"nonzero-origin-affine-roundtrip");}
   check(p.visibleSource.x>=p.crop.x-1e-7&&p.visibleSource.y>=p.crop.y-1e-7&&p.visibleSource.x+p.visibleSource.width<=p.crop.x+p.crop.width+1e-7&&p.visibleSource.y+p.visibleSource.height<=p.crop.y+p.crop.height+1e-7,"visible-source-within-crop");check(p.rasterBytes==RasterBytes&&p.width==Width&&p.height==Height,"planned-resource-bound");if(zoom==1)check(near(p.visibleSource.width,p.crop.width)&&near(p.visibleSource.height,p.crop.height),"fit-shows-whole-crop");++plans;
  }
 }
 rejects(Error::InvalidBounds,[&]{plan({0,0,0,100},{});});rejects(Error::InvalidBounds,[&]{plan({0,0,std::numeric_limits<double>::infinity(),100},{});});rejects(Error::Capacity,[&]{plan({0,0,200001,100},{});});
 for(double zoom:{0.9,4.1,std::numeric_limits<double>::quiet_NaN()})rejects(Error::InvalidView,[&]{View v;v.zoom=zoom;plan(source,v);});rejects(Error::InvalidView,[&]{View v;v.rotation=45;plan(source,v);});
 rejects(Error::CropOutsideSource,[&]{View v;v.crop=Rect{0,0,100,100};plan(source,v);});rejects(Error::Capacity,[&]{View v;v.crop=Rect{40,70,.01,.01};plan(source,v);});rejects(Error::PanOutsideRange,[&]{View v;v.pan={1,0};plan(source,v);});
 inkbound::document::MuPdfDocumentEngine engine;check(engine.openDocument(argv[1]),"actual-authored-pdf-open");auto actual=engine.pageBounds(0);check(actual.has_value()&&near(actual->width,612)&&near(actual->height,792),"authored-source-bounds");Point marker{86,230};unsigned rendered=0;
 for(unsigned rotation:{0u,90u,180u,270u}){View view;view.rotation=rotation;auto output=renderPdfRegion(argv[1],0,*actual,view);bounded(output);check(redAt(output,marker),"quarter-turn-marker-raster-location");++rendered;}
 View zoom;zoom.zoom=4;auto initial=plan(*actual,zoom);auto mapped=initial.forward.map(marker);zoom.pan={Width/2.-mapped.x,Height/2.-mapped.y};auto high=renderPdfRegion(argv[1],0,*actual,zoom);bounded(high);check(redAt(high,marker),"four-times-zoom-panned-marker");check(high.geometry.visibleSource.width<actual->width/2&&high.geometry.visibleSource.height<actual->height/2,"four-times-render-is-visible-region-not-full-page");++rendered;
 View included;included.crop=Rect{40,180,140,120};auto close=renderPdfRegion(argv[1],0,*actual,included);bounded(close);check(redAt(close,marker),"cropped-marker-location");++rendered;
 View excluded;excluded.crop=Rect{40,270,140,120};auto clipped=renderPdfRegion(argv[1],0,*actual,excluded);bounded(clipped);unsigned red=0;for(std::size_t i=0;i<clipped.raster.rgba.size();i+=4)red+=clipped.raster.rgba[i]>210&&clipped.raster.rgba[i+1]<70&&clipped.raster.rgba[i+2]<70;check(red==0,"source-outside-crop-cannot-leak-into-white-margins");++rendered;
 auto wrong=*actual;wrong.x+=1;rejects(Error::SourceMismatch,[&]{renderPdfRegion(argv[1],0,wrong,{});});rejects(Error::InvalidPage,[&]{renderPdfRegion(argv[1],999,*actual,{});});
 std::cout<<"PDF_VIEWPORT_TESTS_PASS affine_plans="<<plans<<" actual_pdf_renders="<<rendered<<" fixed_raster="<<Width<<"x"<<Height<<" max_output_bytes="<<RasterBytes<<" fifo_refusal=true helper_only=true UI_integration=NOT_RUN physical=NOT_RUN\n";return 0;
 }catch(const std::exception& e){std::cerr<<"PDF_VIEWPORT_TEST_FAIL "<<e.what()<<'\n';return 1;}}
