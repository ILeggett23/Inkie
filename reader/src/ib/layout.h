// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// One spatial system for every Inkie surface (docs/LAYOUT-ARCHITECTURE.md). Zones are device-pixel rects on the
// 1620x2160 panel. Painters place surfaces inside their zone and register targets from the same rects, so hit testing
// never disagrees with what was drawn. The reading view also turns to landscape (docs/LANDSCAPE.md): its chrome is laid
// out in an upright logical frame (2160x1620) from the landscape zones and mapped onto the panel by Frame.
#include "ib/canvas.h"
#include "ib/theme.h"
namespace ib::reader {
// Toolbar side follows handedness: Left (the default) for right-handed writers, Right for left-handed.
enum class Side:unsigned {Left,Right};
}
namespace ib::layout {
enum class Orientation:unsigned {Portrait,Landscape};
// The way the screen is turned. Clockwise: the tablet's top edge turned to the right (its left edge on top).
enum class Turn:unsigned {Portrait,Clockwise,Anticlockwise};
inline Orientation orientation(Turn t)noexcept{return t==Turn::Portrait?Orientation::Portrait:Orientation::Landscape;}
// The current turn of the reading chrome (set by the shell, as the toolbar side is). Reader chrome geometry
// (reader::toolbarRect and friends, popoverBounds) is logical in this turn.
Turn turn()noexcept;void setTurn(Turn)noexcept;
// Logical screen and page area of the current turn: 1620x2160 and 2060 in portrait, 2160x1620 and 1520 in landscape.
int screenWidth()noexcept;int screenHeight()noexcept;int pageHeight()noexcept;
// Logical <-> physical for one turn. Logical is upright for the reader; physical is the panel (1620x2160, its own top
// edge up). Points are pixel indices, rects half-open.
struct Frame {
 Turn turn{Turn::Portrait};
 int width()const noexcept{return turn==Turn::Portrait?theme::ScreenWidth:theme::ScreenHeight;}
 int height()const noexcept{return turn==Turn::Portrait?theme::ScreenHeight:theme::ScreenWidth;}
 Rect physical(Rect logical)const noexcept;Rect logical(Rect physical)const noexcept;
 void physical(int x,int y,int& px,int& py)const noexcept;void logical(int px,int py,int& x,int& y)const noexcept;
 // Copies pixels of a logical rect between a physical buffer and an upright logical buffer (stride in pixels).
 void toLogical(const std::uint32_t* physical,int physicalStride,std::uint32_t* logical,int logicalStride,Rect r)const noexcept;
 void toPhysical(const std::uint32_t* logical,int logicalStride,std::uint32_t* physical,int physicalStride,Rect r)const noexcept;
};
// Stride of an upright logical buffer: 1632 in portrait, 2176 in landscape (both 3,525,120 pixels).
inline int logicalStride(Turn t)noexcept{return t==Turn::Portrait?1632:2176;}
// Top chrome row: the toolbar handle, the circled close and the zoom badge live here; popovers stay below it.
inline constexpr int TopRow=112;
// Gesture bands owned by the touch recogniser (touch.cpp), mirrored here so placement can keep clear of them:
// a swipe down from y<=EdgeBand opens Recents (or the drawer); one from x>=StatusCornerX opens quick settings;
// a swipe up from the footer band opens Quick Browse. Taps in these bands still reach chrome targets.
inline constexpr int EdgeBand=80,StatusCornerX=1080;
// Lower sheets (Tom, Note): 1000 px of 2160 (46%). The 1480x680 writing area is fixed because stored strokes are
// relative to it; header and quoted passage need the remaining 320 px. Expanded sheets may rise to 300.
inline constexpr int SheetTop=1160,SheetExpandedTop=300,SheetHeader=136,SheetMargin=70;
struct Zones {
 Rect screen;         // whole panel in this orientation
 Rect content;        // the page; never shrunk or repaginated by chrome
 Rect footer;         // quiet progress line; tap reveals chrome, swipe up opens Quick Browse
 Rect toolbar;        // full-height tool column on the side away from the writing hand
 Rect close;          // circled close, top corner opposite the toolbar
 Rect palette;        // attached palette, level with the pen shortcut
 Rect strip;          // contextual strip band (selection actions, highlight colours)
 Rect sheet;          // lower sheet (portrait) or side sheet (landscape)
 Rect sheetExpanded;  // tallest a sheet may grow
 Rect popover;        // where floating cards may be placed while chrome is shown
 Rect reservedTop,reservedStatus,reservedBottom; // gesture bands
};
Zones zones(reader::Side,Orientation=Orientation::Portrait)noexcept;
// Placement bounds for transient popovers (logical, in the current turn) (selection strip, definition card): the page below the top chrome row,
// minus the toolbar column when it is shown (excluded is that column, or empty when chrome is hidden).
Rect popoverBounds(Rect excluded={})noexcept;
}
