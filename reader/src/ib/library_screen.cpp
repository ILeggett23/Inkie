// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/library_screen.h"
#include "ib/type.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace ib::library {
namespace t=theme;
namespace {
constexpr int HeadingTop=202,HeadingBottom=290,FolderTop=334,FolderH=76,FolderCols=3,FolderRows=2,FolderGap=32;
constexpr int GridGapX=48,GridGapY=54,ThumbH=380,NameH=56,MetaH=40,GridRowH=ThumbH+NameH+MetaH;
constexpr int ListRowH=173;
const Font Label{Face::Text,theme::Label.size},Meta{Face::Text,theme::Meta.size};
int folderRows(int folders){return folders<=0?0:std::min(FolderRows,(std::min(folders,FolderCols*FolderRows)+FolderCols-1)/FolderCols);}
int documentsTop(int folders){int rows=folderRows(folders);return rows?FolderTop+rows*FolderH+t::S4:FolderTop+t::S2;}
constexpr std::array<Icon,unsigned(Section::Count)> NavIcons{Icon::Files,Icon::Notes,Icon::Star,Icon::Clock,Icon::Shared,Icon::Tom,Icon::Trash,Icon::Settings};
std::string upper(std::string_view v){std::string s(v);for(auto& ch:s)ch=char(std::toupper(static_cast<unsigned char>(ch)));return s;}
// Native file-type treatment: ebooks get a spine on the left, PDFs a folded top-right corner.
void thumbnail(Canvas& c,Rect r,const Document& d){
 const bool epub=d.format=="epub";Rect page=r; // the page the spine and the fold belong to
 if(d.cover.empty()){placeholderCover(c,r,d.title,d.format);if(epub){c.fillRound({r.x0,r.y0,r.x0+14,r.y1},t::RadiusSmall,t::InkPrimary);c.fill({r.x0+7,r.y0,r.x0+14,r.y1},t::InkPrimary);}}
 else{
  // A real cover is shown whole (never cropped), standing in the tile at full height, with the spine on its left.
  c.fill(r,t::Paper);int h=height(r),w=std::min(width(r),int(std::lround(double(d.cover.width)*h/std::max(1,d.cover.height))));Rect book{r.x0+(width(r)-w)/2,r.y0,r.x0+(width(r)-w)/2+w,r.y1};
  c.image(book,d.cover.pixels,d.cover.width,d.cover.height,d.cover.stride,true);c.hairline(book,t::InkPrimary,t::Outline);if(epub)c.fill({book.x0,book.y0,book.x0+12,book.y1},t::InkPrimary);page=book;}
 // The fold is on the cover's corner: on the tile's corner it stood in the empty space beside a narrow cover.
 if(d.format=="pdf"){const float k=40;Path fold;fold.move(float(page.x1)-k,float(page.y0)).line(float(page.x1),float(page.y0)).line(float(page.x1),float(page.y0)+k).close();c.fillPath(fold,t::Paper);
  Path tri;tri.move(float(page.x1)-k,float(page.y0)).line(float(page.x1)-k,float(page.y0)+k).line(float(page.x1),float(page.y0)+k);c.stroke(tri,float(t::Outline),t::InkPrimary);}
}
}
const char* sectionName(Section s)noexcept{static constexpr std::array<const char*,unsigned(Section::Count)> N{"My files","Notes","Favorites","Recents","Shared","Inkie","Trash","Settings"};return unsigned(s)<N.size()?N[unsigned(s)]:"";}
int folderCapacity()noexcept{return FolderCols*FolderRows;}
Geometry geometry(int w,int h)noexcept{Geometry g;if(w<=h)return g; /* portrait: the constants */
 g.width=w;g.height=h;g.contentRight=w-(t::ScreenWidth-ContentRight);g.gridCols=4;
 /* the pill 50 px above the foot (68 in portrait), so two rows of covers fit above it */const int pillW=width(PillRect),pillTop=h-50-height(PillRect);g.pill={(w-pillW)/2,pillTop,(w+pillW)/2,pillTop+height(PillRect)};g.bottomLimit=pillTop-(PillRect.y0-1985);g.pagerLeft=g.pill.x0+(1160-PillRect.x0);
 g.notice={ContentLeft,NoticeRect.y0,g.contentRight,NoticeRect.y1};g.status={StatusRect.x0+(w-t::ScreenWidth),StatusRect.y0,StatusRect.x1+(w-t::ScreenWidth),StatusRect.y1};g.drawer={DrawerRect.x0,0,DrawerRect.x1,h};return g;}
