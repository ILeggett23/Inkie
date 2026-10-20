// SPDX-License-Identifier: AGPL-3.0-only
#include "sand_transition.h"
#include <algorithm>
#include <chrono>
#include <limits>
namespace sand {namespace {
constexpr auto Invalid=std::numeric_limits<std::uint64_t>::max();
std::uint64_t nanos()noexcept{return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
Rect intersect(Rect a,Rect b)noexcept{Rect r{std::max(a.x0,b.x0),std::max(a.y0,b.y0),std::min(a.x1,b.x1),std::min(a.y1,b.y1)};return r.empty()?Rect{}:r;}
bool contains(Rect a,Rect b)noexcept{return !b.empty()&&a.x0<=b.x0&&a.y0<=b.y0&&a.x1>=b.x1&&a.y1>=b.y1;}
std::uint64_t area(Rect r)noexcept{return r.empty()?0:std::uint64_t(std::int64_t(r.x1)-r.x0)*std::uint64_t(std::int64_t(r.y1)-r.y0);}
std::uint32_t hash(std::uint32_t x)noexcept{x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;return x^(x>>16);}
bool valid(int w,int h,int stride,std::size_t size)noexcept{return w>0&&h>0&&w<=16384&&h<=16384&&stride>=w&&std::uint64_t(stride)*unsigned(h)<=size;}
}
Quality Transition::choose(Policy p,std::uint64_t pixels,Reason& why)noexcept{
 why=Reason::None;
 if(p.quality==Quality::Disabled||p.quality>Quality::Full)why=Reason::Disabled;
 else if(p.reducedMotion)why=Reason::Motion;
 else if(p.contact)why=Reason::Contact;
 else if(p.selecting)why=Reason::Selection;
 else if(p.power)why=Reason::Power;
 else if(p.heavy)why=Reason::Work;
 else if(p.congested)why=Reason::Congestion;
 else if(p.memoryTight)why=Reason::Memory;
 else if(p.expensive)why=Reason::Cost;
 else if(!pixels||pixels>MaxPixels)why=Reason::Geometry;
 if(why!=Reason::None)return Quality::Disabled;
 // Larger sheets use fewer accepted sources, not a growing frame queue.
 if(pixels>900000&&p.quality>Quality::Minimal){why=Reason::Cost;return Quality::Minimal;}
 if(pixels>400000&&p.quality>Quality::Reduced){why=Reason::Cost;return Quality::Reduced;}
 return p.quality;
}
bool Transition::start(Request r,Policy p,std::uint64_t now)noexcept{
 if(pending()||now==Invalid||r.screen.x0<0||r.screen.y0<0||r.screen.x1>16384||r.screen.y1>16384||!contains(r.screen,r.target))return false;
 const Rect region=intersect({std::max(0,r.target.x0-int(Trail)),std::max(0,r.target.y0-4),std::min(16384,r.target.x1+int(Trail)),std::min(16384,r.target.y1+4)},r.screen);
 Reason why;const auto quality=choose(p,area(region),why);metrics_.quality=quality;metrics_.reason=why;if(quality==Quality::Disabled)return false;
 request_=r;policy_=p;region_=region;duration_=std::clamp(r.durationMs,60u,MaxDurationMs);stages_=quality==Quality::Full?4:quality==Quality::Reduced?2:1;
 stage_=0;started_=lastTime_=now;captured_=painted_=finishing_=false;update_={};state_=State::Preparing;++metrics_.starts;
 return true;
}
bool Transition::capture(std::span<const std::uint32_t> src,int w,int h,int stride,std::uint64_t now)noexcept{
 if(!needsCapture())return false;
 if(!src.data()||now==Invalid||now<lastTime_||!valid(w,h,stride,src.size())||!contains({0,0,w,h},region_)){finish(Reason::Geometry);return false;}
 const auto begin=nanos();const int width=region_.x1-region_.x0;
 try{const auto n=std::size_t(area(region_));if(n>before_.capacity()){std::vector<std::uint32_t>().swap(before_);metrics_.cacheBytes=0;}before_.resize(n);}catch(...){finish(Reason::Memory);return false;}
 for(int y=region_.y0;y<region_.y1;++y)std::copy_n(src.data()+std::size_t(y)*stride+region_.x0,width,before_.data()+std::size_t(y-region_.y0)*width);
 grainCount_=metrics_.quality==Quality::Full?256:metrics_.quality==Quality::Reduced?128:48;
 for(unsigned i=0;i<grainCount_;++i){auto a=hash(request_.seed+i*0x9e3779b9u),b=hash(a);grains_[i]={request_.target.x0+int(a%unsigned(request_.target.x1-request_.target.x0)),request_.target.y0+int(b%unsigned(request_.target.y1-request_.target.y0)),8+int((a>>16)%24),int((b>>16)%5)-2,(a>>24)%5};}
 captured_=true;started_=lastTime_=now;state_=request_.dismiss?State::Dismissing:State::Revealing;
 metrics_.generationNs+=nanos()-begin;metrics_.cacheBytes=before_.capacity()*sizeof(std::uint32_t);
 // vector growth is implementation-defined: refuse an allocator that exceeds
 // the hard snapshot budget rather than silently retaining an oversized cache.
 if(metrics_.cacheBytes>MaxCacheBytes){std::vector<std::uint32_t>().swap(before_);metrics_.cacheBytes=0;captured_=false;finish(Reason::Memory);return false;}
 issue(1,true);return true;
}
bool Transition::buildSilhouette(std::span<const std::uint32_t> src,int w,int h,int stride)noexcept{
 if(!captured_||!update_||!src.data()||!valid(w,h,stride,src.size())||!contains({0,0,w,h},region_))return false;
 const auto begin=nanos();unsigned seen=0;const int rw=region_.x1-region_.x0;
 for(int y=request_.target.y0;y<request_.target.y1;++y)for(int x=request_.target.x0;x<request_.target.x1;++x){
  const auto old=before_[std::size_t(y-region_.y0)*rw+x-region_.x0],next=src[std::size_t(y)*stride+x];
  if(old==next)continue;
  const auto ink=request_.dismiss?old:next;
  if(((ink>>16)&255)>200&&((ink>>8)&255)>200&&(ink&255)>200)continue;
  const unsigned index=seen<grainCount_?seen:hash(request_.seed^seen)%(seen+1);++seen;
  if(index<grainCount_){grains_[index].x=x;grains_[index].y=y;}
 }
 ++metrics_.masks;metrics_.generationNs+=nanos()-begin;return true;
}
Update Transition::issue(unsigned stage,bool intermediate)noexcept{
 if(serial_==Invalid)return {}; // no stamp reuse
 stage_=stage;update_={region_,++serial_,stage,intermediate};uncovered_[0]=region_;uncoveredCount_=1;painted_=false;return update_;
}
void Transition::finish(Reason why)noexcept{
 if(!pending()||finishing_)return;
 metrics_.reason=why;if(why!=Reason::None)++metrics_.cancelled;
 if(!captured_){update_={};finishing_=false;state_=request_.dismiss?State::Hidden:State::Visible;return;}
 // Recompose canonical content even if the previous stage was painted but not
 // accepted. A stale acknowledgement cannot retire this new cleanup stamp.
 if(!finishing_){finishing_=true;issue(5,false);++metrics_.cleanups;}
 state_=request_.dismiss?State::Hidden:State::Visible;
}
Update Transition::advance(Identity identity,Policy p,std::uint64_t now)noexcept{
 if(!pending())return {};
 if(identity!=request_.identity)finish(Reason::Scene);
 else if(now==Invalid||now<lastTime_)finish(Reason::Clock);
 else {
  Reason why;const auto q=choose(p,area(region_),why);
  if(q==Quality::Disabled)finish(why);
  else if(q<metrics_.quality){metrics_.quality=q;metrics_.reason=Reason::Cost;finish(Reason::Cost);}
  lastTime_=now;
 }
 if(finishing_)return update_;
 if(!captured_){if(state_==State::Preparing&&now>=started_&&now-started_>=duration_)finish(Reason::Timeout);return {};}
 const auto elapsed=now-started_;
 if(elapsed>=duration_){if(stages_>stage_)metrics_.skipped+=stages_-stage_;finish(update_?Reason::Timeout:Reason::None);return update_;}
 if(update_)return update_; // one outstanding source; never queue frames
 unsigned next=1+unsigned(elapsed*stages_/duration_);next=std::min(stages_,next);
 if(next<=stage_)return {};
 metrics_.skipped+=next-stage_-1;return issue(next,true);
}
bool Transition::paint(std::uint64_t stamp,std::span<std::uint32_t> dst,int w,int h,int stride,Rect clip)noexcept{
 if(!update_||stamp!=update_.stamp||!dst.data()||!valid(w,h,stride,dst.size())||!contains({0,0,w,h},region_))return false;
 clip=intersect(clip,region_);if(clip.empty())return false;
 const auto begin=nanos();
 if(update_.intermediate){
  const int rw=region_.x1-region_.x0,tw=request_.target.x1-request_.target.x0;
  // Four semantic stages remain the same at lower quality: skip whole stages.
  const unsigned semantic=stages_==4?stage_:stages_==2?(stage_==1?2:4):3;
  const int progress=semantic==1?0:semantic==2?300:semantic==3?680:1000;
  const int sign=request_.direction==Direction::LeftToRight?1:-1;
  for(int y=clip.y0;y<clip.y1;++y)for(int x=clip.x0;x<clip.x1;++x){
   auto& pixel=dst[std::size_t(y)*stride+x];const auto old=before_[std::size_t(y-region_.y0)*rw+x-region_.x0];
   if(pixel==old)continue; // actual final/old difference is the silhouette
   const int u=sign>0?x-request_.target.x0:request_.target.x1-1-x;
   // Coherent spatial disturbance, unchanged between stages. Solid pixels
   // never regress; no independent random field or alpha fade per redraw.
   const unsigned row=unsigned(y)/24,phase=unsigned(y)%24;
   const int a=int(hash(request_.seed^row*131u)%321)-160,b=int(hash(request_.seed^(row+1)*131u)%321)-160;
   const int noise=(a*int(24-phase)+b*int(phase))/24+int(hash(request_.seed^unsigned(x/3)*31u^unsigned(y/3)*131u)%97)-48;
   // Arrival is directional but consolidation happens in coherent granular
   // islands across the active front, rather than a straight rectangular wipe.
   const int settling=int(hash(request_.seed^unsigned(x/3)*619u^unsigned(y/3)*137u)%361);
   const bool formed=progress==1000||(progress>0&&u*640/std::max(1,tw)+settling<progress+noise);
   if(!formed)pixel=old;
   else if(!request_.dismiss&&semantic<4&&x>request_.target.x0+2&&x<request_.target.x1-3&&y>request_.target.y0+2&&y<request_.target.y1-3&&((pixel>>16)&255)<96&&((pixel>>8)&255)<96&&(pixel&255)<96)pixel=0xffffffffu; // interior text/icons appear intact at consolidation
  }
  const unsigned count=semantic==4?grainCount_/12:grainCount_;
  auto speck=[&](int x,int y){if(x>=clip.x0&&x<clip.x1&&y>=clip.y0&&y<clip.y1)dst[std::size_t(y)*stride+x]=0xff000000u;};
  for(unsigned i=0;i<count;++i){const auto& g=grains_[i];
   // Converging, deterministic tracks; dismissal continues with the wind.
   const int displacement=request_.dismiss?g.travel*progress/1000:-g.travel*(1000-progress)/1000;
   const int entry=sign>0?request_.target.x0:request_.target.x1-1;
   const int exit=sign>0?request_.target.x1:request_.target.x0;
   const int x=semantic==4?(request_.dismiss?exit+sign*(4+int(i%24)):entry-sign*(4+int(i%24))):(semantic==1?entry+sign*int(i%24):g.x)+sign*displacement,y=g.y+g.drift*(1000-progress)/1000;
   const int u=sign>0?g.x-request_.target.x0:request_.target.x1-1-g.x;
   const int edge=u*1000/std::max(1,tw)-progress;
   if(semantic!=1&&(edge<-140||edge>320)&&i%9)continue;
   speck(x,y);if(g.variant>=1)speck(x-sign,y);if(g.variant>=2)speck(x-2*sign,y);if(g.variant==3)speck(x-3*sign,y-1);if(g.variant==4){speck(x,y+1);speck(x-sign,y+1);}
  }
 }
 paintClip_=clip;painted_=true;const auto cost=nanos()-begin;metrics_.paintNs+=cost;metrics_.lastPaintNs=cost;metrics_.worstPaintNs=std::max(metrics_.worstPaintNs,cost);
 // Future stages yield to expensive CPU composition; final canonical cleanup
 // remains owed. No sleep or compensating fast display requests.
 // The caller can feed this cost into Policy.expensive at the next boundary.
 return true;
}
bool Transition::acknowledge(std::uint64_t stamp,Rect accepted)noexcept{
 if(!update_||stamp!=update_.stamp||!painted_||!contains(paintClip_,intersect(accepted,region_)))return false;
 std::array<Rect,64> next{};unsigned n=0;
 for(unsigned i=0;i<uncoveredCount_;++i){const auto a=uncovered_[i],cut=intersect(a,accepted);if(cut.empty()){if(n==next.size())return false;next[n++]=a;continue;}
  const std::array pieces{Rect{a.x0,a.y0,a.x1,cut.y0},Rect{a.x0,cut.y1,a.x1,a.y1},Rect{a.x0,cut.y0,cut.x0,cut.y1},Rect{cut.x1,cut.y0,a.x1,cut.y1}};
  for(auto part:pieces)if(!part.empty()){if(n==next.size())return false;next[n++]=part;}
 }
 uncovered_=next;uncoveredCount_=n;if(n)return false;
 ++metrics_.accepted;const bool final=!update_.intermediate;update_={};painted_=false;
 if(final){captured_=false;finishing_=false;state_=request_.dismiss?State::Hidden:State::Visible;}
 return true;
}
}
