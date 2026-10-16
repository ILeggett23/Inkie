// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Text highlights as blocks, the way a native reader draws them: the word quads of one text line join
// into one rectangle (the spaces between words included), padded evenly, and each line stays its own
// rectangle. Quads are page-space MuPDF quads (ul, ur, ll, lr as 8 numbers); `map` takes a page point to
// the surface and returns false when it cannot.
#include <QJsonArray>
#include <algorithm>
#include <cmath>
#include <vector>
namespace semantic {
struct Block {double x0{},y0{},x1{},y1{};};
template<class Map>std::vector<Block> lineBlocks(const QJsonArray& quads,Map map,double pad=2.0){
 std::vector<Block> boxes;boxes.reserve(std::size_t(quads.size()));bool across=true;
 for(auto value:quads){auto q=value.toArray();if(q.size()!=8)continue;Block b{1e18,1e18,-1e18,-1e18};bool ok=true;
  double ux{},uy{},rx{},ry{};
  for(int i=0;i<8&&ok;i+=2){double x{},y{};ok=map(q[i].toDouble(),q[i+1].toDouble(),x,y);if(i==0){ux=x;uy=y;}if(i==2){rx=x;ry=y;}b.x0=std::min(b.x0,x);b.y0=std::min(b.y0,y);b.x1=std::max(b.x1,x);b.y1=std::max(b.y1,y);}
  if(ok&&std::abs(ry-uy)>std::abs(rx-ux))across=false; /* text runs vertically on the surface (a rotated page) */
  if(ok&&b.x1>b.x0&&b.y1>b.y0)boxes.push_back(b);}
 // Rotated text: each word keeps its own block; lines are not joined (their boxes would line up across lines).
 if(!across){for(auto& b:boxes){b.x0-=pad;b.y0-=pad;b.x1+=pad;b.y1+=pad;}return boxes;}
 // Lines: sort strictly by vertical centre, then a box joins the current line when at least half of the
 // shorter height overlaps it. Within a line, left to right, a word-sized gap still joins one block.
 std::sort(boxes.begin(),boxes.end(),[](const Block& a,const Block& b){return a.y0+a.y1<b.y0+b.y1;});
 std::vector<Block> out;std::vector<Block> line;Block span{};
 auto flush=[&]{std::sort(line.begin(),line.end(),[](const Block& a,const Block& b){return a.x0<b.x0;});
  const std::size_t first=out.size(); /* blocks of this line only */
  for(const auto& b:line){if(out.size()>first&&b.x0-out.back().x1<=(span.y1-span.y0)*1.5){out.back().x1=std::max(out.back().x1,b.x1);continue;}out.push_back({b.x0,span.y0,b.x1,span.y1});}
  line.clear();};
 for(const auto& b:boxes){
  if(!line.empty()){const double overlap=std::min(span.y1,b.y1)-std::max(span.y0,b.y0),shorter=std::min(span.y1-span.y0,b.y1-b.y0);
   if(overlap>=shorter*0.5){line.push_back(b);span.y0=std::min(span.y0,b.y0);span.y1=std::max(span.y1,b.y1);continue;}flush();}
  line.push_back(b);span=b;}
 if(!line.empty())flush();
 for(auto& b:out){b.x0-=pad;b.y0-=pad;b.x1+=pad;b.y1+=pad;}
 return out;
}
}
