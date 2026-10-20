// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Inkie design tokens (docs/INKIE-DESIGN.md). Every screen reads these; no view hard-codes its own near-duplicate
// values. Values are device pixels on the 1620x2160, 229 ppi Paper Pro panel (8 px ~ 0.89 mm).
//
// Inkie is stationery: white paper, black ink, and colour where a good notebook has it — index tabs, cloth covers,
// a sheet's coloured edge, a highlighter swash under a heading. Colour is flat process colour the Gallery 3 panel mixes
// from its own pigments (no tints or gradients), always met by a black rule so edges stay crisp in the monochrome fast
// pass. Text is black (or white on the darkest pops); small text is never coloured.
#include <array>
#include <cstdint>
namespace ib::theme {
// Surfaces and ink. Gallery 3 has no black particle and no grey: black is a full mix of its cyan, magenta and yellow
// pigments, and greys are dithered from the colour states, so they read as grain. Inkie therefore uses no grey fills
// and no grey text: surfaces are white, text and rules are black, and hierarchy comes from size, face and weight.
inline constexpr std::uint32_t Paper=0xffffffffu;           // device white
inline constexpr std::uint32_t Surface=0xffffffffu;         // sheets, cards, menus
inline constexpr std::uint32_t SurfaceSunken=0xffffffffu;   // wells and inputs: white, closed by a rule
inline constexpr std::uint32_t SurfaceSelected=0xffffffffu; // (state is drawn in ink, never as a grey fill)
inline constexpr std::uint32_t SurfacePressed=0xffffffffu;
inline constexpr std::uint32_t InkPrimary=0xff0a0a0au;      // near-black: the panel's full-mix state
// Secondary inks are runtime values so Settings > Accessibility > High contrast can still adjust them; Inkie draws
// text and rules in black by default, so the switch now only darkens disabled controls. Shell thread only.
inline std::uint32_t InkSecondary=0xff0a0a0au;
inline std::uint32_t InkTertiary=0xff0a0a0au;
inline std::uint32_t Border=0xff0a0a0au;          // rules (divider() draws them dotted)
inline std::uint32_t BorderStrong=0xff0a0a0au;
inline std::uint32_t Disabled=0xffb8b8b8u;        // the one grey, for rare disabled controls
inline bool HighContrast=false;
inline void setHighContrast(bool on)noexcept{HighContrast=on;Disabled=on?0xff6e6e6eu:0xffb8b8b8u;}
inline constexpr std::uint32_t ToolbarInk=0xff000000u;
inline constexpr std::uint32_t Inverse=0xffffffffu;         // text on ink
// Colour: only the panel's clean states — cyan, magenta and yellow pigment, and their two-pigment mixes red (M+Y),
// green (C+Y) and blue (C+M). Anything between (orange, violet, pink, pastels, tints) is dithered or shifts hue on
// Gallery 3, so it is not in the palette. Colour updates take 500-1500 ms against 350 ms for black and white and can
// leave a tinted ghost when they change, so colour marks identity (which place, tool or book) and stays put, while
// state (selected, on, progress) is drawn in ink. Colour is a shape at least EdgeBand thick, met by a black rule.
inline constexpr std::uint32_t PopCyan=0xff00a8e8u,PopMagenta=0xffe6007eu,PopYellow=0xffffe100u,PopGreen=0xff00a650u,
 PopBlue=0xff1e50dcu,PopRed=0xffe8202au;
inline constexpr std::array<std::uint32_t,6> Pop{PopCyan,PopMagenta,PopYellow,PopGreen,PopBlue,PopRed};
// One colour per place, in library::Section order: My files and Recents cyan (your books), Notes magenta, Favorites
// yellow, Shared green, Tom blue, Trash red, Settings ink (a utility, not a place to be).
inline constexpr std::array<std::uint32_t,8> SectionColor{PopCyan,PopMagenta,PopYellow,PopCyan,PopGreen,PopBlue,PopRed,InkPrimary};
// Per-surface accents, so a sheet's coloured edge names where it belongs.
inline constexpr std::uint32_t NoteAccent=PopMagenta,TomAccent=PopBlue,GoAccent=InkPrimary;
// Ink drawn on a colour: black wherever it reads, white on the darkest (blue, red, magenta, ink).
constexpr std::uint32_t onPop(std::uint32_t c)noexcept{const unsigned r=c>>16&255,g=c>>8&255,b=c&255;return (r*299+g*587+b*114)/1000<110?0xffffffffu:0xff0a0a0au;}
// Selected state is ink: a black tile or cell with white on it, as on the native Paper Pro toolbar.
inline constexpr std::uint32_t ToolSelected=InkPrimary;
inline constexpr std::uint32_t OnSelected=onPop(ToolSelected);
inline constexpr std::uint32_t PopoverBorder=InkPrimary;
// Transient text selection wash and handles.
inline constexpr std::uint32_t Selection=0xffb8ecffu;
inline constexpr std::uint32_t SelectionHandle=InkPrimary;
// Destructive actions only (fills; never small coloured text).
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
// Shape: cards and tiles with modest radii and one crisp 2 px ink rule, no shadows (a card's content is centred in
// exactly the rect it reports). Sheets are square with a coloured edge. Dividers are dotted ink; ruled-paper
// guides (1 px) are paper. EdgeBand is a sheet's coloured top edge.
inline constexpr float RadiusSmall=8,RadiusMedium=12,RadiusLarge=16,RadiusPill=999;
inline constexpr int Hairline=2,StrokeStrong=3,Outline=2,PopShadow=0,EdgeBand=10;
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
// Typography roles (pixel size, line height). Every text size in Inkie is one of these. Words are written, data is
// printed: from Label up, every UI face draws in Caveat (ScriptScale; semibold faces in a heavier cut) — titles,
// headings, list and menu rows, settings labels, buttons, Tom. Below Label (Control, Meta, Caption) text stays Source
// Sans 3: metadata, counts, dates, notes, captions and tight control labels, so fine print reads as fine print. Italic
// serif stays the voice of the book's words. Plain text restores Source Sans 3 / Serif 4 everywhere. 23 px is 2.6 mm.
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
