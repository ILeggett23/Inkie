/* SPDX-License-Identifier: AGPL-3.0-only */
/* Line-aware raster-to-native conversion (converter r1). Thin dark or colored structures become centerline pen strokes, larger areas become a bounded set of fills, speckle and antialias fringes merge into their surroundings, and acceptance is structural rather than pixel-exact. */
#include "ink_trace.h"
#include "product_services.h"
#include "styled_ink.h"
#include <optional>
#include <QStringList>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace tomink {namespace {
constexpr unsigned char Black=0,None=8,DarkGray=9,LightGray=10,TintBase=10,Labels=18; /* 0..7 palette, 8 paper, 9/10 grays, 11..17 light tints of palette 1..7 */
constexpr std::uint16_t Far=65535;
const int DX[8]={0,1,1,1,0,-1,-1,-1},DY[8]={-1,-1,0,1,1,1,0,-1}; /* N,NE,E,SE,S,SW,W,NW */
const int Order[8]={0,2,4,6,1,3,5,7}; /* 4-neighbours first so walks follow staircases instead of cutting them */
struct Params {unsigned minArea{},spur{};double lineEps{},fillEps{},thinRatio{.9},toneShare{};int gap{};};
Params params(Detail d,double fillEps){return d==Detail::QuickSketch?Params{16,5,1.0,std::max(fillEps,.7),.9,1/150.0,5}:d==Detail::Standard?Params{8,4,.7,std::max(fillEps,.5),.9,1/400.0,4}:d==Detail::Detailed?Params{5,3,.5,std::max(fillEps,.4),.9,1/900.0,3}:Params{4,3,.4,std::max(fillEps,.35),.9,1/1600.0,3};} /* toneShare: largest shading island, as a share of the raster, that joins its enclosing tone */
double hueOf(int r,int g,int b){int mx=std::max({r,g,b}),mn=std::min({r,g,b});double c=mx-mn;if(c<=0)return 0;double h=mx==r?std::fmod((g-b)/c,6.0):mx==g?(b-r)/c+2:(r-g)/c+4;h*=60;return h<0?h+360:h;}
std::array<double,3> lab(double r,double g,double b){static const auto lin=[]{std::array<double,256> t{};for(int i=0;i<256;++i){double c=i/255.0;t[std::size_t(i)]=c<=.04045?c/12.92:std::pow((c+.055)/1.055,2.4);}return t;}();
 auto at=[&](double v){return lin[std::size_t(std::clamp(int(std::lround(v)),0,255))];};double R=at(r),G=at(g),B=at(b);auto f=[](double t){return t>.008856?std::cbrt(t):7.787*t+16.0/116;};
 double fx=f((.4124*R+.3576*G+.1805*B)/.95047),fy=f(.2126*R+.7152*G+.0722*B),fz=f((.0193*R+.1192*G+.9505*B)/1.08883);return {116*fy-16,500*(fx-fy),200*(fy-fz)};}
/* A pixel takes the palette colour that, laid down at the opacity matching its lightness, comes closest in Lab, but only when that is closer than plain gray of the same lightness: pale pastels stay coloured, warm gray-browns become gray, light blue stays a blue tint, orange-red stays red. Hue names settle the neighbouring families the palette places unevenly: yellow against green at about 68 degrees (not the midpoint), red against orange at 16, so tan and brown stay orange. */
struct Classifier {std::array<double,8> hues{},luma{};std::array<std::array<double,3>,8> rgb{};int yellow{-1},green{-1},red{-1},orange{-1};mutable std::vector<unsigned char> memo=std::vector<unsigned char>(std::size_t(1)<<18,255);
 static constexpr double ChromaFloor=6,FullOpacity=.7,YellowGreen=68,RedOrange=16;
 double opacity(int k,double y)const{return std::clamp((255-y)/std::max(1.0,255-luma[std::size_t(k)]),0.0,1.0);}
 unsigned char colour(int r,int g,int b)const{auto L=lab(r,g,b);double C=std::hypot(L[1],L[2]);if(C<ChromaFloor)return 0;double y=.2126*r+.7152*g+.0722*b,bd=C;int best=-1;
  for(int k=1;k<8;++k){double a=opacity(k,y);auto& c=rgb[std::size_t(k)];auto q=lab(255-a*(255-c[0]),255-a*(255-c[1]),255-a*(255-c[2]));double d=std::hypot(q[0]-L[0],q[1]-L[1],q[2]-L[2]);if(d<bd){bd=d;best=k;}}
  if(best<0)return 0;double h=hueOf(r,g,b);if(best==yellow&&green>0&&h>=YellowGreen&&h<170)best=green;else if(best==green&&yellow>0&&h<YellowGreen&&h>=40)best=yellow;else if(best==red&&orange>0&&h>=RedOrange&&h<60)best=orange;else if(best==orange&&red>0&&(h<RedOrange||h>=300))best=red;
  return static_cast<unsigned char>(opacity(best,y)>=FullOpacity?best:TintBase+best);}
 unsigned char operator()(int r,int g,int b)const{int mn=std::min({r,g,b});double y=.2126*r+.7152*g+.0722*b;if(mn>236)return None; /* no luma shortcut to black: pure red (luma 54) and pure blue are colours; dark navy or brown still lose to gray and become black */
  auto& m=memo[std::size_t((r>>2)<<12|(g>>2)<<6|(b>>2))];if(m==255)m=colour((r&~3)+2,(g&~3)+2,(b&~3)+2); /* 6 bits per channel is far below what the decision can resolve */
  return m?m:y<80?Black:y<150?DarkGray:y<222?LightGray:None;}};
bool inside(int x,int y,int w,int h){return x>=0&&y>=0&&x<w&&y<h;}
/* Deepest luminance valley through (x,y): in any of four directions, how much darker it is than the brighter of the pixels at distance 1 and 2 on each side, so one- and two-pixel lines score their full contrast. */
int valley(const std::vector<unsigned char>& lum,int w,int h,int x,int y){auto at=[&](int u,int v){return int(lum[std::size_t(std::clamp(v,0,h-1))*w+std::clamp(u,0,w-1)]);};const int VX[4]={1,0,1,1},VY[4]={0,1,1,-1};int best=0,c=at(x,y);for(int k=0;k<4;++k){int a=std::max(at(x-VX[k],y-VY[k]),at(x-2*VX[k],y-2*VY[k])),b=std::max(at(x+VX[k],y+VY[k]),at(x+2*VX[k],y+2*VY[k]));best=std::max(best,std::min(a,b)-c);}return best;}
/* Merge components that are too small, or too thin to be anything but antialias fringe, into their most common neighbour. */
unsigned cleanup(std::vector<unsigned char>& L,int w,int h,unsigned minArea,double thinRatio,const std::atomic<bool>* stop){std::vector<unsigned char> seen(L.size());std::vector<int> pixels;unsigned merged=0;
 for(int start=0;start<w*h;++start){if(seen[start]||L[start]==None)continue;if(start%65536==0)nativeink::checkCancelled(stop);auto label=L[start];pixels.clear();pixels.push_back(start);seen[start]=1;std::array<unsigned,Labels> around{};unsigned perimeter=0;
  for(std::size_t i=0;i<pixels.size();++i){int p=pixels[i],x=p%w,y=p/w;for(int k=0;k<8;k+=2){int nx=x+DX[k],ny=y+DY[k];if(!inside(nx,ny,w,h)){++perimeter;continue;}int q=ny*w+nx;if(L[q]==label){if(!seen[q]){seen[q]=1;pixels.push_back(q);}}else{++perimeter;++around[L[q]];}}}
  bool thin=thinRatio>0&&perimeter&&double(pixels.size())/perimeter<thinRatio;if(pixels.size()>=minArea&&!thin)continue;
  auto target=static_cast<unsigned char>(std::max_element(around.begin(),around.end())-around.begin());if(!around[target])target=None;for(int p:pixels)L[p]=target;++merged;}
 return merged;}
