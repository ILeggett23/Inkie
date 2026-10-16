#pragma once
#include "core.h"
#include <algorithm>
#include <cmath>
#include <utility>
namespace ux {
// Presentation only: never changes source/layout coordinates or persisted annotation identity.
class ViewZoom {
 spike::Rect frame_;double scale_{1},x_{},y_{};
 void bound()noexcept{const double mx=(frame_.x1-frame_.x0)*(scale_-1)/2,my=(frame_.y1-frame_.y0)*(scale_-1)/2;x_=std::clamp(x_,-mx,mx);y_=std::clamp(y_,-my,my);}
public:
 explicit ViewZoom(spike::Rect frame={0,0,1620,2060}):frame_(frame){}
 double scale()const noexcept{return scale_;}bool active()const noexcept{return scale_>1.001;}
 double dx()const noexcept{return x_+(1-scale_)*(frame_.x0+frame_.x1)/2;}double dy()const noexcept{return y_+(1-scale_)*(frame_.y0+frame_.y1)/2;}
 void reset()noexcept{scale_=1;x_=y_=0;}
 std::pair<double,double> map(double x,double y)const noexcept{return {scale_*x+dx(),scale_*y+dy()};}
 std::pair<double,double> inverse(double x,double y)const noexcept{return {(x-dx())/scale_,(y-dy())/scale_};}
 bool zoom(double factor,int fx,int fy)noexcept{if(!std::isfinite(factor)||factor<=0||!frame_.contains(fx,fy))return false;const double next=std::clamp(std::round(scale_*factor*20)/20,1.,4.);if(next==scale_)return false;auto p=inverse(fx,fy);scale_=next;x_=fx-scale_*p.first-(1-scale_)*(frame_.x0+frame_.x1)/2;y_=fy-scale_*p.second-(1-scale_)*(frame_.y0+frame_.y1)/2;bound();return true;}
 bool pan(double x,double y)noexcept{if(!active()||!std::isfinite(x)||!std::isfinite(y))return false;const auto oldX=x_,oldY=y_;x_+=x;y_+=y;bound();return oldX!=x_||oldY!=y_;}
 spike::Rect damage(spike::Rect r)const noexcept{if(r.empty())return {};auto a=map(r.x0,r.y0),b=map(r.x1,r.y1);return {std::clamp(int(std::floor(a.first)),frame_.x0,frame_.x1),std::clamp(int(std::floor(a.second)),frame_.y0,frame_.y1),std::clamp(int(std::ceil(b.first)),frame_.x0,frame_.x1),std::clamp(int(std::ceil(b.second)),frame_.y0,frame_.y1)};}
 spike::Rect source(spike::Rect r)const noexcept{if(r.empty())return {};auto a=inverse(r.x0,r.y0),b=inverse(r.x1,r.y1);return {std::clamp(int(std::floor(a.first)),frame_.x0,frame_.x1),std::clamp(int(std::floor(a.second)),frame_.y0,frame_.y1),std::clamp(int(std::ceil(b.first))+1,frame_.x0,frame_.x1),std::clamp(int(std::ceil(b.second))+1,frame_.y0,frame_.y1)};}
};
}
