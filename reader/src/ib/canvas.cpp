// SPDX-License-Identifier: AGPL-3.0-only
#include "ib/canvas.h"
#include <algorithm>
#include <cmath>
namespace ib {
namespace {
constexpr float Pi=3.14159265358979f;
inline int coverage(float signedDistance)noexcept{float c=0.5f-signedDistance;return c<=0?0:c>=1?255:int(c*255.f+0.5f);}
float roundBoxDistance(float px,float py,float cx,float cy,float hx,float hy,float r)noexcept{
 float qx=std::fabs(px-cx)-(hx-r),qy=std::fabs(py-cy)-(hy-r);
 float ox=std::max(qx,0.f),oy=std::max(qy,0.f);
 return std::sqrt(ox*ox+oy*oy)+std::min(std::max(qx,qy),0.f)-r;
}
float segmentDistance(float px,float py,Pt a,Pt b)noexcept{
 float vx=b.x-a.x,vy=b.y-a.y,wx=px-a.x,wy=py-a.y,len=vx*vx+vy*vy;
 float t=len>0?std::clamp((wx*vx+wy*vy)/len,0.f,1.f):0.f;
 float dx=wx-t*vx,dy=wy-t*vy;return std::sqrt(dx*dx+dy*dy);
}
}
std::uint32_t mix(std::uint32_t a,std::uint32_t b,int t)noexcept{
 if(t<=0)return a|0xff000000u;if(t>=255)return b|0xff000000u;
 auto ch=[&](int shift){int x=int(a>>shift&255),y=int(b>>shift&255);return std::uint32_t((x*(255-t)+y*t+127)/255)<<shift;};
 return 0xff000000u|ch(16)|ch(8)|ch(0);
}
Path& Path::move(float x,float y){contours_.push_back({{x,y}});return *this;}
Path& Path::line(float x,float y){if(contours_.empty())return move(x,y);contours_.back().push_back({x,y});return *this;}
Path& Path::quad(float cx,float cy,float x,float y){if(contours_.empty())return move(x,y);Pt p=contours_.back().back();for(int i=1;i<=12;++i){float t=i/12.f,u=1-t;line(u*u*p.x+2*u*t*cx+t*t*x,u*u*p.y+2*u*t*cy+t*t*y);}return *this;}
Path& Path::cubic(float ax,float ay,float bx,float by,float x,float y){if(contours_.empty())return move(x,y);Pt p=contours_.back().back();for(int i=1;i<=16;++i){float t=i/16.f,u=1-t;line(u*u*u*p.x+3*u*u*t*ax+3*u*t*t*bx+t*t*t*x,u*u*u*p.y+3*u*u*t*ay+3*u*t*t*by+t*t*t*y);}return *this;}
Path& Path::arc(float cx,float cy,float r,float a0,float a1){int n=std::max(4,int(std::fabs(a1-a0)/8.f));for(int i=0;i<=n;++i){float a=(a0+(a1-a0)*i/n)*Pi/180.f;float x=cx+r*std::cos(a),y=cy+r*std::sin(a);if(contours_.empty())move(x,y);else line(x,y);}return *this;}
Path& Path::close(){if(!contours_.empty()&&contours_.back().size()>1)contours_.back().push_back(contours_.back().front());return *this;}
void Canvas::blend(int x,int y,std::uint32_t color,int alpha)noexcept{
 if(alpha<=0||x<clip_.x0||y<clip_.y0||x>=clip_.x1||y>=clip_.y1)return;
 auto& p=s_.pixels[std::size_t(y)*s_.stridePixels+x];p=alpha>=255?(color|0xff000000u):mix(p,color,alpha);
}
std::uint32_t Canvas::at(int x,int y)const noexcept{if(x<0||y<0||x>=s_.width||y>=s_.height)return 0xffffffffu;return s_.pixels[std::size_t(y)*s_.stridePixels+x];}
void Canvas::fill(Rect r,std::uint32_t c)noexcept{r=intersect(r,clip_);c|=0xff000000u;for(int y=r.y0;y<r.y1;++y){auto row=s_.pixels.begin()+std::ptrdiff_t(std::size_t(y)*s_.stridePixels);std::fill(row+r.x0,row+r.x1,c);}}
void Canvas::multiply(Rect r,std::uint32_t c)noexcept{r=intersect(r,clip_);int cr=int(c>>16&255),cg=int(c>>8&255),cb=int(c&255);for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x){auto& p=s_.pixels[std::size_t(y)*s_.stridePixels+x];p=0xff000000u|std::uint32_t((int(p>>16&255)*cr+127)/255)<<16|std::uint32_t((int(p>>8&255)*cg+127)/255)<<8|std::uint32_t((int(p&255)*cb+127)/255);}}
void Canvas::hline(int x0,int x1,int y,std::uint32_t c,int t)noexcept{fill({x0,y,x1,y+t},c);}
void Canvas::vline(int x,int y0,int y1,std::uint32_t c,int t)noexcept{fill({x,y0,x+t,y1},c);}
void Canvas::hairline(Rect r,std::uint32_t c,int t)noexcept{hline(r.x0,r.x1,r.y0,c,t);hline(r.x0,r.x1,r.y1-t,c,t);vline(r.x0,r.y0,r.y1,c,t);vline(r.x1-t,r.y0,r.y1,c,t);}
void Canvas::fillRound(Rect r,float rad,std::uint32_t c)noexcept{
 if(r.empty())return;rad=std::min(rad,std::min(width(r),height(r))/2.f);
 int ir=int(std::ceil(rad));
 // Straight interior bands are exact; only the four corner squares need distances.
 fill({r.x0+ir,r.y0,r.x1-ir,r.y1},c);fill({r.x0,r.y0+ir,r.x0+ir,r.y1-ir},c);fill({r.x1-ir,r.y0+ir,r.x1,r.y1-ir},c);
 float cx=(r.x0+r.x1)/2.f,cy=(r.y0+r.y1)/2.f,hx=width(r)/2.f,hy=height(r)/2.f;
 for(Rect q:{Rect{r.x0,r.y0,r.x0+ir,r.y0+ir},Rect{r.x1-ir,r.y0,r.x1,r.y0+ir},Rect{r.x0,r.y1-ir,r.x0+ir,r.y1},Rect{r.x1-ir,r.y1-ir,r.x1,r.y1}}){
  q=intersect(q,clip_);for(int y=q.y0;y<q.y1;++y)for(int x=q.x0;x<q.x1;++x)blend(x,y,c,coverage(roundBoxDistance(x+.5f,y+.5f,cx,cy,hx,hy,rad)));
 }
}
void Canvas::strokeRound(Rect r,float rad,float w,std::uint32_t c)noexcept{
 if(r.empty())return;rad=std::min(rad,std::min(width(r),height(r))/2.f);
 float cx=(r.x0+r.x1)/2.f,cy=(r.y0+r.y1)/2.f,hx=width(r)/2.f,hy=height(r)/2.f;int band=int(std::ceil(std::max(rad,w)))+1;
 auto ring=[&](int x,int y){float d=roundBoxDistance(x+.5f,y+.5f,cx,cy,hx,hy,rad);int a=coverage(d)-coverage(d+w);blend(x,y,c,a);};
 Rect all=intersect(r,clip_);
 for(int y=all.y0;y<all.y1;++y){bool edgeRow=y<r.y0+band||y>=r.y1-band;
  if(edgeRow){for(int x=all.x0;x<all.x1;++x)ring(x,y);continue;}
  for(int x=all.x0;x<std::min(all.x1,r.x0+band);++x)ring(x,y);
  for(int x=std::max(all.x0,r.x1-band);x<all.x1;++x)ring(x,y);
 }
}
void Canvas::fillCircle(float cx,float cy,float r,std::uint32_t c)noexcept{
 Rect b=intersect({int(std::floor(cx-r-1)),int(std::floor(cy-r-1)),int(std::ceil(cx+r+1)),int(std::ceil(cy+r+1))},clip_);
 for(int y=b.y0;y<b.y1;++y)for(int x=b.x0;x<b.x1;++x){float dx=x+.5f-cx,dy=y+.5f-cy;blend(x,y,c,coverage(std::sqrt(dx*dx+dy*dy)-r));}
}
void Canvas::strokeCircle(float cx,float cy,float r,float w,std::uint32_t c)noexcept{
 Rect b=intersect({int(std::floor(cx-r-w)),int(std::floor(cy-r-w)),int(std::ceil(cx+r+w)),int(std::ceil(cy+r+w))},clip_);
 for(int y=b.y0;y<b.y1;++y)for(int x=b.x0;x<b.x1;++x){float dx=x+.5f-cx,dy=y+.5f-cy;float d=std::fabs(std::sqrt(dx*dx+dy*dy)-r)-w/2;blend(x,y,c,coverage(d));}
}
void Canvas::line(float x0,float y0,float x1,float y1,float w,std::uint32_t c)noexcept{
 float h=w/2+1;Rect b=intersect({int(std::floor(std::min(x0,x1)-h)),int(std::floor(std::min(y0,y1)-h)),int(std::ceil(std::max(x0,x1)+h)),int(std::ceil(std::max(y0,y1)+h))},clip_);
 for(int y=b.y0;y<b.y1;++y)for(int x=b.x0;x<b.x1;++x)blend(x,y,c,coverage(segmentDistance(x+.5f,y+.5f,{x0,y0},{x1,y1})-w/2));
}
void Canvas::stroke(const Path& p,float w,std::uint32_t c)noexcept{
 // Union of capsules: per pixel use the nearest segment so joins never double-blend.
 float h=w/2+1;Rect b{};bool any=false;
 for(const auto& k:p.contours())for(auto q:k){Rect r{int(std::floor(q.x-h)),int(std::floor(q.y-h)),int(std::ceil(q.x+h)),int(std::ceil(q.y+h))};if(!any){b=r;any=true;}else b.include(r);}
 if(!any)return;b=intersect(b,clip_);
 for(int y=b.y0;y<b.y1;++y)for(int x=b.x0;x<b.x1;++x){float best=1e9f;
  for(const auto& k:p.contours()){if(k.size()==1)best=std::min(best,segmentDistance(x+.5f,y+.5f,k[0],k[0]));for(std::size_t i=1;i<k.size();++i)best=std::min(best,segmentDistance(x+.5f,y+.5f,k[i-1],k[i]));}
  blend(x,y,c,coverage(best-w/2));}
}
void Canvas::fillPath(const Path& p,std::uint32_t c)noexcept{
 Rect b{};bool any=false;
 for(const auto& k:p.contours())for(auto q:k){Rect r{int(std::floor(q.x)),int(std::floor(q.y)),int(std::ceil(q.x))+1,int(std::ceil(q.y))+1};if(!any){b=r;any=true;}else b.include(r);}
 if(!any)return;b=intersect(b,clip_);
 auto winding=[&](float px,float py){int wn=0;for(const auto& k:p.contours()){std::size_t n=k.size();for(std::size_t i=0;i<n;++i){Pt a=k[i],e=k[(i+1)%n];if(a.y<=py){if(e.y>py&&(e.x-a.x)*(py-a.y)-(px-a.x)*(e.y-a.y)>0)++wn;}else if(e.y<=py&&(e.x-a.x)*(py-a.y)-(px-a.x)*(e.y-a.y)<0)--wn;}}return wn!=0;};
 for(int y=b.y0;y<b.y1;++y)for(int x=b.x0;x<b.x1;++x){int hits=0;for(int sy=0;sy<4;++sy)for(int sx=0;sx<4;++sx)hits+=winding(x+(sx+.5f)/4,y+(sy+.5f)/4);if(hits)blend(x,y,c,hits*255/16);}
}
void Canvas::image(Rect dst,std::span<const std::uint32_t> src,int sw,int sh,int stride,bool cover)noexcept{
 if(dst.empty()||sw<1||sh<1||stride<sw||src.size()<std::size_t(stride)*sh)return;
 double scale=cover?std::max(double(width(dst))/sw,double(height(dst))/sh):std::min(double(width(dst))/sw,double(height(dst))/sh);
 int w=std::max(1,int(sw*scale)),h=std::max(1,int(sh*scale));Rect out=centered(dst,w,h);Rect vis=intersect(intersect(out,dst),clip_);
 for(int y=vis.y0;y<vis.y1;++y){double sy0=(y-out.y0)/scale,sy1=(y+1-out.y0)/scale;
  for(int x=vis.x0;x<vis.x1;++x){double sx0=(x-out.x0)/scale,sx1=(x+1-out.x0)/scale;
   int ax=std::clamp(int(sx0),0,sw-1),bx=std::clamp(int(std::ceil(sx1)),ax+1,sw),ay=std::clamp(int(sy0),0,sh-1),by=std::clamp(int(std::ceil(sy1)),ay+1,sh);
   unsigned r=0,g=0,bl=0,n=0;for(int yy=ay;yy<by;++yy)for(int xx=ax;xx<bx;++xx){auto p=src[std::size_t(yy)*stride+xx];r+=p>>16&255;g+=p>>8&255;bl+=p&255;++n;}
   s_.pixels[std::size_t(y)*s_.stridePixels+x]=0xff000000u|(r/n)<<16|(g/n)<<8|(bl/n);}}
}
Rect Canvas::grayImage(Rect dst,const std::uint8_t* src,int sw,int sh,int stride,std::uint32_t paper)noexcept{
 if(!src||dst.empty()||sw<1||sh<1||stride<sw)return {};
 const double scale=std::min(double(width(dst))/sw,double(height(dst))/sh);const int w=std::max(1,int(sw*scale)),h=std::max(1,int(sh*scale));const Rect out=centered(dst,w,h);const Rect vis=intersect(out,clip_);
 const unsigned pr=paper>>16&255,pg=paper>>8&255,pb=paper&255;
 for(int y=vis.y0;y<vis.y1;++y){const double sy0=(y-out.y0)/scale,sy1=(y+1-out.y0)/scale;const int ay=std::clamp(int(sy0),0,sh-1),by=std::clamp(int(std::ceil(sy1)),ay+1,sh);
  for(int x=vis.x0;x<vis.x1;++x){const double sx0=(x-out.x0)/scale,sx1=(x+1-out.x0)/scale;const int ax=std::clamp(int(sx0),0,sw-1),bx=std::clamp(int(std::ceil(sx1)),ax+1,sw);
   unsigned sum=0,n=0;for(int yy=ay;yy<by;++yy)for(int xx=ax;xx<bx;++xx){sum+=src[std::size_t(yy)*stride+xx];++n;}const unsigned v=sum/n;
   s_.pixels[std::size_t(y)*s_.stridePixels+x]=0xff000000u|(pr*v/255)<<16|(pg*v/255)<<8|(pb*v/255);}}
 return out;
}
void Canvas::mask(int x,int y,const std::uint8_t* a,int w,int h,int stride,std::uint32_t c,float gain)noexcept{
 Rect r=intersect({x,y,x+w,y+h},clip_);
 for(int yy=r.y0;yy<r.y1;++yy)for(int xx=r.x0;xx<r.x1;++xx){int v=a[std::size_t(yy-y)*stride+(xx-x)];if(gain!=1.f)v=std::min(255,int(v*gain+.5f));blend(xx,yy,c,v);}
}
}
