#include "pdf_viewport.h"
#include <algorithm>
#include <cmath>

namespace reader::pdfview {namespace {
void need(bool b,Error code,const char* reason){if(!b)throw Failure(code,reason);}
bool finiteRect(Rect r){return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&r.width>0&&r.height>0&&std::isfinite(r.x+r.width)&&std::isfinite(r.y+r.height);}
bool within(Rect inner,Rect outer){return inner.x>=outer.x&&inner.y>=outer.y&&inner.x+inner.width<=outer.x+outer.width&&inner.y+inner.height<=outer.y+outer.height;}
Rect intersection(Rect a,Rect b){double x=std::max(a.x,b.x),y=std::max(a.y,b.y),endX=std::min(a.x+a.width,b.x+b.width),endY=std::min(a.y+a.height,b.y+b.height);return {x,y,std::max(0.0,endX-x),std::max(0.0,endY-y)};}
Rect transformed(Rect r,const Affine& m){std::array<Point,4> points{{m.map({r.x,r.y}),m.map({r.x+r.width,r.y}),m.map({r.x,r.y+r.height}),m.map({r.x+r.width,r.y+r.height})}};double x=points[0].x,y=points[0].y,endX=x,endY=y;for(auto p:points){x=std::min(x,p.x);y=std::min(y,p.y);endX=std::max(endX,p.x);endY=std::max(endY,p.y);}return {x,y,endX-x,endY-y};}

}
Point Affine::map(Point p)const noexcept{auto [a,b,c,d,e,f]=value;return {a*p.x+c*p.y+e,b*p.x+d*p.y+f};}
Plan plan(Rect source,const View& view){
 need(finiteRect(source),Error::InvalidBounds,"pdf-bounds-invalid");for(double v:{source.x,source.y,source.x+source.width,source.y+source.height})need(std::abs(v)<=MaxCoordinate,Error::Capacity,"pdf-coordinate-capacity");
 need(std::isfinite(view.zoom)&&view.zoom>=1&&view.zoom<=4&&(view.rotation==0||view.rotation==90||view.rotation==180||view.rotation==270)&&std::isfinite(view.pan.x)&&std::isfinite(view.pan.y),Error::InvalidView,"pdf-view-invalid");
 Rect crop=view.crop.value_or(source);need(finiteRect(crop),Error::InvalidView,"pdf-crop-invalid");need(within(crop,source),Error::CropOutsideSource,"pdf-crop-outside-source");
 bool turn=view.rotation==90||view.rotation==270;double width=turn?crop.height:crop.width,height=turn?crop.width:crop.height;double scale=std::min(Width/width,Height/height)*view.zoom;
 need(std::isfinite(scale)&&scale>0&&scale<=MaxScale,Error::Capacity,"pdf-scale-capacity");double maxX=std::max(0.0,(width*scale-Width)/2),maxY=std::max(0.0,(height*scale-Height)/2);
 need(std::abs(view.pan.x)<=maxX+1e-7&&std::abs(view.pan.y)<=maxY+1e-7,Error::PanOutsideRange,"pdf-pan-outside-range");double x=(Width-width*scale)/2+view.pan.x,y=(Height-height*scale)/2+view.pan.y;
 Affine forward;
 switch(view.rotation){case 0:forward.value={scale,0,0,scale,x-scale*crop.x,y-scale*crop.y};break;case 90:forward.value={0,scale,-scale,0,x+scale*(crop.y+crop.height),y-scale*crop.x};break;case 180:forward.value={-scale,0,0,-scale,x+scale*(crop.x+crop.width),y+scale*(crop.y+crop.height)};break;case 270:forward.value={0,-scale,scale,0,x-scale*crop.y,y+scale*(crop.x+crop.width)};break;}
 need(std::abs(forward.value[4])<=MaxAffineTranslation&&std::abs(forward.value[5])<=MaxAffineTranslation,Error::Capacity,"pdf-affine-capacity");auto [a,b,c,d,e,f]=forward.value;double det=a*d-b*c;need(std::isfinite(det)&&det>0,Error::InvalidView,"pdf-affine-singular");Affine inverse{{d/det,-b/det,-c/det,a/det,(c*f-d*e)/det,(b*e-a*f)/det}};
 Rect visible=intersection(crop,transformed({0,0,Width,Height},inverse));need(finiteRect(visible),Error::InvalidView,"pdf-empty-viewport");return {source,crop,visible,view,forward,inverse,scale,maxX,maxY,Width,Height,RasterBytes};
}
}
