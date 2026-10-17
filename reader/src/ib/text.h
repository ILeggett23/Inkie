// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Inkbound typesetter: bundled SIL OFL fonts rasterized with stb_truetype.
// No QGuiApplication or system font database is required. UI-boundary only.
#include "ib/canvas.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
namespace ib {
// Source Serif 4 optical sizes: Text for small UI, Serif for body, Subhead and
// Display for titles. Source Sans 3 is reserved for numerals and tiny captions.
// Hand is Caveat as the source of Tom's pen strokes (ib/handwriting); it falls back to SerifItalic.
// Script is Caveat as a typed face: Inkbound's cursive voice, used only through the cursive roles in ib/type.h. A
// Script font names the plain face that replaces it when cursive is switched off (setPlainScript) or unavailable.
enum class Face:unsigned {Serif,SerifItalic,SerifSemibold,Text,TextItalic,TextSemibold,Subhead,Display,Sans,SansMedium,SansSemibold,Hand,Script,Count};
// size is the role size: a Script font is drawn at ScriptScale times it, its plain face at exactly it.
struct Font {Face face{Face::Text};float size{30};float tracking{0};Face plain{Face::Text};constexpr Font(Face f=Face::Text,float s=30,float t=0,Face p=Face::Text)noexcept:face(f),size(s),tracking(t),plain(p){}};
// Caveat's x-height is 0.357 em against 0.486 (Source Sans 3) and 0.475 (Source Serif 4). At 1.25x a cursive role sets
// about the same line width as its plain face (within 3-4%), so switching faces neither reflows a layout nor moves a
// control; matching x-height exactly (1.36x) would make lines 9% wider and ascenders collide.
inline constexpr float ScriptScale=1.25f;
enum class Align {Left,Center,Right};
namespace text {
// Loads every face found in dir; returns false only when the core Serif/Text
// faces are missing or invalid. Safe to call more than once.
bool load(const std::filesystem::path& dir,const std::filesystem::path& fixtureDir={});
bool ready()noexcept;
float measure(Font,std::string_view utf8);
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
// Settings > Accessibility > Plain text instead of cursive: every Script font draws in its plain face instead.
// Process-wide, switched on the shell thread like theme::setHighContrast; glyphs change inside unchanged rects.
void setPlainScript(bool)noexcept;bool plainScript()noexcept;
// The face and pixel size a font actually draws with (Script resolved to Caveat or its plain face).
Font resolved(Font)noexcept;
}
}
