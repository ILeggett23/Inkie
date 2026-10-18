// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/icons.h"
#include "ib/theme.h"
#include <algorithm>
#include <array>
#include <cmath>
namespace ib {
namespace {
constexpr float Pi=3.14159265358979f;
// Grid painter: 24x24 design units mapped into a square box.
struct G {
 Canvas& c;float ox,oy,k;std::uint32_t col;float w;
 float X(float x)const{return ox+x*k;}float Y(float y)const{return oy+y*k;}
 Path path()const{return Path{};}
 G& ln(float a,float b,float x,float y){c.line(X(a),Y(b),X(x),Y(y),w,col);return *this;}
 G& poly(std::initializer_list<Pt> pts,bool closed=false,bool filled=false){Path p;bool first=true;for(auto q:pts){if(first)p.move(X(q.x),Y(q.y));else p.line(X(q.x),Y(q.y));first=false;}if(closed)p.close();if(filled)c.fillPath(p,col);else c.stroke(p,w,col);return *this;}
 G& circle(float x,float y,float r,bool filled=false){if(filled)c.fillCircle(X(x),Y(y),r*k,col);else c.strokeCircle(X(x),Y(y),r*k,w,col);return *this;}
 G& dot(float x,float y,float r=1.5f){c.fillCircle(X(x),Y(y),r*k,col);return *this;}
 G& rect(float x0,float y0,float x1,float y1,float r=2,bool filled=false){Rect q{int(std::lround(X(x0))),int(std::lround(Y(y0))),int(std::lround(X(x1))),int(std::lround(Y(y1)))};if(filled)c.fillRound(q,r*k,col);else c.strokeRound(outset(q,int(w/2)),r*k+w/2,w,col);return *this;}
 G& arc(float x,float y,float r,float a0,float a1){Path p;p.arc(X(x),Y(y),r*k,a0,a1);c.stroke(p,w,col);return *this;}
 G& shape(const Path& p,bool filled){if(filled)c.fillPath(p,col);else c.stroke(p,w,col);return *this;}
 G& dash(float a,float b,float x,float y,float on=2.2f,float off=1.6f){float dx=x-a,dy=y-b,len=std::sqrt(dx*dx+dy*dy);if(len<=0)return *this;float ux=dx/len,uy=dy/len;for(float d=0;d<len;d+=on+off){float e=std::min(len,d+on);c.line(X(a+ux*d),Y(b+uy*d),X(a+ux*e),Y(b+uy*e),w,col);}return *this;}
 G& dashRect(float x0,float y0,float x1,float y1){return dash(x0,y0,x1,y0).dash(x1,y0,x1,y1).dash(x1,y1,x0,y1).dash(x0,y1,x0,y0);}
};
void star(G& g,bool filled){Path p;for(int i=0;i<10;++i){float r=i%2?3.9f:9.2f,a=(-90+i*36)*Pi/180;float x=g.X(12+r*std::cos(a)),y=g.Y(12.6f+r*std::sin(a));if(!i)p.move(x,y);else p.line(x,y);}p.close();if(filled)g.c.fillPath(p,g.col);g.c.stroke(p,g.w*0.9f,g.col);}
void gear(G& g){Path p;for(int i=0;i<=48;++i){float a=i*2*Pi/48;int tooth=(i/3)%2;float r=tooth?9.2f:7.4f;float x=g.X(12+r*std::cos(a)),y=g.Y(12+r*std::sin(a));if(!i)p.move(x,y);else p.line(x,y);}g.c.stroke(p,g.w,g.col);g.circle(12,12,3.1f);}
void folder(G& g,bool filled){Path p;p.move(g.X(2.5f),g.Y(6.5f)).line(g.X(2.5f),g.Y(18.5f)).line(g.X(21.5f),g.Y(18.5f)).line(g.X(21.5f),g.Y(8.5f)).line(g.X(11.2f),g.Y(8.5f)).line(g.X(9.2f),g.Y(5.5f)).line(g.X(3.5f),g.Y(5.5f)).close();g.shape(p,filled);if(filled)g.c.stroke(p,g.w*0.6f,g.col);}
// 40-unit grid used by the native toolbar family (the reference mockup's geometry).
G grid40(Canvas& c,Rect box,std::uint32_t col,float sw){float side=float(std::min(width(box),height(box)));float k=side/40.f;float w=sw>0?sw:std::max(2.75f,2.25f*k);return G{c,box.x0+(width(box)-side)/2,box.y0+(height(box)-side)/2,k,col,w};}
void native(Canvas& c,Icon i,Rect box,std::uint32_t col,float sw){
 G g=grid40(c,box,col,sw);
 switch(i){
 // Usability family: outlines on the 40 grid, one stroke weight, stationery motifs (a panel, a page, a stack of sheets).
 case Icon::ToolbarHandle:g.rect(5,8,35,32,4).ln(15,8,15,32).ln(8.5f,14,11.5f,14).ln(8.5f,19,11.5f,19);break;
 case Icon::CloseCircle:g.circle(20,20,14).ln(15,15,25,25).ln(25,15,15,25);break;
 case Icon::DocumentMenu:g.poly({{8,4},{25,4},{32,11},{32,36},{8,36}},true).poly({{25,4},{25,11},{32,11}}).ln(14,19,26,19).ln(14,25,26,25).ln(14,31,21,31);break;
 case Icon::BookOpen:{Path p;p.move(g.X(20),g.Y(9)).cubic(g.X(13),g.Y(4),g.X(6),g.Y(5),g.X(3),g.Y(7)).line(g.X(3),g.Y(35)).cubic(g.X(8),g.Y(32),g.X(13),g.Y(32),g.X(20),g.Y(35))
  .cubic(g.X(27),g.Y(32),g.X(32),g.Y(32),g.X(37),g.Y(35)).line(g.X(37),g.Y(7)).cubic(g.X(34),g.Y(5),g.X(27),g.Y(4),g.X(20),g.Y(9)).close();g.shape(p,false);g.ln(20,9,20,35);break;}
 case Icon::Tag:g.poly({{5,6},{20,6},{36,22},{22,36},{5,19}},true).circle(12,13,2);break;
 case Icon::NativeEraser:g.poly({{5,24},{23,6},{36,19},{19,36},{16,36}},true).ln(14,15,27,28).ln(20,36,35,36);break;
 case Icon::NativeSelection:{ // a dashed lasso loop with its solid tail
  for(int i=0;i<18;++i){const float a0=(i*20.f+4)*Pi/180,a1=(i*20.f+14)*Pi/180;g.c.line(g.X(21+15*std::cos(a0)),g.Y(16+10*std::sin(a0)),g.X(21+15*std::cos(a1)),g.Y(16+10*std::sin(a1)),g.w,g.col);}
  Path p;p.move(g.X(12),g.Y(24)).cubic(g.X(8),g.Y(29),g.X(15),g.Y(32),g.X(12),g.Y(37));g.shape(p,false);break;}
 case Icon::NativeLayers:g.rect(4,15,26,36,2.5f).poly({{9,15},{9,10},{31,10},{31,31},{26,31}}).poly({{14,10},{14,5},{36,5},{36,26},{31,26}});break; // a stack of sheets
 case Icon::NativeUndo:{g.poly({{4,15},{13,8},{13,22}},true,true);Path p;p.move(g.X(12),g.Y(15)).line(g.X(21),g.Y(15)).cubic(g.X(30),g.Y(15),g.X(35),g.Y(21),g.X(35),g.Y(26)).cubic(g.X(35),g.Y(31),g.X(30),g.Y(34),g.X(24),g.Y(34));g.shape(p,false);break;}
 case Icon::NativeRedo:{g.poly({{36,15},{27,8},{27,22}},true,true);Path p;p.move(g.X(28),g.Y(15)).line(g.X(19),g.Y(15)).cubic(g.X(10),g.Y(15),g.X(5),g.Y(21),g.X(5),g.Y(26)).cubic(g.X(5),g.Y(31),g.X(10),g.Y(34),g.X(16),g.Y(34));g.shape(p,false);break;}
 default:break;}
}
}
void icon(Canvas& c,Icon i,Rect box,std::uint32_t col,float sw){
 float side=float(std::min(width(box),height(box)));float k=side/24.f;float w=sw>0?sw:std::max(2.f,theme::IconStroke*side/theme::Icon);
 G g{c,box.x0+(width(box)-side)/2,box.y0+(height(box)-side)/2,k,col,w};
 switch(i){
 case Icon::Back:g.ln(20,12,4.5f,12).poly({{10.5f,6},{4.5f,12},{10.5f,18}});break;
 case Icon::ChevronLeft:g.poly({{15,5},{8,12},{15,19}});break;
 case Icon::ChevronRight:g.poly({{9,5},{16,12},{9,19}});break;
 case Icon::ChevronDown:g.poly({{5,9},{12,16},{19,9}});break;
 case Icon::ChevronUp:g.poly({{5,15},{12,8},{19,15}});break;
 case Icon::Close:g.ln(6,6,18,18).ln(18,6,6,18);break;
 case Icon::Search:g.circle(10.5f,10.5f,6.5f).ln(15.4f,15.4f,20.5f,20.5f);break;
 case Icon::Plus:g.ln(12,4.5f,12,19.5f).ln(4.5f,12,19.5f,12);break;
 case Icon::Grid:g.rect(3.5f,3.5f,10.5f,10.5f,1.5f).rect(13.5f,3.5f,20.5f,10.5f,1.5f).rect(3.5f,13.5f,10.5f,20.5f,1.5f).rect(13.5f,13.5f,20.5f,20.5f,1.5f);break;
 case Icon::List:g.dot(4.5f,6.5f,1.4f).dot(4.5f,12,1.4f).dot(4.5f,17.5f,1.4f).ln(8.5f,6.5f,20,6.5f).ln(8.5f,12,20,12).ln(8.5f,17.5f,20,17.5f);break;
 case Icon::Menu:g.ln(4,6.5f,20,6.5f).ln(4,12,20,12).ln(4,17.5f,20,17.5f);break;
 case Icon::More:g.dot(5.5f,12,1.7f).dot(12,12,1.7f).dot(18.5f,12,1.7f);break;
 case Icon::MoreVertical:g.dot(12,5.5f,1.7f).dot(12,12,1.7f).dot(12,18.5f,1.7f);break;
 case Icon::Bookmark:g.poly({{6.5f,3.5f},{17.5f,3.5f},{17.5f,20.5f},{12,16.2f},{6.5f,20.5f}},true);break;
 case Icon::BookmarkFilled:g.poly({{6.5f,3.5f},{17.5f,3.5f},{17.5f,20.5f},{12,16.2f},{6.5f,20.5f}},true,true);break;
 case Icon::Star:star(g,false);break;
 case Icon::StarFilled:star(g,true);break;
 case Icon::Folder:folder(g,false);break;
 case Icon::FolderFilled:folder(g,true);break;
 case Icon::Files:folder(g,false);break;
 case Icon::Notes:g.rect(5.5f,3,18.5f,21,2).ln(8.8f,8,15.2f,8).ln(8.8f,12,15.2f,12).ln(8.8f,16,12.8f,16);break;
 case Icon::Clock:g.circle(12,12,8.5f).poly({{12,7},{12,12},{15.5f,14.2f}});break;
 case Icon::Shared:g.circle(9,8.5f,3.2f).arc(9,20,6.5f,200,340).circle(16.8f,9.6f,2.5f).arc(16.8f,19.6f,5,215,325);break;
 case Icon::Tom:{Path p;p.move(g.X(6.5f),g.Y(4)).line(g.X(17.5f),g.Y(4)).quad(g.X(21),g.Y(4),g.X(21),g.Y(7.5f)).line(g.X(21),g.Y(13)).quad(g.X(21),g.Y(16.5f),g.X(17.5f),g.Y(16.5f)).line(g.X(11.5f),g.Y(16.5f)).line(g.X(7),g.Y(20.5f)).line(g.X(7.5f),g.Y(16.5f)).line(g.X(6.5f),g.Y(16.5f)).quad(g.X(3),g.Y(16.5f),g.X(3),g.Y(13)).line(g.X(3),g.Y(7.5f)).quad(g.X(3),g.Y(4),g.X(6.5f),g.Y(4)).close();g.shape(p,false);g.dot(8,10.3f,1.25f).dot(12,10.3f,1.25f).dot(16,10.3f,1.25f);break;}
 case Icon::Note:g.poly({{4.5f,19.5f},{5.6f,15.2f},{15.6f,5.2f},{18.8f,8.4f},{8.8f,18.4f}},true).ln(13.6f,7.2f,16.8f,10.4f).ln(4.5f,19.5f,8.8f,18.4f);break;
 case Icon::Highlight:g.poly({{9,15},{15.8f,4.6f},{19.6f,7.2f},{12.8f,17.6f}},true).poly({{9,15},{7.2f,18.9f},{11,19.4f},{12.8f,17.6f}},true).ln(3.5f,21,13,21);break;
 case Icon::Copy:g.rect(8.5f,8.5f,20,20,2).poly({{15.5f,5.5f},{15.5f,4},{4,4},{4,15.5f},{5.5f,15.5f}});break;
 case Icon::Settings:gear(g);break;
 case Icon::Eye:{Path p;p.move(g.X(2),g.Y(12)).quad(g.X(12),g.Y(2.5f),g.X(22),g.Y(12)).quad(g.X(12),g.Y(21.5f),g.X(2),g.Y(12)).close();g.shape(p,false);g.circle(12,12,3.2f);break;}
 case Icon::EyeOff:{Path p;p.move(g.X(2),g.Y(12)).quad(g.X(12),g.Y(2.5f),g.X(22),g.Y(12)).quad(g.X(12),g.Y(21.5f),g.X(2),g.Y(12)).close();g.shape(p,false);g.circle(12,12,3.2f).ln(4,20,20,4);break;}
 case Icon::Lock:g.rect(5,10.5f,19,20.5f,2.2f).poly({{8,10.5f},{8,7.5f}}).arc(12,7.5f,4,180,360).poly({{16,7.5f},{16,10.5f}});break;
 case Icon::Unlock:g.rect(5,10.5f,19,20.5f,2.2f).poly({{8,10.5f},{8,7.5f}}).arc(12,7.5f,4,180,330);break;
 case Icon::Layers:g.poly({{12,3.5f},{21,8.5f},{12,13.5f},{3,8.5f}},true).poly({{3,12.5f},{12,17.5f},{21,12.5f}}).poly({{3,16.5f},{12,21.5f},{21,16.5f}});break;
 case Icon::Undo:{Path p;p.move(g.X(5.5f),g.Y(10)).line(g.X(14.5f),g.Y(10)).cubic(g.X(18.5f),g.Y(10),g.X(20.5f),g.Y(12.6f),g.X(20.5f),g.Y(15)).cubic(g.X(20.5f),g.Y(17.6f),g.X(18.3f),g.Y(20),g.X(14.5f),g.Y(20)).line(g.X(10),g.Y(20));g.shape(p,false);g.poly({{9.5f,5.8f},{5.3f,10},{9.5f,14.2f}});break;}
 case Icon::Redo:{Path p;p.move(g.X(18.5f),g.Y(10)).line(g.X(9.5f),g.Y(10)).cubic(g.X(5.5f),g.Y(10),g.X(3.5f),g.Y(12.6f),g.X(3.5f),g.Y(15)).cubic(g.X(3.5f),g.Y(17.6f),g.X(5.7f),g.Y(20),g.X(9.5f),g.Y(20)).line(g.X(14),g.Y(20));g.shape(p,false);g.poly({{14.5f,5.8f},{18.7f,10},{14.5f,14.2f}});break;}
 case Icon::Eraser:g.poly({{13.8f,3.8f},{20.6f,10.6f},{11.4f,19.8f},{8,19.8f},{4.2f,16},{4.2f,13.4f}},true).ln(9,9,15.8f,15.8f).ln(11.4f,19.8f,20.5f,19.8f);break;
 case Icon::Lasso:{Path p;for(int n=0;n<=40;++n){float a=(150+n*8.5f)*Pi/180;float x=g.X(12.5f+8.5f*std::cos(a)),y=g.Y(9.5f+5.8f*std::sin(a));if(!n)p.move(x,y);else p.line(x,y);}g.shape(p,false);g.circle(6.3f,15.6f,2.1f).poly({{5.7f,17.6f},{5.2f,20.8f}});break;}
 case Icon::Send:g.ln(12,19,12,5.5f).poly({{6.5f,11},{12,5.5f},{17.5f,11}});break;
 case Icon::Check:g.poly({{4.5f,12.5f},{9.6f,17.6f},{19.5f,6.8f}});break;
 case Icon::Trash:g.ln(3.5f,6.5f,20.5f,6.5f).poly({{9,6.5f},{9,4},{15,4},{15,6.5f}}).poly({{5.8f,6.5f},{7,20.5f},{17,20.5f},{18.2f,6.5f}}).ln(10,10.5f,10,16.5f).ln(14,10.5f,14,16.5f);break;
 case Icon::Restore:g.arc(12,12.5f,7.5f,200,520).poly({{3.2f,8.5f},{4.6f,12.6f},{8.6f,11}});break;
 case Icon::Import:g.ln(12,3.5f,12,14.5f).poly({{7,9.5f},{12,14.5f},{17,9.5f}}).poly({{3.5f,14.5f},{3.5f,20.5f},{20.5f,20.5f},{20.5f,14.5f}});break;
 case Icon::Pages:g.rect(3.5f,3.5f,10.5f,10.5f,1.2f).rect(13.5f,3.5f,20.5f,10.5f,1.2f).rect(3.5f,13.5f,10.5f,20.5f,1.2f).rect(13.5f,13.5f,20.5f,20.5f,1.2f);break;
 case Icon::PageSingle:g.rect(6.5f,3,17.5f,21,1.5f).ln(9,8,15,8).ln(9,11.5f,15,11.5f).ln(9,15,13,15);break;
 case Icon::PageDouble:g.rect(2.5f,4.5f,11.2f,19.5f,1.2f).rect(12.8f,4.5f,21.5f,19.5f,1.2f);break;
 case Icon::PageScroll:g.rect(6.5f,2.5f,17.5f,11,1.2f).rect(6.5f,13,17.5f,21.5f,1.2f);break;
 case Icon::Keyboard:g.rect(2.5f,6,21.5f,18,2).dot(6.5f,10,1.1f).dot(10,10,1.1f).dot(13.5f,10,1.1f).dot(17.5f,10,1.1f).ln(7.5f,14.2f,16.5f,14.2f);break;
 case Icon::Handwrite:g.poly({{12.5f,17.5f},{13.2f,14.4f},{19.6f,8},{21.6f,10},{15.2f,16.4f}},true);{Path p;p.move(g.X(2.5f),g.Y(18)).cubic(g.X(4.5f),g.Y(13),g.X(6.5f),g.Y(13),g.X(6.8f),g.Y(17)).cubic(g.X(7.1f),g.Y(20.5f),g.X(9.6f),g.Y(19.8f),g.X(11),g.Y(17.8f));g.shape(p,false);}break;
 case Icon::Link:g.rect(2.5f,8.5f,13.5f,15.5f,3.5f).rect(10.5f,8.5f,21.5f,15.5f,3.5f);break;
 case Icon::Sort:g.ln(4,6.5f,20,6.5f).ln(4,12,15,12).ln(4,17.5f,10,17.5f);break;
 case Icon::Contrast:{g.circle(12,12,8.5f);Path p;p.move(g.X(12),g.Y(3.5f)).arc(g.X(12),g.Y(12),8.5f*k,-90,90).close();c.fillPath(p,col);break;}
 case Icon::Crop:g.poly({{7,2.5f},{7,17},{21.5f,17}}).poly({{2.5f,7},{17,7},{17,21.5f}});break;
 case Icon::Info:g.circle(12,12,8.5f).ln(12,11,12,16.8f).dot(12,7.6f,1.3f);break;
 case Icon::Device:g.rect(6,2.5f,18,21.5f,2.2f).ln(10.5f,18.5f,13.5f,18.5f);break;
 case Icon::Books:g.rect(3.5f,4,8,20,1).rect(9,4,13.5f,20,1).poly({{15,5.4f},{19.3f,4.2f},{21.5f,19},{17.2f,20.2f}},true);break;
 case Icon::Rotate:g.arc(12,12.5f,7.5f,-60,230).poly({{18.5f,3.8f},{16.2f,6.4f},{19.6f,7.8f}});break;
 case Icon::ZoomIn:g.circle(10.5f,10.5f,6.5f).ln(15.4f,15.4f,20.5f,20.5f).ln(10.5f,7.5f,10.5f,13.5f).ln(7.5f,10.5f,13.5f,10.5f);break;
 case Icon::ZoomOut:g.circle(10.5f,10.5f,6.5f).ln(15.4f,15.4f,20.5f,20.5f).ln(7.5f,10.5f,13.5f,10.5f);break;
 case Icon::Fit:g.poly({{3.5f,8.5f},{3.5f,3.5f},{8.5f,3.5f}}).poly({{15.5f,3.5f},{20.5f,3.5f},{20.5f,8.5f}}).poly({{20.5f,15.5f},{20.5f,20.5f},{15.5f,20.5f}}).poly({{8.5f,20.5f},{3.5f,20.5f},{3.5f,15.5f}});break;
 case Icon::Text:g.poly({{3,19},{8,5},{13,19}}).ln(5,14.5f,11,14.5f).circle(17.5f,15.5f,3.3f).ln(20.8f,12,20.8f,19);break;
 case Icon::Drag:g.dot(9,6,1.5f).dot(15,6,1.5f).dot(9,12,1.5f).dot(15,12,1.5f).dot(9,18,1.5f).dot(15,18,1.5f);break;
 case Icon::Wifi:g.arc(12,19,13.5f,225,315).arc(12,19,9,225,315).arc(12,19,4.5f,225,315).dot(12,19,1.7f);break;
 case Icon::Selection:g.dashRect(3.5f,3.5f,17.5f,17.5f).poly({{12,12},{20.5f,15},{16.6f,16.6f},{15,20.5f}},true,true);break;
 case Icon::EraseArea:g.dashRect(3.5f,7,20.5f,17).ln(8,12,16,12);break;
 case Icon::TomWrite:{g.rect(3.5f,4,20.5f,16.5f,2.5f);Path p;p.move(g.X(6.5f),g.Y(11)).cubic(g.X(8),g.Y(7.6f),g.X(9.6f),g.Y(13.6f),g.X(11.2f),g.Y(10)).cubic(g.X(12.6f),g.Y(7.4f),g.X(14),g.Y(13),g.X(15.6f),g.Y(10.2f)).line(g.X(17.6f),g.Y(10.2f));g.shape(p,false);g.poly({{7,16.5f},{7,20.5f},{11,16.5f}});break;}
 case Icon::PageGrid:g.rect(3.5f,3.5f,10.5f,11.5f,1.2f).rect(13.5f,3.5f,20.5f,11.5f,1.2f).rect(3.5f,13.5f,10.5f,20.5f,1.2f).rect(13.5f,13.5f,20.5f,20.5f,1.2f);break;
 case Icon::ToolbarHandle:case Icon::CloseCircle:case Icon::DocumentMenu:case Icon::BookOpen:case Icon::Tag:
 case Icon::NativeEraser:case Icon::NativeSelection:case Icon::NativeLayers:case Icon::NativeUndo:case Icon::NativeRedo:native(c,i,box,col,sw);break;
 case Icon::Count:break;
 }
}
const char* instrumentName(Instrument i)noexcept{
 static constexpr std::array<const char*,10> Names{"Ballpoint","Fineliner","Pencil","Mechanical pencil","Paintbrush","Calligraphy","Marker","Shader","Highlighter","Eraser"};
 return unsigned(i)<Names.size()?Names[unsigned(i)]:"";
}
void instrument(Canvas& c,Instrument kind,Rect box,std::uint32_t ink,bool inverse){
 // Tip-up instrument in a w:h box (about 1:2.4). Units: u = width/10.
 const std::uint32_t line=inverse?theme::Inverse:theme::InkPrimary,body=inverse?theme::InkPrimary:theme::Surface,shade=inverse?0xff4a4a4au:theme::SurfaceSunken;
 float w=float(width(box)),h=float(height(box));float u=std::min(w/10.f,h/24.f);float cx=box.x0+w/2,top=box.y0+(h-24*u)/2;
 auto X=[&](float x){return cx+x*u;};auto Y=[&](float y){return top+y*u;};float lw=std::max(2.f,u*0.42f);
 auto poly=[&](std::initializer_list<Pt> pts,std::uint32_t fillColor,bool outline=true){Path p;bool first=true;for(auto q:pts){if(first)p.move(X(q.x),Y(q.y));else p.line(X(q.x),Y(q.y));first=false;}p.close();c.fillPath(p,fillColor);if(outline)c.stroke(p,lw,line);};
 auto barrel=[&](float halfWidth,float from){Rect r{int(X(-halfWidth)),int(Y(from)),int(X(halfWidth)),int(Y(24))};c.fillRound(r,u*1.1f,body);c.strokeRound(r,u*1.1f,lw,line);};
 switch(kind){
 case Instrument::Ballpoint:barrel(2.1f,8.5f);poly({{-2.1f,8.5f},{2.1f,8.5f},{0.5f,2.2f},{-0.5f,2.2f}},body);c.fillCircle(X(0),Y(1.6f),u*0.75f,ink);c.line(X(-2.1f),Y(12),X(2.1f),Y(12),lw,line);c.fill({int(X(1.2f)),int(Y(13)),int(X(2.1f))-int(lw/2),int(Y(19))},ink);break;
 case Instrument::Fineliner:barrel(2.2f,9);poly({{-2.2f,9},{2.2f,9},{1.1f,5},{-1.1f,5}},shade);c.fill({int(X(-0.35f)),int(Y(1.2f)),int(X(0.35f))+1,int(Y(5))},ink);c.line(X(-2.2f),Y(12.5f),X(2.2f),Y(12.5f),lw,line);break;
 case Instrument::Pencil:barrel(2.2f,8.5f);poly({{-2.2f,8.5f},{2.2f,8.5f},{0,1.2f}},body);poly({{-0.9f,4.3f},{0.9f,4.3f},{0,1.2f}},ink,false);c.line(X(-0.8f),Y(8.5f),X(-0.8f),Y(24),lw*0.7f,shade==theme::SurfaceSunken?theme::Border:0xff6a6a6au);c.line(X(0.8f),Y(8.5f),X(0.8f),Y(24),lw*0.7f,shade==theme::SurfaceSunken?theme::Border:0xff6a6a6au);break;
 case Instrument::Mechanical:barrel(1.9f,8);poly({{-1.9f,8},{1.9f,8},{0.8f,4.6f},{-0.8f,4.6f}},shade);c.fill({int(X(-0.3f)),int(Y(1.4f)),int(X(0.3f))+1,int(Y(4.6f))},line);c.fill({int(X(-0.3f)),int(Y(0.8f)),int(X(0.3f))+1,int(Y(1.8f))},ink);c.line(X(-1.9f),Y(15),X(1.9f),Y(15),lw,line);break;
 case Instrument::Paintbrush:barrel(1.7f,11);poly({{-1.7f,11},{1.7f,11},{2.2f,8.6f},{-2.2f,8.6f}},shade);{Path p;p.move(X(-2.2f),Y(8.6f)).cubic(X(-2.6f),Y(5.5f),X(-0.8f),Y(2.6f),X(0),Y(0.8f)).cubic(X(0.8f),Y(2.6f),X(2.6f),Y(5.5f),X(2.2f),Y(8.6f)).close();c.fillPath(p,ink);c.stroke(p,lw,line);}break;
 case Instrument::Calligraphy:barrel(2.1f,10);{Path p;p.move(X(-2.1f),Y(10)).line(X(-1.9f),Y(5.5f)).line(X(0),Y(1)).line(X(1.9f),Y(5.5f)).line(X(2.1f),Y(10)).close();c.fillPath(p,shade);c.stroke(p,lw,line);c.line(X(0),Y(1.6f),X(0),Y(6.3f),lw*0.8f,line);c.fillCircle(X(0),Y(6.8f),u*0.55f,line);c.fill({int(X(-0.9f)),int(Y(1.1f)),int(X(0.9f)),int(Y(2.1f))},ink);}break;
 case Instrument::Marker:barrel(2.5f,9);poly({{-2.5f,9},{2.5f,9},{1.7f,5.4f},{-1.7f,5.4f}},shade);poly({{-1.3f,5.4f},{1.3f,5.4f},{1.3f,2.6f},{-1.3f,1.4f}},ink);break;
 case Instrument::Shader:barrel(2.7f,9.5f);poly({{-2.7f,9.5f},{2.7f,9.5f},{2.1f,6.2f},{-2.1f,6.2f}},shade);{Rect r{int(X(-1.9f)),int(Y(1.6f)),int(X(1.9f)),int(Y(6.6f))};c.fillRound(r,u*1.7f,theme::wash(ink,150));c.strokeRound(r,u*1.7f,lw,line);}break;
 case Instrument::Highlighter:barrel(2.7f,9);poly({{-2.7f,9},{2.7f,9},{2,5.6f},{-2,5.6f}},shade);poly({{-1.7f,5.6f},{1.7f,5.6f},{1.7f,2.8f},{-1.7f,1.2f}},theme::wash(ink,200));c.fill({int(X(-2.7f))+int(lw),int(Y(14)),int(X(2.7f))-int(lw),int(Y(16.5f))},theme::wash(ink,200));break;
 case Instrument::Eraser:barrel(2.5f,9);{Rect r{int(X(-2.5f)),int(Y(1.8f)),int(X(2.5f)),int(Y(9.6f))};c.fillRound(r,u*1.2f,shade);c.strokeRound(r,u*1.2f,lw,line);c.line(X(-2.5f),Y(6),X(2.5f),Y(6),lw,line);}break;
 case Instrument::Count:break;
 }
}
// Slanted 45-degree tool, tip at lower left. P(along,across) maps axis coordinates to the 24-unit grid.
void toolGlyph(Canvas& c,Instrument kind,Rect box,std::uint32_t col,std::uint32_t dot){
 float side=float(std::min(width(box),height(box)));float k=side/24.f;float w=std::max(2.f,theme::IconStroke*side/theme::Icon);
 G g{c,box.x0+(width(box)-side)/2,box.y0+(height(box)-side)/2,k,col,w};
 const float tx=4.5f,ty=19.5f,s=0.70710678f; // tip, unit along (+x,-y), across (+x,+y)
 auto X=[&](float a,float b){return g.X(tx+(a+b)*s);};auto Y=[&](float a,float b){return g.Y(ty+(-a+b)*s);};
 auto body=[&](float from,float to,float half){Path p;p.move(X(from,-half),Y(from,-half)).line(X(to,-half),Y(to,-half)).line(X(to,half),Y(to,half)).line(X(from,half),Y(from,half)).close();c.stroke(p,w,col);};
 auto cone=[&](float from,float to,float half,float tipHalf){Path p;p.move(X(from,-half),Y(from,-half)).line(X(to,-tipHalf),Y(to,-tipHalf));if(tipHalf>0)p.line(X(to,tipHalf),Y(to,tipHalf));p.line(X(from,half),Y(from,half));c.stroke(p,w,col);};
 const float end=19.5f;
 switch(kind){
 case Instrument::Ballpoint:body(6,end,2.1f);cone(6,0.9f,2.1f,0);c.fillCircle(X(0.4f,0),Y(0.4f,0),w*0.7f,col);break;
 case Instrument::Fineliner:body(6.5f,end,2.1f);cone(6.5f,3.4f,2.1f,0.9f);body(3.4f,0.6f,0.6f);break;
 case Instrument::Pencil:body(6,end,2.1f);cone(6,0.6f,2.1f,0);c.line(X(2.4f,-0.9f),Y(2.4f,-0.9f),X(2.4f,0.9f),Y(2.4f,0.9f),w,col);break;
 case Instrument::Mechanical:body(7.5f,end,1.8f);cone(7.5f,4,1.8f,0.5f);c.line(X(4,0),Y(4,0),X(0.6f,0),Y(0.6f,0),w,col);break;
 case Instrument::Paintbrush:{body(8.5f,end,1.5f);Path p;p.move(X(8.5f,-2.2f),Y(8.5f,-2.2f)).quad(X(4,-2.6f),Y(4,-2.6f),X(0.4f,0),Y(0.4f,0)).quad(X(4,2.6f),Y(4,2.6f),X(8.5f,2.2f),Y(8.5f,2.2f)).close();c.stroke(p,w,col);break;}
 case Instrument::Calligraphy:{body(7,end,2.1f);Path p;p.move(X(7,-2.1f),Y(7,-2.1f)).line(X(1.6f,-1.6f),Y(1.6f,-1.6f)).line(X(0.4f,0),Y(0.4f,0)).line(X(1.6f,1.6f),Y(1.6f,1.6f)).line(X(7,2.1f),Y(7,2.1f));c.stroke(p,w,col);c.line(X(4.8f,0),Y(4.8f,0),X(1.4f,0),Y(1.4f,0),w*0.8f,col);break;}
 case Instrument::Marker:{body(5.5f,end,2.6f);Path p;p.move(X(5.5f,-2.6f),Y(5.5f,-2.6f)).line(X(1.2f,-2),Y(1.2f,-2)).line(X(0.4f,1.6f),Y(0.4f,1.6f)).line(X(5.5f,2.6f),Y(5.5f,2.6f));c.stroke(p,w,col);break;}
 case Instrument::Shader:{body(6,end,2.6f);Path p;p.move(X(6,-2.6f),Y(6,-2.6f)).quad(X(0.2f,-2.6f),Y(0.2f,-2.6f),X(0.6f,0),Y(0.6f,0)).quad(X(0.2f,2.6f),Y(0.2f,2.6f),X(6,2.6f),Y(6,2.6f));c.stroke(p,w,col);break;}
 case Instrument::Highlighter:{body(6,end,2.8f);Path p;p.move(X(6,-2.8f),Y(6,-2.8f)).line(X(2,-2.4f),Y(2,-2.4f)).line(X(0.6f,2.4f),Y(0.6f,2.4f)).line(X(6,2.8f),Y(6,2.8f));c.stroke(p,w,col);c.line(X(9,-2.8f),Y(9,-2.8f),X(9,2.8f),Y(9,2.8f),w,col);break;}
 case Instrument::Eraser:{Path p;p.move(X(0.8f,-3),Y(0.8f,-3)).line(X(end-6,-3),Y(end-6,-3)).line(X(end-6,3),Y(end-6,3)).line(X(0.8f,3),Y(0.8f,3)).close();c.stroke(p,w,col);c.line(X(5,-3),Y(5,-3),X(5,3),Y(5,3),w,col);break;}
 case Instrument::Count:break;}
 if(dot)c.fillCircle(g.X(19.6f),g.Y(19.6f),2.6f*k,dot);
}
void nibGlyph(Canvas& c,Instrument kind,Rect box,std::uint32_t col){
 G g=grid40(c,box,col,0);
 auto base=[&](float top){g.poly({{10,34},{10,top},{30,top},{30,34}});};
 auto collar=[&]{base(23);g.poly({{14,23},{14,15},{26,15},{26,23}});};
 auto filled=[&](Path p){p.close();c.fillPath(p,col);c.stroke(p,g.w,col);};
 switch(kind){
 case Instrument::Fineliner:collar();g.ln(20,15,20,5);break;
 case Instrument::Ballpoint:collar();g.ln(20,15,20,7).ln(17,11,23,11);break;
 case Instrument::Highlighter:collar();g.poly({{17,15},{17,6},{24,10},{24,15}}).poly({{14,29},{26,29},{26,34}});break;
 case Instrument::Pencil:g.poly({{10,34},{10,24},{20,10},{30,24},{30,34}}).ln(10,24,30,24).ln(20,24,20,34).ln(15,17,25,17).poly({{17,14},{20,10},{23,14}},true,true);break;
 case Instrument::Mechanical:g.poly({{10,34},{10,24},{20,14},{30,24},{30,34}}).ln(10,24,30,24).ln(20,24,20,34).poly({{18,16},{18,8},{22,8},{22,16}}).ln(20,8,20,3);break;
 case Instrument::Calligraphy:g.poly({{15,30},{10,18},{20,4},{30,18},{25,30}}).poly({{15,30},{25,30},{25,35},{15,35}},true).ln(20,4,20,16).circle(20,20,2.5f);break;
 case Instrument::Marker:{g.poly({{11,34},{11,24},{29,24},{29,34}});Path p;p.move(g.X(14),g.Y(24)).cubic(g.X(13),g.Y(20),g.X(15),g.Y(14),g.X(20),g.Y(8)).cubic(g.X(25),g.Y(14),g.X(27),g.Y(20),g.X(26),g.Y(24));filled(p);break;}
 case Instrument::Paintbrush:{g.poly({{11,34},{11,28},{29,28},{29,34}});Path p;p.move(g.X(15),g.Y(28)).cubic(g.X(11),g.Y(21),g.X(17),g.Y(16),g.X(20),g.Y(6)).cubic(g.X(24),g.Y(16),g.X(27),g.Y(21),g.X(25),g.Y(28));filled(p);break;}
 case Instrument::Shader:{g.poly({{10,34},{10,26},{30,26},{30,34}});Path p;p.move(g.X(12),g.Y(26)).cubic(g.X(14),g.Y(23),g.X(15),g.Y(22),g.X(15),g.Y(19)).cubic(g.X(15),g.Y(16),g.X(20),g.Y(11),g.X(20),g.Y(7))
  .cubic(g.X(21),g.Y(14),g.X(29),g.Y(16),g.X(25),g.Y(22)).line(g.X(28),g.Y(26));filled(p);break;}
 case Instrument::Eraser:native(c,Icon::NativeEraser,box,col,0);break;
 case Instrument::Count:break;}
}
// Writing family: each tool as the object in the hand, slanted 45 degrees with its tip at lower left as it meets the
// page. A paper body closed by an ink outline, the tip in solid ink in a shape that names the tool, and a band in the
// tool's current ink (the highlighter's tip too), so colour sits in the glyph as an area met by an ink rule.
void penGlyph(Canvas& c,Instrument kind,Rect box,std::uint32_t ink,bool selected,bool disabled){
 G g=grid40(c,box,0,0);g.w=std::max(2.25f,1.6f*g.k);
 const std::uint32_t edge=disabled?theme::Disabled:selected?theme::Inverse:theme::InkPrimary,fill=selected?theme::InkPrimary:theme::Paper,solid=edge;
 const float s=0.70710678f;
 auto X=[&](float a,float b){return g.X(5+(a+b)*s);};auto Y=[&](float a,float b){return g.Y(35+(-a+b)*s);};
 auto quad=[&](float a0,float h0,float a1,float h1){Path p;p.move(X(a0,-h0),Y(a0,-h0)).line(X(a1,-h1),Y(a1,-h1)).line(X(a1,h1),Y(a1,h1)).line(X(a0,h0),Y(a0,h0)).close();return p;};
 auto part=[&](const Path& p,std::uint32_t f){c.fillPath(p,f);c.stroke(p,g.w,edge);};
 const bool wide=kind==Instrument::Highlighter||kind==Instrument::Eraser;const float half=wide?4.8f:3.7f,end=42.f;
 const std::uint32_t band=disabled?theme::Disabled:ink?ink:solid;
 // The barrel ends in a round cap; the band sits a third of the way up, where a cap ring or label would be.
 auto barrel=[&](float from){Path p;p.move(X(from,-half),Y(from,-half)).line(X(end-half,-half),Y(end-half,-half)).arc(X(end-half,0),Y(end-half,0),half*g.k,225,405).line(X(from,half),Y(from,half)).close();part(p,fill);
  if(kind!=Instrument::Eraser)part(quad(from+8,half,from+14,half),band);};
 switch(kind){
 case Instrument::Ballpoint:barrel(12);part(quad(12,half,3,1.3f),fill);c.fillCircle(X(1.6f,0),Y(1.6f,0),1.7f*g.k,solid);break;
 case Instrument::Fineliner:barrel(12);part(quad(12,half,7,2.2f),fill);part(quad(7,0.9f,0.5f,0.9f),solid);break;
 case Instrument::Pencil:barrel(12);part(quad(12,half,0,0.01f),fill);part(quad(4.5f,1.7f,0,0.01f),solid);c.line(X(26,0),Y(26,0),X(end-half,0),Y(end-half,0),g.w*0.7f,edge);break;
 case Instrument::Mechanical:barrel(12);part(quad(12,half,6,1.7f),fill);part(quad(6,0.9f,2,0.9f),fill);part(quad(2,0.5f,0,0.5f),solid);break;
 case Instrument::Paintbrush:{barrel(13);part(quad(13,half-0.6f,9,half-0.2f),fill);Path p;p.move(X(9,-half+0.4f),Y(9,-half+0.4f)).cubic(X(5,-4.6f),Y(5,-4.6f),X(2,-1.6f),Y(2,-1.6f),X(0,0),Y(0,0))
  .cubic(X(2,1.6f),Y(2,1.6f),X(5,4.6f),Y(5,4.6f),X(9,half-0.4f),Y(9,half-0.4f)).close();part(p,solid);break;}
 case Instrument::Calligraphy:{barrel(13);Path p;p.move(X(13,-half),Y(13,-half)).line(X(5,-3.4f),Y(5,-3.4f)).line(X(0,0),Y(0,0)).line(X(5,3.4f),Y(5,3.4f)).line(X(13,half),Y(13,half)).close();part(p,fill);
  c.line(X(1.4f,0),Y(1.4f,0),X(6.4f,0),Y(6.4f,0),g.w*0.8f,edge);c.fillCircle(X(7.6f,0),Y(7.6f,0),1.2f*g.k,solid);break;}
 case Instrument::Marker:{barrel(12);part(quad(12,half,6,3.2f),fill);Path p;p.move(X(6,-3.2f),Y(6,-3.2f)).line(X(1.4f,-2.6f),Y(1.4f,-2.6f)).line(X(0,2.4f),Y(0,2.4f)).line(X(6,3.2f),Y(6,3.2f)).close();part(p,solid);break;}
 case Instrument::Shader:{barrel(12);part(quad(12,half,7,3.4f),fill);Path p;p.move(X(7,-3.4f),Y(7,-3.4f)).cubic(X(2.4f,-3.4f),Y(2.4f,-3.4f),X(0.4f,-1.6f),Y(0.4f,-1.6f),X(0.4f,0),Y(0.4f,0))
  .cubic(X(0.4f,1.6f),Y(0.4f,1.6f),X(2.4f,3.4f),Y(2.4f,3.4f),X(7,3.4f),Y(7,3.4f)).close();part(p,solid);break;}
 case Instrument::Highlighter:{barrel(12);part(quad(12,half,6,3.6f),fill);Path p;p.move(X(6,-3.6f),Y(6,-3.6f)).line(X(2.2f,-3.2f),Y(2.2f,-3.2f)).line(X(0,3.2f),Y(0,3.2f)).line(X(6,3.6f),Y(6,3.6f)).close();part(p,band);break;}
 case Instrument::Eraser:{barrel(14);Path tip=quad(14,half,1,half);part(tip,fill);part(quad(14,half,22,half),solid);c.line(X(5,-half),Y(5,-half),X(5,half),Y(5,half),g.w*0.7f,edge);break;}
 case Instrument::Count:break;}
}
}
