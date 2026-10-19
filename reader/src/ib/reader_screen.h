// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Reading view chrome (concepts #2, #3, #5, #6). Everything here is transient
// and drawn over the document; the clean state shows only the footer strip.
#include "ib/components.h"
#include "ib/layout.h"
#include <string>
#include <vector>
namespace ib::reader {
// ---- Reader chrome (native Paper Pro layout) ----------------------------
// Clean state: the page, a quiet footer and the toolbar handle. Chrome state: the toolbar column on
// the writing-hand-free side plus a circled close at the opposite top corner. There is no top bar;
// search, bookmarks, settings, notes and Tom live in the document menu at the bottom of the toolbar.
struct Chrome {std::string title,chapter;int page{},total{1};bool bookmarked{},loading{},search{true},bookmark{false};std::string status;bool menuMark{};bool progress{true}; /* Settings > Reading: chapter and page under the page */};
enum ChromeId:int {Back=1,Title,PageLabel,Search,Bookmark,More,Footer,PreviousZone,NextZone,RevealZone,ToolHandle,ZoomBadge};
// Toolbar side (Side, ib/layout.h): one process-wide setting, applied by the shell from ui-toolbar.json before any
// chrome is painted. Every rect below follows it; layout::zones() describes the same geometry.
Side toolbarSide()noexcept;void setToolbarSide(Side)noexcept;
inline constexpr int ToolbarWidth=112,ToolbarItem=layout::TopRow,ToolbarGlyph=48,HandleGlyph=44,ToolbarTile=84,PenGlyph=58; // tile: the selected tool's ink square; PenGlyph: writing tools
inline constexpr Rect FooterRect{0,2060,1620,2160}; // portrait; footerRect() follows the turn
Rect footerRect()noexcept;  // the quiet footer under the page, in the current turn (layout::turn)
inline constexpr int FooterInset=160; // footer text margin, clear of the toolbar column
Rect toolbarRect()noexcept;  // the full-height column (page area) on the toolbar side
Rect closeRect()noexcept;    // circled close, top corner opposite the toolbar
Rect topRect()noexcept;      // the top strip refreshed with chrome: handle and close
void closeButton(Canvas&,Targets&);
// Native zoom indicator (top centre while a PDF is zoomed): tap to return to the fitted view.
inline constexpr Rect ZoomBadgeRect{700,24,920,100};
void zoomBadge(Canvas&,Targets&,int percent);
void footer(Canvas&,Targets&,const Chrome&);
// Clean state: the footer reveals chrome (the handle is registered by toolHandle()).
void cleanZones(Targets&);
// ---- Toolbar and attached pen palette -----------------------------------
enum class Width:unsigned {Thin,Medium,Thick};
struct Tools {
 Instrument pen{Instrument::Fineliner};std::uint32_t ink{theme::Ink[0]};unsigned color{};Width width{Width::Medium};
 Instrument primary{Instrument::Fineliner},secondary{Instrument::Highlighter};unsigned primaryColor{},secondaryColor{4},activeShortcut{~0u};
 bool eraser{},highlighter{},selecting{},canUndo{},canRedo{},busy{},panel{},layers{},menu{};unsigned highlightColor{4};
 bool quietEffects{};std::string status;
 bool operator==(const Tools&)const=default;
 bool snapHighlights{true},canSnap{true};
 bool colors{true}; // false on note and Tom pages: dark ink only, no palette
};
// Toolbar ids stay one contiguous range (RailPen..RailMore) so input routing needs no change. RailTom
// and RailPages are document-menu actions now and have no toolbar tile. RailBlank covers the column
// between tiles so a tap there never reaches the page.
enum ToolId:int {RailPen=40,RailHighlighter,RailEraser,RailSelect,RailTom,RailLayers,RailUndo,RailRedo,RailPages,RailBlank,RailMore,PanelClose,QuietEffects,SnapHighlights,
 PenBase=60,WidthBase=80,ColorBase=90};
// Slots, top down, 112 px each: 0 handle, 1 pen shortcut, 2 highlighter shortcut, 3 eraser,
// 4 selection, 5 layers, 6 undo, 7 redo, 8 document menu.
inline constexpr unsigned ToolbarSlots=9,SlotHandle=0,SlotPen=1,SlotHighlighter=2,SlotEraser=3,SlotSelect=4,SlotLayers=5,SlotUndo=6,SlotRedo=7,SlotDocument=8;
Rect toolbarSlot(unsigned)noexcept;
// Palette attached to the toolbar edge, level with the pen shortcut (reference: 472 px wide).
inline constexpr int PenPanelWidth=472,PaletteTitle=82,NibRow=108,WidthRow=100,SwatchRow=116,PaletteGap=8,PalettePad=24;
inline constexpr int PenPanelCompactHeight=PaletteTitle+10+3*NibRow+2*PaletteGap+PalettePad+2+PaletteTitle+WidthRow+28; // no colors
inline constexpr int PenPanelHeight=PenPanelCompactHeight+2+30+3*SwatchRow+2*PaletteGap+30+96;
Rect penPanelRect()noexcept;
inline Rect snapCell()noexcept{auto r=penPanelRect();return {r.x0+24,r.y1-96,r.x1-24,r.y1-8};}
// Pen panel cells (shared by painter and hit testing) of a palette at r.
Rect penCell(unsigned,Rect r)noexcept;Rect widthCell(unsigned,Rect r)noexcept;Rect colorCell(unsigned,Rect r)noexcept;
inline Rect penCell(unsigned i)noexcept{return penCell(i,penPanelRect());}
inline Rect widthCell(unsigned i)noexcept{return widthCell(i,penPanelRect());}
inline Rect colorCell(unsigned i)noexcept{return colorCell(i,penPanelRect());}
void toolbar(Canvas&,Targets&,const Tools&);
void penPanel(Canvas&,Targets&,const Tools&,Rect at);
inline void penPanel(Canvas& c,Targets& tg,const Tools& m){penPanel(c,tg,m,penPanelRect());}
// The collapsed toolbar: the native hide/show handle alone at the top of the toolbar side.
Rect toolHandleRect()noexcept;
void toolHandle(Canvas&,Targets&,const Tools&);
// ---- Selection toolbar, highlight colors, definition card -----------------
enum SelectionId:int {SelNote=120,SelTom,SelHighlight,SelCopy,SelMore,SelDefine,HighlightColorBase=130,HighlightRemove=140,
 DefClose=150,DefWriteNote,DefWriteTom,DefMore,DefPrevious,DefNext};
struct SelectionBar {Rect anchor{};bool define{},copy{true},more{},highlighted{},note{true},tom{true},highlight{true};}; // more: no action is routed for it, so it is off
// Returns the toolbar rect so a caller can place the color row beneath it.
Rect selectionToolbar(Canvas&,Targets&,const SelectionBar&,Rect bounds=layout::popoverBounds());
// Stacks on the side of the toolbar away from the passage so it never covers it.
Rect highlightColors(Canvas&,Targets&,Rect toolbar,Rect passage,unsigned selectedColor,bool removable);
struct Sense {std::string partOfSpeech,definition;};
struct Definition {std::string word,pronunciation,partOfSpeech,status,attribution;std::vector<Sense> senses;bool loading{},actions{true},expanded{};unsigned lineOffset{};};
Rect definitionCard(Canvas&,Targets&,Rect anchor,const Definition&,Rect bounds=layout::popoverBounds());
// Selection wash and handles drawn over page text quads (axis-aligned line boxes).
void selectionWash(Canvas&,std::span<const Rect> lines);
void highlightWash(Canvas&,std::span<const Rect> lines,std::uint32_t color);
}
