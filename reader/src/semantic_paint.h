#pragma once
#include "semantic_controller.h"
#include "semantic_view_clip.h"
#include "ux_paint.h"
#include "ib/reader_screen.h"
#include "highlight_blocks.h"
#include <string>
#include <algorithm>
#include <cmath>
namespace semantic {
// Transient selection UI. Copy regions hold opaque panel pixels (pre-filled
// from the page so anti-aliased corners blend correctly); multiply regions hold
// a wash that darkens the page beneath, so text and ink stay visible.
struct Visual {std::array<spike::Rect,6> regions{};std::array<bool,6> multiply{};unsigned count{};spike::Rect menu{},panel{},selection{};ib::Targets targets;};
struct TransientOptions {bool library{},styled{};spike::Rect excluded{};double scale{1},dx{},dy{};};
inline std::pair<int,int> map(const QJsonArray& m,double x,double y){return {int(std::lround(m[0].toDouble()*x+m[2].toDouble()*y+m[4].toDouble())),int(std::lround(m[1].toDouble()*x+m[3].toDouble()*y+m[5].toDouble()))};}
inline spike::Rect clip(spike::Rect r){r.x0=std::clamp(r.x0,0,1620);r.x1=std::clamp(r.x1,0,1620);r.y0=std::clamp(r.y0,0,2160);r.y1=std::clamp(r.y1,0,2160);return r;}
inline spike::Rect bounds(const QJsonObject& s){spike::Rect r{1620,2160,0,0};auto m=s["transform"].toArray();for(auto v:s["quads"].toArray()){auto q=v.toArray();for(int i=0;i<8;i+=2){auto [x,y]=map(m,q[i].toDouble(),q[i+1].toDouble());r.x0=std::min(r.x0,x-3);r.x1=std::max(r.x1,x+4);r.y0=std::min(r.y0,y-3);r.y1=std::max(r.y1,y+7);}}return clip(r);}
inline void line(spike::Surface s,int x,int y,int ex,int ey,std::optional<SourceClip> crop={},QJsonArray matrix={}){int dx=std::abs(ex-x),sx=x<ex?1:-1,dy=-std::abs(ey-y),sy=y<ey?1:-1,error=dx+dy;for(unsigned n=0;n<10000;++n){bool visible=true;if(crop){double a=matrix[0].toDouble(),b=matrix[1].toDouble(),c=matrix[2].toDouble(),d=matrix[3].toDouble(),px=x+.5-matrix[4].toDouble(),py=y+.5-matrix[5].toDouble(),det=a*d-b*c;visible=std::abs(det)>1e-12&&crop->contains((d*px-c*py)/det,(-b*px+a*py)/det);}if(visible&&x>=0&&x<s.width&&y>=0&&y<s.height)s.pixels[y*s.stridePixels+x]=0xff000000u;if(x==ex&&y==ey)break;int twice=2*error;if(twice>=dy){error+=dy;x+=sx;}if(twice<=dx){error+=dx;y+=sy;}}}
// Saved text highlights: one block per text line (see highlight_blocks.h), multiplied under the text so it stays
// sharp. Crops still hide what lies outside the visible source region.
inline void highlights(spike::Surface surface,const QJsonArray& records){probe(Work::Layout);if(!surface.valid()||records.isEmpty())return;std::vector<unsigned char> covered(surface.pixels.size(),0);for(auto r:records){auto o=r.toObject();auto m=o["transform"].toArray();auto crop=sourceClip(o);
 const double a=m[0].toDouble(),b=m[1].toDouble(),c=m[2].toDouble(),d=m[3].toDouble(),e=m[4].toDouble(),f=m[5].toDouble(),det=a*d-b*c;if(std::abs(det)<1e-12)continue;
 unsigned color=4;for(unsigned i=1;i<ib::theme::InkName.size();++i)if(o["color"].toString().compare(QString::fromLatin1(ib::theme::InkName[i]),Qt::CaseInsensitive)==0)color=i;
 const std::uint32_t wash=ib::theme::wash(ib::theme::Ink[color],150);
 auto toSurface=[&](double x,double y,double& sx,double& sy){sx=a*x+c*y+e;sy=b*x+d*y+f;return std::isfinite(sx)&&std::isfinite(sy);};
 for(const auto& block:lineBlocks(o["quads"].toArray(),toSurface)){auto rect=clip({int(std::floor(block.x0)),int(std::floor(block.y0)),int(std::ceil(block.x1)),int(std::ceil(block.y1))});
  for(int y=rect.y0;y<rect.y1&&y<surface.height;++y)for(int x=rect.x0;x<rect.x1&&x<surface.width;++x){
   if(crop){const double px=x+.5-e,py=y+.5-f;if(!crop->contains((d*px-c*py)/det,(-b*px+a*py)/det))continue;}
   auto index=std::size_t(y)*surface.stridePixels+x;if(covered[index])continue;covered[index]=1;auto& p=surface.pixels[index];auto ch=[&](int s){return std::uint32_t(((p>>s&255)*(wash>>s&255)+127)/255)<<s;};p=0xff000000u|ch(16)|ch(8)|ch(0);}}}}
inline QByteArray displayText(const QString& value){QString out;for(auto u:value.toUpper().toUcs4()){if(u>=32&&u<127)out+=QChar(ushort(u));else if(u=='\n'||u=='\r'||u=='\t')out+=' ';else out+=" U"+QString::number(u,16).toUpper()+" ";}return out.toUtf8();}
inline void wrapped(spike::Surface s,int x,int y,const QString& value,int cols=52,int rows=10){auto text=displayText(value);int at=0;for(int row=0;row<rows&&at<text.size();++row){int n=std::min(cols,int(text.size())-at);if(at+n<text.size()){int space=text.lastIndexOf(' ',at+n);if(space>at)n=space-at;}auto part=text.mid(at,n);ux::text(s,x,y+row*28,3,part.constData());at+=n;while(at<text.size()&&text[at]==' ')++at;}if(at<text.size())ux::text(s,x,y+rows*28,2,"MORE OMITTED");}
inline int menuBand(const Controller& c,int x,int initial){auto g=c.geometry();if(!g)return initial;auto m=g->data["transform"].toArray();int y=initial;for(unsigned pass=0;pass<64&&y<=1900;++pass){int next=y;for(auto v:g->data["chars"].toArray()){auto q=v.toObject()["quad"].toArray();spike::Rect r{1620,2160,0,0};for(int i=0;i<8;i+=2){auto [px,py]=map(m,q[i].toDouble(),q[i+1].toDouble());r.x0=std::min(r.x0,px);r.y0=std::min(r.y0,py);r.x1=std::max(r.x1,px);r.y1=std::max(r.y1,py);}if(r.x1>x&&r.x0<x+960&&r.y1>y&&r.y0<y+120)next=std::max(next,r.y1+20);}if(next==y)return y;y=next;}return initial;}
inline std::string utf8(const QString& v){auto b=v.toUtf8();return std::string(b.constData(),std::size_t(b.size()));}
inline std::string attribution(const QJsonObject& d){auto p=d["provider"].toString();if(p=="princeton-wordnet-3.0")return "WordNet 3.0 \xC2\xB7 Princeton University";if(p=="inkbound-authored-test-v1")return "Test dictionary \xE2\x80\x94 eight entries";return utf8(d["attribution"].toString());}
inline Visual transient(spike::Surface overlay,const Controller& c,std::span<const std::uint32_t> backdrop={},TransientOptions o={}){probe(Work::Layout);Visual v;if(c.view()==View::None)return v; /* nothing composited without regions: skip the 14 MB clear on every page turn */ std::fill(overlay.pixels.begin(),overlay.pixels.end(),0u);
 ib::Canvas canvas(overlay);spike::Rect available{0,0,1620,2060};if(!o.excluded.empty()){if(o.excluded.x0==0)available.x0=o.excluded.x1;else if(o.excluded.x1==1620)available.x1=o.excluded.x0;}
 auto add=[&](spike::Rect r,bool multiply){r=clip(r);if(r.empty())return;const int rows=multiply?std::max(1,int(1620ULL*2160/4)/std::max(1,r.x1-r.x0)):2160;for(int y=r.y0;y<r.y1&&v.count<v.regions.size();y+=rows){v.regions[v.count]={r.x0,y,r.x1,std::min(r.y1,y+rows)};v.multiply[v.count++]=multiply;}};
 // Panels are painted twice: once to learn their rect, then over the page pixels.
 auto panel=[&](auto&& paint){ib::Targets scratch;spike::Rect r=paint(scratch);r=clip(ib::outset(r,4));
  for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x){auto i=std::size_t(y)*overlay.stridePixels+x;overlay.pixels[i]=backdrop.size()>i?(backdrop[i]|0xff000000u):0xffffffffu;}
  v.targets.clear();r=paint(v.targets);add(ib::outset(r,4),false);return r;};
 spike::Rect anchor{};
 if(c.hasSelection()){auto original=c.selection();auto data=original->data;auto matrix=data["transform"].toArray();if(o.scale!=1&&matrix.size()==6){for(int i=0;i<4;++i)matrix[i]=matrix[i].toDouble()*o.scale;matrix[4]=matrix[4].toDouble()*o.scale+o.dx;matrix[5]=matrix[5].toDouble()*o.scale+o.dy;data["transform"]=matrix;}const SelectionSnapshot copy(data);const auto* s=&copy;auto r=bounds(s->data);if(r.empty())return v;anchor=r;v.selection=r;
  // Selection wash per text line (quads), stored as a multiply mask.
  auto m=s->data["transform"].toArray();auto crop=sourceClip(s->data);(void)crop;
  for(auto q:s->data["quads"].toArray()){auto a=q.toArray();spike::Rect line{1620,2160,0,0};for(int i=0;i<8;i+=2){auto [x,y]=map(m,a[i].toDouble(),a[i+1].toDouble());line.x0=std::min(line.x0,x);line.x1=std::max(line.x1,x);line.y0=std::min(line.y0,y);line.y1=std::max(line.y1,y);}
   // Toolbar selection: a wash over the passage. Definition: the word's own pixels stay
   // untouched (qualified readability); a thin underline just below it marks the word.
   if(c.view()==View::Menu){line=clip(ib::outset(line,2));for(int y=line.y0;y<line.y1;++y)for(int x=line.x0;x<line.x1;++x)overlay.pixels[std::size_t(y)*overlay.stridePixels+x]=ib::theme::Selection;}
   else{spike::Rect mark=clip({line.x0,line.y1+6,line.x1,line.y1+9});for(int y=mark.y0;y<mark.y1;++y)for(int x=mark.x0;x<mark.x1;++x)overlay.pixels[std::size_t(y)*overlay.stridePixels+x]=ib::theme::SelectionHandle;}}
  add(c.view()==View::Menu?r:spike::Rect{r.x0,r.y0,r.x1,std::min(2160,r.y1+6)},true);
  if(c.view()==View::Menu){bool word=!s->data["word"].toString().isEmpty();
   v.menu=panel([&](ib::Targets& t){ib::reader::SelectionBar bar;bar.anchor=r;bar.define=word;bar.copy=o.library;bar.more=false;bar.note=o.styled;bar.tom=o.library&&o.styled;return ib::reader::selectionToolbar(canvas,t,bar,available);});return v;}
 }
 if(c.view()==View::Notice){auto message=ib::text::quiet(utf8(c.notice()));v.panel=panel([&](ib::Targets&){const ib::Font f{ib::Face::Text,30};int w=std::min(1300,int(ib::text::measure(f,message))+ib::theme::S5*2);spike::Rect r{810-w/2,1950,810+w/2,2034}; /* a slim hairline pill above the footer */ canvas.fillRound(r,42.f,ib::theme::Surface);canvas.strokeRound(r,42.f,ib::theme::Hairline,ib::theme::Border);ib::text::centeredLine(canvas,f,ib::inset(r,ib::theme::S4),message,ib::theme::InkSecondary);return r;});return v;}
 ib::reader::Definition d;auto def=c.definition();d.loading=c.view()==View::Defining;d.actions=o.library&&o.styled&&!c.definitionOnly();d.expanded=c.definitionExpanded();d.lineOffset=c.definitionOffset();
 QString selected=c.hasSelection()?c.selection()->data["text"].toString():QString();d.word=utf8(def["key"].toString().isEmpty()?selected:def["key"].toString());
 if(def["status"].toString()=="FOUND"){d.partOfSpeech=utf8(def["partOfSpeech"].toString());auto senses=def["senses"].toArray();
  if(senses.isEmpty())d.senses.push_back({d.partOfSpeech,utf8(def["definition"].toString())});else for(int i=0;i<senses.size()&&i<3;++i){auto x=senses[i].toObject();d.senses.push_back({utf8(x["partOfSpeech"].toString()),utf8(x["definition"].toString())});}
  d.attribution=attribution(def);}
 else if(!d.loading)d.status=def["status"].toString()=="UNAVAILABLE"?"The dictionary isn\u2019t available":""; /* empty: the card says "No definition for “word”" */
 if(anchor.empty())anchor={710,980,910,1080};
 v.panel=panel([&](ib::Targets& t){return ib::reader::definitionCard(canvas,t,anchor,d,available);});return v;
}
inline void compositeOverlay(spike::Surface out,std::span<const std::uint32_t> mask,const Visual& visual,spike::Rect damage)noexcept{for(unsigned k=0;k<visual.count;++k){auto r=visual.regions[k];r.x0=std::max(r.x0,damage.x0);r.y0=std::max(r.y0,damage.y0);r.x1=std::min(r.x1,damage.x1);r.y1=std::min(r.y1,damage.y1);
 for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x){auto i=std::size_t(y)*out.stridePixels+x;auto m=mask[i];if(!m)continue;if(!visual.multiply[k]){out.pixels[i]=m;continue;}auto p=out.pixels[i];
  auto ch=[&](int s){return std::uint32_t(((p>>s&255)*(m>>s&255)+127)/255)<<s;};out.pixels[i]=0xff000000u|ch(16)|ch(8)|ch(0);}}}
}
