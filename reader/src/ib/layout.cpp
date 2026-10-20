// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/layout.h"
#include "ib/reader_screen.h"
#include "ib/sheets.h"
namespace ib::layout {
namespace t=theme;
namespace {Turn turn_{Turn::Portrait};}
Turn turn()noexcept{return turn_;}void setTurn(Turn t)noexcept{turn_=t;}
int screenWidth()noexcept{return Frame{turn_}.width();}int screenHeight()noexcept{return Frame{turn_}.height();}
int pageHeight()noexcept{return screenHeight()-(t::ScreenHeight-t::PageFitHeight);}
// Clockwise: logical (x,y) is physical (y, 2159-x); anticlockwise: physical (1619-y, x).
Rect Frame::physical(Rect r)const noexcept{switch(turn){case Turn::Clockwise:return {r.y0,t::ScreenHeight-r.x1,r.y1,t::ScreenHeight-r.x0};case Turn::Anticlockwise:return {t::ScreenWidth-r.y1,r.x0,t::ScreenWidth-r.y0,r.x1};default:return r;}}
Rect Frame::logical(Rect r)const noexcept{switch(turn){case Turn::Clockwise:return {t::ScreenHeight-r.y1,r.x0,t::ScreenHeight-r.y0,r.x1};case Turn::Anticlockwise:return {r.y0,t::ScreenWidth-r.x1,r.y1,t::ScreenWidth-r.x0};default:return r;}}
void Frame::physical(int x,int y,int& px,int& py)const noexcept{switch(turn){case Turn::Clockwise:px=y;py=t::ScreenHeight-1-x;break;case Turn::Anticlockwise:px=t::ScreenWidth-1-y;py=x;break;default:px=x;py=y;}}
void Frame::logical(int px,int py,int& x,int& y)const noexcept{switch(turn){case Turn::Clockwise:x=t::ScreenHeight-1-py;y=px;break;case Turn::Anticlockwise:x=py;y=t::ScreenWidth-1-px;break;default:x=px;y=py;}}
void Frame::toLogical(const std::uint32_t* from,int fromStride,std::uint32_t* to,int toStride,Rect r)const noexcept{r=intersect(r,{0,0,width(),height()});
 for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x){int px,py;physical(x,y,px,py);to[std::size_t(y)*toStride+x]=from[std::size_t(py)*fromStride+px];}}
void Frame::toPhysical(const std::uint32_t* from,int fromStride,std::uint32_t* to,int toStride,Rect r)const noexcept{r=intersect(r,{0,0,width(),height()});
 for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x){int px,py;physical(x,y,px,py);to[std::size_t(py)*toStride+px]=from[std::size_t(y)*fromStride+x];}}
Rect SheetMap::physical(Rect s)const noexcept{s=intersect(s,visible());if(!turned()||s.empty())return s;return Frame{turn}.physical({s.x0+TurnedSheetLeft,s.y0-TurnedSheetTop,s.x1+TurnedSheetLeft,s.y1-TurnedSheetTop});}
Rect SheetMap::sheet(Rect p)const noexcept{if(!turned())return p;const Rect l=Frame{turn}.logical(p);return {l.x0-TurnedSheetLeft,l.y0+TurnedSheetTop,l.x1-TurnedSheetLeft,l.y1+TurnedSheetTop};}
void SheetMap::physical(int x,int y,int& px,int& py)const noexcept{if(!turned()){px=x;py=y;return;}Frame{turn}.physical(x+TurnedSheetLeft,y-TurnedSheetTop,px,py);}
void SheetMap::sheet(int px,int py,int& x,int& y)const noexcept{if(!turned()){x=px;y=py;return;}Frame{turn}.logical(px,py,x,y);x-=TurnedSheetLeft;y+=TurnedSheetTop;}
Zones zones(reader::Side side,Orientation o)noexcept{
 namespace R=reader;const bool left=side==R::Side::Left;
 const int w=o==Orientation::Portrait?t::ScreenWidth:t::ScreenHeight,h=o==Orientation::Portrait?t::ScreenHeight:t::ScreenWidth;
 const int footer=t::ScreenHeight-t::PageFitHeight,pageBottom=h-footer;
 Zones z;z.screen={0,0,w,h};z.content={0,0,w,pageBottom};z.footer={0,pageBottom,w,h};
 z.toolbar=left?Rect{0,0,R::ToolbarWidth,pageBottom}:Rect{w-R::ToolbarWidth,0,w,pageBottom};
 z.close=left?Rect{w-R::ToolbarItem,0,w,R::ToolbarItem}:Rect{0,0,R::ToolbarItem,R::ToolbarItem};
 // The palette overlaps the toolbar's inner edge by 2 px so their ink outlines merge (as reader::penPanelRect).
 const int px=left?z.toolbar.x1-2:z.toolbar.x0-R::PenPanelWidth+2;z.palette={px,R::ToolbarItem,px+R::PenPanelWidth,R::ToolbarItem+R::PenPanelHeight};
 z.strip=left?Rect{z.toolbar.x1,TopRow,w,pageBottom}:Rect{0,TopRow,z.toolbar.x0,pageBottom};
 z.popover=z.strip;
 if(o==Orientation::Portrait){z.sheet={0,SheetTop,w,h};z.sheetExpanded={0,SheetExpandedTop,w,h};}
 else{ // The portrait sheet itself, centred at the foot (SheetMap): its 1480 px writing area keeps its size and place,
  // and the tallest sheet is the whole height (taller portrait sheets start at TurnedSheetTop).
  z.sheet={TurnedSheetLeft,SheetTop-TurnedSheetTop,TurnedSheetLeft+t::ScreenWidth,h};z.sheetExpanded={TurnedSheetLeft,0,TurnedSheetLeft+t::ScreenWidth,h};}
 z.reservedTop={0,0,w,EdgeBand};z.reservedStatus={w-(t::ScreenWidth-StatusCornerX),0,w,EdgeBand};z.reservedBottom=z.footer;
 return z;
}
Rect popoverBounds(Rect excluded)noexcept{
 Rect a{0,TopRow,screenWidth(),pageHeight()};
 if(!excluded.empty()){if(excluded.x0==0)a.x0=excluded.x1;else if(excluded.x1==screenWidth())a.x1=excluded.x0;}
 return a;
}
}
