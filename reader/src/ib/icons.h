// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// One original line-icon set on a 24-unit grid: thin, geometric, monochrome by
// default. Drawn as vectors at any size so they stay crisp at 229 ppi.
#include "ib/canvas.h"
namespace ib {
enum class Icon:unsigned {
 Back,ChevronLeft,ChevronRight,ChevronDown,ChevronUp,Close,Search,Plus,Grid,List,Menu,More,MoreVertical,
 Bookmark,BookmarkFilled,Star,StarFilled,Folder,FolderFilled,Files,Notes,Clock,Shared,Tom,Note,Highlight,Copy,
 Settings,Eye,EyeOff,Lock,Unlock,Layers,Undo,Redo,Eraser,Lasso,Send,Check,Trash,Restore,Import,Pages,
 PageSingle,PageDouble,PageScroll,Keyboard,Handwrite,Link,Sort,Contrast,Crop,Info,Device,Books,Rotate,
 ZoomIn,ZoomOut,Fit,Text,Drag,Wifi,
 // Toolbar family (thin, monochrome): dashed selection box, dashed eraser block, Tom (a written reply), page grid.
 Selection,EraseArea,TomWrite,PageGrid,
 // Native Paper Pro toolbar family on a 40-unit grid (redrawn, not vendor assets): hide/show handle,
 // circled close, document menu, open book (text and layout), tag, and the toolbar's own eraser,
 // selection, layers, undo and redo strokes.
 ToolbarHandle,CloseCircle,DocumentMenu,BookOpen,Tag,NativeEraser,NativeSelection,NativeLayers,NativeUndo,NativeRedo,Count};
void icon(Canvas&,Icon,Rect box,std::uint32_t color,float strokeWidth=0); // strokeWidth 0 = theme default scaled
// Writing instruments, drawn tip-up as recognizable physical tools.
enum class Instrument:unsigned {Ballpoint,Fineliner,Pencil,Mechanical,Paintbrush,Calligraphy,Marker,Shader,Highlighter,Eraser,Count};
const char* instrumentName(Instrument)noexcept;
// ink: current ink color shown on the tip. inverse draws light-on-dark for the selected well.
void instrument(Canvas&,Instrument,Rect box,std::uint32_t ink,bool inverse=false);
// Toolbar glyph for a writing tool: one thin slanted line drawing whose tip shape names the tool.
// dot: the current ink/highlight color as a small filled dot (0 = none), as on the Paper Pro rail.
void toolGlyph(Canvas&,Instrument,Rect box,std::uint32_t color,std::uint32_t dot=0);
// Native toolbar glyph for a writing tool: the upright pen tip seen end-on, as on the Paper Pro toolbar
// and its attached palette. Stroke tools are outlines; marker, brush and shader tips are filled.
void nibGlyph(Canvas&,Instrument,Rect box,std::uint32_t color);
// Inkie glyph for a writing tool: its tip seen face-on and upright, as on the native toolbar, in the Inkie hand: a
// paper collar with an ink outline, a tip whose shape names the tool, and the collar's foot banded in the tool's
// current ink (0 = ink). selected draws it in paper for the ink tile. Rasterised once per key and cached.
void penGlyph(Canvas&,Instrument,Rect box,std::uint32_t ink=0,bool selected=false,bool disabled=false);
}
