// SPDX-License-Identifier: AGPL-3.0-only
#include "note_indicators.h"
#include "ux_paint.h"
#include "ib/canvas.h"
#include "ib/components.h"
#include "ib/theme.h"
#include <algorithm>
namespace notes {
std::vector<Indicator> indicators(const QJsonObject& document,const QJsonObject& page,styled::Matrix matrix){auto crop=semantic::sourceClip(page);std::vector<Indicator> result;result.reserve(32);for(auto value:document["notes"].toArray()){const auto note=value.toObject();bool visible=false;for(auto lv:document["layers"].toArray()){const auto layer=lv.toObject();if(layer["id"]==note["layer"])visible=layer["visible"].toBool();}if(!visible)continue;const auto anchor=note["anchor"].toObject();if(anchor["resolutionStatus"]=="unresolved"||anchor["format"]!=page["format"]||anchor["document"]!=page["document"]||anchor["revision"]!=page["revision"]||!library::validHash(anchor["pageId"].toString()))continue;double x=1550,y=250;if(anchor["excerpt"].toString().isEmpty()){if(anchor["document"]!=page["document"]||anchor["revision"]!=page["revision"]||sourcePage(anchor,page)!=page["page"].toInt())continue;y+=result.size()*55;}else{const auto resolved=resolveAnnotation(anchor,page);const auto quads=resolved["quads"].toArray();if(resolved["status"]!="resolved"||quads.isEmpty())continue;auto first=quads[0].toArray();if(crop){first={};for(auto q:quads)if(semantic::quadVisible(q.toArray(),*crop)){first=q.toArray();break;}if(first.isEmpty())continue;}if(first.size()!=8)continue;double px=first[0].toDouble(),py=first[1].toDouble();if(crop){px=std::clamp(px,crop->x0,crop->x1);py=std::clamp(py,crop->y0,crop->y1);}if(!matrix.map(px,py,x,y))continue;x-=50;}int left=std::clamp(int(x),10,1560),top=std::clamp(int(y),140,1870);unsigned color=4;for(unsigned n=1;n<8;++n)if(note["color"]==styled::colorId(n))color=n;result.push_back({note["id"].toString(),{left,top,left+42,top+42},color});if(result.size()==32)break;}return result;}
// A note on the page: a small square of the note's ink, closed by an ink rule, with a folded paper corner (a sticky
// note). Drawn by the kit so it matches every other Inkie surface; the tap target around it is hitArea (96 px).
void paintIndicators(spike::Surface s,std::span<const Indicator> items){ib::Canvas c(s);
 for(const auto& item:items){const auto r=item.bounds;const float k=12;
  c.fill(r,0xff000000u|styled::colorRgb(item.color));c.hairline(r,ib::theme::InkPrimary,ib::theme::Outline);
  ib::Path fold;fold.move(float(r.x1)-k,float(r.y0)).line(float(r.x1),float(r.y0)+k).line(float(r.x1)-k,float(r.y0)+k).close();c.fillPath(fold,ib::theme::Paper);
  c.line(float(r.x1)-k,float(r.y0),float(r.x1),float(r.y0)+k,2.f,ib::theme::InkPrimary);}}
}