/* Shading islands: a region of one tone that is small and mostly enclosed by its sibling tone (a full colour inside its own tint or the reverse, dark gray inside light gray or the reverse, light gray inside a tint) takes that tone, so shaded skin, fur and petals read as a few even regions instead of blotches. Pixels already drawn as strokes (None) do not count against enclosure. */
bool sibling(unsigned char a,unsigned char b){if(a==b||a==None||b==None||a==Black||b==Black)return false;auto hue=[](unsigned char k){return k>TintBase?k-TintBase:k<8?int(k):-1;};if(hue(a)>0&&hue(a)==hue(b))return true;return (a==DarkGray&&b==LightGray)||(a==LightGray&&(b==DarkGray||b>TintBase));}
unsigned tones(std::vector<unsigned char>& L,int w,int h,unsigned maxArea,const std::atomic<bool>* stop){std::vector<unsigned char> seen(L.size());std::vector<int> pixels;unsigned merged=0;
 for(int start=0;start<w*h;++start){if(seen[start]||L[start]==None||L[start]==Black)continue;if(start%65536==0)nativeink::checkCancelled(stop);auto label=L[start];pixels.clear();pixels.push_back(start);seen[start]=1;std::array<unsigned,Labels> around{};unsigned perimeter=0,open=0;
  for(std::size_t i=0;i<pixels.size();++i){int p=pixels[i],x=p%w,y=p/w;for(int k=0;k<8;k+=2){int nx=x+DX[k],ny=y+DY[k];if(!inside(nx,ny,w,h)){++perimeter;continue;}int q=ny*w+nx;if(L[q]==label){if(!seen[q]){seen[q]=1;pixels.push_back(q);}}else{++perimeter;if(L[q]==None)++open;else ++around[L[q]];}}}
  if(pixels.size()>maxArea)continue;unsigned closed=perimeter-open;if(closed*3<perimeter)continue;
  auto target=static_cast<unsigned char>(std::max_element(around.begin(),around.end())-around.begin());if(around[target]*2<closed||!sibling(label,target))continue;for(int p:pixels)L[p]=target;++merged;}
 return merged;}
/* Two-pass 3-4 chamfer distance; d holds 0 at sources and Far elsewhere. Units are thirds of a pixel. */
void chamfer(std::vector<std::uint16_t>& d,int w,int h,bool outsideIsSource){std::uint16_t edge=outsideIsSource?0:Far;auto get=[&](int x,int y){return inside(x,y,w,h)?d[std::size_t(y)*w+x]:edge;};auto add=[](std::uint16_t v,int a){return static_cast<std::uint16_t>(std::min<int>(Far,v+a));};
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto& v=d[std::size_t(y)*w+x];if(!v)continue;v=std::min({v,add(get(x-1,y),3),add(get(x,y-1),3),add(get(x-1,y-1),4),add(get(x+1,y-1),4)});}
 for(int y=h-1;y>=0;--y)for(int x=w-1;x>=0;--x){auto& v=d[std::size_t(y)*w+x];if(!v)continue;v=std::min({v,add(get(x+1,y),3),add(get(x,y+1),3),add(get(x+1,y+1),4),add(get(x-1,y+1),4)});}}
/* Zhang-Suen thinning inside a bounding box. */
void thin(std::vector<unsigned char>& t,int w,int h,int x0,int y0,int x1,int y1,const std::atomic<bool>* stop){auto at=[&](int x,int y){return inside(x,y,w,h)&&t[std::size_t(y)*w+x];};std::vector<int> kill;
 for(int pass=0;pass<64;++pass){bool changed=false;for(int sub=0;sub<2;++sub){kill.clear();for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){if(!t[std::size_t(y)*w+x])continue;int p[8],b=0,a=0;for(int k=0;k<8;++k){p[k]=at(x+DX[k],y+DY[k]);b+=p[k];}if(b<2||b>6)continue;for(int k=0;k<8;++k)a+=!p[k]&&p[(k+1)%8];if(a!=1)continue;
   if(sub==0?(p[0]*p[2]*p[4]==0&&p[2]*p[4]*p[6]==0):(p[0]*p[2]*p[6]==0&&p[0]*p[4]*p[6]==0))kill.push_back(y*w+x);}for(int i:kill)t[i]=0;changed|=!kill.empty();}nativeink::checkCancelled(stop);if(!changed)break;}}
int crossing(const std::vector<unsigned char>& s,int w,int h,int p){int x=p%w,y=p/w,n[8];for(int k=0;k<8;++k){int nx=x+DX[k],ny=y+DY[k];n[k]=inside(nx,ny,w,h)&&s[std::size_t(ny)*w+nx];}int c=0;for(int k=0;k<8;++k)c+=!n[k]&&n[(k+1)%8];return c;}
int neighbour(int p,int k,int w,int h){int x=p%w+DX[k],y=p/w+DY[k];return inside(x,y,w,h)?y*w+x:-1;}
/* Remove short skeleton spurs that end at a junction (thinning noise). */
void prune(std::vector<unsigned char>& s,int w,int h,const std::vector<int>& pixels,unsigned spur){for(int round=0;round<2;++round)for(int e:pixels){if(!s[e]||crossing(s,w,h,e)!=1)continue;std::vector<int> path{e};int prev=-1,cur=e;bool junction=false;
  while(path.size()<=spur){int next=-1;for(int k:Order){int q=neighbour(cur,k,w,h);if(q<0||!s[q]||q==prev||std::find(path.begin(),path.end(),q)!=path.end())continue;next=q;break;}if(next<0)break;if(crossing(s,w,h,next)>=3){junction=true;break;}path.push_back(next);prev=cur;cur=next;}
  if(junction)for(int p:path)s[p]=0;}}
/* Bridge dashes: two skeleton ends that face each other across a gap of at most a few pixels, each pointing at the other within about 35 degrees, are joined, so an outline that faded in and out becomes one stroke. Only end-to-end joins, so lines never snap onto neighbours. */
unsigned bridge(std::vector<unsigned char>& s,int w,int h,std::vector<int>& pixels,int gap){struct End {int p;QPointF dir;bool used{};};std::vector<End> ends;std::unordered_map<int,int> at;
 for(int e:pixels){if(!s[std::size_t(e)]||crossing(s,w,h,e)!=1)continue;std::vector<int> path{e};int prev=-1,cur=e;while(path.size()<7){int next=-1;for(int k:Order){int q=neighbour(cur,k,w,h);if(q<0||!s[std::size_t(q)]||q==prev||std::find(path.begin(),path.end(),q)!=path.end())continue;next=q;break;}if(next<0||crossing(s,w,h,next)>=3)break;path.push_back(next);prev=cur;cur=next;}
  if(path.size()<3)continue;QPointF d(e%w-path.back()%w,e/w-path.back()/w);double l=std::hypot(d.x(),d.y());at[e]=int(ends.size());ends.push_back({e,d/l});}
 std::vector<std::tuple<double,int,int>> pairs;
 for(int a=0;a<int(ends.size());++a){int ax=ends[std::size_t(a)].p%w,ay=ends[std::size_t(a)].p/w;for(int dy=-gap;dy<=gap;++dy)for(int dx=-gap;dx<=gap;++dx){if(!inside(ax+dx,ay+dy,w,h))continue;auto f=at.find((ay+dy)*w+ax+dx);if(f==at.end()||f->second<=a)continue;double d=std::hypot(dx,dy);if(d<1.5||d>gap+.5)continue;QPointF v(dx/d,dy/d);auto& A=ends[std::size_t(a)];auto& B=ends[std::size_t(f->second)];
   if(QPointF::dotProduct(A.dir,v)>=.82&&QPointF::dotProduct(B.dir,-v)>=.82)pairs.push_back({d,a,f->second});}}
 std::sort(pairs.begin(),pairs.end());unsigned joined=0;
 for(auto& [d,a,b]:pairs){auto& A=ends[std::size_t(a)];auto& B=ends[std::size_t(b)];if(A.used||B.used)continue;A.used=B.used=true;int ax=A.p%w,ay=A.p/w,bx=B.p%w,by=B.p/w,n=std::max(std::abs(bx-ax),std::abs(by-ay));
  for(int i=1;i<n;++i){int q=int(std::lround(ay+(by-ay)*double(i)/n))*w+int(std::lround(ax+(bx-ax)*double(i)/n));if(!s[std::size_t(q)]){s[std::size_t(q)]=1;pixels.push_back(q);}}++joined;}
 return joined;}
