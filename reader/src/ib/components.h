// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Inkie standard components. Immediate-mode painters: each draws and records
// its touch targets in the caller's Targets, so hit testing always uses the
// geometry that was painted. Visual size is independent of the hit area.
#include "ib/icons.h"
#include "ib/text.h"
#include "ib/theme.h"
#include <array>
#include <string>
#include <string_view>
#include <vector>
namespace ib {
struct Target {int id{-1};Rect rect{};};
class Targets {
 std::array<Target,160> items_{};unsigned count_{};
public:
 void clear()noexcept{count_=0;}
 void add(int id,Rect r)noexcept{if(count_<items_.size()&&!r.empty())items_[count_++]={id,r};}
 // Later targets win so overlays registered after their background take precedence.
 int find(int x,int y)const noexcept{for(unsigned i=count_;i-->0;)if(items_[i].rect.contains(x,y))return items_[i].id;return -1;}
 Rect rect(int id)const noexcept{for(unsigned i=0;i<count_;++i)if(items_[i].id==id)return items_[i].rect;return {};}
 unsigned size()const noexcept{return count_;}
 const Target& operator[](unsigned i)const noexcept{return items_[i];}
};
// Expands a visual rect to at least the minimum target size around its center.
Rect hitArea(Rect visual,int minimum=theme::MinTarget)noexcept;
// accent: the pop colour a selected button fills with (0 = theme::ToolSelected).
struct ButtonState {bool selected{},disabled{};std::uint32_t accent{};constexpr ButtonState(bool s=false,bool d=false,std::uint32_t a=0)noexcept:selected(s),disabled(d),accent(a){}};
// Icon only; selected draws a pop bubble behind an ink glyph. Returns the hit rect.
Rect iconButton(Canvas&,Targets&,int id,Rect hit,Icon,ButtonState={},int iconSize=theme::Icon);
// Icon above a short label (selection toolbar, sidebar of tools).
Rect iconLabelButton(Canvas&,Targets&,int id,Rect hit,Icon,std::string_view label,ButtonState={});
enum class ButtonKind {Primary,Secondary,Quiet,Destructive};
Rect button(Canvas&,Targets&,int id,Rect r,std::string_view label,ButtonKind=ButtonKind::Secondary,bool disabled=false,Icon leading=Icon::Count);
// Inkie bubble: a rounded surface with a bold ink outline and a solid pop shadow, all drawn inside r (so a surface
// never paints outside the bounds it reports for damage). fill is the body colour.
void bubble(Canvas&,Rect,std::uint32_t fill=theme::Surface,float radius=theme::RadiusMedium,bool shadow=true);
// A surface floating over the page (cards, menus, notices): a paper bubble. border is kept for callers; bubbles are ink.
void panel(Canvas&,Rect,float radius=theme::RadiusMedium,std::uint32_t border=theme::BorderStrong);
// Popover (pen palette, document menu, foldouts): a paper bubble.
void popover(Canvas&,Rect);
// A pop dot: a filled circle with an ink outline and an icon in the ink that reads on it (section and action badges).
void dot(Canvas&,Rect box,Icon,std::uint32_t color);
// A title on a marker swash: the lower half of the text sits on a pop bar, as if underlined with a highlighter.
void swash(Canvas&,Font,float x,float baseline,std::string_view,std::uint32_t color);
// The Inkie wordmark: "Inkie" in heavy cursive, its two dots in pop colours, centred in box.
void wordmark(Canvas&,Rect box,float size);
// Context sheet (Note, Tom, highlight): paper from top to the bottom edge, rounded top corners closed by an ink
// outline, and a pop grab tab in the sheet's accent.
void contextSheet(Canvas&,int top,std::uint32_t accent=theme::PopCyan);
// Header: title (heavy cursive) and a quiet subtitle on the left (subtitle tap target = subtitleId when >= 0).
void sheetTitle(Canvas&,Targets&,int top,int height,std::string_view title,std::string_view subtitle,int subtitleId,int rightEdge);
// Quoted passage: a pigment rule on the left, serif text in secondary ink.
void sheetQuote(Canvas&,Rect,std::string_view text,std::uint32_t rule);
// Faint writing guides every gap px inside area (paper, not UI).
void ruledPaper(Canvas&,Rect area,int gap=96);
// Native primary action: a black square button with paper text (Send, Done).
Rect primaryAction(Canvas&,Targets&,int id,Rect,std::string_view label,Icon leading=Icon::Count,bool disabled=false);
// Text action: label only, underlined when emphasised (Retry).
Rect textAction(Canvas&,Targets&,int id,Rect,std::string_view label,bool underline=false,bool disabled=false,Align=Align::Left);
struct SheetFrame {Rect header,content;};
struct SheetOptions {bool back{},more{},close{true},done{};std::string_view subtitle{};int backId{-1},moreId{-1},closeId{-1},doneId{-1};};
SheetFrame sheet(Canvas&,Targets&,Rect,std::string_view title,const SheetOptions&);
// Underlined text tabs. Returns nothing; registers firstId+index targets.
void tabs(Canvas&,Targets&,int firstId,Rect,std::span<const std::string_view>,unsigned selected);
// Filter chips: "All  Linked  Pages  Favorites"; selected chip is filled.
void chips(Canvas&,Targets&,int firstId,Rect,std::span<const std::string_view>,unsigned selected);
void toggle(Canvas&,Targets&,int id,Rect hit,bool on,bool disabled=false);
void swatch(Canvas&,Targets&,int id,Rect hit,std::uint32_t color,bool selected,bool disabled=false,int diameter=56);
// Labelled settings row: "Zoom ............ Fit to width v"
Rect row(Canvas&,Targets&,int id,Rect,std::string_view label,std::string_view value={},bool chevron=false,bool divider=true);
// Sunken quote block with a leading rule. Returns used bottom.
int quote(Canvas&,Rect,std::string_view caption,std::string_view text,int maxLines=4);
void divider(Canvas&,int x0,int x1,int y);
// Segmented icon group (page layout, stroke widths). Registers firstId+index.
void segmented(Canvas&,Targets&,int firstId,Rect,std::span<const Icon>,unsigned selected);
// Small numeric/status badge in a pill.
void pill(Canvas&,Rect,std::string_view,bool filled=false);
// Places a floating w x h surface next to an anchor rect without covering it.
// Prefers below, then above; clamped to bounds with the theme gutter.
Rect placeNear(Rect anchor,int w,int h,Rect bounds,int gap=theme::S3);
// Wrapped body text helper with theme defaults; returns bottom.
int paragraph(Canvas&,Rect,std::string_view,Font={Face::Serif,float(theme::Body.size)},int lineHeight=theme::Body.line,std::uint32_t=theme::InkPrimary,int maxLines=1000);
}
