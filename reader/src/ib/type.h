// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Cursive roles (docs/INKIE-DESIGN.md). Inkie writes every UI face in Caveat (ib/text.h); these roles name the places
// whose plain-text face differs from a UI default — Tom's typed reply and note titles fall back to Source Serif 4,
// headings to Source Sans 3, empty states to Serif Italic — and Tom's reply sizes, which follow his pen. Caveat is the
// face Tom's pen strokes are traced from, so typed cursive and Tom's handwriting read as one hand. Rects and line
// heights come from the role, never the face. The user's own ink is always real strokes; the book's words (pages and
// quoted passages) stay in the book's type.
#include "ib/text.h"
#include "ib/theme.h"
#include <array>
namespace ib::type {
inline constexpr Font Voice{Face::Script,float(theme::Body.size),0,Face::Serif};               // Tom's typed reply (Small)
// Tom's typed reply follows Settings > Tom > Handwriting size, the one text-size preference, so typed cursive is drawn
// at Tom's pen sizes (42.5/50/60 px against the pen's 42/50/60; ib/handwriting traces the same face). Small is Body.
inline constexpr std::array<float,3> VoiceRole{float(theme::Body.size),40,48};
inline constexpr std::array<int,3> VoiceLine{52,60,72};
constexpr Font voice(unsigned handSize)noexcept{return {Face::Script,VoiceRole[handSize<3?handSize:2],0,Face::Serif};}
constexpr int voiceLine(unsigned handSize)noexcept{return VoiceLine[handSize<3?handSize:2];}
inline constexpr Font ScriptHeading{Face::Script,float(theme::Heading.size),0,Face::Text};     // one section heading per screen
inline constexpr Font ScriptTitle{Face::Script,float(theme::Body.size),0,Face::Serif};         // note titles in the Notes hub
inline constexpr Font ScriptEmpty{Face::Script,float(theme::Body.size),0,Face::SerifItalic};   // one-line empty states
// Smallest cursive size accepted on the panel: Caveat x-height 0.357 em, so 34 x 1.25 = 42.5 px gives a 15 px
// (1.7 mm) x-height, above the 14 px (1.6 mm) floor used for the candidate sheets.
inline constexpr float ScriptMinimumRole=34;
}