/* Walk the skeleton into polylines: node to node, then closed loops, then isolated dots. */
std::vector<std::vector<int>> walk(std::vector<unsigned char>& s,int w,int h,const std::vector<int>& pixels){std::vector<unsigned char> node(s.size()),used(s.size());for(int p:pixels)if(s[p])node[p]=crossing(s,w,h,p)!=2;std::vector<std::vector<int>> out;
 auto follow=[&](std::vector<int>& path,int start){int prev=path.size()>1?path[path.size()-2]:-1,cur=path.back();for(;;){int next=-1;for(int k:Order){int q=neighbour(cur,k,w,h);if(q>=0&&s[q]&&q!=prev&&!node[q]&&!used[q]){next=q;break;}}
   if(next<0){for(int k:Order){int q=neighbour(cur,k,w,h);if(q>=0&&s[q]&&q!=prev&&node[q]&&!(q==start&&path.size()<3)){path.push_back(q);break;}}return;}used[next]=1;path.push_back(next);prev=cur;cur=next;}};
 std::vector<unsigned char> touched(s.size());
 for(int n:pixels){if(!s[n]||!node[n])continue;for(int k:Order){int q=neighbour(n,k,w,h);if(q<0||!s[q]||node[q]||used[q])continue;used[q]=1;std::vector<int> path{n,q};follow(path,n);touched[n]=1;touched[path.back()]=1;out.push_back(std::move(path));}}
 for(int p:pixels){if(!s[p]||node[p]||used[p])continue;used[p]=1;std::vector<int> path{p};follow(path,p);for(int k:Order){int q=neighbour(path.back(),k,w,h);if(q==p&&path.size()>2){path.push_back(p);break;}}out.push_back(std::move(path));}
 for(int p:pixels)if(s[p]&&node[p]&&!touched[p]&&crossing(s,w,h,p)==0)out.push_back({p});
 return out;}
/* Join polylines that meet at a junction and continue in nearly the same direction (within about 50 degrees), so the stroke budget is spent on whole lines instead of fragments. */
std::vector<std::vector<int>> stitch(std::vector<std::vector<int>> paths,const std::vector<unsigned char>& s,int w,int h){std::vector<int> cluster(s.size(),-1),stack;int clusters=0;auto junction=[&](int p){return s[p]&&crossing(s,w,h,p)>=3;};
 for(auto& path:paths)for(int e:{path.front(),path.back()})if(cluster[e]<0&&junction(e)){stack={e};cluster[e]=clusters;while(!stack.empty()){int q0=stack.back();stack.pop_back();for(int k=0;k<8;++k){int q=neighbour(q0,k,w,h);if(q>=0&&cluster[q]<0&&junction(q)){cluster[q]=clusters;stack.push_back(q);}}}++clusters;}
 struct End {int path,side;QPointF dir;};std::vector<std::vector<End>> at(static_cast<std::size_t>(clusters));
 auto direction=[&](const std::vector<int>& p,int side){int n=int(p.size()),k=std::min(6,n-1),a=side?p[std::size_t(n-1)]:p[0],b=side?p[std::size_t(n-1-k)]:p[std::size_t(k)];QPointF d(a%w-b%w,a/w-b/w);double l=std::hypot(d.x(),d.y());return l>0?d/l:QPointF();};
 for(int i=0;i<int(paths.size());++i){auto& p=paths[std::size_t(i)];if(p.size()<2)continue;for(int side=0;side<2;++side){int e=side?p.back():p.front();if(cluster[e]>=0)at[std::size_t(cluster[e])].push_back({i,side,direction(p,side)});}}
 std::vector<std::array<int,2>> link(paths.size(),{-1,-1});
 for(auto& ends:at){std::vector<std::tuple<double,int,int>> pairs;for(int a=0;a<int(ends.size());++a)for(int b=a+1;b<int(ends.size());++b){if(ends[std::size_t(a)].path==ends[std::size_t(b)].path)continue;double c=-QPointF::dotProduct(ends[std::size_t(a)].dir,ends[std::size_t(b)].dir);if(c>.64)pairs.push_back({-c,a,b});}
  std::sort(pairs.begin(),pairs.end());for(auto& [c,a,b]:pairs){auto& A=ends[std::size_t(a)];auto& B=ends[std::size_t(b)];if(link[std::size_t(A.path)][std::size_t(A.side)]>=0||link[std::size_t(B.path)][std::size_t(B.side)]>=0)continue;link[std::size_t(A.path)][std::size_t(A.side)]=B.path*2+B.side;link[std::size_t(B.path)][std::size_t(B.side)]=A.path*2+A.side;}}
 std::vector<unsigned char> done(paths.size());std::vector<std::vector<int>> out;
 auto build=[&](int start,int enter){std::vector<int> chain;int cur=start;while(cur>=0&&!done[std::size_t(cur)]){done[std::size_t(cur)]=1;auto& p=paths[std::size_t(cur)];std::size_t skip=chain.empty()?0:1;if(enter==0)chain.insert(chain.end(),p.begin()+long(std::min(skip,p.size())),p.end());else chain.insert(chain.end(),p.rbegin()+long(std::min(skip,p.size())),p.rend());int l=link[std::size_t(cur)][std::size_t(1-enter)];if(l<0)break;cur=l/2;enter=l%2;}return chain;};
 for(int i=0;i<int(paths.size());++i){if(done[std::size_t(i)])continue;if(link[std::size_t(i)][0]<0)out.push_back(build(i,0));else if(link[std::size_t(i)][1]<0)out.push_back(build(i,1));}
 for(int i=0;i<int(paths.size());++i)if(!done[std::size_t(i)])out.push_back(build(i,0));
 return out;}
double reach(QPointF p,QPointF a,QPointF b){auto d=b-a;double v=QPointF::dotProduct(d,d),t=v?std::clamp(QPointF::dotProduct(p-a,d)/v,0.0,1.0):0;return std::hypot((p-a-d*t).x(),(p-a-d*t).y());}
std::vector<QPointF> simplify(const std::vector<QPointF>& p,double epsilon){if(p.size()<3)return p;std::vector<unsigned char> keep(p.size());std::vector<std::pair<std::size_t,std::size_t>> stack{{0,p.size()-1}};keep.front()=keep.back()=1;std::uint64_t work=0;
 while(!stack.empty()){auto [a,b]=stack.back();stack.pop_back();double best=epsilon;std::size_t at=a;for(auto n=a+1;n<b;++n){product::require(++work<=64000000,"visual-ink-simplify-capacity");double d=reach(p[n],p[a],p[b]);if(d>best){best=d;at=n;}}if(at>a){keep[at]=1;stack.push_back({a,at});stack.push_back({at,b});}}
 std::vector<QPointF> out;for(std::size_t n=0;n<p.size();++n)if(keep[n])out.push_back(p[n]);return out;}
