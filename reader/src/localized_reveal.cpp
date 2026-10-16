#include "localized_reveal.h"
#include "product_services.h"
#include <algorithm>
#include <limits>
namespace localized {namespace {
constexpr auto InvalidTime=std::numeric_limits<std::uint64_t>::max();
bool contains(Rect outer,Rect inner)noexcept{return !outer.empty()&&!inner.empty()&&outer.x0<=inner.x0&&outer.y0<=inner.y0&&outer.x1>=inner.x1&&outer.y1>=inner.y1;}
std::uint64_t area(Rect r)noexcept{return r.empty()?0:std::uint64_t(std::int64_t(r.x1)-r.x0)*std::uint64_t(std::int64_t(r.y1)-r.y0);}
bool bounded(Rect r)noexcept{return !r.empty()&&r.x0>=0&&r.y0>=0&&r.x1<=16384&&r.y1<=16384;}
}
bool Reveal::permitted(const product::AnimationPolicy& policy)const noexcept{
 return request_.userEnabled&&policy.permits(unsigned(area(region_)))&&policy.maxParticles>=particles_&&policy.maxFrames>=frames_&&policy.durationMs>=duration_&&particles_>0&&frames_>0&&duration_>0;
}
void Reveal::cancelLogical()noexcept{active_=false;issuedValid_=false;paintedForStamp_=false;issued_={};}
Update Reveal::issue(bool draw,unsigned frame)noexcept{
 // Saturation is unreachable in normal operation; retain any cleanup obligation.
 if(serial_==InvalidTime){active_=false;return {};}
 issued_={draw?region_:painted_,++serial_,frame,draw,active_};issuedValid_=true;paintedForStamp_=false;return issued_;
}
bool Reveal::start(const Request& request,const product::AnimationPolicy& policy,std::uint64_t now)noexcept{
 if(!quiescent()||now==InvalidTime||!request.userEnabled||!bounded(request.screen)||!contains(request.screen,request.popup)||request.popup.x1-request.popup.x0<24||request.popup.y0-request.screen.y0<10)return false;
 Rect region{request.popup.x0+8,request.popup.y0-10,std::min(request.popup.x1-8,request.popup.x0+116),request.popup.y0-2};
 if(region.empty()||!contains(request.screen,region)||area(region)>MaxArea||area(region)>area(request.screen)/4||!policy.maxParticles||policy.maxFrames<2||!policy.durationMs||!policy.permits(unsigned(area(region))))return false;
 request_=request;region_=region;particles_=std::min({policy.maxParticles,MaxParticles,unsigned((region.x1-region.x0)/3)});frames_=std::min(policy.maxFrames,MaxFrames);duration_=std::min(policy.durationMs,MaxDurationMs);
 if(!particles_||!frames_||!duration_)return false;
 // Quiet deterministic dots stay entirely outside the static popup content.
 for(unsigned i=0;i<particles_;++i){dots_[i]={region.x0+int((std::uint64_t(i)*(region.x1-region.x0-2))/particles_),region.y0+1+int(i%3)*2};}
 started_=lastTime_=now;frame_=~0u;active_=true;contactBlocked_=false;return true;
}
Update Reveal::advance(Identity identity,const product::AnimationPolicy& policy,std::uint64_t now)noexcept{
 // Never add effect damage during contact. Painted/unacknowledged pixels remain
 // conservatively owed until an off-contact cleanup is accepted by the caller.
 if(policy.contact){contactBlocked_=true;if(active_)cancelLogical();return {};}
 contactBlocked_=false;
 if(active_){
  if(identity!=request_.identity||now==InvalidTime||now<lastTime_||!permitted(policy)){cancelLogical();}
  else{lastTime_=now;const auto elapsed=now-started_;if(elapsed>=duration_)cancelLogical();else{
   // The frame budget includes the final clear: at most two particle frames.
   const unsigned frame=unsigned((elapsed*(frames_-1))/duration_);
   if(frame!=frame_){frame_=frame;return issue(true,frame);}
   return issuedValid_?issued_:Update{};
  }}
 }
 if(!painted_.empty())return issuedValid_?issued_:issue(false,frame_);
 return {};
}
bool Reveal::paint(std::uint64_t stamp,std::span<std::uint32_t> pixels,int width,int height,int stride)noexcept{
 if(contactBlocked_||!active_||!issuedValid_||!issued_.paint||stamp!=issued_.stamp)return false;
 if(!pixels.data()||width<=0||height<=0||width>16384||height>16384||stride<width||std::uint64_t(stride)*unsigned(height)>pixels.size()||request_.screen.x1>width||request_.screen.y1>height){cancelLogical();return false;}
 const unsigned paints=frames_-1,remaining=paints-issued_.frame;const unsigned count=(particles_*remaining+paints-1)/paints;
 const unsigned grey=184+unsigned(issued_.frame)*20;const std::uint32_t color=0xff000000u|(grey<<16)|(grey<<8)|grey;
 for(unsigned i=0;i<count;++i){const auto dot=dots_[i];for(int dy=0;dy<2;++dy)for(int dx=0;dx<2;++dx)pixels[std::size_t(dot.y+dy)*unsigned(stride)+unsigned(dot.x+dx)]=color;}
 // Painting borrowed memory is enough to owe cleanup, even without acceptance.
 painted_=region_;paintedForStamp_=true;return true;
}
bool Reveal::acknowledgePresented(std::uint64_t stamp,Rect accepted)noexcept{
 if(contactBlocked_||!issuedValid_||stamp!=issued_.stamp||!bounded(accepted)||!contains(request_.screen,accepted)||!contains(accepted,issued_.damage))return false;
 if(issued_.paint&&!paintedForStamp_)return false;
 if(!issued_.paint)painted_={};
 issuedValid_=false;paintedForStamp_=false;issued_={};return true;
}
}