int capacity(bool grid,int folders,const Geometry& g)noexcept{int top=documentsTop(folders);return grid?std::max(1,(g.bottomLimit-top+GridGapY)/(GridRowH+GridGapY))*g.gridCols:std::max(1,(g.bottomLimit-top)/ListRowH);}
int capacity(bool grid,int folders)noexcept{return capacity(grid,folders,Geometry{});}
Rect createMenuRect(std::size_t rows)noexcept{int h=36+56+int(rows)*120+20;return {CreateRect.x0,CreateRect.y1-h,CreateRect.x1,CreateRect.y1};}
void placeholderCover(Canvas& c,Rect r,std::string_view title,std::string_view format){
 // A clothbound cover: flat cloth in one of the panel's clean colours (picked from the title, so a book keeps its
 // colour), a paper label plate with the title, and the format printed on the cloth. Every colour edge meets ink.
 unsigned hash=2166136261u;for(unsigned char ch:title)hash=(hash^ch)*16777619u;const std::uint32_t cloth=t::Pop[hash%t::Pop.size()],ink=t::onPop(cloth);
 c.fill(r,t::Paper);c.fillRound(r,t::RadiusSmall,cloth);c.strokeRound(r,t::RadiusSmall,float(t::Outline),t::InkPrimary);
 const int w=width(r),h=height(r),side=std::max(t::S2,w/9)+6;
 const Rect plate{r.x0+side,r.y0+h*16/100,r.x1-side+6,r.y0+h*60/100};c.fillRound(plate,4,t::Paper);c.strokeRound(plate,4,float(t::Outline),t::InkPrimary);
 const Font f{Face::SerifSemibold,float(std::clamp(w/10,20,40))};const int lh=int(text::resolved(f).size*1.02f);const Rect in=inset(plate,std::max(6,w/28));
 const int rows=std::max(1,std::min(4,height(in)/std::max(1,lh)));const auto lines=text::wrap(f,title,float(width(in)),rows);
 const float asc=text::capHeight(f);float y=in.y0+(height(in)-float(int(lines.size())*lh))/2.f+(lh+asc)/2.f;
 for(const auto& line:lines){text::drawAligned(c,f,in,y,line,t::InkPrimary,Align::Center);y+=lh;}
 Font m{Face::Literal,float(std::clamp(w/18,14,25)),2};const std::string fmt=upper(format);
 if(int(text::measure(m,fmt))<w-2*side)text::drawAligned(c,m,{plate.x0,plate.y1,plate.x1,r.y1},float(r.y1-(r.y1-plate.y1)/2+text::capHeight(m)/2),fmt,ink,Align::Center);
}
void base(Canvas& c,Targets& tg,const Model& m){
 const auto g=geometry(c.surface().width,c.surface().height);const int ContentX=g.contentLeft,ContentR=g.contentRight,GridCols=g.gridCols;const Rect NoticeArea=g.notice,PillArea=g.pill;
 tg.clear();c.fill(c.clip(),t::Paper);
 // Mast: the collapsed menu, the centered wordmark. Device status shows only when it is real (see docs).
 iconButton(c,tg,Menu,{62,40,174,152},Icon::Menu);
 {const Font brand{Face::TextSemibold,float(t::Heading.size)};float w=text::span(brand,"Inkie");Rect b{int((g.width-w)/2)-t::S2,40,int((g.width+w)/2)+t::S2,152};wordmark(c,b,float(t::Heading.size));tg.add(Wordmark,b);}
 {status::Model status;status.battery=m.battery;status.charging=m.charging;status.wifi=m.wifi?1:-1;status.clock=m.clock;status.zone=m.clockZone;ib::status::bar(c,tg,status,g.status);}
 // Heading row: title (or the search field), then sort and the grid/list toggle.
 if(m.searching){Rect field{ContentX,HeadingTop,ContentR,HeadingTop+88};c.fill(field,t::Paper);c.hairline(field,t::InkPrimary,2);icon(c,Icon::Search,{field.x0+t::S3,field.y0+24,field.x0+t::S3+40,field.y0+64},t::InkSecondary);
  if(m.query.empty())text::centeredLine(c,HintFont,{field.x0+t::S3+40+t::S3,field.y0,field.x1-t::MinTarget,field.y1},"Search titles",t::InkPrimary,Align::Left);else text::centeredLine(c,{Face::Text,theme::Body.size},{field.x0+t::S3+40+t::S3,field.y0,field.x1-t::MinTarget,field.y1},m.query+"|",t::InkPrimary,Align::Left);
  iconButton(c,tg,ClearSearch,{field.x1-t::MinTarget,field.y0-4,field.x1,field.y1+4},Icon::Close);}
 else if(m.section!=Section::Settings){ /* Settings draws its own titles (ib/settings_screen) */
  int titleX=ContentX;if(!m.crumb.empty()){iconButton(c,tg,Up,{ContentX-t::S4,HeadingTop,ContentX-t::S4+t::MinTarget,HeadingBottom},Icon::ChevronLeft);titleX+=t::MinTarget-t::S4;}
  // The section heading sits on a highlighter swash in the section's own colour.
  const Font h=type::ScriptHeading;swash(c,h,float(titleX),HeadingTop+(HeadingBottom-HeadingTop+text::capHeight(h))/2.f,text::fit(h,m.heading,float(ContentR-420-titleX)),t::SectionColor[std::min(unsigned(m.section),7u)]);
  int right=ContentR;if(m.section==Section::MyFiles||m.section==Section::Favorites||m.section==Section::Recents||m.section==Section::Trash){iconButton(c,tg,ViewMode,{right-t::MinTarget+t::S3,HeadingTop,right+t::S3,HeadingBottom},m.grid?Icon::List:Icon::Grid);right-=t::MinTarget;}
  if(!m.sortLabel.empty()){const Font f{Face::Text,theme::Label.size};float w=text::span(f,m.sortLabel);Rect sort{right-int(w)-t::IconSmall-t::S2-t::S2-12,HeadingTop,right-t::S2,HeadingBottom};
   text::centeredLine(c,f,{sort.x0,sort.y0,sort.x1-t::IconSmall-t::S2,sort.y1},m.sortLabel,t::InkPrimary,Align::Right);int mid=(sort.y0+sort.y1)/2;icon(c,Icon::ChevronDown,{sort.x1-t::IconSmall,mid-14,sort.x1-4,mid+14},t::InkPrimary);tg.add(Sort,hitArea(sort));}
 }
 if(!m.notice.empty())text::centeredLine(c,Meta,NoticeArea,text::fit(Meta,text::quiet(m.notice),float(width(NoticeArea))),t::InkSecondary,Align::Left);
 else if(!m.crumb.empty())text::centeredLine(c,Meta,NoticeArea,m.crumb,t::InkSecondary,Align::Left);
 // Folders: an inline icon and name, three per row.
 int shown=std::min(int(m.folders.size()),folderCapacity());int fw=(ContentR-ContentX-(FolderCols-1)*FolderGap)/FolderCols;
 for(int i=0;i<shown;++i){int col=i%FolderCols,row=i/FolderCols;Rect r{ContentX+col*(fw+FolderGap),FolderTop+row*FolderH,0,0};r.x1=r.x0+fw;r.y1=r.y0+FolderH;
  int mid=(r.y0+r.y1)/2;icon(c,Icon::Folder,{r.x0,mid-22,r.x0+44,mid+22},t::InkPrimary);
  text::centeredLine(c,Label,{r.x0+60,r.y0,r.x1,r.y1},text::fit(Label,m.folders[std::size_t(i)].name,float(fw-70)),t::InkPrimary,Align::Left);tg.add(FolderBase+i,hitArea(r));} /* 76 px rows, 96 px targets */
 int top=documentsTop(shown);
 // Documents
 int cap=capacity(m.grid,shown,g);int n=std::min(int(m.documents.size()),cap);
 if(m.documents.empty()&&m.section!=Section::Notes&&m.section!=Section::Settings)text::draw(c,type::ScriptEmpty,float(ContentX),float(top+60),m.searching?"No matching titles":m.section==Section::Trash?"Trash is empty":"Nothing here yet",t::InkSecondary);
 if(m.grid){int cw=(ContentR-ContentX-(GridCols-1)*GridGapX)/GridCols;
  for(int i=0;i<n;++i){const auto& d=m.documents[std::size_t(i)];int col=i%GridCols,row=i/GridCols;Rect thumb{ContentX+col*(cw+GridGapX),top+row*(GridRowH+GridGapY),0,0};thumb.x1=thumb.x0+cw;thumb.y1=thumb.y0+ThumbH;
   thumbnail(c,thumb,d);int ty=thumb.y1+18;float tw=float(cw-(d.favorite?44:0));text::centeredLine(c,Label,{thumb.x0,ty,thumb.x0+int(tw),ty+NameH-18},text::fit(Label,d.title,tw),t::InkPrimary,Align::Left);
   if(d.favorite)icon(c,Icon::Star,{thumb.x1-28,ty+6,thumb.x1,ty+34},t::InkPrimary);
   text::centeredLine(c,Meta,{thumb.x0,thumb.y1+NameH,thumb.x1,thumb.y1+NameH+MetaH-6},text::fit(Meta,d.meta,float(cw)),t::InkSecondary,Align::Left);tg.add(DocumentBase+i,{thumb.x0,thumb.y0,thumb.x1,thumb.y1+NameH+MetaH});}
 }else{
  for(int i=0;i<n;++i){const auto& d=m.documents[std::size_t(i)];Rect r{ContentX,top+i*ListRowH,ContentR,top+(i+1)*ListRowH};Rect thumb{r.x0,r.y0+23,r.x0+96,r.y1-23};
   thumbnail(c,thumb,d);int x=thumb.x1+35;
   text::centeredLine(c,{Face::Text,theme::Label.size},{x,r.y0+30,r.x1-80,r.y0+86},text::fit({Face::Text,theme::Label.size},d.title,float(r.x1-80-x)),t::InkPrimary,Align::Left);
   text::centeredLine(c,{Face::Text,theme::Meta.size},{x,r.y0+90,r.x1-80,r.y0+136},text::fit({Face::Text,theme::Meta.size},upper(d.format)+" \xC2\xB7 "+d.meta,float(r.x1-80-x)),t::InkSecondary,Align::Left);
   if(d.favorite)icon(c,Icon::Star,{r.x1-40,r.y0+70,r.x1-8,r.y0+102},t::InkPrimary);
   divider(c,r.x0,r.x1,r.y1-t::Hairline);tg.add(DocumentBase+i,r);}
 }
 // Pager (e-ink pages instead of scrolling; swipes page too), right of the pill.
 if(m.pages>1&&m.section!=Section::Notes&&m.section!=Section::Settings){Rect p{g.pagerLeft,PillArea.y0,ContentR,PillArea.y1};std::string label=std::to_string(m.page+1)+" of "+std::to_string(m.pages);
  text::centeredLine(c,Meta,{p.x0+t::MinTarget,p.y0,p.x1-t::MinTarget,p.y1},label,t::InkSecondary,Align::Center);
  iconButton(c,tg,Prev,{p.x0,p.y0,p.x0+t::MinTarget,p.y1},Icon::ChevronLeft,{false,m.page<=0});iconButton(c,tg,Next,{p.x1-t::MinTarget,p.y0,p.x1,p.y1},Icon::ChevronRight,{false,m.page+1>=m.pages});}
}
void overlays(Canvas& c,Targets& tg,const Model& m){
 const auto g=geometry(c.surface().width,c.surface().height);
 // Reading/search remains available; standalone notebook creation has no control or target.
 if(m.section!=Section::Settings){const Rect p=g.pill,v=inset(p,8);
  c.fillRound(v,t::RadiusMedium,m.searching?t::ToolSelected:t::Surface);
  c.strokeRound(v,t::RadiusMedium,float(t::Outline),t::InkPrimary);
  icon(c,Icon::Search,centered(p,40,40),m.searching?t::OnSelected:t::InkPrimary);tg.add(Search,p);}


 if(m.drawer){Rect r=g.drawer;c.fill(r,t::Paper);c.vline(r.x1-t::Outline,r.y0,r.y1,t::InkPrimary,t::Outline);
  iconButton(c,tg,Menu,{r.x0+40,38,r.x0+152,180},Icon::Menu);text::centeredLine(c,{Face::Text,theme::Label.size},{r.x0+40+112+t::S1,38,r.x1-40,180},"Menu",t::InkPrimary,Align::Left);
  // Each place has an index tab in its colour at the drawer's edge, as a divided notebook; the current place is an ink
  // row. Places without a colour (Settings, extras) have no tab.
  auto row=[&](int id,Icon ic,std::string_view label,int y,bool active,std::uint32_t pop){Rect q{r.x0+40,y,r.x1-40,y+100};std::uint32_t ink=t::InkPrimary;
   if(pop!=t::InkPrimary){const Rect tab{r.x0,q.y0+14,r.x0+18,q.y1-14};c.fill(tab,pop);c.hairline(tab,t::InkPrimary,t::Outline);}
   if(active){c.fillRound(inset(q,6),t::RadiusMedium,t::ToolSelected);ink=t::OnSelected;}
   icon(c,ic,{q.x0+24,q.y0+28,q.x0+68,q.y0+72},ink);text::centeredLine(c,{Face::Text,theme::Label.size},{q.x0+28+35+26,q.y0,q.x1,q.y1},label,ink,Align::Left);tg.add(id,q);};
  int y=180;for(Section s:{Section::MyFiles,Section::Notes,Section::Favorites,Section::Recents}){row(NavBase+int(s),NavIcons[unsigned(s)],sectionName(s),y,s==m.section,t::SectionColor[unsigned(s)]);y+=100;}
  y+=24;divider(c,r.x0+40,r.x1-40,y);y+=24;
  if(m.tomAvailable){row(NavBase+int(Section::Tom),Icon::Tom,"Inkie",y,false,t::SectionColor[unsigned(Section::Tom)]);y+=100;}
  if(m.sharedAvailable){row(NavBase+int(Section::Shared),Icon::Shared,"Shared",y,false,t::SectionColor[unsigned(Section::Shared)]);y+=100;}
  int by=r.y1-70-int(2+m.drawerExtras.size())*100;
  for(const auto& it:m.drawerExtras){row(it.id,it.icon,it.label,by,false,t::InkPrimary);by+=100;}
  row(NavBase+int(Section::Trash),Icon::Trash,"Trash",by,m.section==Section::Trash,t::SectionColor[unsigned(Section::Trash)]);by+=100;row(NavBase+int(Section::Settings),Icon::Settings,"Settings",by,m.section==Section::Settings,t::SectionColor[unsigned(Section::Settings)]);}
}
void paint(Canvas& c,Targets& tg,const Model& m){base(c,tg,m);overlays(c,tg,m);}
}