std::vector<QPointF> corners(const std::vector<QPointF>& ring){if(ring.size()<4)return ring;std::vector<QPointF> out;for(std::size_t i=0;i<ring.size();++i){auto a=ring[(i+ring.size()-1)%ring.size()],b=ring[i],c=ring[(i+1)%ring.size()];if(std::abs((b-a).x()*(c-b).y()-(b-a).y()*(c-b).x())>1e-9)out.push_back(b);}return out.size()>=3?out:ring;}
double area(const std::vector<QPointF>& p){double a=0;for(std::size_t n=0;n<p.size();++n){auto b=p[(n+1)%p.size()];a+=p[n].x()*b.y()-b.x()*p[n].y();}return a/2;}
struct Item {QJsonObject style;std::vector<std::vector<QPointF>> raster;std::vector<int> pixels;bool line{};double importance{},opacity{1};unsigned points{};QPointF anchor;unsigned color{};QString component;std::uint32_t source{};}; /* geometry stays in raster space; JSON is built only for kept items */
}
namespace {
struct PaletteData{QStringList names;Classifier classify;std::array<double,8> luma{};};
PaletteData tracePalette(){ auto palette=product::palette();PaletteData result;auto& names=result.names;auto& classify=result.classify;auto& paletteY=result.luma;for(int i=0;i<palette.size()&&i<8;++i){auto p=palette[i].toObject();bool ok=false;unsigned rgb=p["rgb"].toString().mid(1).toUInt(&ok,16);product::require(ok,"visual-ink-palette");names.append(p["id"].toString());classify.hues[i]=hueOf(int((rgb>>16)&255),int((rgb>>8)&255),int(rgb&255));paletteY[i]=.2126*((rgb>>16)&255)+.7152*((rgb>>8)&255)+.0722*(rgb&255);classify.luma[i]=paletteY[i];classify.rgb[i]={double((rgb>>16)&255),double((rgb>>8)&255),double(rgb&255)};}
 classify.yellow=int(names.indexOf("yellow"));classify.green=int(names.indexOf("green"));classify.red=int(names.indexOf("red"));classify.orange=int(names.indexOf("orange"));
 product::require(names.size()==8&&names[0]=="black","visual-ink-palette");return result;}
struct RasterData{std::vector<unsigned char> labels,luma;unsigned paper{};};
RasterData sourceRaster(const QImage& image,Classifier& classify,const std::atomic<bool>* stop,std::vector<unsigned char>* removedPaper=nullptr){const int w=image.width(),h=image.height(); /* 1. classify */
 RasterData result{std::vector<unsigned char>(std::size_t(w)*h,None),std::vector<unsigned char>(std::size_t(w)*h,255),0};auto& L0=result.labels;auto& lum=result.luma;if(removedPaper)removedPaper->assign(L0.size(),0);
 for(int y=0;y<h;++y){nativeink::checkCancelled(stop);auto line=reinterpret_cast<const QRgb*>(image.constScanLine(y));for(int x=0;x<w;++x){auto px=line[x];int a=qAlpha(px),r=(qRed(px)*a+255*(255-a))/255,g=(qGreen(px)*a+255*(255-a))/255,b=(qBlue(px)*a+255*(255-a))/255;L0[std::size_t(y)*w+x]=classify(r,g,b);lum[std::size_t(y)*w+x]=static_cast<unsigned char>(std::lround(.2126*r+.7152*g+.0722*b));}}
 /* Paper detection: a pale tone (light gray or tint) that dominates a thin band inside the image border (ignoring white) is the paper, not ink; clear it where it connects to that band. */
 {const int band=std::max(3,std::min(w,h)/50);std::array<unsigned,Labels> tally{};unsigned inked=0,total=0;std::vector<int> seeds;
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){if(x>=band&&y>=band&&x<w-band&&y<h-band)continue;int p=y*w+x;++total;auto k=L0[std::size_t(p)];if(k==None)continue;++inked;++tally[k];seeds.push_back(p);}
  std::array<bool,Labels> paper{};unsigned pale=0;for(unsigned char k=0;k<Labels;++k)if((k==LightGray||k>TintBase)&&tally[k]*100>=inked*15){paper[k]=true;pale+=tally[k];} /* aged paper can straddle two hues, so pale tones count as one family */
  if(pale*10>inked*6&&inked*4>total){std::vector<int> stack;auto seed=[&](int p){if(paper[L0[std::size_t(p)]]){L0[std::size_t(p)]=None;if(removedPaper)(*removedPaper)[std::size_t(p)]=1;stack.push_back(p);}};for(int p:seeds)seed(p);
   while(!stack.empty()){int q0=stack.back();stack.pop_back();++result.paper;if(result.paper%65536==0)nativeink::checkCancelled(stop);for(int k=0;k<8;k+=2){int q=neighbour(q0,k,w,h);if(q>=0)seed(q);}}}}return result;}
struct TraceReference{int width{},height{};std::vector<unsigned char> ink,nearInk,lines;unsigned inkCount{},lineCount{};};
TraceReference traceReference(const QImage& image,const std::atomic<bool>* stop){auto palette=tracePalette();std::vector<unsigned char> paper;auto source=sourceRaster(image,palette.classify,stop,&paper);TraceReference result;result.width=image.width();result.height=image.height();result.ink=std::move(source.labels);result.nearInk.resize(result.ink.size());result.lines.resize(result.ink.size());
 for(int y=0;y<result.height;++y){nativeink::checkCancelled(stop);for(int x=0;x<result.width;++x){auto p=std::size_t(y)*result.width+x;bool line=!paper[p]&&source.luma[p]<235&&valley(source.luma,result.width,result.height,x,y)>=32;result.lines[p]=line;result.lineCount+=line;result.ink[p]=!paper[p]&&(result.ink[p]!=None||line);result.inkCount+=result.ink[p];}}
 for(int y=0;y<result.height;++y){nativeink::checkCancelled(stop);for(int x=0;x<result.width;++x)if(result.ink[std::size_t(y)*result.width+x])for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx)if(inside(x+dx,y+dy,result.width,result.height))result.nearInk[std::size_t(y+dy)*result.width+std::size_t(x+dx)]=1;}return result;}
struct ReferenceScore{double lines{},recall{},precision{},score{};};
/* Compare both candidates to the same original pixels through the actual native renderer. This is structural fidelity, not semantic or anatomical certification. */
std::optional<ReferenceScore> referenceScore(const TraceReference& reference,const QJsonArray& strokes,const IllustrationOptions& o,const std::atomic<bool>* stop){const int w=reference.width,h=reference.height;const double scale=std::min(o.width/w,o.height/h),ox=o.x+(o.width-w*scale)/2,oy=o.y+(o.height-h*scale)/2;styled::Matrix matrix;matrix.value={1/scale,0,0,1/scale,-ox/scale,-oy/scale};styled::Raster raster(w,h,w);
 for(auto value:strokes){nativeink::checkCancelled(stop);auto stroke=value.toObject();if(stroke["shape"]=="polygon"){try{raster.fill(stroke,matrix);}catch(const std::runtime_error& e){if(QByteArray(e.what())=="ink-fill-work-capacity")return std::nullopt;throw;}}else{if(!raster.begin(styled::parseStyle(stroke["style"].toObject()),matrix))return std::nullopt;bool first=true;for(auto value:stroke["points"].toArray()){auto p=value.toArray();if(!raster.point({p[0].toDouble(),p[1].toDouble()},first))return std::nullopt;first=false;}raster.end();}}
 unsigned lineHit=0,inkHit=0,drawn=0,precise=0;for(int y=0;y<h;++y){nativeink::checkCancelled(stop);for(int x=0;x<w;++x){auto p=std::size_t(y)*w+x;auto pixel=styled::compositeInk(0xffffffff,raster.pixels()[p]);bool visible=std::min({qRed(pixel),qGreen(pixel),qBlue(pixel)})<236;lineHit+=reference.lines[p]&&visible;inkHit+=reference.ink[p]&&visible;drawn+=visible;precise+=reference.nearInk[p]&&visible;}}
 ReferenceScore result;result.lines=reference.lineCount?double(lineHit)/reference.lineCount:1;result.recall=reference.inkCount?double(inkHit)/reference.inkCount:1;result.precision=drawn?double(precise)/drawn:1;result.score=result.lines+(result.recall+result.precision?2*result.recall*result.precision/(result.recall+result.precision):0);return result;}
