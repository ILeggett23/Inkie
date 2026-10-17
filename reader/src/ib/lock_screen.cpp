// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/lock_screen.h"
#include "ib/components.h"
#include "ib/text.h"
#include "ib/theme.h"
#include <algorithm>
namespace ib::lock {
namespace t=ib::theme;
void paint(Canvas& c,Targets& tg,const Model& m){
 c.fill(c.clip(),t::Paper);
 const Font brand{Face::SerifSemibold,theme::Heading.size},prompt{Face::Text,theme::Subhead.size},key{Face::Literal,theme::Title.size},small{Face::Text,theme::Label.size};
 (void)brand;wordmark(c,{0,250,1620,430},float(t::Display.size));
 text::centeredLine(c,prompt,{0,480,1620,580},m.prompt,t::InkPrimary,Align::Center);
 // One dot per digit: filled once entered.
 const int n=std::clamp(m.length,1,8),gap=72,cx=(DotsRect.x0+DotsRect.x1)/2,cy=(DotsRect.y0+DotsRect.y1)/2,x0=cx-(n-1)*gap/2;
 for(int i=0;i<n;++i){const float x=float(x0+i*gap);if(i<m.entered)c.fillCircle(x,float(cy),16,t::InkPrimary);else c.strokeCircle(x,float(cy),15,float(t::StrokeStrong),t::InkPrimary);}
 if(!m.message.empty())text::centeredLine(c,small,MessageRect,m.message,t::InkPrimary,Align::Center);
 // Keypad: 1-9, then (Cancel) 0 (delete). Keys are inert while waiting out the attempt limit.
 const int kw=(KeypadRect.x1-KeypadRect.x0-2*40)/3,kh=(KeypadRect.y1-KeypadRect.y0-3*40)/4;
 auto cell=[&](int col,int row){return Rect{KeypadRect.x0+col*(kw+40),KeypadRect.y0+row*(kh+40),KeypadRect.x0+col*(kw+40)+kw,KeypadRect.y0+row*(kh+40)+kh};};
 // Keys are paper with an ink rule, the digits printed (a passcode is read exactly, never in script).
 auto keycap=[&](Rect r,int d){c.fillRound(r,t::RadiusLarge,t::Surface);c.strokeRound(r,t::RadiusLarge,float(t::Outline),m.waiting?t::Disabled:t::InkPrimary);text::centeredLine(c,key,r,std::to_string(d),m.waiting?t::Disabled:t::InkPrimary,Align::Center);};
 for(int d=1;d<=9;++d){Rect r=cell((d-1)%3,(d-1)/3);keycap(r,d);if(!m.waiting)tg.add(KeyBase+d,r);}
 {Rect r=cell(1,3);keycap(r,0);if(!m.waiting)tg.add(KeyBase,r);}
 {Rect r=cell(2,3);icon(c,Icon::Back,centered(r,56,56),m.entered&&!m.waiting?t::InkPrimary:t::Disabled);if(m.entered&&!m.waiting)tg.add(Back,r);}
 if(m.cancel){Rect r=cell(0,3);text::centeredLine(c,small,r,"Cancel",t::InkPrimary,Align::Center);tg.add(Cancel,r);}
}
}
