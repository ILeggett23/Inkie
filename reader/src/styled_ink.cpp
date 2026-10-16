#include "styled_ink.h"
#include "native_ink.h"
#include "highlight_blocks.h"
#include "annotation_geometry.h"
#include "e2_provisional_v2.h"
#include <chrono>
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <malloc.h>
#include <cstdio>
#include <string_view>
#include <vector>
namespace styled {
namespace {
constexpr const char* Tools[]{"ballpoint","fineliner","pencil","mechanical-pencil","paintbrush","calligraphy","marker","shader","highlighter"};
constexpr const char* Labels[]{"BALLPOINT","FINELINER","PENCIL","MECH PENCIL","PAINTBRUSH","CALLIGRAPHY","MARKER","SHADER","HIGHLIGHTER"};
constexpr const char* Colors[]{"black","pink","green","orange","yellow","purple","blue","red"};
constexpr const char* Widths[]{"thin","medium","thick"};
constexpr std::uint32_t RGB[]{0x1c2028,0xdf6c9c,0x55905a,0xd78b39,0xe7c84c,0x9467ac,0x567db8,0xbd5250};
constexpr double Diameters[9][3]={{1.1,2,3.2},{0.8,1.5,2.4},{1,2,3.8},{0.55,0.9,1.5},{1.7,3.5,7},{1.8,3.8,6.2},{3,6,10},{7,13,22},{7,12,19}};
bool validStyle(Style s)noexcept{return unsigned(s.tool)<9&&unsigned(s.width)<3&&s.color<8&&(s.tool!=Tool::Highlighter||s.color>0);}
// Ink on paper, as on the Paper Pro: the ink layer multiplies the page instead of covering it, so text under a
// highlighter (or any translucent ink) stays dark; over white paper it is identical to plain "over" blending.
std::uint32_t multiplyInk(std::uint32_t paper,std::uint32_t rgb,unsigned alpha)noexcept{
 if(!alpha)return paper;
 auto channel=[&](unsigned shift){const unsigned p=(paper>>shift)&255,c=(rgb>>shift)&255,tint=255*255-alpha*(255-c);return ((p*tint+32512)/65025)<<shift;};
 return (paper&0xff000000u)|channel(16)|channel(8)|channel(0);
}
std::uint32_t over(std::uint32_t background,std::uint32_t rgb,unsigned alpha)noexcept{
 unsigned a=background>>24,remain=a*(255-alpha),out=alpha*255+remain;if(!out)return 0;
 auto channel=[&](unsigned shift){return ((((rgb>>shift)&255)*alpha*255+((background>>shift)&255)*remain+out/2)/out)<<shift;};
 return ((out+127)/255)<<24|channel(16)|channel(8)|channel(0);
}
spike::Rect clip(spike::Rect r,int w,int h)noexcept{return {std::clamp(r.x0,0,w),std::clamp(r.y0,0,h),std::clamp(r.x1,0,w),std::clamp(r.y1,0,h)};}
Matrix parseMatrix(const QJsonArray& values){product::require(values.size()==6,"styled-transform-required");Matrix m;for(unsigned i=0;i<6;++i){product::require(values[int(i)].isDouble(),"styled-transform-type");m.value[i]=values[int(i)].toDouble();}product::require(m.valid(),"styled-transform-invalid");return m;}
QJsonArray jsonMatrix(const Matrix& m){QJsonArray a;for(auto n:m.value)a.append(n);return a;}
QJsonObject document(const QJsonObject& snapshot,const QJsonObject& page){auto docs=snapshot["documents"].toObject();auto doc=docs[page["document"].toString()].toObject();product::require(!doc.isEmpty()&&doc["revision"]==page["revision"]&&!doc["trashed"].toBool(),"styled-document-unavailable");return doc;}
bool matches(const QJsonObject& stroke,const QJsonObject& page){const auto a=stroke.value("anchor").toObject();bool source=a.value("document")==page.value("document")&&a.value("revision")==page.value("revision")&&a.value("page")==page.value("page");if(!source)return false;if(a.value("format")=="pdf"&&page.value("format")=="pdf")return true;return a.value("pageId")==page.value("pageId")&&a.value("layoutId")==page.value("layoutId");}

double pointDistance(Coordinate p,Coordinate a,Coordinate b){double dx=b.x-a.x,dy=b.y-a.y,l=dx*dx+dy*dy,t=l?std::clamp(((p.x-a.x)*dx+(p.y-a.y)*dy)/l,0.0,1.0):0;return std::hypot(p.x-a.x-t*dx,p.y-a.y-t*dy);}
double segmentDistance(Coordinate a,Coordinate b,Coordinate c,Coordinate d){double ux=b.x-a.x,uy=b.y-a.y,vx=d.x-c.x,vy=d.y-c.y,den=ux*vy-uy*vx;if(std::abs(den)>1e-12){double wx=c.x-a.x,wy=c.y-a.y,t=(wx*vy-wy*vx)/den,u=(wx*uy-wy*ux)/den;if(t>=0&&t<=1&&u>=0&&u<=1)return 0;}return std::min({pointDistance(a,c,d),pointDistance(b,c,d),pointDistance(c,a,b),pointDistance(d,a,b)});}
// D2 precise eraser: the parts of one stroke (document points) farther than radius from the eraser path, as runs of
// at least two points. Segments that pass near the path are subdivided first, so a cut lands where the eraser crossed
// rather than at the stroke's nearest stored point. hit is false when nothing of the stroke is within reach.
struct EraseCut {bool hit{},overflow{};std::vector<QJsonArray> runs;};
struct Segment {Coordinate a,b;double x0,y0,x1,y1;};
// Only the eraser segments near this stroke are passed in (bounding boxes pre-filtered), and each test first rejects by
// box, so a long scribble over a dense page stays cheap. Past the work budget it reports overflow instead of failing.
EraseCut cutStroke(const QJsonArray& points,const std::vector<Segment>& path,double radius,std::uint64_t& work,std::uint64_t budget){
 EraseCut out;QJsonArray run;struct Overflow{};
 auto at=[](const QJsonArray& a){return Coordinate{a[0].toDouble(),a[1].toDouble()};};
 auto reach=[&](Coordinate a,Coordinate b){double ax0=std::min(a.x,b.x)-radius,ay0=std::min(a.y,b.y)-radius,ax1=std::max(a.x,b.x)+radius,ay1=std::max(a.y,b.y)+radius;
  for(const auto& s:path){if(++work>budget)throw Overflow{};if(s.x1<ax0||s.x0>ax1||s.y1<ay0||s.y0>ay1)continue;if(segmentDistance(a,b,s.a,s.b)<=radius)return true;}return false;};
 auto flush=[&]{if(run.size()>=2)out.runs.push_back(run);run=QJsonArray{};};
 auto sample=[&](const QJsonArray& a){auto c=at(a);if(reach(c,c)){out.hit=true;flush();return;}if(run.size()==8192){auto last=run.last();flush();run.append(last);}run.append(a);};
 const double step=std::max(0.5,radius*0.5);
 try{
  for(int n=0;n<points.size();++n){auto current=points[n].toArray();
   if(n){auto prior=points[n-1].toArray();Coordinate a=at(prior),b=at(current);double length=std::hypot(b.x-a.x,b.y-a.y);
    if(length>step&&reach(a,b)){int parts=std::min(64,int(std::ceil(length/step)));for(int k=1;k<parts;++k){double t=double(k)/parts;QJsonArray m{a.x+(b.x-a.x)*t,a.y+(b.y-a.y)*t};if(prior.size()==3&&current.size()==3)m.append(prior[2].toDouble()+(current[2].toDouble()-prior[2].toDouble())*t);sample(m);}}}
   sample(current);}
 }catch(const Overflow&){out.overflow=true;out.runs.clear();return out;}
 flush();return out;}

}
const char* toolId(Tool v)noexcept{return unsigned(v)<9?Tools[unsigned(v)]:"invalid";}const char* toolLabel(Tool v)noexcept{return unsigned(v)<9?Labels[unsigned(v)]:"INVALID";}
const char* colorId(unsigned v)noexcept{return v<8?Colors[v]:"invalid";}const char* widthId(Width v)noexcept{return unsigned(v)<3?Widths[unsigned(v)]:"invalid";}
std::uint32_t colorRgb(unsigned v)noexcept{return v<8?RGB[v]:0;}std::uint32_t inkRgb(Style s)noexcept{return s.exact?(s.rgb&0xffffffu):colorRgb(s.color);}double diameter(Style s)noexcept{return validStyle(s)?Diameters[unsigned(s.tool)][unsigned(s.width)]:0;}
QJsonObject jsonStyle(Style s){product::require(validStyle(s),"styled-style-invalid");QJsonObject o{{"version",1},{"tool",toolId(s.tool)},{"width",widthId(s.width)},{"color",colorId(s.color)}};if(s.exact)o["rgb"]=QString("#%1").arg(s.rgb&0xffffffu,6,16,QLatin1Char('0'));return o;}
Style parseStyle(const QJsonObject& o){product::validateStyle(o);Style s;for(unsigned i=0;i<9;++i)if(o["tool"]==Tools[i])s.tool=Tool(i);for(unsigned i=0;i<3;++i)if(o["width"]==Widths[i])s.width=Width(i);for(unsigned i=0;i<8;++i)if(o["color"]==Colors[i])s.color=i;if(o.contains("rgb")){s.exact=true;s.rgb=product::inkRgb(o);}return s;}
bool Matrix::valid()const noexcept{for(auto d:value)if(!std::isfinite(d)||std::abs(d)>100000)return false;return std::abs(value[0]*value[3]-value[1]*value[2])>1e-12&&scale()<=16&&scale()>=0.1;}
double Matrix::scale()const noexcept{return std::max(std::hypot(value[0],value[1]),std::hypot(value[2],value[3]));}
bool Matrix::map(double x,double y,double& ox,double& oy,bool inverse)const noexcept{if(!valid()||!std::isfinite(x)||!std::isfinite(y))return false;auto [a,b,c,d,e,f]=value;if(inverse){double det=a*d-b*c;x-=e;y-=f;ox=(d*x-c*y)/det;oy=(-b*x+a*y)/det;}else{ox=a*x+c*y+e;oy=b*x+d*y+f;}return std::isfinite(ox)&&std::isfinite(oy)&&std::abs(ox)<=100000&&std::abs(oy)<=100000;}
std::uint32_t compositeInk(std::uint32_t paper,std::uint32_t ink)noexcept{return multiplyInk(paper,ink&0xffffffu,ink>>24);}
std::uint32_t combineInk(std::uint32_t below,std::uint32_t above)noexcept{return over(below,above&0xffffffu,above>>24);}
Raster::Raster(int w,int h,int stride):width_(w),height_(h),stride_(stride){product::require(w>0&&h>0&&stride>=w&&std::uint64_t(stride)*h<=8*1024*1024,"styled-surface-invalid");auto n=std::size_t(stride)*h;pixels_.resize(n);under_.resize(n);coverage_.resize(n);stamps_.resize(n);}
void Raster::fill(const QJsonObject& stroke,Matrix matrix){product::require(!active_&&matrix.valid()&&stroke["shape"]=="polygon","styled-fill-state");auto style=parseStyle(stroke["style"].toObject());unsigned alpha=style.tool==Tool::Highlighter?191:style.tool==Tool::Shader?153:style.tool==Tool::Marker?242:255;alpha=unsigned(std::lround(alpha*product::fillOpacity(stroke)));nativeink::polygonSpans(stroke["points"].toArray(),width_,height_,jsonMatrix(matrix),[&](int y,int x0,int x1){for(int x=x0;x<x1;++x){auto at=std::size_t(y)*stride_+x;pixels_[at]=over(pixels_[at],inkRgb(style),alpha);}pending_.include({x0,y,x1,y+1});},stroke["holes"].toArray());}
bool Raster::begin(Style s,Matrix matrix,bool eraser)noexcept{if(active_||!validStyle(s)||!matrix.valid()||serial_==std::numeric_limits<std::uint32_t>::max())return false;++serial_;style_=s;matrix_=matrix;auto [a,b,c,d,e,f]=matrix.value;double det=a*d-b*c;inverse_={d/det,-b/det,-c/det,a/det,(c*f-d*e)/det,(b*e-a*f)/det};stretch_=std::max(std::abs(a)+std::abs(c),std::abs(b)+std::abs(d));eraser_=eraser;active_=true;activeDamage_={};return true;}
bool Raster::point(Coordinate p,bool first)noexcept{if(!active_||!std::isfinite(p.x)||!std::isfinite(p.y)||std::abs(p.x)>100000||std::abs(p.y)>100000)return false;bool ok=line(first?p:previous_,p);if(ok)previous_=p;return ok;}
bool Raster::line(Coordinate from,Coordinate to)noexcept{
 double dx=to.x-from.x,dy=to.y-from.y,len=std::hypot(dx,dy),angle=std::atan2(dy,dx),radius=eraser_?std::max(2.0,10.0/std::max(0.1,matrix_.scale())):diameter(style_)*0.5; /* D2: live erase band matches the ~10 px precise erase reach */
 // Ballpoint expression is geometry-derived, not represented as pressure.
 if(!eraser_&&style_.tool==Tool::Ballpoint)radius*=0.80+0.20*std::abs(std::cos(angle*2));
 double scale=matrix_.scale(),expansion=(!eraser_&&(style_.tool==Tool::Paintbrush||style_.tool==Tool::Calligraphy))?1.8:1.0,extent=std::max(1.0,radius*stretch_*expansion+1),step=std::max(0.25,radius*0.4);int steps=std::max(1,int(std::ceil(len/step)));
 if(steps>10000||double(steps+1)*(extent*2+2)*(extent*2+2)>4000000)return false;
 // A transformed segment is linear, so expanded endpoint bounds contain every
 // brush stamp. Skip offscreen work without changing contact or point history.
 {const auto& m=matrix_.value;const double ax=m[0]*from.x+m[2]*from.y+m[4],ay=m[1]*from.x+m[3]*from.y+m[5],bx=m[0]*to.x+m[2]*to.y+m[4],by=m[1]*to.x+m[3]*to.y+m[5];if(std::max(ax,bx)+extent<0||std::min(ax,bx)-extent>width_||std::max(ay,by)+extent<0||std::min(ay,by)-extent>height_)return true;}

 double cosine=std::cos(angle),sine=std::sin(angle);auto [ia,ib,ic,id,ie,iff]=inverse_;auto [ma,mb,mc,md,me,mf]=matrix_.value;
 for(int k=0;k<=steps;++k){double t=double(k)/steps,cx=from.x+dx*t,cy=from.y+dy*t,vx=ma*cx+mc*cy+me,vy=mb*cx+md*cy+mf;
  auto bounds=clip({int(std::floor(vx-extent)),int(std::floor(vy-extent)),int(std::ceil(vx+extent))+1,int(std::ceil(vy+extent))+1},width_,height_);
  for(int y=bounds.y0;y<bounds.y1;++y)for(int x=bounds.x0;x<bounds.x1;++x){double sx=ia*(x+0.5)+ic*(y+0.5)+ie,sy=ib*(x+0.5)+id*(y+0.5)+iff,px=sx-cx,py=sy-cy,squared=(px*px+py*py)/(radius*radius),alpha=0;
   if(eraser_)alpha=squared<=1?1:0;
   else if(style_.tool==Tool::Paintbrush){double u=(px*cosine+py*sine)/(radius*1.6),v=(-px*sine+py*cosine)/(radius*0.65),distance=std::sqrt(u*u+v*v);alpha=std::clamp((1-distance)*2,0.0,1.0);}
   else if(style_.tool==Tool::Calligraphy){double u=(px+py)*0.70710678118,v=(py-px)*0.70710678118;alpha=std::max(std::abs(u)/(radius*1.5),std::abs(v)/(radius*0.28))<=1?1:0;}
   else if(style_.tool==Tool::Marker)alpha=std::max(std::abs(px),std::abs(py))<=radius?0.95:0;
   else if(style_.tool==Tool::Shader){if(squared<=1){double distance=std::sqrt(squared);alpha=0.60*(1-distance);}} /* D2: stronger, like stock (was 0.35, quadratic) */
   else if(style_.tool==Tool::Highlighter)alpha=squared<=1?0.75:0; /* D2: vivid like stock (was 0.32); text stays dark because ink multiplies the page */
   else if(style_.tool==Tool::Fineliner)alpha=squared<=1?1:0;
   else {alpha=std::clamp((1-std::sqrt(squared))*std::max(1.0,radius*scale)+0.5,0.0,1.0);if(style_.tool==Tool::Pencil||style_.tool==Tool::Mechanical){auto ix=std::int64_t(std::floor(sx*4)),iy=std::int64_t(std::floor(sy*4));std::uint32_t grain=std::uint32_t(ix)*73856093u^std::uint32_t(iy)*19349663u;grain^=grain>>13;double texture=(grain%1000)/1000.0;alpha*=style_.tool==Tool::Pencil?(texture<0.23?0:0.30+texture*0.5):(texture<0.09?0.1:0.65+texture*0.2);}}
   unsigned coverage=unsigned(std::clamp(alpha*255,0.0,255.0));if(!coverage)continue;auto index=std::size_t(y)*stride_+x;if(stamps_[index]!=serial_){stamps_[index]=serial_;under_[index]=pixels_[index];coverage_[index]=0;}if(coverage<=coverage_[index])continue;coverage_[index]=coverage;pixels_[index]=eraser_?0:over(under_[index],inkRgb(style_),coverage);
  }
  activeDamage_.include(bounds);pending_.include(bounds);
 }return true;
}
void Raster::cancel()noexcept{if(!active_)return;for(int y=activeDamage_.y0;y<activeDamage_.y1;++y)for(int x=activeDamage_.x0;x<activeDamage_.x1;++x){auto at=std::size_t(y)*stride_+x;if(stamps_[at]==serial_)pixels_[at]=under_[at];}pending_.include(activeDamage_);active_=false;}
void Raster::replace(std::span<const std::uint32_t> next){product::require(!active_&&next.size()==pixels_.size(),"styled-replace-invalid");spike::Rect changed{};for(int y=0;y<height_;++y){const auto at=std::size_t(y)*stride_;const auto* source=next.data()+at;auto* target=pixels_.data()+at;if(std::memcmp(source,target,std::size_t(width_)*sizeof(std::uint32_t))==0)continue;int left=0,right=width_;while(left<right&&source[left]==target[left])++left;while(right>left&&source[right-1]==target[right-1])--right;std::copy_n(source+left,right-left,target+left);changed.include({left,y,right,y+1});}pending_.include(changed);}
void Raster::beginHighlights(){product::require(!active_&&serial_!=std::numeric_limits<std::uint32_t>::max(),"styled-highlight-epoch");++serial_;}
void Raster::highlight(const QJsonArray& quads,Matrix matrix,unsigned color,std::optional<semantic::SourceClip> crop,bool sameLayer){product::require((!crop||crop->valid())&&!active_&&matrix.valid()&&color>0&&color<8&&quads.size()<=8192&&serial_!=std::numeric_limits<std::uint32_t>::max(),"styled-highlight-invalid");if(!sameLayer)++serial_;std::uint64_t work=0;
 // Text highlights are blocks, as in a native reader: one rectangle per text line (words and the spaces between
 // them), evenly padded, drawn at a strength that reads on colour e-ink. Hidden source regions stay clipped.
 QJsonArray visible;for(auto value:quads){auto q=value.toArray();product::require(q.size()==8,"styled-highlight-quad");for(auto n:q)product::require(n.isDouble()&&std::isfinite(n.toDouble()),"styled-highlight-quad-value");if(crop&&!semantic::quadVisible(q,*crop))continue;visible.append(q);}
 auto toSurface=[&](double x,double y,double& sx,double& sy){return matrix.map(x,y,sx,sy);};
 for(const auto& block:semantic::lineBlocks(visible,toSurface)){auto bounds=clip({int(std::floor(block.x0)),int(std::floor(block.y0)),int(std::ceil(block.x1)),int(std::ceil(block.y1))},width_,height_);
  work+=std::uint64_t(std::max(0,bounds.x1-bounds.x0))*std::max(0,bounds.y1-bounds.y0);product::require(work<=16000000,"styled-highlight-work-capacity");
  for(int y=bounds.y0;y<bounds.y1;++y)for(int x=bounds.x0;x<bounds.x1;++x){if(crop){double px,py;product::require(matrix.map(x+.5,y+.5,px,py,true),"styled-highlight-clip-transform");if(!crop->contains(px,py))continue;}
   auto index=std::size_t(y)*stride_+x;if(stamps_[index]!=serial_){stamps_[index]=serial_;coverage_[index]=0;}const unsigned bit=1u<<color;if(coverage_[index]&bit)continue;coverage_[index]|=bit;pixels_[index]=over(pixels_[index],colorRgb(color),128);}
  pending_.include(bounds);}}
void Raster::composite(spike::Surface out,std::span<const std::uint32_t> background,spike::Rect r)const noexcept{if(!out.valid()||out.width!=width_||out.height!=height_||background.size()<pixels_.size())return;r=clip(r,width_,height_);for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x){auto i=std::size_t(y)*stride_+x;out.pixels[std::size_t(y)*out.stridePixels+x]=multiplyInk(background[i],pixels_[i]&0xffffff,pixels_[i]>>24);}}
Ink::Ink(const std::filesystem::path& catalogue,const std::filesystem::path& baseline,QJsonObject page,std::span<const std::uint32_t> legacy,int w,int h,int stride,QJsonObject geometry,reader::lifecycle::Tracking tracking):tracking_(tracking),catalogue_(catalogue),baseline_(baseline),page_(std::move(page)),geometry_(std::move(geometry)),legacy_(legacy.begin(),legacy.end()),raster_(w,h,stride),slots_(std::make_unique<std::array<Slot,QueueSlots>>()),width_(w),height_(h),stride_(stride){
 product::require(legacy.size()==std::size_t(stride)*h,"styled-legacy-size");matrix_=parseMatrix(page_["transform"].toArray());for(unsigned i=0;i<9;++i)styles_[i]={Tool(i),Width::Medium,i==8?4u:0u};product::require(tracking_.accept(bootstrapTracking_),"styled-progress-bootstrap-refused");try{worker_=std::thread([this]{work();});}catch(...){tracking_.complete(bootstrapTracking_,false);throw;}
}
Ink::~Ink(){if(!finish(5000))std::_Exit(78);}
void Ink::recordError(const char* message)noexcept{std::fprintf(stderr,"STYLED_ERROR %s\n",message);{std::lock_guard lock(resultMutex_);std::snprintf(error_.data(),error_.size(),"%s",message);}failed_.store(true,std::memory_order_release);}
bool Ink::reserve()noexcept{if(!admissionOpen()||active_>=0||stop_){++rejected_;return false;}for(unsigned i=0;i<QueueSlots;++i){auto& slot=(*slots_)[i];auto state=slot.state.load(std::memory_order_acquire);if(state==SlotState::Free||state==SlotState::Done){if(!tracking_.accept(slot.tracking)){++rejected_;return false;}slot.kind=Kind::Stroke;slot.style=style_;slot.matrix=matrix_;slot.count=0;slot.interrupted=false;slot.eraser=eraser_;slot.state=SlotState::Active;active_=int(i);capacity_=false;return true;}}++rejected_;capacity_=true;return false;}
bool Ink::append(const spike::Point& p)noexcept{
 if(active_<0||failed_)return false;auto& slot=(*slots_)[active_];if(slot.count==MaxPoints||(slot.count==0?p.phase!=spike::Phase::Down:p.phase==spike::Phase::Down)){capacity_=true;++rejected_;raster_.cancel();tracking_.complete(slot.tracking,false);slot.state=SlotState::Failed;active_=-1;failed_=true;return false;}
 bool erase=p.eraser||eraser_;if(slot.count==0){slot.eraser=erase;Style live=slot.style;if(erase)live.width=Width::Thick;if(!raster_.begin(live,slot.matrix,erase)){failed_=true;return false;}}else if(erase!=slot.eraser){cancel();failed_=true;return false;}
 Coordinate point;if(!slot.matrix.map(p.x,p.y,point.x,point.y,true)||!raster_.point(point,slot.count==0)){cancel();capacity_=true;failed_=true;return false;}slot.points[slot.count++]=point;
 if(p.phase==spike::Phase::Up){raster_.end();unsigned index=unsigned(active_);active_=-1;slot.state.store(SlotState::Queued,std::memory_order_release);++queued_;if(!queue_.push(index)){tracking_.complete(slot.tracking,false);slot.state=SlotState::Failed;failed_=true;return false;}}
 return true;
}
void Ink::cancel()noexcept{if(active_>=0){raster_.cancel();auto& slot=(*slots_)[active_];tracking_.complete(slot.tracking,slot.count==0);(*slots_)[active_].state.store(SlotState::Free,std::memory_order_release);active_=-1;}}
bool Ink::enqueue(Kind kind,bool option)noexcept{if(!ready_||active_>=0||failed_||stop_)return false;for(unsigned i=0;i<QueueSlots;++i){auto& slot=(*slots_)[i];auto state=slot.state.load(std::memory_order_acquire);if(state==SlotState::Free||state==SlotState::Done){if(!tracking_.accept(slot.tracking)){++rejected_;return false;}if(kind==Kind::Undo||kind==Kind::Redo||kind==Kind::Refresh)mutationPending_.store(true,std::memory_order_release);slot.kind=kind;slot.option=option;slot.interrupted=false;slot.style=style_;slot.matrix=matrix_;slot.count=0;slot.state.store(SlotState::Queued,std::memory_order_release);++queued_;if(!queue_.push(i)){tracking_.complete(slot.tracking,false);--queued_;slot.state=SlotState::Failed;failed_=true;return false;}return true;}}++rejected_;capacity_=true;return false;}
bool Ink::selectStyle(Style next){if(!validStyle(next)||active_>=0||!ready_||failed_||stop_)return false;if(jsonStyle(style_)==jsonStyle(next)){eraser_=false;return true;}auto prior=style_,saved=styles_[unsigned(next.tool)];style_=next;styles_[unsigned(next.tool)]=next;if(!enqueue(Kind::Settings)){style_=prior;styles_[unsigned(next.tool)]=saved;return false;}eraser_=false;return true;}
bool Ink::selectTool(Tool tool){if(unsigned(tool)>=9||active_>=0||!ready_||failed_||stop_)return false;if(style_.tool==tool){eraser_=false;return true;}auto old=style_;style_=styles_[unsigned(tool)];if(!enqueue(Kind::Settings)){style_=old;return false;}eraser_=false;return true;}
bool Ink::selectWidth(Width width){if(unsigned(width)>=3||active_>=0||!ready_||failed_||stop_)return false;if(style_.width==width)return true;auto old=style_;style_.width=width;if(!enqueue(Kind::Settings)){style_=old;return false;}styles_[unsigned(style_.tool)]=style_;return true;}
bool Ink::selectColor(unsigned color){if(color>=8||(style_.tool==Tool::Highlighter&&!color)||active_>=0||!ready_||failed_||stop_)return false;if(style_.color==color)return true;auto old=style_;style_.color=color;if(!enqueue(Kind::Settings)){style_=old;return false;}styles_[unsigned(style_.tool)]=style_;return true;}
bool Ink::setEraser(bool value){if(active_>=0||!ready_||failed_)return false;eraser_=value;return true;}bool Ink::undo(){return enqueue(Kind::Undo);}bool Ink::redo(){return enqueue(Kind::Redo);}
bool Ink::reload(){if(active_>=0||queued_||!ready_||failed_)return false;transformPending_=true;if(!enqueue(Kind::Refresh)){transformPending_=false;return false;}return true;}
bool Ink::setTransform(Matrix m){if(active_>=0||queued_||!m.valid()||!ready_)return false;if(legacyPresent_&&m.value!=matrix_.value){legacyPinned_=true;return false;}auto previous=matrix_;matrix_=m;transformPending_=true;if(!enqueue(Kind::Settings)){matrix_=previous;transformPending_=false;return false;}legacyPinned_=false;return true;}
Status Ink::status()const noexcept{bool failed=failed_.load()||interrupted_.load(),ready=ready_.load(),capacity=capacity_.load();auto pending=queued_.load();capacity=capacity||pending>=QueueSlots;return {ready,pending>0||resultPending_.load(std::memory_order_acquire),active_>=0,failed,capacity,pending,saved_.load(),rejected_.load(),failed?(capacity?"INK CAPACITY - STROKE NOT SAVED":"SAVE ERROR - INK NOT SAVED"):legacyPinned_?"LEGACY INK - VIEW IS PINNED":!editable_?"ACTIVE LAYER LOCKED OR HIDDEN":transformPending_?"UPDATING VIEW":capacity?"INK CAPACITY - WAIT":!ready?"LOADING INK":active_>=0?"WRITING - NOT SAVED":pending?"SAVING":resultPending_?"UPDATING VIEW":interruptedEraser_?"INTERRUPTED ERASER RETAINED - ORIGINAL INK UNCHANGED":interruptedRecovered_?"INTERRUPTED STROKE RECOVERED":unresolved_?"UNRESOLVED TEXT ANCHORS - OPEN NOTES":"SAVED"};}
QString Ink::error()const{std::lock_guard lock(resultMutex_);return error_[0]?QString::fromUtf8(error_.data()):capacity_?QString("styled-contact-point-or-raster-budget-capacity"):QString();}QJsonObject Ink::snapshot()const{std::lock_guard lock(resultMutex_);return snapshot_;}
bool Ink::tick(){if(active_>=0||queued_)return false;std::lock_guard lock(resultMutex_);if(!resultPending_)return false;raster_.replace(result_);revealEnabled_=resultReveal_;capacity_=false;transformPending_=false;if(settingsPending_){styles_=resultStyles_;style_=resultStyle_;settingsPending_=false;}mutationPending_.store(false,std::memory_order_release);ready_.store(true,std::memory_order_release);resultPending_.store(false,std::memory_order_release);return true;}
bool Ink::finish(unsigned ms)noexcept{
 // Quiescence preserves actual sampled pen geometry without fabricating Up.
 // Interrupted erasers are archived by the worker, never applied destructively.
 if(active_>=0){auto index=unsigned(active_);auto& slot=(*slots_)[index];active_=-1;raster_.cancel();if(slot.count){slot.interrupted=true;interrupted_=true;++queued_;slot.state.store(SlotState::Queued,std::memory_order_release);if(!queue_.push(index)){tracking_.complete(slot.tracking,false);--queued_;slot.state=SlotState::Failed;failed_=true;}}else{tracking_.complete(slot.tracking,true);slot.state=SlotState::Free;}}
 stop_=true;if(!worker_.joinable())return !failed_&&unsaved()==0;auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(ms);while(!done_.load(std::memory_order_acquire)&&std::chrono::steady_clock::now()<deadline)std::this_thread::sleep_for(std::chrono::milliseconds(1));if(!done_)return false;worker_.join();return !failed_&&unsaved()==0;}