/* 20z exact color: a kept item's ink is the mean color of the source pixels it covers (the darker half for a line, whose edge
   pixels are anti-aliased toward the paper), compensated for the tool's fixed opacity over paper so the drawn result lands on
   that color; unreachable channels clamp, and tools with textured coverage use the mean itself. The classified ink name stays. */
std::uint32_t paperRgb(const QImage& image,int x,int y){const auto px=reinterpret_cast<const QRgb*>(image.constScanLine(y))[x];const int a=qAlpha(px);auto c=[&](int v){return std::uint32_t((v*a+255*(255-a))/255);};return c(qRed(px))<<16|c(qGreen(px))<<8|c(qBlue(px));}
std::uint32_t sourceInk(const QImage& image,int w,int h,const std::vector<int>& pixels,const std::vector<QPointF>& path,bool line,const QString& tool,double opacity){
 std::vector<std::uint32_t> samples;samples.reserve(pixels.empty()?path.size():pixels.size());
 if(!pixels.empty()){for(int p:pixels)if(p>=0&&p<w*h)samples.push_back(paperRgb(image,p%w,p/w));}else for(auto q:path)samples.push_back(paperRgb(image,std::clamp(int(q.x()),0,w-1),std::clamp(int(q.y()),0,h-1)));
 if(samples.empty())return 0;
 auto luma=[](std::uint32_t c){return .2126*((c>>16)&255)+.7152*((c>>8)&255)+.0722*(c&255);};
 std::size_t n=samples.size();if(line&&n>1){n=(n+1)/2;std::nth_element(samples.begin(),samples.begin()+std::ptrdiff_t(n-1),samples.end(),[&](auto a,auto b){return luma(a)<luma(b);});}
 double sum[3]{};for(std::size_t i=0;i<n;++i){sum[0]+=(samples[i]>>16)&255;sum[1]+=(samples[i]>>8)&255;sum[2]+=samples[i]&255;}
 double alpha=1;if(!line)alpha=(tool=="highlighter"?191:tool=="shader"?153:tool=="marker"?242:255)/255.0*std::clamp(opacity,0.05,1.0);else if(tool=="marker")alpha=.95;else if(tool=="highlighter")alpha=.75;
 std::uint32_t out=0;for(int k=0;k<3;++k){const double mean=sum[k]/double(n),c=(mean-(1-alpha)*255)/alpha;out|=std::uint32_t(std::lround(std::clamp(c,0.0,255.0)))<<(16-8*k);}return out;}
