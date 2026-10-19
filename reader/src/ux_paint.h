#pragma once
#include "underlay.h"
#include "chrome.h"
#include "ib/library_screen.h"
#include "ib/reader_screen.h"
#include <cstdio>
#include <QString>
namespace ux {
enum class State {Picker,Loading,Clean,Chrome,NavigationPending,ExitPending,Failed};
enum class Action {None,Open,Show,Hide,Previous,Next,Exit};
// Reader chrome geometry (native Paper Pro layout, ib/reader_screen.h). Toolbar side follows the
// handedness setting, so the toolbar-side rects are functions. Regions are the partial refresh areas
// for clean<->chrome changes: 0 top strip (handle, close), 1 toolbar column, 2 footer, 3 picker row.
inline constexpr spike::Rect PickerRow{100,560,1520,820},Reveal=ib::reader::FooterRect;
// On the panel: the reader chrome is laid out in the current turn (docs/LANDSCAPE.md); these are its panel pixels.
inline spike::Rect onPanel(spike::Rect logical)noexcept{return ib::layout::Frame{ib::layout::turn()}.physical(logical);}
inline spike::Rect topStrip()noexcept{return onPanel(ib::reader::topRect());}
inline spike::Rect toolbarColumn()noexcept{auto r=ib::reader::toolbarRect();return onPanel({r.x0,ib::reader::ToolbarItem,r.x1,r.y1});}
inline spike::Rect closeTarget()noexcept{return onPanel(ib::reader::closeRect());}
inline spike::Rect handleTarget()noexcept{return onPanel(ib::reader::toolHandleRect());}
inline std::array<spike::Rect,4> regions()noexcept{return {topStrip(),toolbarColumn(),onPanel(ib::reader::footerRect()),PickerRow};}
inline constexpr unsigned RegionCount=4,ChromeRegions=1u|2u|4u;
// Clean: only the footer reveals chrome (the shell routes the visible handle itself); page taps do
// nothing and pages turn by swipe, as on the native reader. Chrome: the circled close leaves, the
// handle hides the toolbar; taps on the page stay inert so a stray finger never hides tools.
inline Action hit(State state,int x,int y){
 if(state==State::Picker||state==State::Loading){if(state==State::Picker&&PickerRow.contains(x,y))return Action::Open;if(spike::Rect{1380,0,1620,130}.contains(x,y))return Action::Exit;return Action::None;}
 if(state==State::Clean)return onPanel(ib::reader::footerRect()).contains(x,y)?Action::Show:Action::None; /* Reveal in portrait */
 if(state==State::Chrome){if(closeTarget().contains(x,y))return Action::Exit;if(handleTarget().contains(x,y))return Action::Hide;return Action::None;}
 if(state==State::NavigationPending&&closeTarget().contains(x,y))return Action::Exit;
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
 case ',':return std::array<unsigned char,7>{0,0,0,0,0,4,8};case '\'':return std::array<unsigned char,7>{4,4,8,0,0,0,0};case ':':return std::array<unsigned char,7>{0,4,4,0,4,4,0};case ';':return std::array<unsigned char,7>{0,4,4,0,4,4,8};case '!':return std::array<unsigned char,7>{4,4,4,4,4,0,4};case '?':return std::array<unsigned char,7>{14,17,1,2,4,0,4};
 /* symbols for passwords and network names */
 case '@':return std::array<unsigned char,7>{14,17,23,21,23,16,14};case '#':return std::array<unsigned char,7>{10,10,31,10,31,10,10};case '$':return std::array<unsigned char,7>{4,15,20,14,5,30,4};case '%':return std::array<unsigned char,7>{24,25,2,4,8,19,3};case '^':return std::array<unsigned char,7>{4,10,17,0,0,0,0};case '&':return std::array<unsigned char,7>{12,18,20,8,21,18,13};case '*':return std::array<unsigned char,7>{0,4,21,14,21,4,0};case '(':return std::array<unsigned char,7>{2,4,8,8,8,4,2};case ')':return std::array<unsigned char,7>{8,4,2,2,2,4,8};case '_':return std::array<unsigned char,7>{0,0,0,0,0,0,31};case '+':return std::array<unsigned char,7>{0,4,4,31,4,4,0};case '=':return std::array<unsigned char,7>{0,0,31,0,31,0,0};case '[':return std::array<unsigned char,7>{14,8,8,8,8,8,14};case ']':return std::array<unsigned char,7>{14,2,2,2,2,2,14};case '{':return std::array<unsigned char,7>{6,8,8,16,8,8,6};case '}':return std::array<unsigned char,7>{12,2,2,1,2,2,12};case '|':return std::array<unsigned char,7>{4,4,4,4,4,4,4};case '~':return std::array<unsigned char,7>{0,0,8,21,2,0,0};case '`':return std::array<unsigned char,7>{8,4,2,0,0,0,0};case '"':return std::array<unsigned char,7>{10,10,0,0,0,0,0};case '<':return std::array<unsigned char,7>{2,4,8,16,8,4,2};case '>':return std::array<unsigned char,7>{8,4,2,1,2,4,8};case '/':return std::array<unsigned char,7>{1,1,2,4,8,16,16};case '\\':return std::array<unsigned char,7>{16,16,8,4,2,1,1};
 default:return reader::glyph(c);}}
