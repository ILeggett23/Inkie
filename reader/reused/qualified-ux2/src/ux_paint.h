#pragma once
#include "underlay.h"
#include "chrome.h"
namespace ux {
enum class State {Picker,Loading,Clean,Chrome,NavigationPending,ExitPending,Failed};
enum class Action {None,Open,Show,Hide,Previous,Next,Exit};
inline constexpr spike::Rect Top{0,0,1620,120},Previous{0,930,220,1210},Next{1400,930,1620,1210},PickerRow{100,500,1520,780},Reveal{600,2060,1020,2160};
inline constexpr std::array<spike::Rect,4> Regions{Top,Previous,Next,PickerRow};
inline Action hit(State state,int x,int y){
 if(state==State::Picker||state==State::Loading){if(state==State::Picker&&PickerRow.contains(x,y))return Action::Open;if(spike::Rect{1300,60,1620,220}.contains(x,y))return Action::Exit;return Action::None;}
 if(state==State::Clean)return Reveal.contains(x,y)?Action::Show:Action::None;
 if(state==State::Chrome){if(spike::Rect{0,0,220,120}.contains(x,y))return Action::Exit;if(Previous.contains(x,y))return Action::Previous;if(Next.contains(x,y))return Action::Next;return x>=0&&x<1620&&y>=0&&y<2160?Action::Hide:Action::None;}
 if(state==State::NavigationPending&&spike::Rect{0,0,220,120}.contains(x,y))return Action::Exit;
 return Action::None;
}
inline auto glyph(char c){switch(c){
 case 'B':return std::array<unsigned char,7>{30,17,17,30,17,17,30};case 'C':return std::array<unsigned char,7>{14,17,16,16,16,17,14};
 case 'F':return std::array<unsigned char,7>{31,16,16,30,16,16,16};case 'H':return std::array<unsigned char,7>{17,17,17,31,17,17,17};
 case 'J':return std::array<unsigned char,7>{7,2,2,2,18,18,12};case 'K':return std::array<unsigned char,7>{17,18,20,24,20,18,17};
 case 'M':return std::array<unsigned char,7>{17,27,21,21,17,17,17};case 'Q':return std::array<unsigned char,7>{14,17,17,17,21,18,13};
 case 'S':return std::array<unsigned char,7>{15,16,16,14,1,1,30};case 'U':return std::array<unsigned char,7>{17,17,17,17,17,17,14};
 case 'W':return std::array<unsigned char,7>{17,17,17,21,21,27,17};case 'Y':return std::array<unsigned char,7>{17,17,10,4,4,4,4};
 case 'Z':return std::array<unsigned char,7>{31,1,2,4,8,16,31};case '-':return std::array<unsigned char,7>{0,0,0,31,0,0,0};case '.':return std::array<unsigned char,7>{0,0,0,0,0,6,6};
 default:return reader::glyph(c);}}
inline void fill(spike::Surface s,spike::Rect r,std::uint32_t color=0xffffffffu){for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x)if(x>=0&&x<s.width&&y>=0&&y<s.height)s.pixels[std::size_t(y)*s.stridePixels+x]=color;}
inline void text(spike::Surface s,int x,int y,int scale,const char* value){for(;*value;++value,x+=6*scale){auto g=glyph(*value);for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(g[row]&(16>>col))fill(s,{x+col*scale,y+row*scale,x+(col+1)*scale,y+(row+1)*scale},0xff000000u);}}
inline void border(spike::Surface s,spike::Rect r){fill(s,{r.x0,r.y0,r.x1,r.y0+2},0xff000000u);fill(s,{r.x0,r.y1-2,r.x1,r.y1},0xff000000u);fill(s,{r.x0,r.y0,r.x0+2,r.y1},0xff000000u);fill(s,{r.x1-2,r.y0,r.x1,r.y1},0xff000000u);}
inline void pickerRow(spike::Surface s,bool loading){fill(s,PickerRow);border(s,PickerRow);text(s,150,560,5,loading?"OPENING PDF":"INKBOUND DEVICE TEST");text(s,150,650,4,loading?"PLEASE WAIT":"INKBOUND-DEVICE-TEST.PDF");}
inline void picker(spike::Surface s){fill(s,{0,0,s.width,s.height});text(s,100,130,10,"INKBOUND");text(s,100,330,5,"PDF DOCUMENTS");text(s,1380,110,4,"EXIT");pickerRow(s,false);text(s,100,1850,4,"TAP BOTTOM CENTRE FOR CONTROLS");text(s,100,1920,3,"IN THE READER - MARKER ALWAYS WRITES");}
inline void overlay(spike::Surface s,const char* counter,bool loading=false){for(auto r:{Top,Previous,Next}){fill(s,r);border(s,r);}text(s,50,44,4,"EXIT");text(s,300,44,4,loading?"LOADING":"INKBOUND DEVICE TEST");text(s,1300,44,4,counter);text(s,45,1050,5,"PREV");text(s,1445,1050,5,"NEXT");}
}