void Ink::work()noexcept{
 try{
  // Freeze legacy-only pixels once before opening admission. Subsequent fixed
  // contacts are persisted only by this canonical feature worker.
  const auto bootStart=std::chrono::steady_clock::now();auto sinceUs=[](auto t){return (unsigned long long)std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now()-t).count();};
  underlay::privateDirectory(baseline_.parent_path());auto size=legacy_.size()*4;
  // D2 page-turn latency: a page without legacy pixels freezes as a 40-byte blank marker ("INKBASE0" + digest of the
  // marker label) instead of a full-page copy, so opening a page no longer writes or re-hashes ~14 MB. A full baseline
  // that turns out blank is rewritten once as the marker; one with pixels is kept exactly as before.
  const auto blankDigest=underlay::digest(QByteArray("inkbound-blank-legacy-baseline"));auto blank=[](std::span<const std::uint32_t> pixels){return std::all_of(pixels.begin(),pixels.end(),[](auto pixel){return pixel==0xffffffffu||pixel==0;});};
  if(std::filesystem::exists(baseline_)){auto bytes=underlay::readBounded(baseline_,size+40);
   if(bytes.size()==40&&bytes.first(8)=="INKBASE0"){product::require(bytes.mid(8,32)==blankDigest,"styled-legacy-baseline-corrupt");std::fill(legacy_.begin(),legacy_.end(),0xffffffffu);}
   else{product::require(bytes.size()==qint64(size+40)&&bytes.first(8)=="INKBASE1"&&bytes.mid(8,32)==underlay::digest(bytes.mid(40)),"styled-legacy-baseline-corrupt");std::memcpy(legacy_.data(),bytes.constData()+40,size);if(blank(legacy_))underlay::atomicFile(baseline_,QByteArray("INKBASE0",8)+blankDigest);}}
  else if(blank(legacy_))underlay::atomicFile(baseline_,QByteArray("INKBASE0",8)+blankDigest);
  else{auto bytes=QByteArray(reinterpret_cast<const char*>(legacy_.data()),qsizetype(size));underlay::atomicFile(baseline_,QByteArray("INKBASE1",8)+underlay::digest(bytes)+bytes);}
  recovery::v2::e2::mark("styled:baseline-us",sinceUs(bootStart));
  for(auto& pixel:legacy_)if(pixel==0xffffffffu)pixel=0;legacyPresent_=std::any_of(legacy_.begin(),legacy_.end(),[](auto pixel){return pixel!=0;});
  interruptedEraser_=std::filesystem::exists(catalogue_/"interrupted")&&!std::filesystem::is_empty(catalogue_/"interrupted");
  auto acquire=[&](){ /* D2: another user of the store (previous page's ink finishing a save, notes, an EPUB re-layout) may hold its lock;
   waiting up to ~10 s instead of ~1 s keeps a slow save from failing this page (and the session) */
   const auto giveUp=std::chrono::steady_clock::now()+std::chrono::seconds(10); /* a time budget, not an attempt count: an attempt costs more on slow builds */
   for(unsigned attempt=0;;++attempt){try{auto services=std::make_unique<product::Services>(catalogue_);if(attempt)recovery::v2::e2::mark("styled:store-lock-waits",attempt);return services;}catch(const std::exception& e){if(std::string_view(e.what())!="product-store-already-open"||std::chrono::steady_clock::now()>=giveUp)throw;std::this_thread::sleep_for(std::chrono::milliseconds(5));}}};
  auto usable=[&](const QJsonObject& state){auto doc=document(state,page_);bool editable=false;for(auto lv:doc["layers"].toArray()){auto l=lv.toObject();if(l["id"]==doc["activeLayer"])editable=l["visible"].toBool()&&!l["locked"].toBool();}editable_=editable;};
  // D2: the last full composition is kept so a batch of plain pen strokes on the top visible layer
  // (same presentation transform) is drawn onto it instead of replaying the whole document.
  std::unique_ptr<Raster> cache;Matrix cacheMatrix;QString cacheTop;QJsonObject cacheDocument;
  auto draw=[&](const QJsonObject& state,Matrix matrix){unsigned unresolved=0;bool recovered=false;Raster completed(width_,height_,stride_);completed.replace(legacy_);auto geometry=geometry_.isEmpty()?page_:geometry_;auto crop=semantic::sourceClip(geometry);auto doc=document(state,page_);auto layers=doc["layers"].toArray(),strokes=doc["strokes"].toArray();for(auto lv:layers){auto layer=lv.toObject();if(!layer["visible"].toBool())continue;completed.beginHighlights();for(auto hv:doc["highlights"].toArray()){const auto h=hv.toObject();if(h["layer"]!=layer["id"])continue;if(h["anchor"].toObject()["resolutionStatus"]=="unresolved"){++unresolved;continue;}const auto resolved=notes::resolveAnnotation(h["anchor"].toObject(),geometry);if(resolved["status"]!="resolved"){if(resolved["status"]=="unresolved")++unresolved;continue;}unsigned color=0;for(unsigned i=1;i<8;++i)if(h["color"]==Colors[i])color=i;if(color)completed.highlight(resolved["quads"].toArray(),matrix,color,crop,true);}for(auto sv:strokes){auto stroke=sv.toObject();if(stroke["layer"]!=layer["id"]||!matches(stroke,page_))continue;recovered|=stroke["interrupted"].toBool(false);auto points=stroke["points"].toArray();product::require(completed.begin(parseStyle(stroke["style"].toObject()),matrix),"styled-replay-begin");for(int i=0;i<points.size();++i){auto p=points[i].toArray();product::require(p.size()>=2&&completed.point({p[0].toDouble(),p[1].toDouble()},i==0),"styled-replay-budget");}completed.end();}}unresolved_=unresolved;interruptedRecovered_=recovered;QString top;for(auto lv:layers){auto layer=lv.toObject();if(layer["visible"].toBool())top=layer["id"].toString();}cache=std::make_unique<Raster>(completed);cacheMatrix=matrix;cacheTop=top;cacheDocument=doc;return std::vector<std::uint32_t>(completed.pixels().begin(),completed.pixels().end());};
  {auto services=acquire();recovery::v2::e2::mark("styled:store-open-us",sinceUs(bootStart));if(page_["format"]=="epub"&&page_.contains("viewId"))services->registerEpubLayout(page_["viewId"].toString());auto state=services->snapshot();services.reset();recovery::v2::e2::mark("styled:snapshot-us",sinceUs(bootStart));document(state,page_);usable(state);auto settings=state["settings"].toObject(),styles=settings["tools"].toObject();auto selected=settings["activeTool"].toString("ballpoint");auto pixels=draw(state,matrix_);recovery::v2::e2::mark("styled:draw-us",sinceUs(bootStart));std::lock_guard lock(resultMutex_);for(unsigned i=0;i<9;++i){resultStyles_[i]=styles.contains(Tools[i])?parseStyle(styles[Tools[i]].toObject()):Style{Tool(i),Width::Medium,i==8?4u:0u};if(selected==Tools[i])resultStyle_=resultStyles_[i];}resultReveal_=settings["quietEffects"].toBool(false);result_=std::move(pixels);snapshot_=state;resultPending_=settingsPending_=true;}
  recovery::v2::e2::mark("styled:bootstrap-us",sinceUs(bootStart));
  if(!tracking_.complete(bootstrapTracking_,true))throw std::runtime_error("styled-progress-bootstrap-completion");
  auto strokeObject=[&](const Slot& slot,const QJsonObject& doc){QJsonArray points;for(unsigned i=0;i<slot.count;++i)points.append(QJsonArray{slot.points[i].x,slot.points[i].y});auto anchor=page_;for(auto key:{"transform","pageToSurface","text","chars","words","blocks","annotationResolutions","annotationLayoutId"})anchor.remove(key);QJsonObject stroke{{"layer",doc["activeLayer"]},{"anchor",anchor},{"style",jsonStyle(slot.style)},{"points",points},{"coordinateSpace",page_["format"]=="epub"?"pinned-layout":"source-page"},{"presentationTransform",jsonMatrix(slot.matrix)},{"raster","inkbound-styled-union-v1"},{"sensors","position-only; pressure and tilt unavailable"}};if(slot.interrupted)stroke["interrupted"]=true;return stroke;};
  // D2: consecutive queued plain pen strokes are saved together: one store transaction (one write)
  // and one redraw for the whole batch, so a backlog never costs one full rewrite per stroke.
  auto plain=[&](unsigned i){const auto& s=(*slots_)[i];return s.kind==Kind::Stroke&&!s.eraser&&!s.interrupted;};
  unsigned index;int carry=-1;bool trim=false;
  while(!stop_.load(std::memory_order_acquire)||queued_.load()||carry>=0){
   if(carry>=0){index=unsigned(carry);carry=-1;}
   else if(!queue_.pop(index)){if(trim){malloc_trim(0);trim=false;}std::this_thread::sleep_for(std::chrono::milliseconds(1));continue;}trim=true; /* D2: once idle after work, hand freed heap back to the system */
   if(plain(index)){
    std::vector<unsigned> batch{index};unsigned more;
    while(batch.size()<QueueSlots&&queue_.pop(more)){if(plain(more))batch.push_back(more);else{carry=int(more);break;}}
    try{
     const auto batchStart=std::chrono::steady_clock::now();
     if(failed_)throw std::runtime_error("styled-worker-stopped");auto services=acquire();auto state=services->snapshot(),doc=document(state,page_);auto id=doc["id"].toString();
     recovery::v2::e2::mark("styled:batch-open-us",sinceUs(batchStart));
     QJsonArray commands;for(auto i:batch)commands.append(QJsonObject{{"op","stroke.add"},{"document",id},{"stroke",strokeObject((*slots_)[i],doc)}});
     services->performAll(commands);recovery::v2::e2::mark("styled:batch-write-us",sinceUs(batchStart));
     auto updated=services->snapshot();usable(updated);services.reset();recovery::v2::e2::mark("styled:batch-reread-us",sinceUs(batchStart));
     const Matrix bm=(*slots_)[batch.back()].matrix;bool fast=cache&&cacheDocument==doc&&cacheMatrix.value==bm.value&&doc["activeLayer"].toString()==cacheTop&&!cacheTop.isEmpty();
     for(auto i:batch)fast=fast&&(*slots_)[i].matrix.value==bm.value;
     std::vector<std::uint32_t> pixels;
     if(fast){Raster next(*cache);for(auto i:batch){const auto& sl=(*slots_)[i];product::require(next.begin(sl.style,bm),"styled-incremental-begin");for(unsigned k=0;k<sl.count;++k)product::require(next.point(sl.points[k],k==0),"styled-incremental-budget");next.end();}*cache=std::move(next);cacheDocument=document(updated,page_);pixels.assign(cache->pixels().begin(),cache->pixels().end());recovery::v2::e2::mark("styled:incremental",batch.size());}
     else pixels=draw(updated,bm);{std::lock_guard lock(resultMutex_);resultReveal_=updated["settings"].toObject()["quietEffects"].toBool(false);snapshot_=std::move(updated);result_=std::move(pixels);resultPending_=true;}
     recovery::v2::e2::mark("styled:batch-strokes",batch.size());recovery::v2::e2::mark("styled:batch-us",sinceUs(batchStart));
     for(auto i:batch){auto& slot=(*slots_)[i];if(!tracking_.complete(slot.tracking,true))throw std::runtime_error("styled-progress-completion");++saved_;slot.state.store(SlotState::Done,std::memory_order_release);--queued_;}
    }catch(const std::exception& e){for(auto i:batch){auto& slot=(*slots_)[i];if(slot.state.load()==SlotState::Queued){tracking_.complete(slot.tracking,false);slot.state=SlotState::Failed;--queued_;}}recordError(e.what());}
    continue;
   }
   auto& slot=(*slots_)[index];
   try{
    if(failed_)throw std::runtime_error("styled-worker-stopped");auto services=acquire();auto state=services->snapshot(),doc=document(state,page_);auto id=doc["id"].toString();
    if(slot.interrupted&&slot.eraser){QJsonArray points;for(unsigned i=0;i<slot.count;++i)points.append(QJsonArray{slot.points[i].x,slot.points[i].y});QJsonObject draft{{"schema",1},{"kind","interrupted-eraser"},{"page",page_},{"style",jsonStyle(slot.style)},{"matrix",jsonMatrix(slot.matrix)},{"points",points},{"applied",false}};auto directory=catalogue_/"interrupted";underlay::privateDirectory(directory);underlay::atomicFile(directory/(product::newId().toStdString()+".json"),QJsonDocument(draft).toJson(QJsonDocument::Compact));interruptedEraser_=true;}
    else if(slot.kind==Kind::Refresh){}
    else if(slot.kind==Kind::Reveal)services->perform({{"op","settings.reveal"},{"enabled",slot.option}});
    else if(slot.kind==Kind::Settings)services->perform({{"op","settings.tool"},{"style",jsonStyle(slot.style)}});
    else if(slot.kind==Kind::Undo||slot.kind==Kind::Redo){auto op=slot.kind==Kind::Undo?"undo":"redo";if(!doc[op].toArray().isEmpty())services->perform({{"op",op},{"document",id}});}
    else if(!slot.eraser){services->perform({{"op","stroke.add"},{"document",id},{"stroke",strokeObject(slot,doc)}});}
    else {QJsonArray ids;QJsonObject pieces;std::uint64_t work=0;constexpr std::uint64_t Budget=40000000;bool exhausted=false;int added=0;
     std::vector<Segment> path;for(unsigned i=0;i<slot.count;++i){auto a=slot.points[i?i-1:0],b=slot.points[i];path.push_back({a,b,std::min(a.x,b.x),std::min(a.y,b.y),std::max(a.x,b.x),std::max(a.y,b.y)});}
     double x0=1e300,y0=1e300,x1=-1e300,y1=-1e300;for(const auto& g:path){x0=std::min(x0,g.x0);y0=std::min(y0,g.y0);x1=std::max(x1,g.x1);y1=std::max(y1,g.y1);}
     const auto strokes=doc["strokes"].toArray();const int existing=int(strokes.size());
     for(auto sv:strokes){if(exhausted)break;auto stroke=sv.toObject();if(!matches(stroke,page_))continue;bool editable=false;for(auto lv:doc["layers"].toArray()){auto l=lv.toObject();if(l["id"]==stroke["layer"])editable=l["visible"].toBool()&&!l["locked"].toBool();}if(!editable)continue;auto points=stroke["points"].toArray();if(points.isEmpty())continue;
      /* D2 precise eraser: ~10 screen px reach to the stroke's visible edge (was ~20 px plus most of the stroke width, removing whole strokes) */
      double radius=std::max(2.0,10.0/std::max(0.1,slot.matrix.scale()))+diameter(parseStyle(stroke["style"].toObject()))*0.5;
      double sx0=1e300,sy0=1e300,sx1=-1e300,sy1=-1e300;for(auto pv:points){auto p=pv.toArray();sx0=std::min(sx0,p[0].toDouble());sy0=std::min(sy0,p[1].toDouble());sx1=std::max(sx1,p[0].toDouble());sy1=std::max(sy1,p[1].toDouble());}
      if(sx0>x1+radius||sx1<x0-radius||sy0>y1+radius||sy1<y0-radius)continue;
      std::vector<Segment> local;for(const auto& g:path)if(!(g.x1<sx0-radius||g.x0>sx1+radius||g.y1<sy0-radius||g.y0>sy1+radius))local.push_back(g);if(local.empty())continue;
      auto cut=cutStroke(points,local,radius,work,Budget);
      if(cut.overflow){exhausted=true;recovery::v2::e2::mark("styled:erase-budget",unsigned(ids.size()));continue;} /* D2: a huge erase stops early; never fails the session */
      if(!cut.hit)continue;auto key=stroke["id"].toString();ids.append(key);
      /* keep the untouched parts unless that would pass the per-document stroke capacity (then the stroke is erased whole) */
      if(!cut.runs.empty()&&cut.runs.size()<=256&&existing-int(ids.size())+added+int(cut.runs.size())<8000){QJsonArray kept;for(const auto& r:cut.runs)kept.append(r);pieces[key]=kept;added+=int(cut.runs.size());}}
     QJsonArray highlightIds;
     /* Text highlights are semantic records, not freehand strokes. Resolve exact current-page quads and retain linked Notes. */
     const auto geometry=geometry_.isEmpty()?page_:geometry_;const double reach=std::max(2.0,10.0/std::max(.1,slot.matrix.scale()));
     for(auto value:doc["highlights"].toArray()){if(exhausted)break;auto highlight=value.toObject();bool editable=false;for(auto layer:doc["layers"].toArray()){auto l=layer.toObject();if(l["id"]==highlight["layer"])editable=l["visible"].toBool()&&!l["locked"].toBool();}if(!editable)continue;auto resolved=notes::resolveAnnotation(highlight["anchor"].toObject(),geometry);if(resolved["status"]!="resolved")continue;bool hit=false;
      QJsonArray visibleQuads;const auto crop=semantic::sourceClip(geometry);for(auto value:resolved["quads"].toArray())if(!crop||semantic::quadVisible(value.toArray(),*crop))visibleQuads.append(value);
      auto toSurface=[&](double x,double y,double& sx,double& sy){return slot.matrix.map(x,y,sx,sy);};
      for(const auto& block:semantic::lineBlocks(visibleQuads,toSurface)){std::array<Coordinate,4> polygon{};const std::array<Coordinate,4> surface{{{block.x0,block.y0},{block.x1,block.y0},{block.x1,block.y1},{block.x0,block.y1}}};for(unsigned n=0;n<4;++n)product::require(slot.matrix.map(surface[n].x,surface[n].y,polygon[n].x,polygon[n].y,true),"styled-highlight-erase-transform");double qx0=1e300,qy0=1e300,qx1=-1e300,qy1=-1e300;for(auto p:polygon){qx0=std::min(qx0,p.x);qy0=std::min(qy0,p.y);qx1=std::max(qx1,p.x);qy1=std::max(qy1,p.y);}if(qx0>x1+reach||qx1<x0-reach||qy0>y1+reach||qy1<y0-reach)continue;
       auto inside=[&](Coordinate p){bool positive=false,negative=false;for(unsigned k=0;k<4;++k){auto a=polygon[k],b=polygon[(k+1)%4];double cross=(b.x-a.x)*(p.y-a.y)-(b.y-a.y)*(p.x-a.x);positive|=cross>1e-9;negative|=cross< -1e-9;}return !(positive&&negative)&&(positive||negative);};
       for(const auto& segment:path){if(++work>Budget){exhausted=true;break;}if(segment.x1<qx0-reach||segment.x0>qx1+reach||segment.y1<qy0-reach||segment.y0>qy1+reach)continue;if(inside(segment.a)||inside(segment.b)){hit=true;break;}for(unsigned k=0;k<4;++k)if(segmentDistance(segment.a,segment.b,polygon[k],polygon[(k+1)%4])<=reach){hit=true;break;}if(hit)break;}if(hit||exhausted)break;}
      if(hit)highlightIds.append(highlight["id"]);}
     if(!ids.isEmpty()||!highlightIds.isEmpty()){QJsonObject command{{"op","stroke.erase"},{"document",id},{"ids",ids},{"highlights",highlightIds}};if(!pieces.isEmpty())command["pieces"]=pieces;try{services->perform(command);}catch(const std::exception& first){std::fprintf(stderr,"STYLED_ERASE_RETRY %s\n",first.what());command.remove("pieces");try{services->perform(command);}catch(const std::exception& second){std::fprintf(stderr,"STYLED_ERASE_SKIPPED %s\n",second.what());}}}}

    auto updated=services->snapshot();usable(updated);services.reset();auto pixels=cache&&cacheDocument==document(updated,page_)&&(slot.kind==Kind::Reveal||(slot.kind==Kind::Settings&&cacheMatrix.value==slot.matrix.value))?std::vector<std::uint32_t>(cache->pixels().begin(),cache->pixels().end()):draw(updated,slot.matrix);{std::lock_guard lock(resultMutex_);resultReveal_=updated["settings"].toObject()["quietEffects"].toBool(false);snapshot_=std::move(updated);result_=std::move(pixels);resultPending_=true;}if(!tracking_.complete(slot.tracking,true))throw std::runtime_error("styled-progress-completion");if(slot.kind==Kind::Stroke)++saved_;slot.state.store(slot.interrupted?SlotState::Failed:SlotState::Done,std::memory_order_release);--queued_;
   }catch(const std::exception& e){tracking_.complete(slot.tracking,false);recordError(e.what());slot.state=SlotState::Failed;--queued_;}
  }
 }catch(const std::exception& e){tracking_.complete(bootstrapTracking_,false);recordError(e.what());}done_.store(true,std::memory_order_release);
}
}
