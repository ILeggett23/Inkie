// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/layout.h"
#include "ib/reader_screen.h"
#include "ib/sheets.h"
namespace ib::layout {
namespace t=theme;
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
 else{ // Side sheet on the writing-hand side (the hand rests on what it writes in), as wide as the portrait sheet is tall.
  const int sw=h-SheetTop;z.sheet=left?Rect{w-sw,0,w,h}:Rect{0,0,sw,h};z.sheetExpanded=left?Rect{w-(h-SheetExpandedTop)*w/h,0,w,h}:Rect{0,0,(h-SheetExpandedTop)*w/h,h};}
 z.reservedTop={0,0,w,EdgeBand};z.reservedStatus={w-(t::ScreenWidth-StatusCornerX),0,w,EdgeBand};z.reservedBottom=z.footer;
 return z;
}
Rect popoverBounds(Rect excluded)noexcept{
 Rect a{0,TopRow,t::ScreenWidth,t::PageFitHeight};
 if(!excluded.empty()){if(excluded.x0==0)a.x0=excluded.x1;else if(excluded.x1==t::ScreenWidth)a.x1=excluded.x0;}
 return a;
}
}
