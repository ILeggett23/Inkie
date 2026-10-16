// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/handwriting.h"
#include "ib/text.h"
#include <algorithm>
#include <cmath>
#include <list>
#include <map>
#include <unordered_map>
namespace ib::hand {namespace {
// Zhang-Suen thinning of a binary mask (1 = ink) to a one-pixel centre line.
void thin(std::vector<std::uint8_t>& m,int w,int h){
 auto at=[&](int x,int y)->int{return x<0||y<0||x>=w||y>=h?0:m[std::size_t(y)*w+x];};
 std::vector<std::size_t> clear;bool changed=true;
 while(changed){changed=false;
  for(int pass=0;pass<2;++pass){clear.clear();
   for(int y=1;y+1<h;++y)for(int x=1;x+1<w;++x){if(!at(x,y))continue;
    const int p2=at(x,y-1),p3=at(x+1,y-1),p4=at(x+1,y),p5=at(x+1,y+1),p6=at(x,y+1),p7=at(x-1,y+1),p8=at(x-1,y),p9=at(x-1,y-1);
    const int b=p2+p3+p4+p5+p6+p7+p8+p9;if(b<2||b>6)continue;
    const int a=(!p2&&p3)+(!p3&&p4)+(!p4&&p5)+(!p5&&p6)+(!p6&&p7)+(!p7&&p8)+(!p8&&p9)+(!p9&&p2);if(a!=1)continue;
    if(pass==0?((p2&&p4&&p6)||(p4&&p6&&p8)):((p2&&p4&&p8)||(p2&&p6&&p8)))continue;
    clear.push_back(std::size_t(y)*w+x);}
   for(auto i:clear)m[i]=0;changed|=!clear.empty();}}
}
// Douglas-Peucker on a traced polyline.
void simplify(const std::vector<Pt>& in,std::size_t a,std::size_t b,float eps,std::vector<Pt>& out){
 float best=0;std::size_t at=a;const float dx=in[b].x-in[a].x,dy=in[b].y-in[a].y,len=std::max(1e-3f,std::hypot(dx,dy));
 for(std::size_t i=a+1;i<b;++i){const float d=std::abs(dy*(in[i].x-in[a].x)-dx*(in[i].y-in[a].y))/len;if(d>best){best=d;at=i;}}
 if(best>eps){simplify(in,a,at,eps,out);out.pop_back();simplify(in,at,b,eps,out);}else{out.push_back(in[a]);out.push_back(in[b]);}}
Word build(const std::string& utf8,float size){
 Word w;const Font f{Face::Hand,size};w.advance=text::measure(f,utf8);if(utf8.empty()||w.advance<=0)return w;
 // Rasterize at 2x: thin strokes thin to a cleaner centre line.
 const float s2=size*2;const Font big{Face::Hand,s2};const int pad=int(s2*0.25f)+4,asc=int(std::ceil(text::ascent(big))),desc=int(std::ceil(text::descent(big)));
 const int width=int(std::ceil(text::measure(big,utf8)))+2*pad,height=asc+desc+2*pad;if(width<=0||height<=0||std::int64_t(width)*height>4'000'000)return w;
 std::vector<std::uint32_t> px(std::size_t(width)*height,0xffffffffu);{Canvas c({px,width,height,width});text::draw(c,big,float(pad),float(pad+asc),utf8,0xff000000u);}
 std::vector<std::uint8_t> m(px.size());std::size_t inkPixels=0;for(std::size_t i=0;i<px.size();++i){m[i]=(px[i]&255u)<140u;inkPixels+=m[i];}
 if(!inkPixels)return w;thin(m,width,height);std::size_t skeleton=0;for(auto v:m)skeleton+=v;if(!skeleton)return w;
 w.pen=std::clamp(float(inkPixels)/float(skeleton)/2.f,1.4f,7.f); // average stroke width, back at 1x
 // Trace: start at the leftmost free end (or, for closed loops, the leftmost pixel), follow neighbours preferring the
 // straightest continuation, until no unvisited neighbour remains.
 auto at=[&](int x,int y){return x>=0&&y>=0&&x<width&&y<height&&m[std::size_t(y)*width+x];};
 auto neighbours=[&](int x,int y){int n=0;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if((dx||dy)&&at(x+dx,y+dy))++n;return n;};
 std::vector<std::vector<Pt>> raw;
 while(true){int sx=-1,sy=-1,ex=-1,ey=-1;
  for(int x=0;x<width&&sx<0;++x)for(int y=0;y<height;++y)if(at(x,y)){if(ex<0){ex=x;ey=y;}if(neighbours(x,y)<=1){sx=x;sy=y;break;}}
  if(ex<0)break;if(sx<0){sx=ex;sy=ey;}
  std::vector<Pt> line{{float(sx),float(sy)}};m[std::size_t(sy)*width+sx]=0;int x=sx,y=sy,ldx=0,ldy=0;
  while(true){int bx=0,by=0,score=-99;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if((!dx&&!dy)||!at(x+dx,y+dy))continue;const int sc=dx*ldx+dy*ldy-(dx&&dy?0:0);if(sc>score){score=sc;bx=dx;by=dy;}}
   if(score==-99)break;x+=bx;y+=by;ldx=bx;ldy=by;m[std::size_t(y)*width+x]=0;line.push_back({float(x),float(y)});}
  if(line.size()>=2||raw.empty())raw.push_back(std::move(line));}
 // Writing order: strokes by where they start, left to right; tiny specks (dots) keep their place in that order.
 std::stable_sort(raw.begin(),raw.end(),[](const auto& a,const auto& b){return a.front().x<b.front().x;});
 for(auto& line:raw){Stroke s;if(line.size()==1)line.push_back({line[0].x+0.5f,line[0].y});std::vector<Pt> simple;simplify(line,0,line.size()-1,0.9f,simple);
  for(auto& p:simple)s.points.push_back({(p.x-float(pad))/2.f,(p.y-float(pad+asc))/2.f});
  for(std::size_t i=1;i<s.points.size();++i)s.length+=std::hypot(s.points[i].x-s.points[i-1].x,s.points[i].y-s.points[i-1].y);
  s.length=std::max(s.length,0.5f);w.ink+=s.length;w.strokes.push_back(std::move(s));}
 return w;}
struct Cache {std::map<std::pair<std::string,int>,std::list<std::pair<std::pair<std::string,int>,Word>>::iterator> index;std::list<std::pair<std::pair<std::string,int>,Word>> order;};
Cache& cache(){static Cache c;return c;}
}
const Word& word(const std::string& utf8,float size){
 auto& c=cache();const std::pair<std::string,int> key{utf8,int(std::lround(size*2))};
 if(auto i=c.index.find(key);i!=c.index.end()){c.order.splice(c.order.begin(),c.order,i->second);return i->second->second;}
 c.order.emplace_front(key,build(utf8,float(key.second)/2.f));c.index[key]=c.order.begin();
 while(c.order.size()>768){c.index.erase(c.order.back().first);c.order.pop_back();}
 return c.order.front().second;}
float draw(Canvas& c,const Word& w,float x,float baseline,float ink,std::uint32_t color){
 float used=0;
 for(const auto& s:w.strokes){if(ink>=0&&used>=ink)break;Path path;path.move(x+s.points[0].x,baseline+s.points[0].y);float left=ink<0?s.length+1:ink-used;
  for(std::size_t i=1;i<s.points.size();++i){const auto& a=s.points[i-1];const auto& b=s.points[i];const float seg=std::hypot(b.x-a.x,b.y-a.y);
   if(seg<=left){path.line(x+b.x,baseline+b.y);left-=seg;continue;}
   const float t=seg>0?left/seg:0;path.line(x+a.x+(b.x-a.x)*t,baseline+a.y+(b.y-a.y)*t);left=0;break;}
  c.stroke(path,w.pen,color);used+=ink<0?s.length:std::min(s.length,ink-used);}
 return used;}
}
