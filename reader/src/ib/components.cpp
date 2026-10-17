// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/components.h"
#include <algorithm>
namespace ib {
namespace t=theme;
namespace {
const Font LabelFont{Face::Text,float(t::Label.size)},MetaFont{Face::Text,float(t::Meta.size)},CaptionFont{Face::Text,float(t::Caption.size)};
}
void bubble(Canvas& c,Rect r,std::uint32_t fill,float radius,bool shadow){
 const int s=shadow?t::PopShadow:0;const Rect body{r.x0,r.y0,r.x1-s,r.y1-s};radius=std::min(radius,std::min(width(body),height(body))/2.f);
 if(s)c.fillRound({body.x0+s,body.y0+s,body.x1+s,body.y1+s},radius,t::InkPrimary);
 c.fillRound(body,radius,fill);c.strokeRound(body,radius,float(t::Outline),t::InkPrimary);}
void popover(Canvas& c,Rect r){bubble(c,r,t::Surface,t::RadiusMedium);}
void panel(Canvas& c,Rect r,float radius,std::uint32_t){bubble(c,r,t::Surface,radius);}
void dot(Canvas& c,Rect box,Icon i,std::uint32_t color){const float cx=(box.x0+box.x1)/2.f,cy=(box.y0+box.y1)/2.f,rr=std::min(width(box),height(box))/2.f;
 c.fillCircle(cx,cy,rr,color);c.strokeCircle(cx,cy,rr-1.5f,float(t::Outline),t::InkPrimary);if(i!=Icon::Count){const int g=int(rr*1.05f);icon(c,i,centered(box,g,g),t::onPop(color));}}
void swash(Canvas& c,Font f,float x,float baseline,std::string_view s,std::uint32_t color){
 const float w=text::measure(f,s),xh=text::capHeight(f)*0.62f;const int h=std::max(10,int(xh*0.62f));
 c.fillRound({int(x)-6,int(baseline)-h+6,int(x+w)+10,int(baseline)+8},h/2.f,color);text::draw(c,f,x,baseline,s,t::InkPrimary);}
void wordmark(Canvas& c,Rect box,float size){
 const Font f{Face::TextSemibold,size};
 if(text::plainScript()){const float w=text::measure(f,"Inkie");const float x=box.x0+(width(box)-w)/2,b=box.y0+(height(box)+text::capHeight(f))/2.f;swash(c,f,x,b,"Inkie",t::PopCyan);return;}
 // Cursive with a dotless i whose dot is a magenta bubble.
 const std::string_view word="Ink\xC4\xB1" "e";const float w=text::measure(f,word);const float x=box.x0+(width(box)-w)/2,b=box.y0+(height(box)+text::capHeight(f))/2.f;
 swash(c,f,x,b,word,t::PopCyan);
 const float i0=text::measure(f,"Ink"),i1=text::measure(f,"Ink\xC4\xB1");const Font drawn=text::resolved(f);
 const float r=std::max(6.f,drawn.size*0.085f),cx=x+(i0+i1)/2+drawn.size*0.04f,cy=b-drawn.size*0.357f-r*2.1f;
 c.fillCircle(cx,cy,r,t::PopMagenta);c.strokeCircle(cx,cy,r-1,2.5f,t::InkPrimary);}
void contextSheet(Canvas& c,int top,std::uint32_t accent){
 const float R=t::RadiusLarge;const Rect sheet{0,top,t::ScreenWidth,t::ScreenHeight+int(R)+8};
 c.fillRound(sheet,R,t::Surface);c.strokeRound(sheet,R,float(t::Outline),t::InkPrimary);
 const Rect tab=centered({0,top+10,t::ScreenWidth,top+30},140,16);c.fillRound(tab,8,accent);c.strokeRound(tab,8,2.f,t::InkPrimary);}
void sheetTitle(Canvas& c,Targets& tg,int top,int h,std::string_view title,std::string_view subtitle,int subtitleId,int rightEdge){
 const Font tf{Face::TextSemibold,theme::Subhead.size},sf{Face::Text,theme::Meta.size};text::draw(c,tf,70.f,float(top+62),title,t::InkPrimary);
 if(!subtitle.empty()){Rect s{70,top+76,rightEdge-t::S3,top+h-16};text::centeredLine(c,sf,s,text::fit(sf,subtitle,float(width(s))),t::InkSecondary,Align::Left);if(subtitleId>=0)tg.add(subtitleId,s);}
}
void sheetQuote(Canvas& c,Rect r,std::string_view s,std::uint32_t rule){const Font qf{Face::SerifItalic,theme::Label.size}; // the book's words stay serif
 const Rect bar{r.x0+34,r.y0+8,r.x0+48,r.y1-8};c.fillRound(bar,7,rule);c.strokeRound(bar,7,2.f,t::InkPrimary);
 Rect q{r.x0+68,r.y0,r.x1,r.y1};text::centeredLine(c,qf,q,text::fit(qf,s,float(width(q))),t::InkSecondary,Align::Left);}
void ruledPaper(Canvas& c,Rect a,int gap){for(int y=a.y0+gap-6;y<a.y1-8;y+=gap)c.hline(a.x0,a.x1,y,0xffe2e2e2u,1);}
Rect primaryAction(Canvas& c,Targets& tg,int id,Rect r,std::string_view label,Icon leading,bool disabled){
 const std::uint32_t fill=disabled?t::SurfaceSunken:t::GoAccent,ink=disabled?t::Disabled:t::onPop(fill);
 c.fillRound(r,height(r)/2.f,fill);c.strokeRound(r,height(r)/2.f,float(t::Outline),disabled?t::Disabled:t::InkPrimary);
 const Font f{Face::TextSemibold,theme::Label.size};float w=text::measure(f,label)+(leading!=Icon::Count?30+14:0);int x=int(r.x0+(width(r)-w)/2);
 if(leading!=Icon::Count){int mid=(r.y0+r.y1)/2;icon(c,leading,{x,mid-15,x+30,mid+15},ink);x+=44;}
 text::centeredLine(c,f,{x,r.y0,r.x1,r.y1},label,ink,Align::Left);if(!disabled)tg.add(id,hitArea(r));return r;}
Rect textAction(Canvas& c,Targets& tg,int id,Rect r,std::string_view label,bool underline,bool disabled,Align a){
 const Font f{underline?Face::TextSemibold:Face::Text,theme::Control.size};std::uint32_t ink=disabled?t::Disabled:t::InkPrimary;text::centeredLine(c,f,r,label,ink,a);
 if(underline){float w=text::measure(f,label);int x0=a==Align::Right?r.x1-int(w):r.x0;const int y=(r.y0+r.y1)/2+20;c.fillRound({x0,y,x0+int(w),y+7},3.5f,disabled?t::Disabled:t::GoAccent);}
 if(!disabled)tg.add(id,hitArea(r));return r;}
Rect hitArea(Rect v,int m)noexcept{int w=std::max(width(v),m),h=std::max(height(v),m);int cx=(v.x0+v.x1)/2,cy=(v.y0+v.y1)/2;return {cx-w/2,cy-h/2,cx-w/2+w,cy-h/2+h};}
Rect iconButton(Canvas& c,Targets& tg,int id,Rect hit,Icon i,ButtonState s,int size){
 hit=hitArea(hit);int well=std::min({width(hit),height(hit),size+36});const std::uint32_t pop=s.accent?s.accent:t::ToolSelected;
 if(s.selected){const Rect b=centered(hit,well,well);c.fillCircle((b.x0+b.x1)/2.f,(b.y0+b.y1)/2.f,well/2.f,pop);c.strokeCircle((b.x0+b.x1)/2.f,(b.y0+b.y1)/2.f,well/2.f-1.5f,float(t::Outline),t::InkPrimary);}
 icon(c,i,centered(hit,size,size),s.selected?t::onPop(pop):s.disabled?t::Disabled:t::InkPrimary);
 if(!s.disabled)tg.add(id,hit);return hit;
}
Rect iconLabelButton(Canvas& c,Targets& tg,int id,Rect hit,Icon i,std::string_view label,ButtonState s){
 hit=hitArea(hit);std::uint32_t ink=s.disabled?t::Disabled:t::InkPrimary;
 const std::uint32_t pop=s.accent?s.accent:t::ToolSelected;if(s.selected){c.fillRound(inset(hit,6),t::RadiusMedium,pop);c.strokeRound(inset(hit,6),t::RadiusMedium,float(t::Outline),t::InkPrimary);ink=t::onPop(pop);}
 int block=t::Icon+12+int(t::Meta.size);int top=hit.y0+(height(hit)-block)/2;
 icon(c,i,{(hit.x0+hit.x1-t::Icon)/2,top,(hit.x0+hit.x1+t::Icon)/2,top+t::Icon},ink);
 text::drawAligned(c,MetaFont,hit,float(top+t::Icon+12+text::ascent(MetaFont)*0.82f),label,ink,Align::Center);
 if(!s.disabled)tg.add(id,hit);return hit;
}
Rect button(Canvas& c,Targets& tg,int id,Rect r,std::string_view label,ButtonKind k,bool disabled,Icon leading){
 Rect hit=hitArea(r);std::uint32_t fg=t::InkPrimary;const float radius=height(r)/2.f; /* Inkie buttons are pills */
 if(k==ButtonKind::Quiet)fg=disabled?t::Disabled:t::InkPrimary;
 else{const std::uint32_t fill=disabled?t::SurfaceSunken:k==ButtonKind::Primary?t::GoAccent:k==ButtonKind::Destructive?t::Caution:t::Surface;
  c.fillRound(r,radius,fill);c.strokeRound(r,radius,float(t::Outline),disabled?t::Disabled:t::InkPrimary);fg=disabled?t::Disabled:t::onPop(fill);}
 Rect content=inset(r,t::S2);
 if(leading!=Icon::Count){float w=text::measure(LabelFont,label)+t::IconSmall+t::S2;int x=int(r.x0+(width(r)-w)/2);icon(c,leading,{x,(r.y0+r.y1-t::IconSmall)/2,x+t::IconSmall,(r.y0+r.y1+t::IconSmall)/2},fg);content.x0=x+t::IconSmall+t::S2;text::centeredLine(c,LabelFont,content,label,fg,Align::Left);}
 else text::centeredLine(c,LabelFont,content,label,fg,Align::Center);
 if(!disabled)tg.add(id,hit);return hit;
}
SheetFrame sheet(Canvas& c,Targets& tg,Rect r,std::string_view title,const SheetOptions& o){
 popover(c,r);int h=t::TopBar+8; /* native: square, thin ink outline */Rect header{r.x0,r.y0,r.x1,r.y0+h};int x=r.x0+t::S4,right=r.x1-t::S3;
 if(o.back){iconButton(c,tg,o.backId,{r.x0+t::S2,header.y0+4,r.x0+t::S2+t::MinTarget,header.y1-4},Icon::Back);x=r.x0+t::S2+t::MinTarget+t::S1;}
 if(o.close){iconButton(c,tg,o.closeId,{right-t::MinTarget,header.y0+4,right,header.y1-4},Icon::Close);right-=t::MinTarget;}
 if(o.done){iconButton(c,tg,o.doneId,{right-t::MinTarget,header.y0+4,right,header.y1-4},Icon::Check);right-=t::MinTarget;}
 if(o.more){iconButton(c,tg,o.moreId,{right-t::MinTarget,header.y0+4,right,header.y1-4},Icon::More);right-=t::MinTarget;}
 Font f{Face::TextSemibold,theme::Body.size};
 if(o.subtitle.empty())text::centeredLine(c,f,{x,header.y0,right-t::S2,header.y1},title,t::InkPrimary,Align::Left);
 else{text::draw(c,f,float(x),float(header.y0+58),text::fit(f,title,float(right-x-t::S2)),t::InkPrimary);text::draw(c,CaptionFont,float(x),float(header.y0+94),text::fit(CaptionFont,o.subtitle,float(right-x-t::S2)),t::InkSecondary);}
 return {header,{r.x0+t::S4,header.y1,r.x1-t::S4,r.y1-t::S4}};
}
void tabs(Canvas& c,Targets& tg,int first,Rect r,std::span<const std::string_view> labels,unsigned selected){
 int x=r.x0;for(unsigned i=0;i<labels.size();++i){float w=text::span(LabelFont,labels[i]);Rect cell{x,r.y0,x+int(w)+t::S5,r.y1};
  text::centeredLine(c,LabelFont,cell,labels[i],i==selected?t::InkPrimary:t::InkSecondary);
  if(i==selected){const Rect bar{cell.x0+t::S2,r.y1-10,cell.x1-t::S2,r.y1};c.fillRound(bar,5,t::PopCyan);c.strokeRound(bar,5,2.f,t::InkPrimary);}
  tg.add(first+int(i),hitArea(cell));x=cell.x1;}
 c.hline(r.x0,r.x1,r.y1,t::Border);
}
void chips(Canvas& c,Targets& tg,int first,Rect r,std::span<const std::string_view> labels,unsigned selected){
 int x=r.x0;for(unsigned i=0;i<labels.size();++i){float w=text::span(MetaFont,labels[i]);Rect chip{x,r.y0,x+int(w)+t::S5,r.y1};
  const bool on=i==selected;c.fillRound(chip,height(chip)/2.f,on?t::ToolSelected:t::Surface);c.strokeRound(chip,height(chip)/2.f,on?float(t::Outline):float(t::Hairline),t::InkPrimary);
  text::centeredLine(c,MetaFont,chip,labels[i],on?t::OnSelected:t::InkPrimary);tg.add(first+int(i),hitArea(chip));x=chip.x1+t::S2;}
}
void toggle(Canvas& c,Targets& tg,int id,Rect hit,bool on,bool disabled){
 hit=hitArea(hit);Rect track=centered(hit,84,46);std::uint32_t fill=disabled?t::SurfaceSunken:on?t::PopGreen:t::Surface;
 c.fillRound(track,23,fill);c.strokeRound(track,23,float(t::Outline),disabled?t::Disabled:t::InkPrimary);
 float knob=on?float(track.x1-23):float(track.x0+23),ky=(track.y0+track.y1)/2.f;c.fillCircle(knob,ky,16,disabled?t::Border:t::Surface);c.strokeCircle(knob,ky,15,float(t::Outline),disabled?t::Disabled:t::InkPrimary);
 if(!disabled)tg.add(id,hit);
}
void swatch(Canvas& c,Targets& tg,int id,Rect hit,std::uint32_t color,bool selected,bool disabled,int d){
 hit=hitArea(hit);float cx=(hit.x0+hit.x1)/2.f,cy=(hit.y0+hit.y1)/2.f;
 if(selected){c.fillCircle(cx,cy,d/2.f+13,t::ToolSelected);c.strokeCircle(cx,cy,d/2.f+12,float(t::Outline),t::InkPrimary);}
 c.fillCircle(cx,cy,d/2.f,disabled?t::SurfaceSunken:color);c.strokeCircle(cx,cy,d/2.f-1.5f,float(t::Outline),disabled?t::Disabled:t::InkPrimary);
 if(disabled)c.line(cx-d/3.f,cy+d/3.f,cx+d/3.f,cy-d/3.f,3,t::Disabled);
 if(!disabled)tg.add(id,hit);
}
Rect row(Canvas& c,Targets& tg,int id,Rect r,std::string_view label,std::string_view value,bool chevron,bool rule){
 text::centeredLine(c,LabelFont,{r.x0,r.y0,r.x0+width(r)/2,r.y1},label,t::InkPrimary,Align::Left);
 int right=r.x1;if(chevron){icon(c,Icon::ChevronDown,centered({r.x1-t::IconSmall,r.y0,r.x1,r.y1},t::IconSmall-4,t::IconSmall-4),t::InkPrimary);right-=t::IconSmall+t::S1;}
 if(!value.empty())text::centeredLine(c,LabelFont,{r.x0+width(r)/2,r.y0,right,r.y1},value,t::InkPrimary,Align::Right);
 if(rule)divider(c,r.x0,r.x1,r.y1);if(id>=0)tg.add(id,r);return r;
}
int quote(Canvas& c,Rect r,std::string_view caption,std::string_view body,int maxLines){
 Font q{Face::SerifItalic,float(t::Body.size)};int y=r.y0+t::S3;int lh=t::Body.line-4;
 auto lines=text::wrap(q,body,float(width(r)-t::S5-t::S3),maxLines);int h=t::S3*2+int(lines.size())*lh+(caption.empty()?0:t::Meta.line+t::S1);
 Rect box{r.x0,r.y0,r.x1,r.y0+h};c.fillRound(box,t::RadiusMedium,t::SurfaceSunken);c.strokeRound(box,t::RadiusMedium,float(t::Hairline),t::InkPrimary);
 if(!caption.empty()){text::draw(c,CaptionFont,float(r.x0+t::S4),float(y+t::Caption.size),text::fit(CaptionFont,caption,float(width(r)-t::S5)),t::InkSecondary);y+=t::Meta.line+t::S1;}
 for(std::size_t i=0;i<lines.size();++i)text::draw(c,q,float(r.x0+t::S4),float(y+int(i)*lh+text::ascent(q)),lines[i],t::InkPrimary);
 return box.y1;
}
void divider(Canvas& c,int x0,int x1,int y){c.hline(x0,x1,y,t::Border);}
void segmented(Canvas& c,Targets& tg,int first,Rect r,std::span<const Icon> icons,unsigned selected){
 int n=int(icons.size());if(!n)return;int w=width(r)/n;
 c.fillRound(r,height(r)/2.f,t::Surface);c.strokeRound(r,height(r)/2.f,float(t::Outline),t::InkPrimary);
 for(int i=0;i<n;++i){Rect cell{r.x0+i*w,r.y0,i==n-1?r.x1:r.x0+(i+1)*w,r.y1};const bool on=unsigned(i)==selected;
  if(on){const Rect b=inset(cell,7);c.fillRound(b,height(b)/2.f,t::PopYellow);c.strokeRound(b,height(b)/2.f,2.f,t::InkPrimary);}
  icon(c,icons[std::size_t(i)],centered(cell,t::IconSmall+6,t::IconSmall+6),t::InkPrimary);tg.add(first+i,hitArea(cell));}
}
void pill(Canvas& c,Rect r,std::string_view s,bool filled){c.fillRound(r,height(r)/2.f,filled?t::ToolSelected:t::SurfaceSunken);c.strokeRound(r,height(r)/2.f,2.f,t::InkPrimary);text::centeredLine(c,CaptionFont,r,s,filled?t::OnSelected:t::InkPrimary);}
Rect placeNear(Rect a,int w,int h,Rect b,int gap){
 b=inset(b,t::Gutter/2);int x=std::clamp((a.x0+a.x1)/2-w/2,b.x0,std::max(b.x0,b.x1-w));
 if(a.y1+gap+h<=b.y1)return {x,a.y1+gap,x+w,a.y1+gap+h};
 if(a.y0-gap-h>=b.y0)return {x,a.y0-gap-h,x+w,a.y0-gap};
 int y=a.y1+gap+h<=b.y1?a.y1+gap:std::max(b.y0,b.y1-h);return {x,y,x+w,y+h};
}
int paragraph(Canvas& c,Rect r,std::string_view s,Font f,int lh,std::uint32_t color,int maxLines){return text::block(c,f,r,s,color,lh,maxLines);}
}
