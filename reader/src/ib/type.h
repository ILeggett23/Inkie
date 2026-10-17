// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Cursive roles: Inkbound's written voice inside typed layouts (docs/LAYOUT-ARCHITECTURE.md, "Cursive roles").
// Each is drawn in Caveat (Face::Script) at ScriptScale times its role size, the same face Tom's pen strokes are traced
// from, so typed cursive and Tom's handwriting read as one hand. With Settings > Accessibility > Plain text instead of
// cursive, each draws in its named plain face at the role size. Rects and line heights come from the role, never from
// the face, so a switch changes glyphs inside unchanged rects. Nothing else in Inkbound is cursive: controls, labels,
// numbers, metadata, quotes and book text stay Source Sans 3 / Source Serif 4, and the user's own ink stays strokes.
#include "ib/text.h"
#include "ib/theme.h"
namespace ib::type {
inline constexpr Font Voice{Face::Script,float(theme::Body.size),0,Face::Serif};               // Tom's typed reply
inline constexpr int VoiceLine=52;                                                             // Tom's typed line pitch
inline constexpr Font ScriptHeading{Face::Script,float(theme::Heading.size),0,Face::Text};     // one section heading per screen
inline constexpr Font ScriptTitle{Face::Script,float(theme::Body.size),0,Face::Serif};         // note titles in the Notes hub
inline constexpr Font ScriptEmpty{Face::Script,float(theme::Body.size),0,Face::SerifItalic};   // one-line empty states
// Smallest cursive size accepted on the panel: Caveat x-height 0.357 em, so 34 x 1.25 = 42.5 px gives a 15 px
// (1.7 mm) x-height, above the 14 px (1.6 mm) floor used for the candidate sheets.
inline constexpr float ScriptMinimumRole=34;
}
