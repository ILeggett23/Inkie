#pragma once
#include <QJsonObject>
#include <QJsonArray>
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <stdexcept>
#include <vector>
namespace semantic {
// Stored visibleSourceRect is [x, y, width, height]; this validated clip uses edges.
struct SourceClip {
 double x0{},y0{},x1{},y1{};
 bool valid()const noexcept{return std::isfinite(x0)&&std::isfinite(y0)&&std::isfinite(x1)&&std::isfinite(y1)&&x0<x1&&y0<y1&&std::max({std::abs(x0),std::abs(x1),std::abs(y0),std::abs(y1)})<100000;}
 bool contains(double x,double y)const noexcept{return std::isfinite(x)&&std::isfinite(y)&&x>=x0&&x<x1&&y>=y0&&y<y1;}
};
inline std::optional<SourceClip> sourceClip(const QJsonObject& object){
 if(!object.contains("visibleSourceRect")){if(object.contains("viewId"))throw std::runtime_error("semantic-view-crop-missing");return {};}
 auto id=object["viewId"].toString();if(id.size()!=64)throw std::runtime_error("semantic-view-identity");for(auto c:id)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))throw std::runtime_error("semantic-view-identity");
 auto values=object["visibleSourceRect"].toArray();if(values.size()!=4)throw std::runtime_error("semantic-view-crop-shape");for(auto v:values)if(!v.isDouble()||!std::isfinite(v.toDouble())||std::abs(v.toDouble())>=100000)throw std::runtime_error("semantic-view-crop-value");
 SourceClip out{values[0].toDouble(),values[1].toDouble(),values[0].toDouble()+values[2].toDouble(),values[1].toDouble()+values[3].toDouble()};if(!out.valid())throw std::runtime_error("semantic-view-crop-order");return out;
}
inline bool quadVisible(const QJsonArray& quad,const SourceClip& crop){
 if(quad.size()!=8)throw std::runtime_error("semantic-view-quad-shape");for(auto v:quad)if(!v.isDouble()||!std::isfinite(v.toDouble())||std::abs(v.toDouble())>=100000)throw std::runtime_error("semantic-view-quad-value");
 using P=std::array<double,2>;std::vector<P> polygon;for(int i:{0,2,6,4})polygon.push_back({quad[i].toDouble(),quad[i+1].toDouble()});
 for(int edge=0;edge<4;++edge){if(polygon.empty())return false;std::vector<P> next;int axis=edge/2;double limit=edge==0?crop.x0:edge==1?crop.x1:edge==2?crop.y0:crop.y1;auto inside=[&](P p){return edge%2?p[axis]<=limit:p[axis]>=limit;};auto previous=polygon.back();bool was=inside(previous);for(auto current:polygon){bool now=inside(current);if(now!=was){double t=(limit-previous[axis])/(current[axis]-previous[axis]);next.push_back({previous[0]+t*(current[0]-previous[0]),previous[1]+t*(current[1]-previous[1])});}if(now)next.push_back(current);previous=current;was=now;}polygon=std::move(next);}
 double area=0;for(std::size_t i=0;i<polygon.size();++i){auto a=polygon[i],b=polygon[(i+1)%polygon.size()];area+=a[0]*b[1]-a[1]*b[0];}return std::abs(area)>1e-10;
}
}
