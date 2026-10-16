// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Inkbound presentation kit: anti-aliased drawing on a borrowed RGB32 surface.
// Pure C++ (no Qt) so the identical painters run in the device build and in the
// host preview harness. Never used on the Marker contact path.
#include "core.h"
#include <cstdint>
#include <span>
#include <vector>
namespace ib {
using spike::Rect;
struct Pt {float x{},y{};};
inline Rect inset(Rect r,int d)noexcept{return {r.x0+d,r.y0+d,r.x1-d,r.y1-d};}
inline Rect outset(Rect r,int d)noexcept{return inset(r,-d);}
inline Rect intersect(Rect a,Rect b)noexcept{Rect r{a.x0>b.x0?a.x0:b.x0,a.y0>b.y0?a.y0:b.y0,a.x1<b.x1?a.x1:b.x1,a.y1<b.y1?a.y1:b.y1};if(r.empty())return {};return r;}
inline bool overlaps(Rect a,Rect b)noexcept{return !intersect(a,b).empty();}
inline Rect unite(Rect a,Rect b)noexcept{a.include(b);return a;}
inline int width(Rect r)noexcept{return r.x1-r.x0;}
inline int height(Rect r)noexcept{return r.y1-r.y0;}
inline Rect centered(Rect in,int w,int h)noexcept{int x=in.x0+(width(in)-w)/2,y=in.y0+(height(in)-h)/2;return {x,y,x+w,y+h};}
// Polyline/polygon in surface coordinates. Curves are flattened on construction.
class Path {
 std::vector<std::vector<Pt>> contours_;
public:
 Path& move(float x,float y);Path& line(float x,float y);
 Path& quad(float cx,float cy,float x,float y);
 Path& cubic(float c1x,float c1y,float c2x,float c2y,float x,float y);
 Path& arc(float cx,float cy,float r,float startDeg,float endDeg); // continues the contour
 Path& close();
 const std::vector<std::vector<Pt>>& contours()const noexcept{return contours_;}
 bool empty()const noexcept{return contours_.empty();}
};
class Canvas {
 spike::Surface s_;Rect clip_;
public:
 explicit Canvas(spike::Surface s)noexcept:s_(s),clip_{0,0,s.width,s.height}{}
 Canvas(spike::Surface s,Rect clip)noexcept:s_(s),clip_(intersect(clip,{0,0,s.width,s.height})){}
 Canvas clipped(Rect r)const noexcept{return Canvas(s_,intersect(r,clip_));}
 Rect clip()const noexcept{return clip_;}
 spike::Surface surface()const noexcept{return s_;}
 bool valid()const noexcept{return s_.valid();}
 // alpha 0..255; colors are 0xAARRGGBB with the alpha byte ignored.
 void blend(int x,int y,std::uint32_t color,int alpha)noexcept;
 std::uint32_t at(int x,int y)const noexcept;
 void fill(Rect,std::uint32_t)noexcept;
 // Multiplies the destination by color (highlighter wash keeps text dark).
 void multiply(Rect,std::uint32_t)noexcept;
 void hairline(Rect,std::uint32_t,int thickness=2)noexcept; // square outline
 void hline(int x0,int x1,int y,std::uint32_t,int thickness=2)noexcept;
 void vline(int x,int y0,int y1,std::uint32_t,int thickness=2)noexcept;
 void fillRound(Rect,float radius,std::uint32_t)noexcept;
 void strokeRound(Rect,float radius,float width,std::uint32_t)noexcept;
 void fillCircle(float cx,float cy,float r,std::uint32_t)noexcept;
 void strokeCircle(float cx,float cy,float r,float width,std::uint32_t)noexcept;
 void line(float x0,float y0,float x1,float y1,float width,std::uint32_t)noexcept; // round caps
 void stroke(const Path&,float width,std::uint32_t)noexcept; // round joins/caps
 void fillPath(const Path&,std::uint32_t)noexcept; // nonzero, 4x4 supersampled
 // Box-filtered scale of an RGB32 image into dst, preserving aspect ratio (cover=true crops).
 void image(Rect dst,std::span<const std::uint32_t> src,int sw,int sh,int sstride,bool cover=false)noexcept;
 // 8-bit gray page image fitted (aspect kept, centred) into dst and multiplied onto paper: white becomes the paper
 // tone, black stays ink. Returns the rect actually covered (empty when nothing was drawn).
 Rect grayImage(Rect dst,const std::uint8_t* src,int sw,int sh,int sstride,std::uint32_t paper)noexcept;
 // Flat 0..255 coverage mask, e.g. a text glyph.
 void mask(int x,int y,const std::uint8_t* alpha,int w,int h,int stride,std::uint32_t color,float gain=1.f)noexcept;
};
std::uint32_t mix(std::uint32_t a,std::uint32_t b,int alphaOfB)noexcept;
}