inline void fill(spike::Surface s,spike::Rect r,std::uint32_t color=0xffffffffu){for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x)if(x>=0&&x<s.width&&y>=0&&y<s.height)s.pixels[std::size_t(y)*s.stridePixels+x]=color;}
inline void text(spike::Surface s,int x,int y,int scale,const char* value){for(;*value;++value,x+=6*scale){auto g=glyph(*value);for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(g[row]&(16>>col))fill(s,{x+col*scale,y+row*scale,x+(col+1)*scale,y+(row+1)*scale},0xff000000u);}}
// Mixed-case text (lowercase glyphs, then the UI glyphs): account names, network names.
inline auto accountGlyph(char c){switch(c){
 case 'a':return std::array<unsigned char,7>{0,0,14,1,15,17,15};case 'b':return std::array<unsigned char,7>{16,16,22,25,17,17,30};
 case 'c':return std::array<unsigned char,7>{0,0,14,16,16,17,14};case 'd':return std::array<unsigned char,7>{1,1,13,19,17,17,15};
 case 'e':return std::array<unsigned char,7>{0,0,14,17,31,16,14};case 'f':return std::array<unsigned char,7>{6,9,8,28,8,8,8};
 case 'g':return std::array<unsigned char,7>{0,15,17,17,15,1,14};case 'h':return std::array<unsigned char,7>{16,16,22,25,17,17,17};
 case 'i':return std::array<unsigned char,7>{4,0,12,4,4,4,14};case 'j':return std::array<unsigned char,7>{2,0,6,2,2,18,12};
 case 'k':return std::array<unsigned char,7>{16,16,18,20,24,20,18};case 'l':return std::array<unsigned char,7>{12,4,4,4,4,4,14};
 case 'm':return std::array<unsigned char,7>{0,0,26,21,21,17,17};case 'n':return std::array<unsigned char,7>{0,0,22,25,17,17,17};
 case 'o':return std::array<unsigned char,7>{0,0,14,17,17,17,14};case 'p':return std::array<unsigned char,7>{0,0,30,17,30,16,16};
 case 'q':return std::array<unsigned char,7>{0,0,15,17,15,1,1};case 'r':return std::array<unsigned char,7>{0,0,22,25,16,16,16};
 case 's':return std::array<unsigned char,7>{0,0,15,16,14,1,30};case 't':return std::array<unsigned char,7>{8,8,28,8,8,9,6};
 case 'u':return std::array<unsigned char,7>{0,0,17,17,17,19,13};case 'v':return std::array<unsigned char,7>{0,0,17,17,17,10,4};
 case 'w':return std::array<unsigned char,7>{0,0,17,17,21,21,10};case 'x':return std::array<unsigned char,7>{0,0,17,10,4,10,17};
 case 'y':return std::array<unsigned char,7>{0,0,17,17,15,1,14};case 'z':return std::array<unsigned char,7>{0,0,31,2,4,8,31};
 case '@':return std::array<unsigned char,7>{14,17,23,21,23,16,14};case '+':return std::array<unsigned char,7>{0,4,4,31,4,4,0};case '_':return std::array<unsigned char,7>{0,0,0,0,0,0,31};
 default:return ux::glyph(c);}}
