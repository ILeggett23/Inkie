#pragma once
// The tablet's turn for page planning (docs/LANDSCAPE.md), shared by the PDF view planner, the EPUB renderer and the
// prepared-page loader. No other dependencies: the EPUB library includes it without the PDF engine.
#include <algorithm>
#include <array>
namespace reader::pdfview {
// 0 portrait, 90 the tablet's top edge turned clockwise, 270 anticlockwise.
inline constexpr unsigned Portrait=0,Clockwise=90,Anticlockwise=270;
// The page area on the panel: 1620x2060 at the top in portrait; turned, the landscape page area (1520x2160 at x 0 for
// 90, x 100 for 270; the footer strip is beside it).
struct Viewport {int x{},y{},width{1620},height{2060};};
inline Viewport viewport(unsigned orientation)noexcept{if(orientation==Clockwise)return {0,0,1520,2160};if(orientation==Anticlockwise)return {100,0,1520,2160};return {};}
// A reflowed (EPUB) page box fitted turned into the landscape viewport (orientation 90 or 270): the quarter turn
// that keeps it upright, centred, as float coefficients (MuPDF consumes floats; the renderer publishes and the loader
// checks exactly these). Maps box coordinates to panel pixels.
inline std::array<float,6> fitTurned(double x,double y,double w,double h,unsigned orientation)noexcept{
 const auto port=viewport(orientation);const double s=std::min(port.width/h,port.height/w); /* a quarter turn: the box's width runs down the panel */
 const double ox=port.x+(port.width-h*s)/2,oy=port.y+(port.height-w*s)/2;
 if(orientation==Anticlockwise)return {0.f,float(s),float(-s),0.f,float(ox+s*(y+h)),float(oy-s*x)}; /* content turned 90: its top to the panel's right */
 return {0.f,float(-s),float(s),0.f,float(ox-s*y),float(oy+s*(x+w))};} /* turned 270: its top to the panel's left */
}
