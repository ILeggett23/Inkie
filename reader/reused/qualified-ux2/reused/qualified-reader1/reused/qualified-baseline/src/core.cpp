// SPDX-License-Identifier: AGPL-3.0-only
#include "core.h"
#include <algorithm>
#include <cstdlib>
#include <time.h>
namespace spike {
static Ns readClock(clockid_t id)noexcept{timespec t{};if(clock_gettime(id,&t)!=0)return 0;return Ns(t.tv_sec)*1000000000+Ns(t.tv_nsec);}
Ns monotonic()noexcept{return readClock(CLOCK_MONOTONIC);}
Ns threadCpu()noexcept{return readClock(CLOCK_THREAD_CPUTIME_ID);}
void Rect::include(Rect r)noexcept{
 if(r.empty())return;
 if(empty()){*this=r;return;}
 x0=std::min(x0,r.x0);y0=std::min(y0,r.y0);x1=std::max(x1,r.x1);y1=std::max(y1,r.y1);
}
bool Transform::valid()const noexcept{
 return x.max>x.min&&y.max>y.min&&width>0&&height>0&&width<=8192&&height<=8192&&
 (rotation==0||rotation==90||rotation==180||rotation==270);
}
bool Transform::map(const Sample&s,Point&p)const noexcept{
 if(!valid()||s.x<x.min||s.x>x.max||s.y<y.min||s.y>y.max)return false;
 auto scale=[](int v,Axis a,int extent){return int(((std::int64_t(v)-a.min)*(extent-1)+(std::int64_t(a.max)-a.min)/2)/(std::int64_t(a.max)-a.min));};
 int px=scale(s.x,x,(rotation==90||rotation==270)?height:width);
 int py=scale(s.y,y,(rotation==90||rotation==270)?width:height);
 switch(rotation){case 90:p.x=width-1-py;p.y=px;break;case 180:p.x=width-1-px;p.y=height-1-py;break;case 270:p.x=py;p.y=height-1-px;break;default:p.x=px;p.y=py;}
 if(mirrorX)p.x=width-1-p.x;
 p.phase=s.phase;p.receipt=s.receipt;p.eraser=s.eraser;return true;
}
bool Surface::valid()const noexcept{return width>0&&height>0&&width<=8192&&height<=8192&&stridePixels>=width&&stridePixels<=16384&&std::size_t(stridePixels)*height<=8*1024*1024&&pixels.size()>=std::size_t(stridePixels)*height;}
Rect segment(Surface s,Point a,Point b,int radius,std::uint32_t color)noexcept{
 if(!s.valid()||radius<1||radius>4||a.x<0||a.y<0||b.x<0||b.y<0||a.x>=s.width||b.x>=s.width||a.y>=s.height||b.y>=s.height)return {};
 Rect damage{std::max(0,std::min(a.x,b.x)-radius),std::max(0,std::min(a.y,b.y)-radius),std::min(s.width,std::max(a.x,b.x)+radius+1),std::min(s.height,std::max(a.y,b.y)+radius+1)};
 int dx=std::abs(b.x-a.x),sx=a.x<b.x?1:-1,dy=-std::abs(b.y-a.y),sy=a.y<b.y?1:-1,err=dx+dy;
 for(;;){
  for(int y=-radius;y<=radius;++y)for(int x=-radius;x<=radius;++x){
   int px=a.x+x,py=a.y+y;
   if(x*x+y*y<=radius*radius&&px>=0&&py>=0&&px<s.width&&py<s.height)s.pixels[std::size_t(py)*s.stridePixels+px]=color;
  }
  if(a.x==b.x&&a.y==b.y)break;
  int e=2*err;if(e>=dy){err+=dy;a.x+=sx;}if(e<=dx){err+=dx;a.y+=sy;}
 }
 return damage;
}
Engine::Engine(Transform t,Surface s,Display&d,int r)noexcept:transform_(t),surface_(s),display_(d),radius_(r){
 failed_=!t.valid()||!s.valid()||t.width!=s.width||t.height!=s.height||r<1||r>4;
}
bool Engine::accept(Sample s)noexcept{
 Point p;
 if(failed_||size_==Capacity||!s.receipt||(s.phase!=Phase::Down&&s.phase!=Phase::Move&&s.phase!=Phase::Up)||!transform_.map(s,p)||
    (size_&&s.receipt<points_[size_-1].receipt)||
    (s.phase==Phase::Down?contact_:!contact_)||
    (contact_&&s.eraser!=previous_.eraser)){++rejected;failed_=true;return false;}
 Ns accepted=monotonic();if(!accepted){failed_=true;return false;}
 Point from=s.phase==Phase::Down?p:previous_;
 points_[size_]=p;Timing timing{s.receipt,accepted,0,0,0,0};
 dirty_.include(segment(surface_,from,p,s.eraser?4:radius_,s.eraser?0xffffffffu:0xff000000u));timing.t2=monotonic();
 timing_[size_++]=timing;previous_=p;contact_=s.phase!=Phase::Up;
 if(!timing.t2)failed_=true;
 return !failed_;
}
Submit Engine::flush()noexcept{
 if(failed_)return Submit::Failed;
 if(dirty_.empty())return Submit::Accepted;
 Ns t3=monotonic(),cpu0=threadCpu();if(!t3||!cpu0){failed_=true;return Submit::Failed;}
 Submit r=display_.submit(dirty_);
 Ns t4=monotonic(),cpu1=threadCpu();if(!t4||!cpu1||cpu1<cpu0){failed_=true;return Submit::Failed;}
 if(r==Submit::Busy){++busy;return r;}
 if(r==Submit::Failed){++submitFailures;failed_=true;return r;}
 for(std::size_t i=submitted_;i<size_;++i){timing_[i].t3=t3;timing_[i].t4=t4;timing_[i].cpu=cpu1-cpu0;}
 submitted_=size_;dirty_={};return r;
}
}
