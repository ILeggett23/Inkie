// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/sheets.h"
#include "ib/library_screen.h"
#include "ib/type.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
namespace ib::sheets {
namespace t=theme;
namespace {
const Font Label{Face::Text,float(t::Label.size)},Meta{Face::Text,float(t::Meta.size)},Caption{Face::Text,float(t::Caption.size)},Numbers{Face::Sans,float(t::Label.size)};
std::string percent(double v){char b[16];std::snprintf(b,sizeof b,"%.0f%%",v);return b;}
// "Label ........ [-] value [+]" stepper row.
void stepper(Canvas& c,Targets& tg,Rect r,std::string_view label,std::string_view value,int less,int more,bool canLess,bool canMore,bool rule=true){
 text::centeredLine(c,Label,{r.x0,r.y0,r.x1-330,r.y1},label,t::InkPrimary,Align::Left);
 iconButton(c,tg,less,{r.x1-330,r.y0,r.x1-234,r.y1},Icon::ChevronLeft,{false,!canLess},36);
 text::centeredLine(c,Numbers,{r.x1-234,r.y0,r.x1-96,r.y1},value,t::InkPrimary);
 iconButton(c,tg,more,{r.x1-96,r.y0,r.x1,r.y1},Icon::ChevronRight,{false,!canMore},36);
 if(rule)divider(c,r.x0,r.x1,r.y1);
}
}
void layers(Canvas& c,Targets& tg,const Layers& m){
 Rect r=LayersRect;auto frame=sheet(c,tg,r,"Layers",{.close=true,.closeId=LayersClose});
 iconButton(c,tg,LayersUndo,{r.x1-3*t::MinTarget-t::S2,frame.header.y0+8,r.x1-2*t::MinTarget-t::S2,frame.header.y1-4},Icon::Undo,{false,!m.canUndo||m.busy});
 iconButton(c,tg,LayersRedo,{r.x1-2*t::MinTarget-t::S2,frame.header.y0+8,r.x1-t::MinTarget-t::S2,frame.header.y1-4},Icon::Redo,{false,!m.canRedo||m.busy});
 int y=frame.content.y0+t::S2;const int rowH=150;Rect content=frame.content;
 auto thumb=[&](Rect b,const Layer& l){c.fillRound(b,6,t::Surface);c.strokeRound(b,6,2,t::Border);
  // Abstract preview: a few strokes when the layer has content.
  if(l.items>0){for(int k=0;k<std::min(3,l.items);++k)c.line(float(b.x0+14),float(b.y0+22+k*20),float(b.x1-14-k*12),float(b.y0+18+k*20),3,l.visible?t::InkSecondary:t::Disabled);}
  else text::centeredLine(c,Caption,b,"empty",t::InkTertiary);};
 for(unsigned i=0;i<LayerPageSize&&m.offset+i<m.layers.size();++i){const auto& l=m.layers[m.offset+i];Rect row{content.x0,y,content.x1,y+rowH};
  if(l.selected)c.fillRound(inset(row,4),t::RadiusSmall,t::SurfaceSunken);
  iconButton(c,tg,EyeBase+int(m.offset+i),{row.x0,row.y0,row.x0+t::MinTarget,row.y1},l.visible?Icon::Eye:Icon::EyeOff,{false,m.busy},40);
  thumb({row.x0+t::MinTarget+t::S2,row.y0+t::S3,row.x0+t::MinTarget+t::S2+140,row.y1-t::S3},l);
  int tx=row.x0+t::MinTarget+t::S2+140+t::S4;
  text::draw(c,{l.active?Face::SerifSemibold:Face::Serif,theme::Body.size},float(tx),float(row.y0+64),text::fit({Face::Serif,theme::Body.size},l.name,float(row.x1-tx-t::MinTarget-60)),l.visible?t::InkPrimary:t::InkSecondary);
  std::string meta=l.active?"Writing here":std::to_string(l.items)+(l.items==1?" item":" items");if(l.locked)meta+="  \xC2\xB7  Locked";if(!l.visible)meta+="  \xC2\xB7  Hidden";
  text::draw(c,Meta,float(tx),float(row.y0+108),meta,t::InkSecondary);
  if(l.locked)icon(c,Icon::Lock,{row.x1-t::MinTarget-44,row.y0+55,row.x1-t::MinTarget-8,row.y0+91},t::InkSecondary);
  iconButton(c,tg,RowMenuBase+int(m.offset+i),{row.x1-t::MinTarget,row.y0,row.x1,row.y1},Icon::MoreVertical,{false,m.busy},40);
  tg.add(RowBase+int(m.offset+i),{row.x0+t::MinTarget,row.y0,row.x1-t::MinTarget,row.y1});
  divider(c,row.x0,row.x1,row.y1);y+=rowH;}
 // Protected document background, always at the bottom.
 Rect bg{content.x0,y,content.x1,y+rowH};icon(c,Icon::Lock,{bg.x0+30,bg.y0+57,bg.x0+66,bg.y0+93},t::InkTertiary);
 Rect bt{bg.x0+t::MinTarget+t::S2,bg.y0+t::S3,bg.x0+t::MinTarget+t::S2+140,bg.y1-t::S3};c.fillRound(bt,6,t::SurfaceSunken);for(int k=0;k<5;++k)c.hline(bt.x0+14,bt.x1-14-(k%2)*30,bt.y0+18+k*16,t::Border,3);
 int tx=bt.x1+t::S4;text::draw(c,{Face::Serif,theme::Body.size},float(tx),float(bg.y0+64),m.background,t::InkSecondary);text::draw(c,Meta,float(tx),float(bg.y0+108),"Original \xC2\xB7 protected",t::InkTertiary);
 y+=rowH+t::S3;
 if(m.layers.size()>LayerPageSize){iconButton(c,tg,LayersPrev,{content.x0,y,content.x0+t::MinTarget,y+t::MinTarget},Icon::ChevronUp,{false,m.offset==0});iconButton(c,tg,LayersNext,{content.x0+t::MinTarget,y,content.x0+2*t::MinTarget,y+t::MinTarget},Icon::ChevronDown,{false,m.offset+LayerPageSize>=m.layers.size()});}
 button(c,tg,LayerAdd,{content.x1-360,y,content.x1,y+92},"New layer",ButtonKind::Secondary,m.busy,Icon::Plus);
 if(!m.status.empty())text::draw(c,Caption,float(content.x0),float(r.y1-t::S3),text::fit(Caption,text::quiet(m.status),float(width(content))),t::InkTertiary);
 if(m.menuFor>=0&&unsigned(m.menuFor)<m.layers.size()&&unsigned(m.menuFor)>=m.offset&&unsigned(m.menuFor)<m.offset+LayerPageSize){
  const auto& l=m.layers[unsigned(m.menuFor)];int rowY=frame.content.y0+t::S2+int(unsigned(m.menuFor)-m.offset)*rowH;
  std::vector<MenuItem> items{{MenuActivate,Icon::Handwrite,"Write on this layer",false,l.active||l.locked||!l.visible},{MenuRename,Icon::Note,"Rename"},{MenuLock,l.locked?Icon::Unlock:Icon::Lock,l.locked?"Unlock":"Lock"},
   {MenuUp,Icon::ChevronUp,"Move up",false,m.menuFor==0},{MenuDown,Icon::ChevronDown,"Move down",false,unsigned(m.menuFor)+1>=m.layers.size()},{MenuDelete,Icon::Trash,m.confirmDelete?"Delete with its content":"Delete",true}};
  menu(c,tg,{content.x1-t::MinTarget,rowY,content.x1,rowY+rowH},items,560);}
}
void settings(Canvas& c,Targets& tg,const Settings& m){
 Rect r=SettingsRect;static constexpr std::array<const char*,4> Titles{"View and zoom","Text and layout","Page overview","About"};
 auto frame=sheet(c,tg,r,m.tab==Tab::View&&!m.pdf?"View":Titles[unsigned(m.tab)%4],{.close=true,.closeId=SettingsClose});Rect cx=frame.content;
 std::array<std::string_view,4> names{"View","Reading","Pages","About"};
 tabs(c,tg,TabBase,{cx.x0,cx.y0,cx.x1,cx.y0+84},names,unsigned(m.tab));
 int y=cx.y0+84+t::S4;const int row=110;bool viewBlocked=m.busy||m.pinned||m.pending||m.contact;
 if(m.tab==Tab::View){
  if(!m.pdf){text::block(c,{Face::SerifItalic,theme::Body.size},{cx.x0,y,cx.x1,y+200},"EPUB pages reflow to the screen. Text size and line width are in Reading.",t::InkSecondary,46,4);}
  else{
   text::centeredLine(c,Label,{cx.x0,y,cx.x0+300,y+row},"Zoom",t::InkPrimary,Align::Left);
   button(c,tg,FitWidth,{cx.x1-780,y+12,cx.x1-580,y+row-12},"Fit width",ButtonKind::Secondary,viewBlocked);
   button(c,tg,Fit,{cx.x1-560,y+12,cx.x1-360,y+row-12},"Fit page",ButtonKind::Secondary,viewBlocked||m.zoom<=1);
   iconButton(c,tg,ZoomOut,{cx.x1-340,y,cx.x1-244,y+row},Icon::ZoomOut,{false,viewBlocked||m.zoom<=1});text::centeredLine(c,Numbers,{cx.x1-244,y,cx.x1-96,y+row},percent(m.zoom*100),t::InkPrimary);iconButton(c,tg,ZoomIn,{cx.x1-96,y,cx.x1,y+row},Icon::ZoomIn,{false,viewBlocked||m.zoom>=4});
   divider(c,cx.x0,cx.x1,y+row);y+=row;
   text::centeredLine(c,Label,{cx.x0,y,cx.x0+300,y+row},"Rotation",t::InkPrimary,Align::Left);
   iconButton(c,tg,RotateLeft,{cx.x1-340,y,cx.x1-244,y+row},Icon::Undo,{false,viewBlocked});text::centeredLine(c,Numbers,{cx.x1-244,y,cx.x1-96,y+row},std::to_string(m.rotation)+"\xC2\xB0",t::InkPrimary);iconButton(c,tg,RotateRight,{cx.x1-96,y,cx.x1,y+row},Icon::Redo,{false,viewBlocked});
   divider(c,cx.x0,cx.x1,y+row);y+=row+t::S3;
   text::draw(c,Meta,float(cx.x0),float(y+24),"Crop margins",t::InkSecondary);button(c,tg,CropReset,{cx.x1-220,y-16,cx.x1,y+52},"Reset",ButtonKind::Quiet,viewBlocked);y+=t::S5;
   std::array<const char*,4> edges{"Left","Right","Top","Bottom"};
   for(unsigned e=0;e<4;++e){stepper(c,tg,{cx.x0,y,cx.x1,y+row-10},edges[e],percent(m.crop[e]),CropBase+int(e*2),CropBase+int(e*2+1),!viewBlocked&&m.crop[e]>0,!viewBlocked);y+=row-10;}
   if(m.panAvailable){y+=t::S3;text::draw(c,Meta,float(cx.x0),float(y+24),"Move page",t::InkSecondary);y+=t::S5;
    std::array<Icon,4> arrows{Icon::ChevronLeft,Icon::ChevronUp,Icon::ChevronDown,Icon::ChevronRight};for(int d=0;d<4;++d)iconButton(c,tg,PanBase+d,{cx.x0+d*130,y,cx.x0+d*130+110,y+t::MinTarget},arrows[std::size_t(d)],{false,viewBlocked});y+=t::MinTarget;}
  }
 }else if(m.tab==Tab::Reading){
  if(!m.pdf){stepper(c,tg,{cx.x0,y,cx.x1,y+row},"Text size",std::to_string(m.fontSize)+" pt",FontLess,FontMore,!viewBlocked&&m.fontSize>6,!viewBlocked&&m.fontSize<72);y+=row;
   stepper(c,tg,{cx.x0,y,cx.x1,y+row},"Line width",std::to_string(m.lineWidth),LineLess,LineMore,!viewBlocked&&m.lineWidth>64,!viewBlocked&&m.lineWidth<4094);y+=row;
   // Native text-and-layout choices: three outlined cells, the chosen one black. "Book" keeps the publisher's choice.
   auto choices=[&](std::string_view label,std::array<const char*,3> options,unsigned selected,int firstId){const Font cf{Face::Text,theme::Control.size};text::centeredLine(c,Label,{cx.x0,y,cx.x0+260,y+row},label,t::InkPrimary,Align::Left);
    int x=cx.x1;for(int i=2;i>=0;--i){int w=int(text::measure(cf,options[std::size_t(i)]))+t::S4*2;Rect cell{x-w,y+16,x,y+row-16};x=cell.x0-8;const bool on=unsigned(i)==selected;
     if(on)c.fill(cell,t::ToolSelected);else c.hairline(cell,t::Border,2);text::centeredLine(c,cf,cell,options[std::size_t(i)],on?t::Paper:viewBlocked?t::Disabled:t::InkPrimary,Align::Center);if(!viewBlocked)tg.add(firstId+i,hitArea(cell));}
    divider(c,cx.x0,cx.x1,y+row);y+=row;};
   choices("Font",{"Book","Serif","Sans"},m.family,FamilyBase);choices("Line spacing",{"Small","Medium","Large"},m.spacing,SpacingBase);
   choices("Margins",{"Small","Medium","Large"},m.margins,MarginBase);choices("Justification",{"Book","Left","Justified"},m.alignment,AlignBase);}
  text::centeredLine(c,Label,{cx.x0,y,cx.x1-140,y+row},"Quiet page effects",t::InkPrimary,Align::Left);toggle(c,tg,Quiet,{cx.x1-120,y,cx.x1,y+row},m.quietEffects,m.busy||m.contact);divider(c,cx.x0,cx.x1,y+row);y+=row;
  text::block(c,{Face::Text,float(t::Caption.size)},{cx.x0,y+t::S2,cx.x1,y+120},"A short, localized settle effect after page changes. Off by default to keep refreshes minimal.",t::InkTertiary,32,3);
 }else if(m.tab==Tab::Pages){
  char label[64];std::snprintf(label,sizeof label,"Pages %d\xE2\x80\x93%d of %d",m.firstPage+1,std::min(m.firstPage+OverviewPageSize,m.pages),m.pages);text::draw(c,Meta,float(cx.x0),float(y+24),label,t::InkSecondary);y+=t::S5;
  // Native page overview: page images in a grid with the number underneath; the chosen page carries a heavy
  // outline, the page you are on a bold number. A page not rendered yet is a plain page outline.
  const int gap=t::S3,cw=(width(cx)-(OverviewColumns-1)*gap)/OverviewColumns,ch=cw*4/3,numberH=48,pitch=ch+numberH+t::S2;
  for(int i=0;i<OverviewPageSize;++i){const int page=m.firstPage+i;if(page>=m.pages)continue;const int col=i%OverviewColumns,row=i/OverviewColumns;
   const Rect frame{cx.x0+col*(cw+gap),y+row*pitch,cx.x0+col*(cw+gap)+cw,y+row*pitch+ch};Rect shown=frame;
   const PageImage image=m.pageImage?m.pageImage(page):PageImage{};
   if(!image.empty()){shown=c.grayImage(frame,image.gray,image.width,image.height,image.width,t::Paper);if(shown.empty())shown=frame;}
   else c.fill(frame,t::Paper);
   c.hairline(shown,t::Border,2);
   if(page==m.selectedPage)c.hairline(outset(shown,10),t::InkPrimary,4);
   if(std::any_of(m.bookmarks.begin(),m.bookmarks.end(),[&](const auto& b){return b.first==page;}))icon(c,Icon::BookmarkFilled,{shown.x1-12-t::IconSmall,shown.y0,shown.x1-12,shown.y0+t::IconSmall},t::InkPrimary);
   const Rect number{frame.x0,frame.y1+t::S1,frame.x1,frame.y1+numberH};const bool here=page==m.currentPage;
   text::centeredLine(c,here?Font{Face::SansSemibold,Numbers.size}:Numbers,number,std::to_string(page+1),t::InkPrimary);
   tg.add(PageCellBase+i,{frame.x0,frame.y0,frame.x1,number.y1});}
  y+=OverviewRows*pitch;std::array<Icon,4> nav{Icon::ChevronLeft,Icon::ChevronLeft,Icon::ChevronRight,Icon::ChevronRight};
  int nx=cx.x0;for(int k=0;k<4;++k){bool back=k<2;bool disabled=m.busy||(back?m.firstPage==0:m.firstPage+OverviewPageSize>=m.pages);iconButton(c,tg,PageFirst+k,{nx,y,nx+t::MinTarget+20,y+t::MinTarget},nav[std::size_t(k)],{false,disabled});if(k==0||k==3){Rect bar=k==0?Rect{nx+30,y+30,nx+34,y+66}:Rect{nx+82,y+30,nx+86,y+66};if(!disabled)c.fill(bar,t::InkPrimary);}nx+=t::MinTarget+40;}
  // Bookmarks: chips select the page (then "Go to page"); "≈" marks one made in another layout.
  y+=t::MinTarget+t::S4;const int footer=r.y1-t::S4-92-t::S3;
  if(!m.bookmarks.empty()&&y+50+t::MinTarget<=footer){text::draw(c,Meta,float(cx.x0),float(y+24),"Bookmarks",t::InkSecondary);y+=50;
   const int chipW=160,gap=t::S2,perRow=std::max(1,(width(cx)+gap)/(chipW+gap)),rows=std::max(1,(footer-y+gap)/(t::MinTarget+gap));
   const int shown=std::min({int(m.bookmarks.size()),BookmarkChips,perRow*rows});
   for(int i=0;i<shown;++i){const auto& b=m.bookmarks[std::size_t(i)];Rect chip{cx.x0+(i%perRow)*(chipW+gap),y+(i/perRow)*(t::MinTarget+gap),cx.x0+(i%perRow)*(chipW+gap)+chipW,y+(i/perRow)*(t::MinTarget+gap)+t::MinTarget};
    std::string label=(b.second?"\xE2\x89\x88 p. ":"p. ")+std::to_string(b.first+1);button(c,tg,BookmarkBase+i,chip,label,b.first==m.selectedPage?ButtonKind::Primary:ButtonKind::Secondary,m.busy);}
   const int below=y+((shown+perRow-1)/perRow)*(t::MinTarget+gap)+30;if(int(m.bookmarks.size())>shown&&below<=footer+t::S3){char more[32];std::snprintf(more,sizeof more,"+%d more",int(m.bookmarks.size())-shown);text::draw(c,Meta,float(cx.x0),float(below),more,t::InkTertiary);}}
 }else{
  for(auto& [k,v]:m.about){text::draw(c,Meta,float(cx.x0),float(y+30),k,t::InkSecondary);auto lines=text::wrap(Label,v,float(width(cx)-280),3);for(std::size_t i=0;i<lines.size();++i)text::draw(c,Label,float(cx.x0+280),float(y+30+int(i)*40),lines[i],t::InkPrimary);y+=std::max(1,int(lines.size()))*40+t::S4;divider(c,cx.x0,cx.x1,y-t::S2);}
 }
 // Footer: notice + Cancel / Apply (settings are staged, never applied mid-stroke).
 int fy=r.y1-t::S4-92;
 if(!m.notice.empty())text::block(c,{Face::Text,float(t::Caption.size)},{cx.x0,fy-90,cx.x1,fy-t::S2},text::quiet(m.notice),m.pinned?t::InkPrimary:t::InkSecondary,32,2);
 if(m.tab!=Tab::About&&m.tab!=Tab::Reading){button(c,tg,Cancel,{cx.x0,fy,cx.x0+(width(cx)-t::S3)/2,fy+92},"Cancel",ButtonKind::Secondary,m.pending||m.contact);
  button(c,tg,Apply,{cx.x1-(width(cx)-t::S3)/2,fy,cx.x1,fy+92},m.tab==Tab::Pages?"Go to page":"Apply",ButtonKind::Primary,m.busy||m.pending||m.contact||!m.changed||(m.pinned&&m.tab==Tab::View));}
 else if(m.tab==Tab::Reading&&!m.pdf){button(c,tg,Cancel,{cx.x0,fy,cx.x0+(width(cx)-t::S3)/2,fy+92},"Cancel",ButtonKind::Secondary,m.pending||m.contact);
  button(c,tg,Apply,{cx.x1-(width(cx)-t::S3)/2,fy,cx.x1,fy+92},"Apply",ButtonKind::Primary,m.busy||m.pending||m.contact||!m.changed||m.pinned);}
}
const char* templateName(Template p)noexcept{static constexpr std::array<const char*,4> N{"Blank","Lined","Grid","Dot grid"};return unsigned(p)<N.size()?N[unsigned(p)]:"";}
void templatePattern(Canvas& c,Rect r,Template p,int scale){
 int step=std::max(6,70*scale/100);std::uint32_t ink=0xffc4c4c4u;
 if(p==Template::Lined){for(int y=r.y0+step*2;y<r.y1-step/2;y+=step)c.hline(r.x0+step/2,r.x1-step/2,y,ink,std::max(1,2*scale/100));}
 if(p==Template::Grid){for(int y=r.y0+step;y<r.y1;y+=step)c.hline(r.x0,r.x1,y,ink,std::max(1,2*scale/100));for(int x=r.x0+step;x<r.x1;x+=step)c.vline(x,r.y0,r.y1,ink,std::max(1,2*scale/100));}
 if(p==Template::Dots){for(int y=r.y0+step;y<r.y1;y+=step)for(int x=r.x0+step;x<r.x1;x+=step)c.fillCircle(float(x),float(y),std::max(1.5f,3.f*scale/100),0xffa8a8a8u);}
}
void newNotebook(Canvas& c,Targets& tg,const NewNotebook& m){
 Rect r=NewNotebookRect;auto frame=sheet(c,tg,r,"New notebook",{.close=true,.closeId=NewClose});Rect cx=frame.content;int y=cx.y0+t::S2;
 int cw=(width(cx)-3*t::S4)/4;
 for(unsigned i=0;i<unsigned(Template::Count);++i){Rect card{cx.x0+int(i)*(cw+t::S4),y,cx.x0+int(i)*(cw+t::S4)+cw,y+cw*4/3};bool sel=unsigned(m.paper)==i;
  c.fill(card,t::Surface);templatePattern(c,inset(card,4),Template(i),40);c.strokeRound(card,4,sel?4:2,sel?t::InkPrimary:t::Border);
  text::centeredLine(c,Meta,{card.x0,card.y1+t::S1,card.x1,card.y1+56},templateName(Template(i)),sel?t::InkPrimary:t::InkSecondary);tg.add(TemplateBase+int(i),{card.x0,card.y0,card.x1,card.y1+56});}
 y+=cw*4/3+56+t::S4;
 auto field=[&](int id,std::string_view label,std::string_view value,std::string_view placeholder,Icon trailing){Rect f{cx.x0,y,cx.x1,y+110};c.strokeRound(f,t::RadiusSmall,t::Hairline,t::Border);
  text::centeredLine(c,Label,{f.x0+t::S4,f.y0,f.x0+260,f.y1},label,t::InkSecondary,Align::Left);text::centeredLine(c,{Face::Serif,theme::Body.size},{f.x0+260,f.y0,f.x1-80,f.y1},value.empty()?placeholder:value,value.empty()?t::InkTertiary:t::InkPrimary,Align::Left);
  icon(c,trailing,{f.x1-64,f.y0+37,f.x1-28,f.y0+73},t::InkSecondary);tg.add(id,f);y+=110+t::S3;};
 field(NewTitle,"Title",m.title,"Untitled notebook",Icon::Note);field(NewFolder,"Folder",m.folder,"My files",Icon::ChevronDown);
 Rect pr{cx.x0,y,cx.x1,y+110};stepper(c,tg,pr,"Pages",std::to_string(m.pages),NewPagesLess,NewPagesMore,!m.busy&&m.pages>1,!m.busy&&m.pages<64,false);y+=110+t::S4;
 button(c,tg,NewCreate,{cx.x0,y,cx.x1,y+104},m.busy?"Creating\xE2\x80\xA6":"Create notebook",ButtonKind::Primary,m.busy);y+=104+t::S4;
 if(m.importAvailable){divider(c,cx.x0,cx.x1,y);y+=t::S3;button(c,tg,NewImport,{cx.x0,y,cx.x1,y+92},"Import documents from Inbox",ButtonKind::Quiet,m.busy,Icon::Import);}
 if(!m.status.empty())text::draw(c,Caption,float(cx.x0),float(r.y1-t::S3),text::fit(Caption,text::quiet(m.status),float(width(cx))),t::InkSecondary);
}
Rect menu(Canvas& c,Targets& tg,Rect anchor,const std::vector<MenuItem>& items,int w){
 const int rowH=100;int h=int(items.size())*rowH+t::S2*2;Rect r=placeNear(anchor,w,h,{0,0,t::ScreenWidth,t::ScreenHeight},t::S1);r.x0=std::clamp(anchor.x1-w,t::S3,t::ScreenWidth-t::S3-w);r.x1=r.x0+w;
 panel(c,r,t::RadiusMedium);int y=r.y0+t::S2;
 for(const auto& it:items){Rect row{r.x0,y,r.x1,y+rowH};std::uint32_t ink=it.disabled?t::Disabled:it.destructive?t::Caution:t::InkPrimary;
  icon(c,it.icon,{row.x0+t::S4,row.y0+32,row.x0+t::S4+36,row.y0+68},ink);text::centeredLine(c,Label,{row.x0+t::S4+36+t::S3,row.y0,row.x1-t::S3,row.y1},it.label,ink,Align::Left);if(!it.disabled)tg.add(it.id,row);y+=rowH;}
 return r;
}
Rect documentMenu(Canvas& c,Targets& tg,Rect tile,std::string_view title,const std::vector<MenuItem>& items,std::size_t separator){
 const int pad=30,head=78,sep=separator<items.size()?24:0;const int h=25+head+int(items.size())*DocumentMenuRow+sep+25;
 const bool left=tile.x0<t::ScreenWidth/2;int x0=left?tile.x1-2:tile.x0-DocumentMenuWidth+2;int y0=std::clamp(tile.y0,t::S2,t::PageFitHeight-t::S2-h);
 Rect r{x0,y0,x0+DocumentMenuWidth,y0+h};popover(c,r);const Font titleFont{Face::TextSemibold,theme::Body.size},rowFont{Face::Text,theme::Label.size};
 text::centeredLine(c,titleFont,{r.x0+pad,r.y0+25,r.x1-pad,r.y0+25+head-16},text::fit(titleFont,title,float(width(r)-pad*2)),t::InkPrimary,Align::Left);
 int y=r.y0+25+head;
 for(std::size_t i=0;i<items.size();++i){const auto& it=items[i];if(i==separator&&sep){c.hline(r.x0+pad,r.x1-pad,y+sep/2,t::Border,2);y+=sep;}
  Rect row{r.x0,y,r.x1,y+DocumentMenuRow};std::uint32_t ink=it.disabled?t::Disabled:it.destructive?t::Caution:t::InkPrimary;
  icon(c,it.icon,{row.x0+pad,row.y0+28,row.x0+pad+32,row.y0+60},ink);
  text::centeredLine(c,rowFont,{row.x0+pad+32+23,row.y0,row.x1-pad-40,row.y1},it.label,ink,Align::Left);
  icon(c,Icon::ChevronRight,{row.x1-pad-24,row.y0+32,row.x1-pad,row.y0+56},it.disabled?t::Disabled:t::InkSecondary);
  if(!it.disabled)tg.add(it.id,row);y+=DocumentMenuRow;}
 return r;
}
Rect quickBrowse(Canvas& c,Targets& tg,const QuickBrowse& m){
 const Rect r=QuickBrowseRect;c.fill(r,t::Surface);c.hline(0,t::ScreenWidth,r.y0,t::PopoverBorder,2);const int x0=QuickTrackRect.x0,x1=QuickTrackRect.x1;
 text::draw(c,{Face::Text,theme::Body.size},float(x0),float(r.y0+44+33),"Quick browse",t::InkPrimary);
 const Font meta{Face::Text,theme::Meta.size};text::centeredLine(c,meta,{x0,r.y0+100,x1-300,r.y0+150},text::fit(meta,m.title,float(x1-300-x0)),t::InkSecondary,Align::Left);
 text::centeredLine(c,meta,{x1-300,r.y0+100,x1,r.y0+150},m.position,t::InkSecondary,Align::Right);
 // The track: the start point (small dot) and the current page (knob), placed by page.
 const int ty=(QuickTrackRect.y0+QuickTrackRect.y1)/2;c.hline(x0,x1,ty-2,0xffbababau,4);const int last=std::max(1,m.total-1);
 auto at=[&](int page){return float(x0)+float(x1-x0)*float(std::clamp(page,0,last))/float(last);};
 if(m.start!=m.page)c.fillCircle(at(m.start),float(ty),9,t::InkPrimary);c.fillCircle(at(m.page),float(ty),17,t::InkPrimary);tg.add(QuickTrack,QuickTrackRect);
 const int by=QuickTrackRect.y1+8,bh=t::MinTarget;
 if(m.start!=m.page)textAction(c,tg,QuickReturn,{x0,by,x0+460,by+bh},"Return to where you started");
 // Single-page steps for exact positioning (a tap on the track lands within a page or two).
 const Font step{Face::Text,theme::Control.size};const std::string label=std::to_string(m.page+1)+" of "+std::to_string(std::max(1,m.total));const int lw=int(text::measure(step,label))+t::S3*2;
 iconButton(c,tg,QuickNext,{x1-t::MinTarget,by,x1,by+bh},Icon::ChevronRight,{false,m.page+1>=m.total},32);
 text::centeredLine(c,step,{x1-t::MinTarget-lw,by,x1-t::MinTarget,by+bh},label,t::InkPrimary,Align::Center);
 iconButton(c,tg,QuickPrevious,{x1-2*t::MinTarget-lw,by,x1-t::MinTarget-lw,by+bh},Icon::ChevronLeft,{false,m.page<=0},32);
 return r;
}
Rect notice(Canvas& c,Targets& tg,const Notice& n,Rect bounds){
 int w=900;auto lines=text::wrap({Face::Serif,theme::Body.size},n.body,float(w-2*t::S5),6);int h=t::S5+56+t::S3+int(lines.size())*48+t::S4+(n.actions.empty()?0:96+t::S3)+t::S4;
 Rect r=centered(bounds,w,h);panel(c,r,t::RadiusLarge);int y=r.y0+t::S5;text::draw(c,{Face::SerifSemibold,theme::Subhead.size},float(r.x0+t::S5),float(y+40),n.title,t::InkPrimary);y+=56+t::S3;
 for(auto& l:lines){text::draw(c,{Face::Serif,theme::Body.size},float(r.x0+t::S5),float(y+36),l,t::InkPrimary);y+=48;}y+=t::S4;
 int bw=n.actions.empty()?0:(w-2*t::S5-(int(n.actions.size())-1)*t::S3)/int(n.actions.size());int x=r.x0+t::S5;
 for(std::size_t i=0;i<n.actions.size();++i){const auto& a=n.actions[i];button(c,tg,a.id,{x,y,x+bw,y+96},a.label,i+1==n.actions.size()?(a.destructive?ButtonKind::Destructive:ButtonKind::Primary):ButtonKind::Secondary,a.disabled);x+=bw+t::S3;}
 return r;
}
Rect recents(Canvas& c,Targets& tg,std::span<const RecentBook> books){
 const Rect r{0,0,theme::ScreenWidth,RecentsHeight};c.fill(r,t::Paper);c.hline(0,theme::ScreenWidth,r.y1-1,t::BorderStrong,2);
 text::draw(c,{Face::Text,float(t::Label.size)},float(t::Gutter),72.f,"Recent",t::InkPrimary);
 button(c,tg,RecentsLibrary,{theme::ScreenWidth-t::Gutter-300,24,theme::ScreenWidth-t::Gutter,24+t::MinTarget},"All books",ButtonKind::Quiet,false,Icon::Books);
 const int n=int(std::min<std::size_t>(books.size(),5)),cw=220,gap=(theme::ScreenWidth-t::Gutter*2-5*cw)/4,y=140;
 for(int i=0;i<n;++i){const auto& b=books[std::size_t(i)];Rect cell{t::Gutter+i*(cw+gap),y,t::Gutter+i*(cw+gap)+cw,y+380};Rect cover{cell.x0+20,cell.y0,cell.x1-20,cell.y0+240};
  ib::library::placeholderCover(c,cover,b.title,b.format);if(b.current)c.strokeRound(outset(cover,6),t::RadiusSmall,t::StrokeStrong,t::InkPrimary);
  auto lines=text::wrap({Face::Text,theme::Meta.size},b.title,float(cw),2);for(std::size_t k=0;k<lines.size();++k)text::draw(c,{Face::Text,theme::Meta.size},float(cell.x0),float(cover.y1+40+int(k)*34),lines[k],t::InkPrimary);
  text::draw(c,Caption,float(cell.x0),float(cover.y1+40+int(lines.size())*34+4),text::fit(Caption,b.meta,float(cw)),t::InkTertiary);
  tg.add(RecentBase+i,cell);}
 if(n==0)text::draw(c,type::ScriptEmpty,float(t::Gutter),260.f,"Books you open appear here.",t::InkTertiary);
 return r;
}
}
