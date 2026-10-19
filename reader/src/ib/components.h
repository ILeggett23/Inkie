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
 // The target nearest (x,y) within slop px of its rect (inside counts as 0, later targets win ties), or -1: a tap on a
 // divider or just off a card's edge still reaches the control it was aimed at.
 int nearest(int x,int y,int slop)const noexcept{int best=-1;long long bestDistance=(long long)slop*slop+1;for(unsigned i=count_;i-->0;){const auto& r=items_[i].rect;const long long dx=x<r.x0?r.x0-x:x>=r.x1?x-r.x1+1:0,dy=y<r.y0?r.y0-y:y>=r.y1?y-r.y1+1:0;if(dx*dx+dy*dy<bestDistance){bestDistance=dx*dx+dy*dy;best=items_[i].id;}}return best;}
 Rect rect(int id)const noexcept{for(unsigned i=0;i<count_;++i)if(items_[i].id==id)return items_[i].rect;return {};}
 unsigned size()const noexcept{return count_;}
 const Target& operator[](unsigned i)const noexcept{return items_[i];}
};
// Expands a visual rect to at least the minimum target size around its center.
Rect hitArea(Rect visual,int minimum=theme::MinTarget)noexcept;
// Placeholder text in an empty field: small printed italic, so a hint never reads as a value (values are Body).
inline constexpr Font HintFont{Face::TextItalic,float(theme::Meta.size)};
// accent: the colour a selected button fills with (0 = theme::ToolSelected, ink).
struct ButtonState {bool selected{},disabled{};std::uint32_t accent{};constexpr ButtonState(bool s=false,bool d=false,std::uint32_t a=0)noexcept:selected(s),disabled(d),accent(a){}};
// Icon only; selected draws a rounded ink tile behind a paper glyph. Returns the hit rect.
Rect iconButton(Canvas&,Targets&,int id,Rect hit,Icon,ButtonState={},int iconSize=theme::Icon);
// Icon above a short label (selection toolbar, sidebar of tools).
Rect iconLabelButton(Canvas&,Targets&,int id,Rect hit,Icon,std::string_view label,ButtonState={});
enum class ButtonKind {Primary,Secondary,Quiet,Destructive};
Rect button(Canvas&,Targets&,int id,Rect r,std::string_view label,ButtonKind=ButtonKind::Secondary,bool disabled=false,Icon leading=Icon::Count);
// Inkie card: a rounded paper surface closed by one 2 px ink rule, drawn entirely inside r (a surface never paints
// outside the bounds it reports for damage). fill is the body colour. No shadows: the rule is the edge.
void bubble(Canvas&,Rect,std::uint32_t fill=theme::Surface,float radius=theme::RadiusMedium,bool shadow=false);
// A surface floating over the page (cards, menus, notices): a paper card. border is kept for callers; cards are ink.
void panel(Canvas&,Rect,float radius=theme::RadiusMedium,std::uint32_t border=theme::BorderStrong);
// Popover (pen palette, document menu, foldouts): a paper card.
void popover(Canvas&,Rect);
// A heading on a highlighter swash: a slanted band of colour under the lower x-height with an ink foot. In ink
// (theme::InkPrimary) it is a plain underline instead.
void swash(Canvas&,Font,float x,float baseline,std::string_view,std::uint32_t color);
// The Inkie wordmark: "Inkie" in heavy script, the dot of the i a magenta drop of ink, centred in box.
void wordmark(Canvas&,Rect box,float size);
// A drop of ink: a round body of radius r centred at (cx,cy) with a pointed top, closed by an ink rule.
void inkDrop(Canvas&,float cx,float cy,float r,std::uint32_t color);
// Context sheet (Note, Tom, highlight, Notes hub, Quick Browse): square paper from top to the bottom edge, the
// accent colour as a band along its top edge, closed by an ink rule. turned: the sheet stands on a landscape page
// (layout::SheetMap), so its sides are ink rules too.
void contextSheet(Canvas&,int top,std::uint32_t accent=theme::PopCyan,bool turned=false);
// Header: title (heavy cursive) and a quiet subtitle on the left (subtitle tap target = subtitleId when >= 0).
void sheetTitle(Canvas&,Targets&,int top,int height,std::string_view title,std::string_view subtitle,int subtitleId,int rightEdge);
// Quoted passage: a pigment rule on the left, serif text in secondary ink.
void sheetQuote(Canvas&,Rect,std::string_view text,std::uint32_t rule);
// Faint writing guides every gap px inside area (paper, not UI).
void ruledPaper(Canvas&,Rect area,int gap=96);
// Native primary action: a rounded ink button with paper text (Send, Done).
Rect primaryAction(Canvas&,Targets&,int id,Rect,std::string_view label,Icon leading=Icon::Count,bool disabled=false);
// Text action: label only, underlined when emphasised (Retry).
Rect textAction(Canvas&,Targets&,int id,Rect,std::string_view label,bool underline=false,bool disabled=false,Align=Align::Left);
struct SheetFrame {Rect header,content;};
struct SheetOptions {bool back{},more{},close{true},done{};std::string_view subtitle{};int backId{-1},moreId{-1},closeId{-1},doneId{-1};};
SheetFrame sheet(Canvas&,Targets&,Rect,std::string_view title,const SheetOptions&);
// Underlined text tabs over a dotted rule. Returns nothing; registers firstId+index targets.
void tabs(Canvas&,Targets&,int firstId,Rect,std::span<const std::string_view>,unsigned selected);
// Filters ("All  Linked  Pages  Favorites"): small underlined tabs; the chosen one is heavier on an ink bar.
void chips(Canvas&,Targets&,int firstId,Rect,std::span<const std::string_view>,unsigned selected);
// On/off: a checkbox, ink with a paper tick when on.
void toggle(Canvas&,Targets&,int id,Rect hit,bool on,bool disabled=false);
void swatch(Canvas&,Targets&,int id,Rect hit,std::uint32_t color,bool selected,bool disabled=false,int diameter=56);
// Labelled settings row: "Zoom ............ Fit to width v"
Rect row(Canvas&,Targets&,int id,Rect,std::string_view label,std::string_view value={},bool chevron=false,bool divider=true);
// Quote card (caption, then the book's words in italic serif). Returns used bottom.
int quote(Canvas&,Rect,std::string_view caption,std::string_view text,int maxLines=4);
void divider(Canvas&,int x0,int x1,int y); // dotted ink
// Segmented icon group (page layout, stroke widths). Registers firstId+index.
void segmented(Canvas&,Targets&,int firstId,Rect,std::span<const Icon>,unsigned selected);
// Small numeric/status badge.
void pill(Canvas&,Rect,std::string_view,bool filled=false);
// Places a floating w x h surface next to an anchor rect without covering it.
// Prefers below, then above; clamped to bounds with the theme gutter.
Rect placeNear(Rect anchor,int w,int h,Rect bounds,int gap=theme::S3);
// Wrapped body text helper with theme defaults; returns bottom.
int paragraph(Canvas&,Rect,std::string_view,Font={Face::Serif,float(theme::Body.size)},int lineHeight=theme::Body.line,std::uint32_t=theme::InkPrimary,int maxLines=1000);
}
