// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// One spatial system for every Inkbound surface (docs/LAYOUT-ARCHITECTURE.md). Zones are device-pixel rects on the
// 1620x2160 panel. Painters place surfaces inside their zone and register targets from the same rects, so hit testing
// never disagrees with what was drawn. Portrait values are the shipping geometry; the runtime has no rotation, so the
// landscape zones are a specification (previews and docs), not a reachable layout.
#include "ib/canvas.h"
#include "ib/theme.h"
namespace ib::reader {
// Toolbar side follows handedness: Left (the default) for right-handed writers, Right for left-handed.
enum class Side:unsigned {Left,Right};
}
namespace ib::layout {
enum class Orientation:unsigned {Portrait,Landscape};
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
// Placement bounds for transient popovers (selection strip, definition card): the page below the top chrome row,
// minus the toolbar column when it is shown (excluded is that column, or empty when chrome is hidden).
Rect popoverBounds(Rect excluded={})noexcept;
}
