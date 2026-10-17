// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Inkie typesetter: bundled SIL OFL fonts rasterized with stb_truetype.
// No QGuiApplication or system font database is required. UI-boundary only.
#include "ib/canvas.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace ib {
// Faces as named at call sites. Inkie writes in cursive: unless plain text is on, every UI face (Text, Sans, Serif,
// Subhead, Display and their weights) draws in Caveat — Script, or ScriptBold for semibold/medium — while SerifItalic
// stays Source Serif 4 Italic, the voice of the book's own words (quoted passages), and Literal stays Source Sans 3 for
// characters that must be read exactly (pairing codes). With plain text on, every face draws as named.
// Hand is Caveat as the source of Tom's pen strokes (ib/handwriting). Script fonts (ib/type.h roles) name the plain
// face that replaces them when cursive is off or Caveat is unavailable. ScriptBold shares Caveat's bytes and
// emboldens its coverage: no heavier file can ship, because the tablet update keeps the baseline's font assets.
enum class Face:unsigned {Serif,SerifItalic,SerifSemibold,Text,TextItalic,TextSemibold,Subhead,Display,Sans,SansMedium,SansSemibold,Hand,Script,ScriptBold,Literal,Count};
// size is the role size: cursive draws at ScriptScale times it (never under ScriptFloor px), plain faces at exactly it.
struct Font {Face face{Face::Text};float size{30};float tracking{0};Face plain{Face::Text};constexpr Font(Face f=Face::Text,float s=30,float t=0,Face p=Face::Text)noexcept:face(f),size(s),tracking(t),plain(p){}};
// Caveat's x-height is 0.357 em against 0.486 (Source Sans 3) and 0.475 (Source Serif 4). At 1.25x a cursive role sets
// within 7% of its plain face's line width (0.93-1.03 on the candidate sheets), and every cursive rect is fitted, so
// switching faces neither reflows a layout nor moves a control; matching x-height exactly (1.36x) would make lines
// up to 12% wider and ascenders collide.
inline constexpr float ScriptScale=1.25f;
// Smallest cursive pixel size: Caveat's 0.357 em x-height gives 13.6 px (1.5 mm) here, so captions and metadata stay
// readable in cursive; their lines get wider, and fitted rects ellipsize rather than grow.
inline constexpr float ScriptFloor=38;
enum class Align {Left,Center,Right};
namespace text {
// Loads every face found in dir; returns false only when the core Serif/Text
// faces are missing or invalid. Safe to call more than once.
bool load(const std::filesystem::path& dir,const std::filesystem::path& fixtureDir={});
bool ready()noexcept;
float measure(Font,std::string_view utf8);
// Layout width: the wider of the text in cursive and in plain text. Geometry fitted to a label (chips, tabs, strips,
// choice cells) uses it, so switching Plain text changes glyphs but never moves or resizes a control.
float span(Font,std::string_view utf8);
float ascent(Font);float descent(Font); // descent is positive
float capHeight(Font);
// Draws on one baseline. Returns the advance in pixels.
float draw(Canvas&,Font,float x,float baseline,std::string_view utf8,std::uint32_t color);
float drawAligned(Canvas&,Font,Rect box,float baseline,std::string_view,std::uint32_t,Align);
// Cuts to width with a trailing ellipsis when needed.
std::string fit(Font,std::string_view utf8,float width);
// Status lines in the old ALL-CAPS style read as sentence case ("PAGE STILL SAVING - TRY AGAIN" → "Page still saving — try again").
std::string quiet(std::string_view);
// Greedy wrap on spaces and newlines; overlong words break by character.
// When maxLines is reached the final line is ellipsized.
std::vector<std::string> wrap(Font,std::string_view utf8,float width,int maxLines=1000);
// Wraps into box starting at its top. Returns the bottom of the last line.
int block(Canvas&,Font,Rect box,std::string_view utf8,std::uint32_t,int lineHeight,int maxLines=1000,Align=Align::Left);
// Vertically centers a single line inside box.
void centeredLine(Canvas&,Font,Rect box,std::string_view,std::uint32_t,Align=Align::Center);
// Contrast gain applied to glyph coverage (e-ink keeps thin serifs visible).
void setGain(float)noexcept;
// Settings > Accessibility > Plain text instead of cursive: every face draws as named (Script fonts in their plain face).
// Process-wide, switched on the shell thread like theme::setHighContrast; glyphs change inside unchanged rects.
void setPlainScript(bool)noexcept;bool plainScript()noexcept;
// The face and pixel size a font actually draws with (UI faces resolved to Caveat, or to themselves in plain text).
Font resolved(Font)noexcept;
}
}
