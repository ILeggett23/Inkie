// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Inkbound design tokens. Every screen reads these; no view hard-codes its own
// near-duplicate values. Values are device pixels on the 1620x2160, 229 ppi
// Paper Pro panel (8 px ~ 0.89 mm).
//
// Color rule for color e-ink: chrome uses neutral grays only (R=G=B) so the
// panel never dithers UI surfaces. Hue is reserved for meaning: ink color,
// highlight color, current selection, layer identity and important states.
#include <array>
#include <cstdint>
namespace ib::theme {
// Surfaces and ink
inline constexpr std::uint32_t Paper=0xffffffffu;           // device white reads as warm paper
inline constexpr std::uint32_t Surface=0xffffffffu;         // sheets, cards, menus
inline constexpr std::uint32_t SurfaceSunken=0xfff3f3f3u;   // quote blocks, inputs, rail well
inline constexpr std::uint32_t SurfaceSelected=0xffe4e4e4u; // selected row/nav item/tool
inline constexpr std::uint32_t SurfacePressed=0xffd6d6d6u;
inline constexpr std::uint32_t InkPrimary=0xff1b1b1bu;
// Secondary grays are runtime values so Settings > Accessibility > High contrast can darken them (still neutral
// R=G=B, so colour e-ink never dithers chrome). Painted and switched on the shell thread only.
inline std::uint32_t InkSecondary=0xff595959u;
inline std::uint32_t InkTertiary=0xff8c8c8cu;
inline std::uint32_t Border=0xffcfcfcfu;          // hairlines
inline std::uint32_t BorderStrong=0xff9a9a9au;    // sheet outlines over a page
inline std::uint32_t Disabled=0xffb8b8b8u;
inline bool HighContrast=false;
inline void setHighContrast(bool on)noexcept{HighContrast=on;InkSecondary=on?0xff2b2b2bu:0xff595959u;InkTertiary=on?0xff444444u:0xff8c8c8cu;Border=on?0xff6e6e6eu:0xffcfcfcfu;BorderStrong=on?0xff444444u:0xff9a9a9au;Disabled=on?0xff7a7a7au:0xffb8b8b8u;}
inline constexpr std::uint32_t ToolbarInk=0xff000000u;
inline constexpr std::uint32_t Inverse=0xffffffffu;         // text on InkPrimary buttons
// Native selected tool tile: near-black square with a paper-colored glyph (Paper Pro toolbar).
inline constexpr std::uint32_t ToolSelected=0xff111111u;
inline constexpr std::uint32_t PopoverBorder=InkPrimary;    // native popovers: square, thin ink outline
// Meaningful color (muted Paper Pro tones)
inline constexpr std::uint32_t Selection=0xffcfdcf0u;       // transient text selection wash
inline constexpr std::uint32_t SelectionHandle=0xff3f6fb5u;
// Destructive buttons and menu items only. Status and notice lines stay neutral: every update on this stack is Mono
// content (vendor.h), and coloured text that a later Mono update replaces can stay faintly on the panel.
inline constexpr std::uint32_t Caution=0xffa8473fu;
// Styled-ink palette order is persisted by index (styled::Style::color); keep it.
inline constexpr std::array<std::uint32_t,8> Ink{0xff1c2028u,0xffdf6c9cu,0xff55905au,0xffd78b39u,0xffe7c84cu,0xff9467acu,0xff567db8u,0xffbd5250u};
inline constexpr std::array<const char*,8> InkName{"Black","Pink","Green","Orange","Yellow","Purple","Blue","Red"};
// Highlighter swatch order shown to the reader (indices into Ink).
inline constexpr std::array<unsigned,7> HighlightOrder{4,6,1,3,2,5,7};
// Soft wash used to paint a highlight color behind text.
inline std::uint32_t wash(std::uint32_t c,int strength=96)noexcept{auto ch=[&](int s){int v=int(c>>s&255);return std::uint32_t(255-((255-v)*strength+127)/255)<<s;};return 0xff000000u|ch(16)|ch(8)|ch(0);}
// Spacing scale
inline constexpr int S1=8,S2=16,S3=24,S4=32,S5=48,S6=64,S7=96;
inline constexpr int Gutter=48;                 // screen side margin
// Shape
inline constexpr float RadiusSmall=10,RadiusMedium=18,RadiusLarge=28,RadiusPill=999;
inline constexpr int Hairline=2,StrokeStrong=3;
// Touch: visual size is separate from hit area. Finger and Marker share it.
inline constexpr int MinTarget=96;              // ~10.6 mm
inline constexpr int ComfortTarget=112;
inline constexpr int TargetGap=16;
// Icons
inline constexpr int IconSmall=32,Icon=44,IconLarge=56;
inline constexpr float IconStroke=2.5f; // thin, Paper Pro-like toolbar strokes
// Chrome sizes
inline constexpr int TopBar=104;                // thin reader/library top bar
inline constexpr int Footer=100;                // reader progress strip (outside 2060px page fit)
inline constexpr int SidebarWidth=300;
inline constexpr int RailWidth=112; // full-height rail column (MinTarget 96 + margins)
// Typography roles (pixel size, line height). Faces are chosen in ib/text.h.
struct Type {int size,line;};
inline constexpr Type Display{72,84};           // Inkbound wordmark on the library
inline constexpr Type Title{58,70};             // "My files", "Notes"
inline constexpr Type Heading{44,54};           // sheet titles, definition word
inline constexpr Type Subhead{36,46};           // section titles
inline constexpr Type Body{35,50};              // definitions, Tom, quotes
inline constexpr Type Label{30,38};             // control labels, list titles
inline constexpr Type Meta{26,34};              // metadata: "12 items", "Page 43"
inline constexpr Type Caption{23,30};           // smallest permitted text
inline constexpr int ScreenWidth=1620,ScreenHeight=2160,PageFitHeight=2060;
}
