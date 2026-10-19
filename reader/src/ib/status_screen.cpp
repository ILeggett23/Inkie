#include "ib/status_screen.h"
#include <cmath>
#include <algorithm>
namespace ib::status {
namespace t=theme;
int nativeLevel(int step,int maximum)noexcept{if(step<0||step>LitLevels||maximum<LitLevels||maximum>65535)return -1;return (maximum*step+LitLevels/2)/LitLevels;}
int nearestStep(int level,int maximum)noexcept{if(level<0||maximum<LitLevels||maximum>65535||level>maximum)return -1;if(!level)return 0;return std::clamp((level*LitLevels+maximum/2)/maximum,1,LitLevels);}
void bar(Canvas& c,Targets& targets,const Model& m,Rect r){c.fill(r,t::Paper);const Font f{Face::Text,theme::Meta.size};const int mid=(r.y0+r.y1)/2;int right=r.x1-12;
 if(!m.clockOnly&&m.battery>=0){Rect body{right-52,mid-13,right-4,mid+13};c.strokeRound(body,8,3.f,t::InkPrimary);c.fillRound({body.x1,mid-5,body.x1+5,mid+5},2,t::InkPrimary);const int fill=(width(body)-8)*std::clamp(m.battery,0,100)/100;if(fill)c.fillRound({body.x0+4,body.y0+4,body.x0+4+fill,body.y1-4},4,m.battery<=20?t::Caution:t::InkPrimary); /* an ink gauge, red when low */
  if(m.charging){Path p;p.move(float(body.x0+27),float(mid-11)).line(float(body.x0+16),float(mid+2)).line(float(body.x0+23),float(mid+2)).line(float(body.x0+19),float(mid+11));c.stroke(p,3,t::Paper);}right=body.x0-12;const auto label=std::to_string(m.battery)+"%";const int w=int(std::ceil(text::measure(f,label))); /* rounded up: a truncated width made fit() ellipsize "73%" to "7…" */text::centeredLine(c,f,{right-w,r.y0,right,r.y1},label,t::InkPrimary,Align::Right);right-=w+18;}
 if(!m.clockOnly&&m.wifi>=0){icon(c,Icon::Wifi,{right-32,mid-16,right,mid+16},m.wifi?t::InkPrimary:t::InkSecondary);if(!m.wifi)c.line(float(right-30),float(mid+14),float(right-2),float(mid-14),2,t::InkSecondary);right-=50;}
 const auto label=m.clock+(m.zone.empty()?"":" "+m.zone);text::centeredLine(c,f,{r.x0+8,r.y0,right,r.y1},text::fit(f,label,float(std::max(0,right-r.x0-8))),t::InkPrimary,Align::Right);targets.add(Open,r);}
void panel(Canvas& c,Targets& targets,const Model& m){popover(c,Panel);const int x=Panel.x0+32,right=Panel.x1-32;const Font f{Face::Text,theme::Label.size};text::centeredLine(c,{Face::Text,theme::Body.size},{x,Panel.y0+20,right-96,Panel.y0+100},"Quick settings",t::InkPrimary,Align::Left);iconButton(c,targets,Close,{right-96,Panel.y0+12,right,Panel.y0+108},Icon::Close,{},36);
 const auto battery=m.battery<0?std::string("Unavailable"):std::to_string(m.battery)+"%"+(m.charging?" · Charging":"");row(c,targets,Battery,{x,Panel.y0+120,right,Panel.y0+216},"Battery",battery,false);
 row(c,targets,Wifi,{x,Panel.y0+224,right,Panel.y0+320},"Wi-Fi",m.wifi<0?"Unknown":m.wifi?"Connected":"Not connected",true);
 text::centeredLine(c,f,{x,Panel.y0+338,right,Panel.y0+400},"Reading light",t::InkPrimary,Align::Left);
 const int selected=nearestStep(m.light,m.maximum);for(int n=0;n<=LitLevels;++n){const int left=x+n*108;Rect target{left,Panel.y0+416,left+96,Panel.y0+512};button(c,targets,n?LevelBase+n-1:LightOff,target,n?std::to_string(n):"Off",selected==n?ButtonKind::Primary:ButtonKind::Secondary,m.busy||m.maximum<LitLevels);}
 const auto message=!m.error.empty()?m.error:m.busy?"Updating reading light…":m.maximum<LitLevels?"Reading light is unavailable":"Five brightness levels";text::centeredLine(c,{Face::Text,theme::Meta.size},{x,Panel.y0+536,right,Panel.y1-24},text::fit({Face::Text,theme::Meta.size},message,float(right-x)),t::InkSecondary,Align::Left);}
}
