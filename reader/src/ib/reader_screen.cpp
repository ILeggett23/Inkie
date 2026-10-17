// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/reader_screen.h"
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
void closeButton(Canvas& c,Targets& tg){Rect r=closeRect();c.fill(r,t::Paper);icon(c,Icon::CloseCircle,centered(r,48,48),t::InkPrimary);tg.add(Back,r);}
void zoomBadge(Canvas& c,Targets& tg,int percent){const Rect r=ZoomBadgeRect;c.fill(r,t::Paper);c.hline(r.x0,r.x1,r.y0,t::InkPrimary,3);c.hline(r.x0,r.x1,r.y1-3,t::InkPrimary,3);c.vline(r.x0,r.y0,r.y1,t::InkPrimary,3);c.vline(r.x1-3,r.y0,r.y1,t::InkPrimary,3);text::centeredLine(c,{Face::Text,theme::Label.size},r,std::to_string(percent)+"%",t::InkPrimary,Align::Center);tg.add(ZoomBadge,r);}
void footer(Canvas& c,Targets& tg,const Chrome& m){
 // Quiet metadata under the page, as in the native reader: title or chapter left, position right.
 Rect r=FooterRect;c.fill(r,t::Paper);Rect text{FooterInset,r.y0,r.x1-FooterInset,r.y1};const Font f{Face::Text,theme::Meta.size};
 if(!m.status.empty()){text::centeredLine(c,Caption,text,text::fit(Caption,text::quiet(m.status),float(width(text))),t::InkSecondary,Align::Center);tg.add(Footer,r);return;}
 // Reading progress off: no chapter or position (notices above still show; the menu mark stays the way to the tools).
 if(!m.progress&&!m.loading){if(m.menuMark){int mid=(r.y0+r.y1)/2;icon(c,Icon::Menu,{text.x1-t::IconSmall,mid-t::IconSmall/2,text.x1,mid+t::IconSmall/2},t::InkSecondary);}tg.add(Footer,r);return;}
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
 Rect r=toolbarRect();c.fill(r,t::Paper);tg.add(RailBlank,r);
 auto tile=[&](unsigned slot,int id,bool selected,bool enabled,auto&& draw){Rect cell=toolbarSlot(slot);if(selected)c.fill(cell,t::ToolSelected);
  draw(centered(cell,ToolbarGlyph,ToolbarGlyph),selected?t::Paper:enabled?t::ToolbarInk:t::Disabled,cell);if(enabled||selected)tg.add(id,cell);};
 // Pigment: the shortcut's current color, a small dot in the tile's upper corner with a paper ring.
 auto pigment=[&](Rect cell,std::uint32_t color){float cx=float(side_==Side::Left?cell.x1-24:cell.x0+24),cy=float(cell.y0+20);c.fillCircle(cx,cy,11.5f,t::Paper);c.fillCircle(cx,cy,10.f,color);};
 // One black tile at a time: an open document menu takes the selection from the writing tool.
 const unsigned shortcut=m.activeShortcut<2?m.activeShortcut:(m.highlighter?1u:0u);
 const bool tool=!m.menu,penActive=tool&&!m.eraser&&!m.selecting&&shortcut==0,hlActive=tool&&!m.eraser&&!m.selecting&&shortcut==1;
 tile(SlotHandle,ToolHandle,false,true,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::ToolbarHandle,centered(b,HandleGlyph,HandleGlyph),col);});
 tile(SlotPen,RailPen,penActive,true,[&](Rect b,std::uint32_t col,Rect cell){nibGlyph(c,m.activeShortcut<2?m.primary:m.pen,b,col);pigment(cell,m.activeShortcut<2?t::Ink[m.primaryColor%8]:m.ink);});
 tile(SlotHighlighter,RailHighlighter,hlActive,true,[&](Rect b,std::uint32_t col,Rect cell){nibGlyph(c,m.activeShortcut<2?m.secondary:Instrument::Highlighter,b,col);pigment(cell,t::Ink[(m.activeShortcut<2?m.secondaryColor:m.highlightColor)%8]);});
 tile(SlotEraser,RailEraser,tool&&m.eraser&&!m.selecting,true,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::NativeEraser,b,col);});
 tile(SlotSelect,RailSelect,tool&&m.selecting,true,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::NativeSelection,b,col);});
 tile(SlotLayers,RailLayers,m.layers,true,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::NativeLayers,b,col);});
 tile(SlotUndo,RailUndo,false,m.canUndo&&!m.busy,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::NativeUndo,b,col);});
 tile(SlotRedo,RailRedo,false,m.canRedo&&!m.busy,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::NativeRedo,b,col);});
 tile(SlotDocument,RailMore,m.menu,true,[&](Rect b,std::uint32_t col,Rect){icon(c,Icon::DocumentMenu,b,col);});
}
void toolHandle(Canvas& c,Targets& tg,const Tools&){Rect r=toolHandleRect();c.fill(r,t::Paper);icon(c,Icon::ToolbarHandle,centered(r,HandleGlyph,HandleGlyph),t::InkPrimary);tg.add(ToolHandle,r);}
void penPanel(Canvas& c,Targets& tg,const Tools& m,Rect at){
 Rect r=at;if(!m.colors)r.y1=r.y0+PenPanelCompactHeight;popover(c,r);
 const Font title{Face::Text,theme::Label.size},label{Face::Text,theme::Meta.size};
 const char* name=m.eraser?"Eraser":instrumentName(m.highlighter?Instrument::Highlighter:m.pen);
 text::centeredLine(c,title,{r.x0+30,r.y0,r.x1-30,r.y0+PaletteTitle},name,t::InkPrimary,Align::Left);
 if(!m.status.empty())text::centeredLine(c,Caption,{r.x0+200,r.y0,r.x1-24,r.y0+PaletteTitle},text::fit(Caption,text::quiet(m.status),float(width(r)-224)),t::InkSecondary,Align::Right);
 // Tips: the selected one is a black tile with a paper glyph, as on the native palette.
 for(std::size_t i=0;i<Pens.size();++i){Rect cell=penCell(unsigned(i),r);
  bool sel=!m.eraser&&(Pens[i]==Instrument::Highlighter?m.highlighter:(!m.highlighter&&m.pen==Pens[i]));
  const bool available=m.colors||Pens[i]!=Instrument::Highlighter;
  if(sel&&available)c.fill(cell,t::ToolSelected);nibGlyph(c,Pens[i],centered(cell,55,55),!available?t::Disabled:sel?t::Paper:t::InkPrimary);if(available)tg.add(PenBase+int(i),cell);}
 int wy=widthTitleY(r);c.hline(r.x0,r.x1,wy-2,t::PopoverBorder,2);
 static constexpr std::array<const char*,3> WidthName{"Thin","Medium","Thick"};
 text::centeredLine(c,title,{r.x0+30,wy,r.x1-30,wy+PaletteTitle},WidthName[unsigned(m.width)%3],t::InkPrimary,Align::Left);
 // Widths: a diagonal stroke at each thickness.
 const float stroke[]={2.5f,5.5f,9.5f};
 for(int i=0;i<3;++i){Rect cell=widthCell(unsigned(i),r);bool sel=unsigned(m.width)==unsigned(i);if(sel)c.fill(cell,t::ToolSelected);
  Rect g=centered(cell,58,58);float k=58.f/60.f;c.line(g.x0+13*k,g.y0+47*k,g.x0+47*k,g.y0+13*k,stroke[i]*(m.highlighter?1.6f:1.f),sel?t::Paper:t::InkPrimary);tg.add(WidthBase+i,cell);}
 // Colors (not for the eraser): persisted palette indices, labelled.
 if(!m.eraser&&m.colors){c.hline(r.x0,r.x1,swatchY(r)-30,t::PopoverBorder,2);
  for(unsigned i=0;i<8;++i){Rect cell=colorCell(i,r);bool unavailable=m.highlighter&&i==0;bool sel=(m.highlighter?m.highlightColor:m.color)==i;bool off=unavailable||m.busy;
   if(sel)c.fill(cell,t::ToolSelected);float cx=float(cell.x0+cell.x1)/2,cy=float(cell.y0+38);c.fillCircle(cx,cy,15.5f,sel?t::Paper:off?t::Disabled:t::InkPrimary);c.fillCircle(cx,cy,14.f,unavailable?t::Paper:t::Ink[i]);
   text::centeredLine(c,label,{cell.x0,cell.y0+62,cell.x1,cell.y1-8},t::InkName[i],sel?t::Paper:off?t::Disabled:t::InkPrimary,Align::Center);if(!off)tg.add(ColorBase+int(i),cell);}}
 if(m.colors&&m.highlighter&&!m.eraser){Rect hit{r.x0+24,r.y1-96,r.x1-24,r.y1-8};c.hline(r.x0,r.x1,hit.y0,t::PopoverBorder,2);text::centeredLine(c,label,{hit.x0,hit.y0,hit.x1-112,hit.y1},"Snap to Text",t::InkPrimary,Align::Left);toggle(c,tg,SnapHighlights,{hit.x1-100,hit.y0,hit.x1,hit.y1},m.snapHighlights,!m.canSnap);if(m.canSnap)tg.add(SnapHighlights,hit);}
}
Rect selectionToolbar(Canvas& c,Targets& tg,const SelectionBar& m,Rect bounds){
 // Native context strip: one square ink-outlined bar, icon + label segments with thin dividers.
 struct Item{int id;const char* label;Icon icon;bool nib;};std::array<Item,6> items{};unsigned n=0;
 if(m.define)items[n++]={SelDefine,"Define",Icon::BookOpen,false};
 if(m.highlight)items[n++]={SelHighlight,m.highlighted?"Color":"Highlight",Icon::Highlight,true};
 if(m.note)items[n++]={SelNote,"Note",Icon::Note,false};if(m.tom)items[n++]={SelTom,"Tom",Icon::Tom,false};
 if(m.copy)items[n++]={SelCopy,"Copy",Icon::Copy,false};if(m.more)items[n++]={SelMore,"",Icon::More,false};
 const Font f{Face::Text,theme::Label.size};const int h=98,pad=t::S3+4,glyph=31,gap=14;std::array<int,6> ws{};int w=0;
 for(unsigned i=0;i<n;++i){ws[i]=items[i].label[0]?int(text::measure(f,items[i].label))+glyph+gap+pad*2:t::MinTarget;w+=ws[i];}
 // Prefer above the passage so the text being read stays visible.
 Rect r;int x=std::clamp((m.anchor.x0+m.anchor.x1)/2-w/2,bounds.x0+t::S3,bounds.x1-t::S3-w);
 if(m.anchor.y0-t::S3-h>=std::max(bounds.y0,layout::TopRow))r={x,m.anchor.y0-t::S3-h,x+w,m.anchor.y0-t::S3};else r=placeNear(m.anchor,w,h,bounds,t::S3);
 popover(c,r);
 int cx=r.x0;for(unsigned i=0;i<n;++i){Rect seg{cx,r.y0,cx+ws[i],r.y1};cx=seg.x1;if(i)c.vline(seg.x0,r.y0+2,r.y1-2,t::Border,t::Hairline);
  const std::uint32_t ink=items[i].id==SelHighlight&&m.highlighted?t::InkSecondary:t::InkPrimary;
  if(!items[i].label[0]){icon(c,items[i].icon,centered(seg,34,34),ink);tg.add(items[i].id,hitArea(seg));continue;}
  int gx=seg.x0+pad,mid=(r.y0+r.y1)/2;Rect g{gx,mid-glyph/2,gx+glyph,mid+glyph/2};if(items[i].nib)nibGlyph(c,Instrument::Highlighter,g,ink);else icon(c,items[i].icon,g,ink);
  text::centeredLine(c,f,{g.x1+gap,r.y0,seg.x1,r.y1},items[i].label,ink,Align::Left);tg.add(items[i].id,hitArea(seg));}
 return r;
}
Rect highlightColors(Canvas& c,Targets& tg,Rect bar,Rect passage,unsigned selected,bool removable){
 // The same quiet hairline surface as the selection bar: a row of swatches, no caption.
 const int h=112,cw=88;int n=int(t::HighlightOrder.size())+(removable?1:0);int w=n*cw+t::S3*2;bool above=bar.y1<=passage.y0+4;
 Rect r=above?Rect{bar.x0,bar.y0-t::S2-h,bar.x0+w,bar.y0-t::S2}:Rect{bar.x0,bar.y1+t::S2,bar.x0+w,bar.y1+t::S2+h};
 if(r.x1>t::ScreenWidth-t::S3){r.x0-=r.x1-(t::ScreenWidth-t::S3);r.x1=t::ScreenWidth-t::S3;}
 if(r.y0<0||r.y1>t::PageFitHeight){r={r.x0,passage.y1+t::S2,r.x1,passage.y1+t::S2+h};}
 c.fillRound(r,float(h/2),t::Surface);c.strokeRound(r,float(h/2),t::Hairline,t::Border);
 for(int i=0;i<int(t::HighlightOrder.size());++i){unsigned idx=t::HighlightOrder[std::size_t(i)];Rect cell{r.x0+t::S3+i*cw,r.y0+8,r.x0+t::S3+(i+1)*cw,r.y1-8};swatch(c,tg,HighlightColorBase+int(idx),cell,t::Ink[idx],idx==selected,false,48);}
 if(removable){Rect cell{r.x0+t::S3+int(t::HighlightOrder.size())*cw,r.y0+8,r.x0+t::S3+(int(t::HighlightOrder.size())+1)*cw,r.y1-8};icon(c,Icon::Trash,centered(cell,40,40),t::InkSecondary);tg.add(HighlightRemove,hitArea(cell));}
 return r;
}
Rect definitionCard(Canvas& c,Targets& tg,Rect anchor,const Definition& d,Rect bounds){
 const int w=d.expanded?940:710,pad=40,lh=46;
 const Font word{Face::SerifSemibold,theme::Heading.size},pos{Face::Text,theme::Label.size},body{Face::Serif,theme::Body.size},src{Face::Text,theme::Meta.size};
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
 int y=r.y0+36;text::draw(c,word,float(r.x0+pad),float(y+44),text::fit(word,d.word,float(w-pad*2-t::MinTarget)),t::InkPrimary);y+=56;
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
