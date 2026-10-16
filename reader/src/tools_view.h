#pragma once
#include "core.h"
#include "ib/reader_screen.h"
#include <string>
namespace ux {
// Pure touch-command view. Caller owns persistence, admission and redraw timing.
// Active contact blocks every action; busy storage still permits Close.
class ToolsView final {
public:
 enum class Tool:unsigned {Ballpoint,Fineliner,Pencil,Mechanical,Paintbrush,Calligraphy,Marker,Shader,Highlighter};
 enum class Width:unsigned {Thin,Medium,Thick};
 struct Style {Tool tool{Tool::Ballpoint};Width width{Width::Medium};unsigned color{};bool eraser{};bool operator==(const Style&)const=default;};
 // Revision covers style/action availability as well as page presentation.
 struct Binding {std::string document,page;std::uint64_t revision{};bool operator==(const Binding&)const=default;};
 struct Snapshot {Binding binding;Style style;bool busy{},contact{},canUndo{},canRedo{},quietEffects{};bool snapHighlights{true};unsigned shortcut{~0u};};
 enum class Action {None,Tool,Width,Color,Eraser,Undo,Redo,Reveal,Close,Snap};
 enum class Refusal {None,Stale,Busy,Unavailable};
 struct Command {Action action{Action::None};Binding binding;Style style;Refusal refusal{Refusal::None};bool consumed{},quietEffects{};bool snapHighlights{true};};
 // Native pen palette attached to the toolbar. Eraser, undo and redo live on the toolbar; the palette
 // routes those tiles too while it is open. The palette has no close glyph: tapping the active
 // shortcut tile again (closeRect) or anywhere outside closes it, as on the native toolbar.
 // Geometry follows the toolbar side, so these are functions.
 static spike::Rect panel()noexcept{return ib::reader::penPanelRect();}
 static spike::Rect eraser()noexcept{return ib::reader::toolbarSlot(ib::reader::SlotEraser);}
 static spike::Rect undo()noexcept{return ib::reader::toolbarSlot(ib::reader::SlotUndo);}
 static spike::Rect redo()noexcept{return ib::reader::toolbarSlot(ib::reader::SlotRedo);}
 spike::Rect closeRect()const noexcept{if(snapshot_.style.eraser)return {}; /* the pen tile then switches the eraser off */return ib::reader::toolbarSlot(highlighterAnchor_?ib::reader::SlotHighlighter:ib::reader::SlotPen);}
 // Quiet effects moved to Document settings > Reading; kept for API stability.
 inline static constexpr spike::Rect RevealToggle{};
 static spike::Rect toolRect(unsigned)noexcept;
 static spike::Rect widthRect(unsigned)noexcept;
 static spike::Rect colorRect(unsigned)noexcept;
 static const char* toolName(Tool)noexcept;
 static const char* colorName(unsigned)noexcept;
 static std::uint32_t colorRgb(unsigned)noexcept;
 static bool valid(const Snapshot&)noexcept;
 bool open(const Snapshot&);
 bool update(const Snapshot&); // No implicit open; new revision releases pending command.
 bool active()const noexcept{return active_;}
 bool pending()const noexcept{return pending_;}
 const Snapshot& snapshot()const noexcept{return snapshot_;}
 Command hit(int x,int y,const Snapshot& current);
 bool paint(spike::Surface)const noexcept;
 spike::Rect takeDamage()noexcept{auto r=damage_;damage_={};return r;}
private:
 Snapshot snapshot_;bool active_{},pending_{},highlighterAnchor_{};spike::Rect damage_{};
};
}
