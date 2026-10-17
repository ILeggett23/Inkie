// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/components.h"
#include <algorithm>
namespace ib {
namespace t=theme;
namespace {
const Font LabelFont{Face::Text,float(t::Label.size)},MetaFont{Face::Text,float(t::Meta.size)},CaptionFont{Face::Text,float(t::Caption.size)};
}
void popover(Canvas& c,Rect r){c.fill(r,t::Surface);c.hairline(r,t::PopoverBorder,2);}
void contextSheet(Canvas& c,int top){c.fill({0,top,t::ScreenWidth,t::ScreenHeight},t::Surface);c.hline(0,t::ScreenWidth,top,t::PopoverBorder,2);}
void sheetTitle(Canvas& c,Targets& tg,int top,int h,std::string_view title,std::string_view subtitle,int subtitleId,int rightEdge){
 const Font tf{Face::Text,theme::Subhead.size},sf{Face::Text,theme::Meta.size};text::draw(c,tf,70.f,float(top+62),title,t::InkPrimary);
 if(!subtitle.empty()){Rect s{70,top+76,rightEdge-t::S3,top+h-16};text::centeredLine(c,sf,s,text::fit(sf,subtitle,float(width(s))),t::InkSecondary,Align::Left);if(subtitleId>=0)tg.add(subtitleId,s);}
}
void sheetQuote(Canvas& c,Rect r,std::string_view s,std::uint32_t rule){const Font qf{Face::Serif,theme::Label.size};c.fill({r.x0+40,r.y0+6,r.x0+44,r.y1-6},rule);
 Rect q{r.x0+68,r.y0,r.x1,r.y1};text::centeredLine(c,qf,q,text::fit(qf,s,float(width(q))),0xff505050u,Align::Left);}
void ruledPaper(Canvas& c,Rect a,int gap){for(int y=a.y0+gap-6;y<a.y1-8;y+=gap)c.hline(a.x0,a.x1,y,0xffe2e2e2u,1);}
Rect primaryAction(Canvas& c,Targets& tg,int id,Rect r,std::string_view label,Icon leading,bool disabled){
 c.fill(r,disabled?t::Disabled:t::ToolSelected);const Font f{Face::Text,theme::Label.size};float w=text::measure(f,label)+(leading!=Icon::Count?30+14:0);int x=int(r.x0+(width(r)-w)/2);
 if(leading!=Icon::Count){int mid=(r.y0+r.y1)/2;icon(c,leading,{x,mid-15,x+30,mid+15},t::Paper);x+=44;}
 text::centeredLine(c,f,{x,r.y0,r.x1,r.y1},label,t::Paper,Align::Left);if(!disabled)tg.add(id,hitArea(r));return r;}
Rect textAction(Canvas& c,Targets& tg,int id,Rect r,std::string_view label,bool underline,bool disabled,Align a){
 const Font f{underline?Face::TextSemibold:Face::Text,theme::Label.size};std::uint32_t ink=disabled?t::Disabled:t::InkPrimary;text::centeredLine(c,f,r,label,ink,a);
 if(underline){float w=text::measure(f,label);int x0=a==Align::Right?r.x1-int(w):r.x0;c.hline(x0,x0+int(w),(r.y0+r.y1)/2+22,ink,2);}
 if(!disabled)tg.add(id,hitArea(r));return r;}
Rect hitArea(Rect v,int m)noexcept{int w=std::max(width(v),m),h=std::max(height(v),m);int cx=(v.x0+v.x1)/2,cy=(v.y0+v.y1)/2;return {cx-w/2,cy-h/2,cx-w/2+w,cy-h/2+h};}
Rect iconButton(Canvas& c,Targets& tg,int id,Rect hit,Icon i,ButtonState s,int size){
 hit=hitArea(hit);int well=std::min({width(hit),height(hit),size+36});
 if(s.selected)c.fillRound(centered(hit,well,well),t::RadiusSmall+4,t::InkPrimary);
 icon(c,i,centered(hit,size,size),s.selected?t::Inverse:s.disabled?t::Disabled:t::InkPrimary);
 if(!s.disabled)tg.add(id,hit);return hit;
}
Rect iconLabelButton(Canvas& c,Targets& tg,int id,Rect hit,Icon i,std::string_view label,ButtonState s){
 hit=hitArea(hit);std::uint32_t ink=s.disabled?t::Disabled:t::InkPrimary;
 if(s.selected)c.fillRound(inset(hit,6),t::RadiusSmall+2,t::SurfaceSelected);
 int block=t::Icon+12+int(t::Meta.size);int top=hit.y0+(height(hit)-block)/2;
 icon(c,i,{(hit.x0+hit.x1-t::Icon)/2,top,(hit.x0+hit.x1+t::Icon)/2,top+t::Icon},ink);
 text::drawAligned(c,MetaFont,hit,float(top+t::Icon+12+text::ascent(MetaFont)*0.82f),label,ink,Align::Center);
 if(!s.disabled)tg.add(id,hit);return hit;
}
Rect button(Canvas& c,Targets& tg,int id,Rect r,std::string_view label,ButtonKind k,bool disabled,Icon leading){
 Rect hit=hitArea(r);std::uint32_t fg=t::InkPrimary;const float radius=0; /* native buttons are square */
 if(k==ButtonKind::Primary){c.fillRound(r,radius,disabled?t::Disabled:t::ToolSelected);fg=t::Inverse;}
 else if(k==ButtonKind::Secondary||k==ButtonKind::Destructive){c.fill(r,t::Surface);c.hairline(r,disabled?t::Border:t::InkPrimary,2);fg=disabled?t::Disabled:k==ButtonKind::Destructive?t::Caution:t::InkPrimary;}
 else fg=disabled?t::Disabled:t::InkPrimary;
 Rect content=inset(r,t::S2);
 if(leading!=Icon::Count){float w=text::measure(LabelFont,label)+t::IconSmall+t::S2;int x=int(r.x0+(width(r)-w)/2);icon(c,leading,{x,(r.y0+r.y1-t::IconSmall)/2,x+t::IconSmall,(r.y0+r.y1+t::IconSmall)/2},fg);content.x0=x+t::IconSmall+t::S2;text::centeredLine(c,LabelFont,content,label,fg,Align::Left);}
 else text::centeredLine(c,LabelFont,content,label,fg,Align::Center);
 if(!disabled)tg.add(id,hit);return hit;
}
void panel(Canvas& c,Rect r,float radius,std::uint32_t border){c.fillRound(r,radius,t::Surface);c.strokeRound(r,radius,t::Hairline,border);}
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
 int x=r.x0;for(unsigned i=0;i<labels.size();++i){float w=text::measure(LabelFont,labels[i]);Rect cell{x,r.y0,x+int(w)+t::S5,r.y1};
  text::centeredLine(c,LabelFont,cell,labels[i],i==selected?t::InkPrimary:t::InkSecondary);
  if(i==selected)c.fillRound({cell.x0+t::S2,r.y1-6,cell.x1-t::S2,r.y1-1},2,t::InkPrimary);
  tg.add(first+int(i),hitArea(cell));x=cell.x1;}
 c.hline(r.x0,r.x1,r.y1,t::Border);
}
void chips(Canvas& c,Targets& tg,int first,Rect r,std::span<const std::string_view> labels,unsigned selected){
 int x=r.x0;for(unsigned i=0;i<labels.size();++i){float w=text::measure(MetaFont,labels[i]);Rect chip{x,r.y0,x+int(w)+t::S5,r.y1};
  if(i==selected)c.fillRound(chip,height(chip)/2.f,t::SurfaceSelected);
  text::centeredLine(c,MetaFont,chip,labels[i],i==selected?t::InkPrimary:t::InkSecondary);tg.add(first+int(i),hitArea(chip));x=chip.x1+t::S2;}
}
void toggle(Canvas& c,Targets& tg,int id,Rect hit,bool on,bool disabled){
 hit=hitArea(hit);Rect track=centered(hit,84,46);std::uint32_t fill=disabled?t::SurfaceSunken:on?t::InkPrimary:t::Surface;
 c.fillRound(track,23,fill);c.strokeRound(track,23,t::Hairline,disabled?t::Border:on?t::InkPrimary:t::BorderStrong);
 float knob=on?float(track.x1-23):float(track.x0+23);c.fillCircle(knob,(track.y0+track.y1)/2.f,17,on?t::Inverse:disabled?t::Border:t::InkSecondary);
 if(!disabled)tg.add(id,hit);
}
void swatch(Canvas& c,Targets& tg,int id,Rect hit,std::uint32_t color,bool selected,bool disabled,int d){
 hit=hitArea(hit);float cx=(hit.x0+hit.x1)/2.f,cy=(hit.y0+hit.y1)/2.f;
 if(selected)c.strokeCircle(cx,cy,d/2.f+9,3,t::InkPrimary);
 c.fillCircle(cx,cy,d/2.f,disabled?t::SurfaceSunken:color);
 if(color==t::Surface||(color&0xffffff)==0xffffff)c.strokeCircle(cx,cy,d/2.f-1,2,t::BorderStrong);
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
 Rect box{r.x0,r.y0,r.x1,r.y0+h};c.fillRound(box,t::RadiusSmall,t::SurfaceSunken);
 if(!caption.empty()){text::draw(c,CaptionFont,float(r.x0+t::S4),float(y+t::Caption.size),text::fit(CaptionFont,caption,float(width(r)-t::S5)),t::InkSecondary);y+=t::Meta.line+t::S1;}
 for(std::size_t i=0;i<lines.size();++i)text::draw(c,q,float(r.x0+t::S4),float(y+int(i)*lh+text::ascent(q)),lines[i],t::InkPrimary);
 return box.y1;
}
void divider(Canvas& c,int x0,int x1,int y){c.hline(x0,x1,y,t::Border);}
void segmented(Canvas& c,Targets& tg,int first,Rect r,std::span<const Icon> icons,unsigned selected){
 int n=int(icons.size());if(!n)return;int w=width(r)/n;
 c.strokeRound(r,t::RadiusSmall,t::Hairline,t::BorderStrong);
 for(int i=0;i<n;++i){Rect cell{r.x0+i*w,r.y0,i==n-1?r.x1:r.x0+(i+1)*w,r.y1};if(unsigned(i)==selected)c.fillRound(inset(cell,5),t::RadiusSmall-3,t::SurfaceSelected);
  if(i)c.vline(cell.x0,r.y0+12,r.y1-12,t::Border);icon(c,icons[std::size_t(i)],centered(cell,t::IconSmall+6,t::IconSmall+6),t::InkPrimary);tg.add(first+i,hitArea(cell));}
}
void pill(Canvas& c,Rect r,std::string_view s,bool filled){c.fillRound(r,height(r)/2.f,filled?t::InkPrimary:t::SurfaceSunken);text::centeredLine(c,CaptionFont,r,s,filled?t::Inverse:t::InkSecondary);}
Rect placeNear(Rect a,int w,int h,Rect b,int gap){
 b=inset(b,t::Gutter/2);int x=std::clamp((a.x0+a.x1)/2-w/2,b.x0,std::max(b.x0,b.x1-w));
 if(a.y1+gap+h<=b.y1)return {x,a.y1+gap,x+w,a.y1+gap+h};
 if(a.y0-gap-h>=b.y0)return {x,a.y0-gap-h,x+w,a.y0-gap};
 int y=a.y1+gap+h<=b.y1?a.y1+gap:std::max(b.y0,b.y1-h);return {x,y,x+w,y+h};
}
int paragraph(Canvas& c,Rect r,std::string_view s,Font f,int lh,std::uint32_t color,int maxLines){return text::block(c,f,r,s,color,lh,maxLines);}
}
