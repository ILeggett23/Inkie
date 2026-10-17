// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/reader_screen.h"
#include <cmath>
#include <algorithm>
#include <array>
namespace ib::reader {
namespace t=theme;
namespace {
const Font Caption{Face::Text,float(t::Caption.size)};
constexpr std::array<Instrument,9> Pens{Instrument::Ballpoint,Instrument::Fineliner,Instrument::Pencil,Instrument::Mechanical,Instrument::Paintbrush,Instrument::Calligraphy,Instrument::Marker,Instrument::Shader,Instrument::Highlighter};
std::string pageText(const Chrome& c){return std::to_string(c.page+1)+" of "+std::to_string(std::max(1,c.total));}
}
namespace {Side side_=Side::Left;}
Side toolbarSide()noexcept{return side_;}
void setToolbarSide(Side s)noexcept{side_=s;}
Rect toolbarRect()noexcept{int x0=side_==Side::Left?0:t::ScreenWidth-ToolbarWidth;return {x0,0,x0+ToolbarWidth,t::PageFitHeight};}
Rect toolbarSlot(unsigned k)noexcept{if(k>=ToolbarSlots)return {};Rect r=toolbarRect();int y=int(k)*ToolbarItem;return {r.x0,y,r.x1,y+ToolbarItem};}
Rect closeRect()noexcept{return side_==Side::Left?Rect{t::ScreenWidth-ToolbarItem,0,t::ScreenWidth,ToolbarItem}:Rect{0,0,ToolbarItem,ToolbarItem};}
Rect topRect()noexcept{return {0,0,t::ScreenWidth,ToolbarItem};}
Rect toolHandleRect()noexcept{return toolbarSlot(SlotHandle);}
void closeButton(Canvas& c,Targets& tg){Rect r=closeRect();c.fill(r,t::Paper);dot(c,centered(r,68,68),Icon::Close,t::Surface);tg.add(Back,r);}
void zoomBadge(Canvas& c,Targets& tg,int percent){const Rect r=ZoomBadgeRect;c.fill(r,t::Paper);const Rect b=inset(r,3);c.fillRound(b,height(b)/2.f,t::PopYellow);c.strokeRound(b,height(b)/2.f,float(t::Outline),t::InkPrimary);text::centeredLine(c,{Face::TextSemibold,theme::Label.size},r,std::to_string(percent)+"%",t::InkPrimary,Align::Center);tg.add(ZoomBadge,r);}
void footer(Canvas& c,Targets& tg,const Chrome& m){
 // Quiet metadata under the page, as in the native reader: title or chapter left, position right.
 Rect r=FooterRect;c.fill(r,t::Paper);Rect text{FooterInset,r.y0,r.x1-FooterInset,r.y1};const Font f{Face::Text,theme::Meta.size};
 if(!m.status.empty()){text::centeredLine(c,Caption,text,text::fit(Caption,text::quiet(m.status),float(width(text))),t::InkSecondary,Align::Center);tg.add(Footer,r);return;}
 // Reading progress off: no chapter or position (notices above still show; the menu mark stays the way to the tools).
 if(!m.progress&&!m.loading){if(m.menuMark){int mid=(r.y0+r.y1)/2;icon(c,Icon::Menu,{text.x1-t::IconSmall,mid-t::IconSmall/2,text.x1,mid+t::IconSmall/2},t::InkSecondary);}tg.add(Footer,r);return;}
 // Progress: a cyan bubble bar along the top of the footer (changes only with the page, like the counter beside it).
 if(!m.loading&&m.total>1){const Rect track{text.x0,r.y0+12,text.x1,r.y0+24};const int fill=int(std::lround(double(width(track))*(m.page+1)/m.total));c.fillRound(track,6,t::SurfaceSunken);
  if(fill>12)c.fillRound({track.x0,track.y0,track.x0+fill,track.y1},6,t::PopCyan);c.strokeRound(track,6,2.f,t::InkPrimary);}
 std::string page=m.loading?"Loading\xE2\x80\xA6":pageText(m);float pw=text::measure(f,page);
 int right=text.x1;text::centeredLine(c,f,{right-int(pw)-t::S1,r.y0,right,r.y1},page,t::InkSecondary,Align::Right);right-=int(pw)+t::S3;
 // Shown only when the toolbar handle is switched off: the footer is then the way into the toolbar.
 if(m.menuMark){int mid=(r.y0+r.y1)/2;icon(c,Icon::Menu,{right-t::IconSmall,mid-t::IconSmall/2,right,mid+t::IconSmall/2},t::InkSecondary);right-=t::IconSmall+t::S3;}
 text::centeredLine(c,f,{text.x0,r.y0,right,r.y1},text::fit(f,m.chapter,float(right-text.x0)),t::InkSecondary,Align::Left);
 tg.add(Footer,r);
}
void cleanZones(Targets& tg){tg.add(RevealZone,FooterRect);}
// Palette layout (reference mockup): title row, 3x3 tips, rule, width title, three widths, rule, 3x3 colors.
Rect penPanelRect()noexcept{Rect tb=toolbarRect();int y=toolbarSlot(SlotPen).y0;int x0=side_==Side::Left?tb.x1-2:tb.x0-PenPanelWidth+2;return {x0,y,x0+PenPanelWidth,y+PenPanelHeight};}
namespace {
int cellWidth(Rect r)noexcept{return (width(r)-PalettePad*2-PaletteGap*2)/3;}
Rect gridCell(Rect r,int y,int rowHeight,unsigned i)noexcept{int cw=cellWidth(r),col=int(i%3),row=int(i/3);int x=r.x0+PalettePad+col*(cw+PaletteGap);int yy=y+row*(rowHeight+PaletteGap);return {x,yy,x+cw,yy+rowHeight};}
int widthTitleY(Rect r)noexcept{return r.y0+PaletteTitle+10+3*NibRow+2*PaletteGap+PalettePad+2;}
int swatchY(Rect r)noexcept{return r.y0+PenPanelCompactHeight+2+30;}
}
Rect penCell(unsigned i,Rect r)noexcept{if(i>=9)return {};return gridCell(r,r.y0+PaletteTitle+10,NibRow,i);}
Rect widthCell(unsigned i,Rect r)noexcept{if(i>=3)return {};return gridCell(r,widthTitleY(r)+PaletteTitle,WidthRow,i);}
Rect colorCell(unsigned i,Rect r)noexcept{if(i>=8)return {};return gridCell(r,swatchY(r),SwatchRow,i);}
void toolbar(Canvas& c,Targets& tg,const Tools& m){
 // A rounded rail inside the column; each tool is a bubble, and the active one fills with that tool's own pop colour.
 Rect r=toolbarRect();c.fill(r,t::Paper);tg.add(RailBlank,r);{const Rect rail=inset(r,6);c.strokeRound(rail,width(rail)/2.f,float(t::Outline),t::InkPrimary);}
 auto tile=[&](unsigned slot,int id,bool selected,bool enabled,std::uint32_t pop,auto&& draw){Rect cell=toolbarSlot(slot);
  if(selected){const float cx=(cell.x0+cell.x1)/2.f,cy=(cell.y0+cell.y1)/2.f;c.fillCircle(cx,cy,42,pop);c.strokeCircle(cx,cy,40.5f,float(t::Outline),t::InkPrimary);}
  draw(centered(cell,ToolbarGlyph,ToolbarGlyph),selected?t::onPop(pop):enabled?t::ToolbarInk:t::Disabled,cell);if(enabled||selected)tg.add(id,cell);};
 // Pigment: the shortcut's current colour as a small bubble in the tile's upper corner.
 auto pigment=[&](Rect cell,std::uint32_t color){float cx=float(side_==Side::Left?cell.x1-26:cell.x0+26),cy=float(cell.y0+22);c.fillCircle(cx,cy,12.f,color);c.strokeCircle(cx,cy,11.f,2.5f,t::InkPrimary);};
 // One black tile at a time: an open document menu takes the selection from the writing tool.
 const unsigned shortcut=m.activeShortcut<2?m.activeShortcut:(m.highlighter?1u:0u);
 const bool tool=!m.menu,penActive=tool&&!m.eraser&&!m.selecting&&shortcut==0,hlActive=tool&&!m.eraser&&!m.selecting&&shortcut==1;
 tile(SlotHandle,ToolHandle,false,true,t::PopCyan,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::ToolbarHandle,centered(b,HandleGlyph,HandleGlyph),col);});
 tile(SlotPen,RailPen,penActive,true,t::PopCyan,[&](Rect b,std::uint32_t col,Rect cell){nibGlyph(c,m.activeShortcut<2?m.primary:m.pen,b,col);pigment(cell,m.activeShortcut<2?t::Ink[m.primaryColor%8]:m.ink);});
 tile(SlotHighlighter,RailHighlighter,hlActive,true,t::PopYellow,[&](Rect b,std::uint32_t col,Rect cell){nibGlyph(c,m.activeShortcut<2?m.secondary:Instrument::Highlighter,b,col);pigment(cell,t::Ink[(m.activeShortcut<2?m.secondaryColor:m.highlightColor)%8]);});
 tile(SlotEraser,RailEraser,tool&&m.eraser&&!m.selecting,true,t::PopMagenta,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::NativeEraser,b,col);});
 tile(SlotSelect,RailSelect,tool&&m.selecting,true,t::PopGreen,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::NativeSelection,b,col);});
 tile(SlotLayers,RailLayers,m.layers,true,t::PopOrange,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::NativeLayers,b,col);});
 tile(SlotUndo,RailUndo,false,m.canUndo&&!m.busy,t::PopCyan,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::NativeUndo,b,col);});
 tile(SlotRedo,RailRedo,false,m.canRedo&&!m.busy,t::PopCyan,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::NativeRedo,b,col);});
 tile(SlotDocument,RailMore,m.menu,true,t::PopViolet,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::DocumentMenu,b,col);});
}
void toolHandle(Canvas& c,Targets& tg,const Tools&){Rect r=toolHandleRect();c.fill(r,t::Paper);dot(c,centered(r,76,76),Icon::Count,t::PopCyan);icon(c,Icon::ToolbarHandle,centered(r,HandleGlyph,HandleGlyph),t::onPop(t::PopCyan));tg.add(ToolHandle,r);}
void penPanel(Canvas& c,Targets& tg,const Tools& m,Rect at){
 Rect r=at;if(!m.colors)r.y1=r.y0+PenPanelCompactHeight;popover(c,r);
 const Font title{Face::TextSemibold,theme::Label.size},label{Face::Text,theme::Meta.size};
 // Selected cells are pop bubbles; rules stop short of the bubble's outline and shadow.
 auto pick=[&](Rect cell,std::uint32_t pop){const Rect b=inset(cell,4);c.fillRound(b,t::RadiusMedium,pop);c.strokeRound(b,t::RadiusMedium,float(t::Outline),t::InkPrimary);};
 auto rule=[&](int y){c.hline(r.x0+t::S3,r.x1-t::PopShadow-t::S3,y,t::InkPrimary,t::Hairline);};
 const char* name=m.eraser?"Eraser":instrumentName(m.highlighter?Instrument::Highlighter:m.pen);
 text::centeredLine(c,title,{r.x0+30,r.y0,r.x1-30,r.y0+PaletteTitle},name,t::InkPrimary,Align::Left);
 if(!m.status.empty())text::centeredLine(c,Caption,{r.x0+200,r.y0,r.x1-24,r.y0+PaletteTitle},text::fit(Caption,text::quiet(m.status),float(width(r)-224)),t::InkSecondary,Align::Right);
 // Tips: the selected one sits in a cyan bubble.
 for(std::size_t i=0;i<Pens.size();++i){Rect cell=penCell(unsigned(i),r);
  bool sel=!m.eraser&&(Pens[i]==Instrument::Highlighter?m.highlighter:(!m.highlighter&&m.pen==Pens[i]));
  const bool available=m.colors||Pens[i]!=Instrument::Highlighter;
  if(sel&&available)pick(cell,t::ToolSelected);nibGlyph(c,Pens[i],centered(cell,55,55),!available?t::Disabled:sel?t::OnSelected:t::InkPrimary);if(available)tg.add(PenBase+int(i),cell);}
 int wy=widthTitleY(r);rule(wy-2);
 static constexpr std::array<const char*,3> WidthName{"Thin","Medium","Thick"};
 text::centeredLine(c,title,{r.x0+30,wy,r.x1-30,wy+PaletteTitle},WidthName[unsigned(m.width)%3],t::InkPrimary,Align::Left);
 // Widths: a diagonal stroke at each thickness.
 const float stroke[]={2.5f,5.5f,9.5f};
 for(int i=0;i<3;++i){Rect cell=widthCell(unsigned(i),r);bool sel=unsigned(m.width)==unsigned(i);if(sel)pick(cell,t::PopYellow);
  Rect g=centered(cell,58,58);float k=58.f/60.f;c.line(g.x0+13*k,g.y0+47*k,g.x0+47*k,g.y0+13*k,stroke[i]*(m.highlighter?1.6f:1.f),t::InkPrimary);tg.add(WidthBase+i,cell);}
 // Colors (not for the eraser): persisted palette indices, labelled.
 if(!m.eraser&&m.colors){rule(swatchY(r)-30);
  for(unsigned i=0;i<8;++i){Rect cell=colorCell(i,r);bool unavailable=m.highlighter&&i==0;bool sel=(m.highlighter?m.highlightColor:m.color)==i;bool off=unavailable||m.busy;
   if(sel)pick(cell,t::ToolSelected);float cx=float(cell.x0+cell.x1)/2,cy=float(cell.y0+38);c.fillCircle(cx,cy,19.f,unavailable?t::Paper:t::Ink[i]);c.strokeCircle(cx,cy,18.f,float(t::Outline),off?t::Disabled:t::InkPrimary);
   text::centeredLine(c,label,{cell.x0,cell.y0+62,cell.x1,cell.y1-8},t::InkName[i],sel?t::OnSelected:off?t::Disabled:t::InkPrimary,Align::Center);if(!off)tg.add(ColorBase+int(i),cell);}}
 if(m.colors&&m.highlighter&&!m.eraser){Rect hit{r.x0+24,r.y1-96,r.x1-24,r.y1-8};rule(hit.y0);text::centeredLine(c,label,{hit.x0,hit.y0,hit.x1-112,hit.y1},"Snap to Text",t::InkPrimary,Align::Left);toggle(c,tg,SnapHighlights,{hit.x1-100,hit.y0,hit.x1,hit.y1},m.snapHighlights,!m.canSnap);if(m.canSnap)tg.add(SnapHighlights,hit);}
}
Rect selectionToolbar(Canvas& c,Targets& tg,const SelectionBar& m,Rect bounds){
 // Context strip: one bubble, each action's glyph on a pop dot in the colour of where it leads (Note magenta like
 // Notes, Tom violet like Tom), cursive labels, short dividers.
 struct Item{int id;const char* label;Icon icon;bool nib;std::uint32_t pop;};std::array<Item,6> items{};unsigned n=0;
 if(m.define)items[n++]={SelDefine,"Define",Icon::BookOpen,false,t::PopCyan};
 if(m.highlight)items[n++]={SelHighlight,m.highlighted?"Color":"Highlight",Icon::Highlight,true,t::PopYellow};
 if(m.note)items[n++]={SelNote,"Note",Icon::Note,false,t::NoteAccent};if(m.tom)items[n++]={SelTom,"Tom",Icon::Tom,false,t::TomAccent};
 if(m.copy)items[n++]={SelCopy,"Copy",Icon::Copy,false,t::PopGreen};if(m.more)items[n++]={SelMore,"",Icon::More,false,0};
 const Font f{Face::Text,theme::Control.size};const int h=98,pad=t::S3+4,glyph=31,gap=14;std::array<int,6> ws{};int w=0;
 for(unsigned i=0;i<n;++i){ws[i]=items[i].label[0]?int(text::span(f,items[i].label))+glyph+gap+pad*2:t::MinTarget;w+=ws[i];}
 // Prefer above the passage so the text being read stays visible.
 Rect r;int x=std::clamp((m.anchor.x0+m.anchor.x1)/2-w/2,bounds.x0+t::S3,bounds.x1-t::S3-w);
 if(m.anchor.y0-t::S3-h>=std::max(bounds.y0,layout::TopRow))r={x,m.anchor.y0-t::S3-h,x+w,m.anchor.y0-t::S3};else r=placeNear(m.anchor,w,h,bounds,t::S3);
 popover(c,r);
 const int body=r.y1-t::PopShadow;
 int cx=r.x0;for(unsigned i=0;i<n;++i){Rect seg{cx,r.y0,cx+ws[i],r.y1};cx=seg.x1;if(i)c.vline(seg.x0,r.y0+18,body-18,t::InkPrimary,t::Hairline);
  const std::uint32_t ink=items[i].id==SelHighlight&&m.highlighted?t::InkSecondary:t::InkPrimary;
  if(!items[i].label[0]){icon(c,items[i].icon,centered({seg.x0,r.y0,seg.x1,body},34,34),ink);tg.add(items[i].id,hitArea(seg));continue;}
  int gx=seg.x0+pad,mid=(r.y0+body)/2;Rect g{gx,mid-glyph/2,gx+glyph,mid+glyph/2};
  {const float dx=(g.x0+g.x1)/2.f,dy=(g.y0+g.y1)/2.f;c.fillCircle(dx,dy,24,items[i].pop);c.strokeCircle(dx,dy,22.5f,2.5f,t::InkPrimary);}
  const std::uint32_t on=t::onPop(items[i].pop);if(items[i].nib)nibGlyph(c,Instrument::Highlighter,inset(g,3),on);else icon(c,items[i].icon,inset(g,3),on);
  text::centeredLine(c,f,{g.x1+gap,r.y0,seg.x1,body},items[i].label,ink,Align::Left);tg.add(items[i].id,hitArea(seg));}
 return r;
}
Rect highlightColors(Canvas& c,Targets& tg,Rect bar,Rect passage,unsigned selected,bool removable){
 // A pill bubble of swatches beside the selection bar, no caption.
 const int h=112,cw=88;int n=int(t::HighlightOrder.size())+(removable?1:0);int w=n*cw+t::S3*2;bool above=bar.y1<=passage.y0+4;
 Rect r=above?Rect{bar.x0,bar.y0-t::S2-h,bar.x0+w,bar.y0-t::S2}:Rect{bar.x0,bar.y1+t::S2,bar.x0+w,bar.y1+t::S2+h};
 if(r.x1>t::ScreenWidth-t::S3){r.x0-=r.x1-(t::ScreenWidth-t::S3);r.x1=t::ScreenWidth-t::S3;}
 if(r.y0<0||r.y1>t::PageFitHeight){r={r.x0,passage.y1+t::S2,r.x1,passage.y1+t::S2+h};}
 bubble(c,r,t::Surface,float(h/2));
 for(int i=0;i<int(t::HighlightOrder.size());++i){unsigned idx=t::HighlightOrder[std::size_t(i)];Rect cell{r.x0+t::S3+i*cw,r.y0+8,r.x0+t::S3+(i+1)*cw,r.y1-8};swatch(c,tg,HighlightColorBase+int(idx),cell,t::Ink[idx],idx==selected,false,48);}
 if(removable){Rect cell{r.x0+t::S3+int(t::HighlightOrder.size())*cw,r.y0+8,r.x0+t::S3+(int(t::HighlightOrder.size())+1)*cw,r.y1-8};icon(c,Icon::Trash,centered(cell,40,40),t::InkSecondary);tg.add(HighlightRemove,hitArea(cell));}
 return r;
}
Rect definitionCard(Canvas& c,Targets& tg,Rect anchor,const Definition& d,Rect bounds){
 const int w=d.expanded?940:710,pad=40,lh=46;
 const Font word{Face::SerifSemibold,theme::Heading.size},pos{Face::Text,theme::Control.size},body{Face::Serif,theme::Body.size},src{Face::Text,theme::Meta.size};
 const bool none=!d.loading&&d.senses.empty();
 std::string tag=d.pronunciation.empty()?(!d.partOfSpeech.empty()?d.partOfSpeech:!d.senses.empty()?d.senses[0].partOfSpeech:""):d.pronunciation;
 std::vector<std::string> all,compact;bool truncated=false;
 if(d.loading||none){const auto line=d.loading?"Looking up\xE2\x80\xA6":!d.status.empty()&&d.status!="NOT_FOUND"?text::quiet(d.status):"No definition found.";all.push_back(line);compact=all;}
 else for(std::size_t i=0;i<d.senses.size()&&i<3;++i){
  auto lines=text::wrap(body,d.senses[i].definition,float(w-pad*2-40),256);truncated|=lines.size()>3;
  for(std::size_t k=0;k<lines.size();++k){auto line=(d.senses.size()>1&&k==0?std::to_string(i+1)+". ":"")+lines[k];all.push_back(line);if(k<3)compact.push_back(line);}
  if(i+1<std::min<std::size_t>(3,d.senses.size())){all.emplace_back();compact.emplace_back();}
 }
 const bool controls=!d.loading&&!none&&(truncated||d.expanded);
 const std::string source=d.attribution.empty()?"":d.attribution+" \xC2\xB7 Available offline";
 const int fixed=36+56+(tag.empty()?0:40)+t::S3+(source.empty()?0:50)+t::S2+(controls?72:0)+(d.actions?100:t::S3);
 const unsigned capacity=unsigned(std::max(1,std::min(20,(std::min(1450,height(bounds)-t::S4*2)-fixed)/lh)));
 const auto& lines=d.expanded?all:compact;
 const unsigned start=d.expanded?std::min<unsigned>(d.lineOffset,lines.empty()?0:unsigned(lines.size()-1)):0;
 const unsigned count=std::min<unsigned>(d.expanded?capacity:unsigned(lines.size()),unsigned(lines.size())-start);
 const unsigned visibleRows=d.expanded?std::min<unsigned>(capacity,unsigned(all.size())):count;
 const int h=fixed+int(visibleRows)*lh;
 int x=std::clamp(anchor.x0,bounds.x0+t::S3,bounds.x1-t::S3-w);Rect r=anchor.y1+t::S4+h<=bounds.y1?Rect{x,anchor.y1+t::S4,x+w,anchor.y1+t::S4+h}:placeNear(anchor,w,h,bounds,t::S4);
 popover(c,r);iconButton(c,tg,DefClose,{r.x1-t::MinTarget,r.y0,r.x1,r.y0+t::MinTarget},Icon::Close,{},30);
 int y=r.y0+36;swash(c,word,float(r.x0+pad),float(y+44),text::fit(word,d.word,float(w-pad*2-t::MinTarget)),t::PopCyan);y+=56;
 if(!tag.empty()){text::draw(c,pos,float(r.x0+pad),float(y+30),tag,t::InkSecondary);y+=40;}y+=t::S3;
 for(unsigned k=0;k<count;++k)text::draw(c,body,float(r.x0+pad),float(y+34+int(k)*lh),lines[start+k],none||d.loading?t::InkSecondary:t::InkPrimary);
 y+=int(visibleRows)*lh;
 if(!source.empty()){text::draw(c,src,float(r.x0+pad),float(y+28),text::fit(src,source,float(w-pad*2)),t::InkSecondary);y+=50;}
 if(controls){textAction(c,tg,DefMore,{r.x0+pad,y,r.x0+pad+200,y+72},d.expanded?"Collapse":"Expand",false,false);
  if(d.expanded){textAction(c,tg,DefPrevious,{r.x1-pad-360,y,r.x1-pad-180,y+72},"Previous",false,start==0);textAction(c,tg,DefNext,{r.x1-pad-180,y,r.x1-pad,y+72},"Next",false,start+count>=all.size());}y+=72;}
 if(d.actions){y=r.y1-100;c.hline(r.x0+pad,r.x1-pad,y,t::Border,t::Hairline);textAction(c,tg,DefWriteNote,{r.x0+pad,y,r.x0+pad+260,r.y1},"Write note",false,d.loading);textAction(c,tg,DefWriteTom,{r.x0+pad+280,y,r.x0+pad+520,r.y1},"Ask Tom",false,d.loading);}
 return r;
}
void selectionWash(Canvas& c,std::span<const Rect> lines){
 for(auto l:lines)c.multiply(outset(l,2),t::Selection);
 if(lines.empty())return;Rect a=lines.front(),b=lines.back();
 c.fill({a.x0-3,a.y0-4,a.x0+1,a.y1+2},t::SelectionHandle);c.fillCircle(float(a.x0-1),float(a.y0-10),9,t::SelectionHandle);
 c.fill({b.x1-1,b.y0-2,b.x1+3,b.y1+4},t::SelectionHandle);c.fillCircle(float(b.x1+1),float(b.y1+10),9,t::SelectionHandle);
}
void highlightWash(Canvas& c,std::span<const Rect> lines,std::uint32_t color){Rect bounds{};for(auto line:lines)bounds.include(intersect(outset(line,2),c.clip()));if(bounds.empty())return;const int stride=width(bounds);std::vector<unsigned char> covered(std::size_t(stride)*height(bounds),0);for(auto line:lines){auto rect=intersect(outset(line,2),bounds);for(int y=rect.y0;y<rect.y1;++y)for(int x=rect.x0;x<rect.x1;++x){auto index=std::size_t(y-bounds.y0)*stride+x-bounds.x0;if(covered[index])continue;covered[index]=1;c.multiply({x,y,x+1,y+1},theme::wash(color,120));}}}
}
