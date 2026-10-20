// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/sleep_screen.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace ib::sleep {
// The sleep screen stays on the colour panel without power, so it is the one place Inkie shows off: the wordmark, a
// yellow crescent moon with an ink outline, "Sleeping" in script and three drops of the panel's own pigments.
void paint(Canvas& c){namespace t=theme;c.fill({0,0,1620,2160},t::Paper);wordmark(c,{180,330,1440,560},float(t::Display.size));
 const float cx=810,cy=920,R=116,ix=866,iy=872,r=100,w=float(t::Outline)+1;
 auto clamp01=[](float v){return std::clamp(v,0.f,1.f);};
 for(int y=int(cy-R-w);y<=int(cy+R+w);++y)for(int x=int(cx-R-w);x<=int(cx+R+w);++x){
  const float d1=std::hypot(x-cx,y-cy),d2=std::hypot(x-ix,y-iy),in=clamp01(R-d1+.5f)*clamp01(d2-r+.5f);
  if(in>0)c.blend(x,y,t::PopYellow,int(in*255));
  const float edge=std::max(clamp01(w/2-std::abs(d1-R)+.5f)*clamp01(d2-r+.5f),clamp01(w/2-std::abs(d2-r)+.5f)*clamp01(R-d1+.5f));
  if(edge>0)c.blend(x,y,t::InkPrimary,int(edge*255));}
 text::centeredLine(c,{Face::TextSemibold,float(t::Title.size)},{180,1100,1440,1220},"Sleeping",t::InkPrimary,Align::Center);
 const std::array<std::uint32_t,3> pigments{t::PopCyan,t::PopMagenta,t::PopYellow};for(int i=0;i<3;++i)inkDrop(c,810+float(i-1)*96,1340,24,pigments[std::size_t(i)]);}
}
