// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Tom writes his reply with a pen. A word in the handwriting face is turned into the strokes a pen would draw: the word
// is rasterized at twice the size, thinned to the centre line of its strokes (Zhang-Suen), and the centre line is
// traced into polylines in writing order (left to right, each stroke followed from one end). Drawing a word up to a
// given length of ink therefore forms its letters stroke by stroke, the way handwriting appears, never a whole word
// or a glyph at once. The pen is as wide as the face's own strokes, so a finished word looks like the face.
// Pure C++, deterministic, cached per (word, size); no animation framework.
#include "ib/canvas.h"
#include <string>
#include <vector>
namespace ib::hand {
struct Stroke {std::vector<Pt> points;float length{};};
struct Word {std::vector<Stroke> strokes;float advance{},pen{2.f},ink{};}; // points relative to (x, baseline)
// The strokes of one word (no spaces) in the handwriting face at this pixel size. Cached (bounded).
const Word& word(const std::string& utf8,float size);
// Draws the word's strokes up to `ink` pixels of pen travel (all of it when ink < 0). Returns the ink drawn.
float draw(Canvas&,const Word&,float x,float baseline,float ink,std::uint32_t color);
}