inline void literal(spike::Surface s,int x,int y,int scale,const QString& value){for(auto c:value){auto g=accountGlyph(c.toLatin1());for(int row=0;row<7;++row)for(int col=0;col<5;++col)if(g[row]&(16>>col))fill(s,{x+col*scale,y+row*scale,x+(col+1)*scale,y+(row+1)*scale},0xff000000u);x+=6*scale;}}
inline void border(spike::Surface s,spike::Rect r){fill(s,{r.x0,r.y0,r.x1,r.y0+2},0xff000000u);fill(s,{r.x0,r.y1-2,r.x1,r.y1},0xff000000u);fill(s,{r.x0,r.y0,r.x0+2,r.y1},0xff000000u);fill(s,{r.x1-2,r.y0,r.x1,r.y1},0xff000000u);}
// Qualification fixture picker (non-library route), in the Inkbound style.
inline void pickerRow(spike::Surface s,bool loading){ib::Canvas c(s);ib::Targets t;c.fill(ib::outset(PickerRow,8),ib::theme::Paper);ib::panel(c,PickerRow,ib::theme::RadiusLarge,ib::theme::Border);
 ib::library::placeholderCover(c,{PickerRow.x0+ib::theme::S4,PickerRow.y0+ib::theme::S4,PickerRow.x0+ib::theme::S4+150,PickerRow.y1-ib::theme::S4},"",loading?"":"pdf");
 ib::text::draw(c,{ib::Face::Serif,40},float(PickerRow.x0+220),float(PickerRow.y0+110),loading?"Opening\xE2\x80\xA6":"Inkbound device test",ib::theme::InkPrimary);
 ib::text::draw(c,{ib::Face::Text,28},float(PickerRow.x0+220),float(PickerRow.y0+160),loading?"Preparing the first page":"PDF \xC2\xB7 2 pages",ib::theme::InkSecondary);}
inline void picker(spike::Surface s){ib::Canvas c(s);ib::Targets t;c.fill({0,0,s.width,s.height},ib::theme::Paper);
 ib::text::draw(c,{ib::Face::Subhead,46},float(ib::theme::Gutter),70.f,"Inkbound",ib::theme::InkPrimary);ib::iconButton(c,t,1,{1420,4,1600,100},ib::Icon::Close);c.hline(0,1620,104,ib::theme::Border);
 ib::text::draw(c,{ib::Face::Subhead,58},float(ib::theme::Gutter),300.f,"Documents",ib::theme::InkPrimary);pickerRow(s,false);
 ib::text::block(c,{ib::Face::SerifItalic,32},{ib::theme::Gutter,1840,1572,1960},"In a book, tap the bottom edge for controls. The Marker always writes.",ib::theme::InkSecondary,46,2);}
// Fixture-route chrome: the circled close and the footer (no toolbar without styled ink).
inline void overlay(spike::Surface s,const char* counter,bool loading=false){ib::Canvas c(s);ib::Targets t;ib::reader::Chrome m;m.title="Inkbound device test";m.loading=loading;m.search=false;
 int page=0,total=1;if(counter&&std::sscanf(counter,"%d / %d",&page,&total)==2){m.page=page-1;m.total=total;}m.chapter=m.title;m.menuMark=true;ib::reader::closeButton(c,t);ib::reader::footer(c,t,m);}
}
