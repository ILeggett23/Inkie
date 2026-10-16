// SPDX-License-Identifier: AGPL-3.0-only
// Block highlights: words of one line join (gaps included), lines and wide gutters stay apart,
// rotated text keeps one block per word.
#include "highlight_blocks.h"
#include <cstdio>
#include <source_location>
#include <stdexcept>
#include <string>
static void check(bool b,std::source_location at=std::source_location::current()){if(!b)throw std::runtime_error("highlight blocks failure at line "+std::to_string(at.line()));}
static QJsonArray quad(double x0,double y0,double x1,double y1){return {x0,y0,x1,y0,x0,y1,x1,y1};} // ul, ur, ll, lr
static bool near(double a,double b){return std::abs(a-b)<1e-6;}
int main(){try{
 auto identity=[](double x,double y,double& sx,double& sy){sx=x;sy=y;return true;};
 // One line, three words with word gaps: one block, padded by 2.
 {QJsonArray q{quad(100,200,180,240),quad(190,200,260,240),quad(270,201,330,239)};auto b=semantic::lineBlocks(q,identity);
  check(b.size()==1&&near(b[0].x0,98)&&near(b[0].x1,332)&&near(b[0].y0,198)&&near(b[0].y1,242));}
 // Two lines: two blocks, in reading order, each spanning its own words only.
 {QJsonArray q{quad(300,260,400,300),quad(100,200,200,240),quad(210,200,280,240),quad(100,260,290,300)};auto b=semantic::lineBlocks(q,identity);
  check(b.size()==2&&near(b[0].y0,198)&&near(b[0].x0,98)&&near(b[0].x1,282)&&near(b[1].y0,258)&&near(b[1].x0,98)&&near(b[1].x1,402));}
 // Gaps: 1.2 x line height joins; 2 x line height (a column gutter) stays apart.
 {const double h=40;QJsonArray joined{quad(100,200,200,240),quad(200+1.2*h,200,300+1.2*h,240)};check(semantic::lineBlocks(joined,identity).size()==1);
  QJsonArray gutter{quad(100,200,200,240),quad(200+2*h,200,300+2*h,240)};check(semantic::lineBlocks(gutter,identity).size()==2);}
 // A 90 degree page: two text lines of two words each become four blocks, never joined across lines.
 {auto rotate=[](double x,double y,double& sx,double& sy){sx=1000-y;sy=x;return true;};
  QJsonArray q{quad(100,200,180,240),quad(190,200,260,240),quad(100,250,180,290),quad(190,250,260,290)};auto b=semantic::lineBlocks(q,rotate);
  check(b.size()==4);for(const auto& block:b)check(block.x1-block.x0<=44&&block.y1-block.y0<=84);}
 // Unmappable quads are skipped; nothing in, nothing out.
 {auto refuse=[](double,double,double&,double&){return false;};check(semantic::lineBlocks(QJsonArray{quad(1,1,2,2)},refuse).empty()&&semantic::lineBlocks(QJsonArray{},identity).empty());}
 std::puts("HIGHLIGHT_BLOCKS_PASS line_join=true reading_order=true gutter=true rotated_per_word=true");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"HIGHLIGHT_BLOCKS_FAIL %s\n",e.what());return 1;}}