QJsonArray traceOnce(const QImage& image,const IllustrationOptions& o,const VisualBudget& budget,const std::atomic<bool>* stop,double coarsen,IllustrationStats& stats,bool& byteBound){
 const int w=image.width(),h=image.height();product::require(w>0&&h>0,"visual-ink-image");const double scale=std::min(o.width/w,o.height/h),ox=o.x+(o.width-w*scale)/2,oy=o.y+(o.height-h*scale)/2;auto P=params(o.detail,budget.simplification);P.lineEps*=coarsen;P.fillEps*=coarsen;const double reachRadius=std::max(1.6,P.lineEps+.8);byteBound=false;
 auto paletteData=tracePalette();auto& names=paletteData.names;auto& classify=paletteData.classify;auto& paletteY=paletteData.luma;
 std::map<QString,std::array<double,3>> widths;for(auto v:product::toolSpecs()){auto t=v.toObject();auto a=t["widths"].toArray();widths[t["id"].toString()]={a[0].toDouble(),a[1].toDouble(),a[2].toDouble()};}
 product::require(QStringList{"pencil","mechanical-pencil","fineliner","paintbrush"}.contains(o.contourTool)&&widths.count(o.contourTool)&&widths.count("paintbrush"),"visual-contour-tool");
 auto source=sourceRaster(image,classify,stop);auto& L0=source.labels;auto& lum=source.luma;stats=IllustrationStats{};stats.coarsen=coarsen;stats.rasterWidth=unsigned(w);stats.rasterHeight=unsigned(h);stats.paper=source.paper;
 auto L=L0;
 /* Faint or thin outlines: downscaling turns one-pixel lines into pale, broken gray. A light pixel that is a clear luminance valley (at least 24 darker than both sides; a line split across two pixels scores about half its contrast in each) seeds line ink, and connected weaker valleys (at least 12) extend it, so outlines stay continuous instead of dashed. */
 {std::vector<unsigned char> weak(L.size());std::vector<int> stack;
  for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto p=std::size_t(y)*w+x;if((L[p]!=LightGray&&L[p]!=None)||lum[p]>=244)continue;int v=valley(lum,w,h,x,y);if(v>=24){L[p]=DarkGray;stack.push_back(int(p));++stats.contrastLines;}else if(v>=12)weak[p]=1;}
  while(!stack.empty()){int q0=stack.back();stack.pop_back();for(int k=0;k<8;++k){int q=neighbour(q0,k,w,h);if(q>=0&&weak[std::size_t(q)]){weak[std::size_t(q)]=0;L[std::size_t(q)]=DarkGray;stack.push_back(q);++stats.contrastLines;}}}}
 stats.merged+=cleanup(L,w,h,P.minArea,0,stop);
 /* 2. lines: per ink colour, thin parts become centerline strokes, thick parts stay for fills */
 const double maxHalf=std::max(1.5,widths["paintbrush"][2]/2/scale);std::vector<unsigned char> skeletonAll(L.size());std::vector<Item> items;
 std::array<bool,8> present{};for(auto label:L){if(label==Black||label==DarkGray)present[0]=true;else if(label<8)present[label]=true;}
 for(unsigned ink=0;ink<8;++ink){if(!present[ink])continue;nativeink::checkCancelled(stop);std::vector<unsigned char> mask(L.size());unsigned count=0;int x0=w,y0=h,x1=-1,y1=-1;
  for(int p=0;p<w*h;++p){bool in=ink==0?(L[p]==Black||L[p]==DarkGray):L[p]==ink;if(!in)continue;mask[p]=1;++count;int x=p%w,y=p/w;x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);}
  if(!count)continue;
  std::vector<std::uint16_t> d1(L.size()),d2(L.size(),Far);for(int p=0;p<w*h;++p)d1[p]=mask[p]?Far:0;chamfer(d1,w,h,true);
  bool anyCore=false;std::vector<int> core;
  for(int p=0;p<w*h;++p){if(!mask[p]||d1[p]<=maxHalf*3||d2[p]!=Far)continue;core={p};d2[p]=Far-1;int cx0=p%w,cx1=cx0,cy0=p/w,cy1=cy0;unsigned deepest=0;
   for(std::size_t i=0;i<core.size();++i){int c=core[i];deepest=std::max(deepest,unsigned(d1[c]));cx0=std::min(cx0,c%w);cx1=std::max(cx1,c%w);cy0=std::min(cy0,c/w);cy1=std::max(cy1,c/w);for(int k=0;k<8;k+=2){int q=neighbour(c,k,w,h);if(q>=0&&mask[q]&&d1[q]>maxHalf*3&&d2[q]==Far){d2[q]=Far-1;core.push_back(q);}}}
   bool junction=false;if(deepest<=3*(maxHalf*1.6+.5)&&core.size()<std::max(16u,unsigned(std::ceil(maxHalf*maxHalf*4)))){ /* A small thickening attached to a long thin arm is a line junction, not a solid patch. Compact dots and real filled areas stay fills. */
    const int pad=int(std::ceil(maxHalf*2));std::vector<int> search=core;std::unordered_set<int> seen(core.begin(),core.end());
    for(std::size_t i=0;i<search.size()&&search.size()<=2048&&!junction;++i){int c=search[i];junction=c%w<cx0-pad||c%w>cx1+pad||c/w<cy0-pad||c/w>cy1+pad;if(junction)break;for(int k=0;k<8;k+=2){int q=neighbour(c,k,w,h);if(q>=0&&mask[q]&&seen.insert(q).second)search.push_back(q);}}}
   if(!junction){for(int c:core)d2[c]=0;anyCore=true;}nativeink::checkCancelled(stop);}
  if(anyCore)chamfer(d2,w,h,false);
  std::vector<unsigned char> t(L.size());std::vector<int> thinPixels;for(int p=0;p<w*h;++p)if(mask[p]&&!(anyCore&&d2[p]<=(maxHalf+1)*3)){t[p]=1;thinPixels.push_back(p);}
  if(thinPixels.empty())continue;
  auto widthOf=[&](int p){return std::max(1.0,2*d1[p]/3.0-1);};
  thin(t,w,h,x0,y0,x1,y1,stop);std::vector<int> skel;for(int p:thinPixels)if(t[p])skel.push_back(p);prune(t,w,h,skel,P.spur);bridge(t,w,h,skel,P.gap);
  for(int p:thinPixels)if(L[p]==Black||L[p]==DarkGray||L[p]==ink)L[p]=None; /* consumed by strokes */
  for(auto& path:stitch(walk(t,w,h,skel),t,w,h)){double sum=0,dark=0;for(int p:path){sum+=widthOf(p);dark+=255-lum[std::size_t(p)];}dark/=255.0*double(path.size());double device=sum/path.size()*scale;std::vector<QPointF> pts;for(int p:path)pts.push_back(QPointF(p%w+.5,p/w+.5));
   double length=0;for(std::size_t i=1;i<pts.size();++i)length+=std::hypot((pts[i]-pts[i-1]).x(),(pts[i]-pts[i-1]).y());if(pts.size()>1&&length<2)continue;for(int p:path)skeletonAll[p]=1;
   auto simple=pts.size()>2?simplify(pts,P.lineEps):pts;QString tool=ink==0?o.contourTool:QString("fineliner");if(device>widths[tool][2]*1.4)tool="paintbrush";auto& tw=widths[tool];int wi=0;for(int k=1;k<3;++k)if(std::abs(tw[k]-device)<std::abs(tw[wi]-device))wi=k;
   for(std::size_t begin=0;begin<simple.size();begin+=2000){std::size_t end=std::min(simple.size(),begin+2001);std::vector<QPointF> chunk(simple.begin()+long(begin),simple.begin()+long(end));if(chunk.size()==1&&begin)break;
    Item item;item.line=true;item.style=QJsonObject{{"version",1},{"tool",tool},{"width",QStringList{"thin","medium","thick"}[wi]},{"color",names[int(ink)]}};item.raster={chunk};if(!begin)item.pixels=path;item.points=unsigned(chunk.size());item.importance=std::max(length,1.0)*(.35+.65*dark)*(begin?0.99:1); /* strong outlines outrank faint texture when the budget is short */item.anchor=chunk[chunk.size()/2];item.color=ink;items.push_back(std::move(item));if(end==simple.size())break;}}}
 /* 3. fills: drop fringes and speckle left around the extracted strokes, then trace bounded regions */
 stats.merged+=cleanup(L,w,h,P.minArea,P.thinRatio,stop);
 stats.merged+=tones(L,w,h,std::max(P.minArea,unsigned(double(w)*h*P.toneShare)),stop);
 std::vector<unsigned char> visited(L.size());std::vector<int> queue,head(std::size_t(w+1)*(h+1),-1),chain; /* per-vertex outgoing edge lists, reset after each region */
 for(int start=0;start<w*h;++start){if(visited[start]||L[start]==None)continue;nativeink::checkCancelled(stop);unsigned color=L[start];queue.clear();queue.push_back(start);visited[start]=1;double luminance=0;
  for(std::size_t i=0;i<queue.size();++i){int p=queue[i],x=p%w,y=p/w;luminance+=lum[p];for(int k=0;k<8;k+=2){int nx=x+DX[k],ny=y+DY[k];if(!inside(nx,ny,w,h))continue;int q=ny*w+nx;if(!visited[q]&&L[q]==color){visited[q]=1;queue.push_back(q);}}}
  if(queue.size()<P.minArea)continue;luminance/=queue.size();
  struct Edge {int from{},to{},direction{};bool used{};};std::vector<Edge> edges;auto cell=[&](int x,int y){return inside(x,y,w,h)&&L[std::size_t(y)*w+x]==color&&visited[std::size_t(y)*w+x];};auto vertex=[&](int x,int y){return y*(w+1)+x;};
  for(int p:queue){int x=p%w,y=p/w;if(!cell(x,y-1))edges.push_back({vertex(x,y),vertex(x+1,y),0});if(!cell(x+1,y))edges.push_back({vertex(x+1,y),vertex(x+1,y+1),1});if(!cell(x,y+1))edges.push_back({vertex(x+1,y+1),vertex(x,y+1),2});if(!cell(x-1,y))edges.push_back({vertex(x,y+1),vertex(x,y),3});}
  if(edges.size()>400000){++stats.droppedFills;continue;}chain.assign(edges.size(),-1);for(int n=0;n<int(edges.size());++n){chain[n]=head[edges[n].from];head[edges[n].from]=n;}std::vector<std::vector<QPointF>> rings;
  for(int first=0;first<int(edges.size());++first){if(edges[first].used)continue;std::vector<QPointF> ring;int at=first;for(unsigned walked=0;;++walked){product::require(walked<=edges.size(),"visual-ink-open-contour");auto& e=edges[at];e.used=true;ring.emplace_back(e.from%(w+1),e.from/(w+1));if(e.to==edges[first].from)break;int next=-1,rank=10;for(int k=head[e.to];k>=0;k=chain[k])if(!edges[k].used){int turn=(edges[k].direction-e.direction+4)%4,priority=turn==1?0:turn==0?1:turn==3?2:3;if(priority<rank){rank=priority;next=k;}}product::require(next>=0,"visual-ink-open-contour");at=next;}
   ring=corners(ring);if(ring.size()>=3)rings.push_back(std::move(ring));}
  for(auto& e:edges)head[e.from]=-1;
  if(rings.empty())continue;std::size_t outerIndex=0;for(std::size_t i=1;i<rings.size();++i)if(area(rings[i])>area(rings[outerIndex]))outerIndex=i;
  std::vector<std::vector<QPointF>> holes;for(std::size_t i=0;i<rings.size();++i)if(i!=outerIndex&&std::abs(area(rings[i]))>=P.minArea)holes.push_back(rings[i]);else if(i!=outerIndex)++stats.droppedHoles;
  std::stable_sort(holes.begin(),holes.end(),[](auto& a,auto& b){return std::abs(area(a))>std::abs(area(b));});if(holes.size()>128){stats.droppedHoles+=unsigned(holes.size()-128);holes.resize(128);}
  /* Simplify until every ring fits a path and the reader's fill renderer can afford the shape at device scale. */
  std::vector<QPointF> outer;std::vector<std::vector<QPointF>> inner;bool fits=false;
  for(double eps=P.fillEps;eps<=P.fillEps*40&&!fits;eps*=1.6){auto close=[&](const std::vector<QPointF>& r){auto c=r;c.push_back(c.front());c=simplify(c,eps);if(c.size()>3)c.pop_back();return c;};outer=close(rings[outerIndex]);inner.clear();unsigned total=unsigned(outer.size());bool ok=outer.size()>=3&&outer.size()<=2000;for(auto& hole:holes){auto c=close(hole);if(c.size()>=3){ok&=c.size()<=2000;total+=unsigned(c.size());inner.push_back(std::move(c));}}
   double top=1e9,bottom=-1e9;for(auto q:outer){top=std::min(top,q.y());bottom=std::max(bottom,q.y());}fits=ok&&(bottom-top)*scale*1.25*total<=3500000;
   while(!fits&&ok&&!inner.empty()&&eps>=P.fillEps*20){total-=unsigned(inner.back().size());inner.pop_back();++stats.droppedHoles;fits=(bottom-top)*scale*1.25*total<=3500000;}}
  if(!fits){++stats.droppedFills;continue;}
  QJsonObject style{{"version",1},{"width","thick"}};double opacity=1;unsigned colorIndex=color;
  if(color==Black){style["tool"]=o.contourTool;style["color"]="black";}
  else if(color==DarkGray||color==LightGray){style["tool"]="shader";style["color"]="black";opacity=std::clamp((255-luminance)/(255-28)/.6,.05,1.0);colorIndex=0;}
  else if(color>TintBase){colorIndex=color-TintBase;style["tool"]="marker";style["color"]=names[int(colorIndex)];opacity=std::clamp(classify.opacity(int(colorIndex),luminance),.12,Classifier::FullOpacity);} /* the region's own lightness, not one fixed tint */
  else{style["tool"]=queue.size()>unsigned(w*h/30)?"paintbrush":"marker";style["color"]=names[int(color)];}
  Item item;item.style=style;item.opacity=opacity;
  item.raster={outer};for(auto& r:inner)item.raster.push_back(r);item.pixels=queue;item.points=unsigned(outer.size());for(auto& r:inner)item.points+=unsigned(r.size());item.importance=std::sqrt(double(queue.size()))*4;double cx=0,cy=0;for(int p:queue){cx+=p%w+.5;cy+=p/w+.5;}item.anchor=QPointF(cx/queue.size(),cy/queue.size());item.color=colorIndex;items.push_back(std::move(item));}
 /* 4. component ownership by anchor point, bounds and colour */
 struct Part {QString id;QRectF bounds;QPointF target;std::array<bool,8> colors{};std::vector<QPointF> mask{};std::vector<std::vector<QPointF>> holes{};bool boundary{};};std::vector<Part> parts;product::require(o.components.size()<=64,"visual-component-capacity");
 for(auto v:o.components){auto c=v.toObject();auto b=c["bounds"].toArray(),t=c["target"].toArray();product::require(b.size()==4&&t.size()==2&&!c["id"].toString().isEmpty(),"visual-component-schema");Part part{c["id"].toString(),QRectF(b[0].toDouble(),b[1].toDouble(),b[2].toDouble(),b[3].toDouble()),QPointF(t[0].toDouble(),t[1].toDouble())};for(auto pigment:c["colors"].toArray()){int i=names.indexOf(pigment.toString());if(i>=0)part.colors[unsigned(i)]=true;}if(c.contains("geometry")){auto g=c["geometry"].toObject();product::require((g["kind"]=="area"||g["kind"]=="boundary")&&g["points"].isArray()&&g["holes"].isArray()&&g["holes"].toArray().size()<=8,"visual-component-geometry");part.boundary=g["kind"]=="boundary";auto points=[&](QJsonArray ring,unsigned minimum){product::require(ring.size()>=int(minimum)&&ring.size()<=64,"visual-component-mask-capacity");std::vector<QPointF> result;for(auto v:ring){auto xy=v.toArray();product::require(xy.size()==2&&xy[0].isDouble()&&xy[1].isDouble()&&std::isfinite(xy[0].toDouble())&&std::isfinite(xy[1].toDouble())&&part.bounds.adjusted(-2,-2,2,2).contains(xy[0].toDouble(),xy[1].toDouble()),"visual-component-mask-point");result.push_back({xy[0].toDouble(),xy[1].toDouble()});}return result;};part.mask=points(g["points"].toArray(),part.boundary?2:3);for(auto h:g["holes"].toArray())part.holes.push_back(points(h.toArray(),3));}parts.push_back(part);}
 auto maskContains=[](const Part& part,QPointF p){auto inside=[&](const std::vector<QPointF>& ring){bool yes=false;for(std::size_t i=0,j=ring.size()-1;i<ring.size();j=i++){auto a=ring[i],b=ring[j];if((a.y()>p.y())!=(b.y()>p.y())&&p.x()<(b.x()-a.x())*(p.y()-a.y())/(b.y()-a.y())+a.x())yes=!yes;}return yes;};if(part.boundary){for(std::size_t i=1;i<part.mask.size();++i){auto a=part.mask[i-1],d=part.mask[i]-a;double length=QPointF::dotProduct(d,d),t=length?std::clamp(QPointF::dotProduct(p-a,d)/length,0.0,1.0):0;auto q=p-a-d*t;if(std::hypot(q.x(),q.y())<=6)return true;}return false;}if(!inside(part.mask))return false;for(const auto& h:part.holes)if(inside(h))return false;return true;};for(auto& it:items){if(parts.empty())break;double ax=ox+it.anchor.x()*scale,ay=oy+it.anchor.y()*scale,best=1e30;int chosen=-1;bool masked=false;for(std::size_t k=0;k<parts.size();++k){const auto& part=parts[k];if(!part.bounds.contains(ax,ay)||(!(it.line&&it.color==0)&&!part.colors[it.color]))continue;bool hasMask=!part.mask.empty();if(hasMask&&!maskContains(part,{ax,ay}))continue;if(masked&&!hasMask)continue;if(hasMask&&!masked){best=1e30;masked=true;}double dd=std::hypot(ax-part.target.x(),ay-part.target.y());if(dd<best){best=dd;chosen=int(k);}}if(chosen>=0)it.component=parts[unsigned(chosen)].id;}
 /* 5. budget: keep the most important items within the stroke, point and Note-size limits; never refuse just because there is too much detail. Stored strokes carry about half a kilobyte of anchor and identity each, so cost is measured, not guessed. Anchor shape matches renderScene's estimate. */
 const QJsonObject anchor{{"document",QString(36,'a')},{"revision",QString(64,'b')},{"format","epub"},{"page",999},{"pageId",QString(64,'c')},{"layoutId",QString(64,'d')},{"viewId",QString(64,'e')},{"version",1},{"scope","page-location"}};
 for(auto& item:items)item.source=sourceInk(image,w,h,item.pixels,item.raster.empty()?std::vector<QPointF>{}:item.raster[0],item.line,item.style["tool"].toString(),item.opacity); /* once per item, from the exact source */
 auto toJson=[&](const Item& it){auto map=[&](const std::vector<QPointF>& r){QJsonArray out;for(auto q:r){double px=ox+q.x()*scale,py=oy+q.y()*scale;out.append(QJsonArray{std::round(px/o.coordinateQuantum)*o.coordinateQuantum,std::round(py/o.coordinateQuantum)*o.coordinateQuantum});}return out;};
  auto style=it.style;style["rgb"]=QString("#%1").arg(it.source&0xffffffu,6,16,QLatin1Char('0'));QJsonObject s{{"style",style},{"points",map(it.raster[0])}};if(!it.line){s["shape"]="polygon";if(it.raster.size()>1){QJsonArray hs;for(std::size_t k=1;k<it.raster.size();++k)hs.append(map(it.raster[k]));s["holes"]=hs;}if(it.opacity<1)s["opacity"]=it.opacity;}if(!it.component.isEmpty())s["component"]=it.component;return s;};
 auto measure=[&](unsigned count,unsigned pts){QJsonArray a;for(unsigned i=0;i<count;++i){QJsonArray q;for(unsigned k=0;k<pts;++k)q.append(QJsonArray{1234.5625,1234.5625});QJsonObject st{{"style",QJsonObject{{"version",1},{"tool","fineliner"},{"width","medium"},{"color","black"},{"rgb","#000000"}}},{"points",q}};if(!parts.empty())st["component"]=QString(64,'f');a.append(st);}return double(product::estimatedGeneratedInkBytes(a,anchor));};
 const double perStroke=measure(2,2)-measure(1,2),perPoint=(measure(1,102)-measure(1,2))/100,byteBudget=product::GeneratedInkByteCap*.85-(measure(1,2)-perStroke);
 std::vector<std::size_t> rank(items.size());for(std::size_t i=0;i<rank.size();++i)rank[i]=i;std::stable_sort(rank.begin(),rank.end(),[&](auto a,auto b){return items[a].importance>items[b].importance;});
 const unsigned capStrokes=std::min(budget.strokes,nativeink::MaxVisualStrokes),capPoints=std::min(budget.points,nativeink::MaxVisualPoints),maxStrokes=capStrokes>o.reserveStrokes?capStrokes-o.reserveStrokes:0,maxPoints=capPoints>o.reservePoints?capPoints-o.reservePoints:0; /* leave room for what the caller draws after the image */unsigned strokes=0,points=0;double bytes=0,kept=0,dropped=0;std::vector<unsigned char> keep(items.size());
 for(auto i:rank){double cost=perStroke+perPoint*items[i].points+(items[i].line?0:48);if(strokes+1<=maxStrokes&&points+items[i].points<=maxPoints&&bytes+cost<=byteBudget){keep[i]=1;++strokes;points+=items[i].points;bytes+=cost;kept+=items[i].importance;}else{byteBound|=bytes+cost>byteBudget;dropped+=items[i].importance;if(items[i].line)++stats.droppedLines;else ++stats.droppedFills;}}
 byteBound|=dropped>(kept+dropped)*.1; /* the budget, not the drawing, decided too much: worth a coarser attempt */
 auto assemble=[&]{QJsonArray f,l;for(std::size_t i=0;i<items.size();++i)if(keep[i])(items[i].line?l:f).append(toJson(items[i]));for(auto v:l)f.append(v);return f;}; /* fills underneath, pen lines on top */
 for(int round=0;round<60;++round){if(product::estimatedGeneratedInkBytes(assemble(),anchor)<=product::GeneratedInkByteCap*85/100)break;unsigned kept=0;for(auto k:keep)kept+=k;unsigned drop=std::max(1u,kept/20);for(auto r=rank.rbegin();r!=rank.rend()&&drop;++r)if(keep[*r]){keep[*r]=0;--drop;if(items[*r].line)++stats.droppedLines;else ++stats.droppedFills;}} /* safety net if the measured costs underestimate */
 /* 7. structural acceptance on the raster grid: integrity of what is kept, plus a floor against catastrophic loss */
 std::vector<unsigned char> covered(L.size()),filled(L.size()),fillY(L.size(),255);const QJsonArray identity{1,0,0,1,0,0}; /* fillY: darkest rendered luminance of the kept fills over each pixel */
 for(std::size_t i=0;i<items.size();++i){if(!keep[i])continue;auto& it=items[i];
  if(it.line){for(auto& chunk:it.raster)for(std::size_t k=0;k<chunk.size();++k){auto a=chunk[k],b=k+1<chunk.size()?chunk[k+1]:chunk[k];double len=std::hypot((b-a).x(),(b-a).y());int steps=std::max(1,int(std::ceil(len*2)));for(int st=0;st<=steps;++st){auto q=a+(b-a)*(double(st)/steps);const int R=int(std::ceil(reachRadius));for(int dy=-R;dy<=R;++dy)for(int dx=-R;dx<=R;++dx){int x=int(std::floor(q.x()))+dx,y=int(std::floor(q.y()))+dy;if(inside(x,y,w,h)&&std::hypot(x+.5-q.x(),y+.5-q.y())<=reachRadius)covered[std::size_t(y)*w+x]=1;}}}}
  else{auto ring=[](const std::vector<QPointF>& r){QJsonArray out;for(auto q:r)out.append(QJsonArray{q.x(),q.y()});return out;};QJsonArray hs;for(std::size_t k=1;k<it.raster.size();++k)hs.append(ring(it.raster[k]));int ci=names.indexOf(it.style["color"].toString());auto fy=static_cast<unsigned char>(std::lround(255-it.opacity*(255-(ci>=0?paletteY[std::size_t(ci)]:0))));nativeink::polygonSpans(ring(it.raster[0]),w,h,identity,[&](int y,int a,int b){for(int x=a;x<b;++x){auto q=std::size_t(y)*w+x;filled[q]=1;fillY[q]=std::min(fillY[q],fy);}},hs);}}
 unsigned keptLine=0,keptLineHit=0,keptFill=0,keptFillHit=0,skel=0,skelHit=0,drawn=0,drawnInk=0,strong=0,strongHit=0;
 for(std::size_t i=0;i<items.size();++i){if(!keep[i])continue;for(int p:items[i].pixels){if(items[i].line){++keptLine;keptLineHit+=covered[std::size_t(p)];}else{++keptFill;keptFillHit+=filled[std::size_t(p)];}}}
 for(int p=0;p<w*h;++p){if(skeletonAll[p]){++skel;skelHit+=covered[p];}if(filled[p]){++drawn;drawnInk+=L0[p]!=None;}if(L0[p]!=None&&L0[p]!=LightGray){++strong;strongHit+=covered[p]||filled[p];}}
 unsigned visible=0,visibleHit=0;for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto p=std::size_t(y)*w+x;if(lum[p]>=235||valley(lum,w,h,x,y)<32)continue;++visible;visibleHit+=covered[p]||fillY[p]<=lum[p]+16;} /* every visibly line-like source pixel, whatever its class: is it drawn at least as dark? */
 stats.linePixels=visible;stats.lineRecall=visible?double(visibleHit)/visible:1;stats.skeleton=skel;stats.lineCoverage=keptLine?double(keptLineHit)/keptLine:1;stats.lineRetention=skel?double(skelHit)/skel:1;stats.fillRecall=keptFill?double(keptFillHit)/keptFill:1;stats.fillPrecision=drawn?double(drawnInk)/drawn:1;stats.inkRecall=strong?double(strongHit)/strong:1;
 for(std::size_t i=0;i<items.size();++i)if(keep[i])++(items[i].line?stats.lines:stats.fills);
 product::require(stats.lines+stats.fills>0,"visual-ink-empty");
 product::require(stats.lineCoverage>=.95&&stats.fillRecall>=.95&&stats.fillPrecision>=.85&&stats.inkRecall>=.5,"visual-ink-structure-loss");
 return assemble();}
}
/* When the budget is what drops detail (strokes, points or Note size), a coarser trace usually keeps more of the drawing: the second attempt works at 70% of the raster, so small features merge into fewer, larger items. Keep whichever attempt draws more of the visible ink and lines. */
nativeink::Group traceInk(const QImage& image,const IllustrationOptions& o,const VisualBudget& budget,const std::atomic<bool>* stop){
 IllustrationStats first,second;bool bound=false,ignored=false;QJsonArray best;std::string failure;
 try{best=traceOnce(image,o,budget,stop,1,first,bound);}catch(const std::runtime_error& e){failure=e.what();bound=true;}
 nativeink::checkCancelled(stop);
 if(bound&&image.width()>=64&&image.height()>=64){auto coarse=image.scaled(std::max(1,int(image.width()*.7)),std::max(1,int(image.height()*.7)),Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
  try{auto other=traceOnce(coarse,o,budget,stop,1.4,second,ignored);if(!failure.empty()){best=other;first=second;failure.clear();}else{auto reference=traceReference(image,stop);auto fineScore=referenceScore(reference,best,o,stop),coarseScore=referenceScore(reference,other,o,stop);if(fineScore&&coarseScore&&coarseScore->score>fineScore->score){best=other;first=second;}} /* A comparison that exceeds renderer work limits retains the valid finer candidate. */}catch(const std::runtime_error& e){if(failure.empty()){}else failure=e.what();}}
 nativeink::checkCancelled(stop); /* a cancel during the coarser attempt is honoured, not swallowed */
 if(o.stats)*o.stats=first;
 if(!failure.empty())throw std::runtime_error(failure);
 return nativeink::checked(best,"tom_illustration");}
}
