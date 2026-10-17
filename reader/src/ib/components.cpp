// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/components.h"
#include <algorithm>
#include <vector>
namespace ib {
namespace t=theme;
namespace {
const Font LabelFont{Face::Text,float(t::Label.size)},MetaFont{Face::Text,float(t::Meta.size)},CaptionFont{Face::Text,float(t::Caption.size)};
}
void bubble(Canvas& c,Rect r,std::uint32_t fill,float radius,bool){
 radius=std::min(radius,std::min(width(r),height(r))/2.f);c.fillRound(r,radius,fill);c.strokeRound(r,radius,float(t::Outline),t::InkPrimary);}
void popover(Canvas& c,Rect r){bubble(c,r,t::Surface,t::RadiusMedium);}
void panel(Canvas& c,Rect r,float radius,std::uint32_t){bubble(c,r,t::Surface,radius);}
void swash(Canvas& c,Font f,float x,float baseline,std::string_view s,std::uint32_t color){
 const float w=text::measure(f,s);
 if(color==t::InkPrimary){c.fill({int(x),int(baseline)+8,int(x+w),int(baseline)+8+t::StrokeStrong+1},t::InkPrimary);text::draw(c,f,x,baseline,s,t::InkPrimary);return;}
 // A highlighter stroke under the lower half of the x-height: slanted ends, a crisp ink edge along its foot.
 const Font drawn=text::resolved(f);const float xh=drawn.size*(drawn.face==Face::Script||drawn.face==Face::ScriptBold?0.357f:0.5f);
 const float top=baseline-xh*0.62f,foot=baseline+std::max(float(t::EdgeBand)*0.5f,xh*0.2f),skew=(foot-top)*0.35f;
 Path band;band.move(x-8+skew,top).line(x+w+12,top).line(x+w+12-skew,foot).line(x-8,foot).close();c.fillPath(band,color);
 c.line(x-8,foot,x+w+12-skew,foot,float(t::Hairline),t::InkPrimary);text::draw(c,f,x,baseline,s,t::InkPrimary);}
void inkDrop(Canvas& c,float cx,float cy,float r,std::uint32_t color){
 Path p;p.move(cx,cy-r*1.9f).cubic(cx+r*0.35f,cy-r*1.3f,cx+r,cy-r*0.75f,cx+r,cy).arc(cx,cy,r,0,180).cubic(cx-r,cy-r*0.75f,cx-r*0.35f,cy-r*1.3f,cx,cy-r*1.9f).close();
 c.fillPath(p,color);c.stroke(p,float(t::Hairline),t::InkPrimary);}
namespace {
// Where the stem of the dotless i ends: drawn into scratch so the dot sits over the stem in whichever face is
// current (cursive or plain), independent of the caller's clip.
struct Stem {float x,top;};
Stem stemOf(Font f){
 const float i0=text::measure(f,"Ink");const int w=int(f.size)+8,h=int(f.size*2.2f),base=int(f.size*1.5f);
 std::vector<std::uint32_t> px(std::size_t(w)*h,0xffffffffu);Canvas s(spike::Surface{px,w,h,w});text::draw(s,f,4,float(base),"\xC4\xB1",0xff000000u);
 for(int y=0;y<h;++y){int sum=0,n=0;for(int x=0;x<w;++x)if((px[std::size_t(y)*w+x]&255u)<96){sum+=x;++n;}if(n)return {i0+float(sum)/n-4,float(y-base)};}
 return {i0+f.size*0.1f,-f.size*0.4f};}
}
void wordmark(Canvas& c,Rect box,float size){
 // "Inkie" in heavy script (or semibold sans with Plain text); the dot of the i is a magenta drop of ink.
 const Font f{Face::TextSemibold,size};const std::string_view word="Ink\xC4\xB1" "e";const float w=text::measure(f,word);
 const float x=box.x0+(width(box)-w)/2,b=box.y0+(height(box)+text::capHeight(f))/2.f;text::draw(c,f,x,b,word,t::InkPrimary);
 const Font drawn=text::resolved(f);const Stem st=stemOf(f);const float r=std::max(float(t::EdgeBand)*0.6f,drawn.size*0.075f);
 inkDrop(c,x+st.x,b+st.top-r*1.7f,r,t::PopMagenta);}
void contextSheet(Canvas& c,int top,std::uint32_t accent){
 // Square, full-width paper with its colour along the top edge, closed by an ink rule (as an index card).
 c.fill({0,top,t::ScreenWidth,t::ScreenHeight},t::Surface);c.fill({0,top,t::ScreenWidth,top+t::EdgeBand},accent);
 c.fill({0,top+t::EdgeBand,t::ScreenWidth,top+t::EdgeBand+t::Hairline},t::InkPrimary);}
void sheetTitle(Canvas& c,Targets& tg,int top,int h,std::string_view title,std::string_view subtitle,int subtitleId,int rightEdge){
 const Font tf{Face::TextSemibold,theme::Subhead.size},sf{Face::Text,theme::Meta.size};text::draw(c,tf,70.f,float(top+62),title,t::InkPrimary);
 if(!subtitle.empty()){Rect s{70,top+76,rightEdge-t::S3,top+h-16};text::centeredLine(c,sf,s,text::fit(sf,subtitle,float(width(s))),t::InkSecondary,Align::Left);if(subtitleId>=0)tg.add(subtitleId,s);}
}
void sheetQuote(Canvas& c,Rect r,std::string_view s,std::uint32_t rule){const Font qf{Face::SerifItalic,theme::Label.size}; // the book's words stay serif; the bar is the highlight's colour
 const Rect bar{r.x0+34,r.y0+8,r.x0+48,r.y1-8};c.fill(bar,rule);c.hairline(bar,t::InkPrimary,t::Hairline);
 Rect q{r.x0+68,r.y0,r.x1,r.y1};text::centeredLine(c,qf,q,text::fit(qf,s,float(width(q))),t::InkSecondary,Align::Left);}
void ruledPaper(Canvas& c,Rect a,int gap){for(int y=a.y0+gap-6;y<a.y1-8;y+=gap)c.hline(a.x0,a.x1,y,0xffe2e2e2u,1);}
Rect primaryAction(Canvas& c,Targets& tg,int id,Rect r,std::string_view label,Icon leading,bool disabled){
 // Ink button with paper text, as the native Send and Done.
 const std::uint32_t fill=disabled?t::Surface:t::GoAccent,ink=disabled?t::Disabled:t::onPop(fill);
 c.fillRound(r,t::RadiusMedium,fill);c.strokeRound(r,t::RadiusMedium,float(t::Outline),disabled?t::Disabled:t::InkPrimary);
 const Font f{Face::TextSemibold,theme::Label.size};float w=text::measure(f,label)+(leading!=Icon::Count?30+14:0);int x=int(r.x0+(width(r)-w)/2);
 if(leading!=Icon::Count){int mid=(r.y0+r.y1)/2;icon(c,leading,{x,mid-15,x+30,mid+15},ink);x+=44;}
 text::centeredLine(c,f,{x,r.y0,r.x1,r.y1},label,ink,Align::Left);if(!disabled)tg.add(id,hitArea(r));return r;}
Rect textAction(Canvas& c,Targets& tg,int id,Rect r,std::string_view label,bool underline,bool disabled,Align a){
 const Font f{underline?Face::TextSemibold:Face::Text,theme::Control.size};std::uint32_t ink=disabled?t::Disabled:t::InkPrimary;text::centeredLine(c,f,r,label,ink,a);
 if(underline){float w=text::measure(f,label);int x0=a==Align::Right?r.x1-int(w):r.x0;const int y=(r.y0+r.y1)/2+18;c.fill({x0,y,x0+int(w),y+t::StrokeStrong},ink);}
 if(!disabled)tg.add(id,hitArea(r));return r;}
Rect hitArea(Rect v,int m)noexcept{int w=std::max(width(v),m),h=std::max(height(v),m);int cx=(v.x0+v.x1)/2,cy=(v.y0+v.y1)/2;return {cx-w/2,cy-h/2,cx-w/2+w,cy-h/2+h};}
Rect iconButton(Canvas& c,Targets& tg,int id,Rect hit,Icon i,ButtonState s,int size){
 // Selected: a rounded ink tile with a paper glyph (or the place's colour, e.g. a yellow Favorites star).
 hit=hitArea(hit);int well=std::min({width(hit),height(hit),size+36});const std::uint32_t fill=s.accent?s.accent:t::ToolSelected;
 if(s.selected){const Rect b=centered(hit,well,well);c.fillRound(b,t::RadiusMedium,fill);if(fill!=t::InkPrimary)c.strokeRound(b,t::RadiusMedium,float(t::Outline),t::InkPrimary);}
 icon(c,i,centered(hit,size,size),s.selected?t::onPop(fill):s.disabled?t::Disabled:t::InkPrimary);
 if(!s.disabled)tg.add(id,hit);return hit;
}
Rect iconLabelButton(Canvas& c,Targets& tg,int id,Rect hit,Icon i,std::string_view label,ButtonState s){
 hit=hitArea(hit);std::uint32_t ink=s.disabled?t::Disabled:t::InkPrimary;
 const std::uint32_t fill=s.accent?s.accent:t::ToolSelected;if(s.selected){c.fillRound(inset(hit,6),t::RadiusMedium,fill);if(fill!=t::InkPrimary)c.strokeRound(inset(hit,6),t::RadiusMedium,float(t::Outline),t::InkPrimary);ink=t::onPop(fill);}
 int block=t::Icon+12+int(t::Meta.size);int top=hit.y0+(height(hit)-block)/2;
 icon(c,i,{(hit.x0+hit.x1-t::Icon)/2,top,(hit.x0+hit.x1+t::Icon)/2,top+t::Icon},ink);
 text::drawAligned(c,MetaFont,hit,float(top+t::Icon+12+text::ascent(MetaFont)*0.82f),label,ink,Align::Center);
 if(!s.disabled)tg.add(id,hit);return hit;
}
Rect button(Canvas& c,Targets& tg,int id,Rect r,std::string_view label,ButtonKind k,bool disabled,Icon leading){
 // Rounded rectangles: primary ink, destructive red, secondary paper with an ink rule, quiet text only.
 Rect hit=hitArea(r);std::uint32_t fg=t::InkPrimary;const float radius=std::min(t::RadiusMedium,height(r)/2.f);
 if(k==ButtonKind::Quiet)fg=disabled?t::Disabled:t::InkPrimary;
 else{const std::uint32_t fill=disabled?t::Surface:k==ButtonKind::Primary?t::GoAccent:k==ButtonKind::Destructive?t::Caution:t::Surface;
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
namespace {
// Underlined text tabs over a dotted rule; the chosen one in heavier type on a solid ink bar.
void underlineTabs(Canvas& c,Targets& tg,int first,Rect r,std::span<const std::string_view> labels,unsigned selected,Font f,int step){
 const Font on{Face::TextSemibold,f.size};int x=r.x0;
 for(unsigned i=0;i<labels.size();++i){float w=text::span(f,labels[i]);Rect cell{x,r.y0,x+int(w)+t::S5,r.y1};const bool sel=i==selected;
  text::centeredLine(c,sel?on:f,{cell.x0,cell.y0,cell.x1,cell.y1-8},labels[i],t::InkPrimary);
  if(sel)c.fill({cell.x0+t::S2,r.y1-6,cell.x1-t::S2,r.y1},t::InkPrimary);
  tg.add(first+int(i),hitArea(cell));x=cell.x1+step;}
 divider(c,r.x0,r.x1,r.y1);}
}
void tabs(Canvas& c,Targets& tg,int first,Rect r,std::span<const std::string_view> labels,unsigned selected){underlineTabs(c,tg,first,r,labels,selected,LabelFont,0);}
void chips(Canvas& c,Targets& tg,int first,Rect r,std::span<const std::string_view> labels,unsigned selected){underlineTabs(c,tg,first,r,labels,selected,MetaFont,t::S2);}
void toggle(Canvas& c,Targets& tg,int id,Rect hit,bool on,bool disabled){
 // A checkbox: a rounded square, filled ink with a paper tick when on.
 hit=hitArea(hit);const Rect box=centered(hit,48,48);const std::uint32_t edge=disabled?t::Disabled:t::InkPrimary;
 if(on){c.fillRound(box,t::RadiusSmall,edge);Path tick;tick.move(box.x0+12.f,box.y0+25.f).line(box.x0+21.f,box.y0+34.f).line(box.x0+37.f,box.y0+15.f);c.stroke(tick,4.5f,t::Surface);}
 else{c.fillRound(box,t::RadiusSmall,t::Surface);c.strokeRound(box,t::RadiusSmall,float(t::StrokeStrong),edge);}
 if(!disabled)tg.add(id,hit);
}
void swatch(Canvas& c,Targets& tg,int id,Rect hit,std::uint32_t color,bool selected,bool disabled,int d){
 hit=hitArea(hit);float cx=(hit.x0+hit.x1)/2.f,cy=(hit.y0+hit.y1)/2.f;
 if(selected)c.strokeCircle(cx,cy,d/2.f+9,4.f,t::InkPrimary);
 c.fillCircle(cx,cy,d/2.f,disabled?t::Surface:color);c.strokeCircle(cx,cy,d/2.f-1,float(t::Outline),disabled?t::Disabled:t::InkPrimary);
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
 Rect box{r.x0,r.y0,r.x1,r.y0+h};c.fillRound(box,t::RadiusMedium,t::Surface);c.strokeRound(box,t::RadiusMedium,float(t::Outline),t::InkPrimary);
 if(!caption.empty()){text::draw(c,CaptionFont,float(r.x0+t::S4),float(y+t::Caption.size),text::fit(CaptionFont,caption,float(width(r)-t::S5)),t::InkSecondary);y+=t::Meta.line+t::S1;}
 for(std::size_t i=0;i<lines.size();++i)text::draw(c,q,float(r.x0+t::S4),float(y+int(i)*lh+text::ascent(q)),lines[i],t::InkPrimary);
 return box.y1;
}
// Dotted ink: 2 px dots every 6 px read as a quiet rule without the grain a grey line gets on the colour panel.
void divider(Canvas& c,int x0,int x1,int y){for(int x=x0;x+2<=x1;x+=6)c.fill({x,y,x+2,y+2},t::InkPrimary);}
void segmented(Canvas& c,Targets& tg,int first,Rect r,std::span<const Icon> icons,unsigned selected){
 int n=int(icons.size());if(!n)return;int w=width(r)/n;
 c.fillRound(r,t::RadiusMedium,t::Surface);c.strokeRound(r,t::RadiusMedium,float(t::Outline),t::InkPrimary);
 for(int i=0;i<n;++i){Rect cell{r.x0+i*w,r.y0,i==n-1?r.x1:r.x0+(i+1)*w,r.y1};const bool on=unsigned(i)==selected;
  if(on)c.fillRound(inset(cell,6),t::RadiusSmall,t::ToolSelected);
  icon(c,icons[std::size_t(i)],centered(cell,t::IconSmall+6,t::IconSmall+6),on?t::OnSelected:t::InkPrimary);tg.add(first+i,hitArea(cell));}
}
void pill(Canvas& c,Rect r,std::string_view s,bool filled){const float R=std::min(t::RadiusSmall,height(r)/2.f);c.fillRound(r,R,filled?t::ToolSelected:t::Surface);c.strokeRound(r,R,float(t::Outline),t::InkPrimary);text::centeredLine(c,CaptionFont,r,s,filled?t::OnSelected:t::InkPrimary);}
Rect placeNear(Rect a,int w,int h,Rect b,int gap){
 b=inset(b,t::Gutter/2);int x=std::clamp((a.x0+a.x1)/2-w/2,b.x0,std::max(b.x0,b.x1-w));
 if(a.y1+gap+h<=b.y1)return {x,a.y1+gap,x+w,a.y1+gap+h};
 if(a.y0-gap-h>=b.y0)return {x,a.y0-gap-h,x+w,a.y0-gap};
 int y=a.y1+gap+h<=b.y1?a.y1+gap:std::max(b.y0,b.y1-h);return {x,y,x+w,y+h};
}
int paragraph(Canvas& c,Rect r,std::string_view s,Font f,int lh,std::uint32_t color,int maxLines){return text::block(c,f,r,s,color,lh,maxLines);}
}
