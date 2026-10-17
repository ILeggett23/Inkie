// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Inkie design tokens (docs/INKIE-DESIGN.md). Every screen reads these; no view hard-codes its own near-duplicate
// values. Values are device pixels on the 1620x2160, 229 ppi Paper Pro panel (8 px ~ 0.89 mm).
//
// Colour rule for the Gallery 3 colour panel: the page stays device white (its brightest, sharpest state) and colour
// arrives as flat, fully saturated "pop" fills drawn from the panel's own process colours (cyan, magenta, yellow and
// their two-pigment mixes), never tints or gradients, always closed by a bold black outline so edges stay crisp in
// the monochrome fast pass. Text is black (or white on the darkest pops); small text is never coloured.
#include <array>
#include <cstdint>
namespace ib::theme {
// Surfaces and ink
inline constexpr std::uint32_t Paper=0xffffffffu;           // device white reads as warm paper
inline constexpr std::uint32_t Surface=0xffffffffu;         // sheets, cards, menus
inline constexpr std::uint32_t SurfaceSunken=0xfff1f1f1u;   // quote blocks, inputs, rail well
inline constexpr std::uint32_t SurfaceSelected=0xffe4e4e4u; // selected row/nav item/tool
inline constexpr std::uint32_t SurfacePressed=0xffd6d6d6u;
inline constexpr std::uint32_t InkPrimary=0xff0a0a0au;      // sharp near-black
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
// Pop palette: flat process colours the Gallery 3 panel mixes from its own pigments. Sharp and bright by design;
// regional quality passes classify these regions as Color content (vendor.h), the fast pass keeps their black outlines.
inline constexpr std::uint32_t PopCyan=0xff00aeefu,PopMagenta=0xffec008cu,PopYellow=0xffffe500u,PopGreen=0xff00b04fu,
 PopOrange=0xffff7a00u,PopViolet=0xff9b30ffu,PopBlue=0xff1060ffu,PopRed=0xffff2a2au;
inline constexpr std::array<std::uint32_t,8> Pop{PopCyan,PopMagenta,PopYellow,PopGreen,PopOrange,PopViolet,PopBlue,PopRed};
// One colour per place, in library::Section order: My files, Notes, Favorites, Recents, Shared, Tom, Trash, Settings.
inline constexpr std::array<std::uint32_t,8> SectionColor{PopCyan,PopMagenta,PopYellow,PopOrange,PopGreen,PopViolet,PopRed,PopBlue};
// Per-surface accents, so a sheet's colour names where it belongs.
inline constexpr std::uint32_t NoteAccent=PopMagenta,TomAccent=PopViolet,GoAccent=PopMagenta;
// Ink drawn on a pop fill: black wherever it reads (crisp in the fast monochrome pass), white only on the darkest pops.
constexpr std::uint32_t onPop(std::uint32_t c)noexcept{const unsigned r=c>>16&255,g=c>>8&255,b=c&255;return (r*299+g*587+b*114)/1000<110?0xffffffffu:0xff0a0a0au;}
// Selected state: a pop bubble with ink on it (was a near-black square with a paper glyph).
inline constexpr std::uint32_t ToolSelected=PopCyan;
inline constexpr std::uint32_t OnSelected=onPop(ToolSelected);
inline constexpr std::uint32_t PopoverBorder=InkPrimary;    // bubbles: bold ink outline
// Meaningful color
inline constexpr std::uint32_t Selection=0xffb8ecffu;       // transient text selection wash (light cyan)
inline constexpr std::uint32_t SelectionHandle=PopMagenta;
// Destructive actions only (fills and outlines; never small coloured text).
inline constexpr std::uint32_t Caution=PopRed;
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
// Shape: bubbles. Every surface is rounded; buttons, chips and toggles are pills; tool tiles are circles. Bubbles
// close with a bold Outline; floating ones carry a solid PopShadow drawn inside their own rect, so a surface never
// paints outside the bounds it reports for damage. Dividers stay Hairline; ruled-paper guides (1 px) are paper.
inline constexpr float RadiusSmall=16,RadiusMedium=28,RadiusLarge=40,RadiusPill=999;
inline constexpr int Hairline=2,StrokeStrong=3,Outline=3,PopShadow=8;
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
// Typography roles (pixel size, line height). Every text size in Inkie is one of these. Inkie writes in cursive:
// ib/text.cpp draws every UI face in Caveat (ScriptScale, never under ScriptFloor px; semibold faces in a heavier
// cut), while italic serif stays the voice of the book's own words. Settings > Accessibility > Plain text restores
// Source Sans 3 / Source Serif 4 at these sizes. Sizes are device pixels at 229 ppi: 23 px is 2.6 mm, 34 px 3.8 mm.
struct Type {int size,line;};
inline constexpr Type Code{110,120};            // pairing code numerals
inline constexpr Type Display{72,84};           // reserved: full-screen states
inline constexpr Type Title{58,70};             // full-screen page titles (Wi-Fi, Companion), sleep screen
inline constexpr Type Heading{44,54};           // section headings, settings titles, definition word, dialog titles
inline constexpr Type Subhead{38,46};           // sheet and popover titles, wordmark, key caps
inline constexpr Type Body{34,50};              // definitions, Tom, typed notes, quotes, empty states, inputs
inline constexpr Type Label{30,38};             // control labels, list titles, menu rows
inline constexpr Type Control{28,36};           // labels inside width-fitted controls: selection strip, choice cells, steppers, text actions
inline constexpr Type Meta{26,34};              // metadata: "12 items", "Page 43", notes and subtitles
inline constexpr Type Caption{23,30};           // smallest permitted text
inline constexpr int ScreenWidth=1620,ScreenHeight=2160,PageFitHeight=2060;
}
