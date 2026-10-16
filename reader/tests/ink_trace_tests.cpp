/* SPDX-License-Identifier: AGPL-3.0-only */
/* lineArt (ink_trace) controls. Real fixtures are downscaled, otherwise unmodified image-model outputs (fixtures/visual/real/PROVENANCE.json). Synthetic drawings are painted here only to exercise geometry, colour, capacity and cancellation paths; they are never drawing-quality evidence. */
#include "illustration_ink.h"
#include "ink_trace.h"
#include "native_ink.h"
#include "product_services.h"
#include "styled_ink.h"
#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QSet>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>
#include <source_location>
#include <stdexcept>
#include <string>
namespace {
unsigned checks{};
void check(bool b,std::source_location l=std::source_location::current()){++checks;if(!b)throw std::runtime_error("ink trace line "+std::to_string(l.line()));}
std::string failure(const std::function<void()>& f){try{f();}catch(const std::exception& e){return e.what();}return {};}
QByteArray encode(const QImage& image){QByteArray out;QBuffer b(&out);b.open(QIODevice::WriteOnly);check(image.save(&b,"PNG"));return out;}
QByteArray file(const QString& path){QFile f(path);if(!f.open(QIODevice::ReadOnly))throw std::runtime_error("missing fixture "+path.toStdString());return f.readAll();}
/* Pixel painters (no QPainter, so the suite needs no GUI platform plugin). */
void line(QImage& im,double ax,double ay,double bx,double by,double width,QRgb c){int x0=std::max(0,int(std::floor(std::min(ax,bx)-width))),x1=std::min(im.width()-1,int(std::ceil(std::max(ax,bx)+width))),y0=std::max(0,int(std::floor(std::min(ay,by)-width))),y1=std::min(im.height()-1,int(std::ceil(std::max(ay,by)+width)));double dx=bx-ax,dy=by-ay,v=dx*dx+dy*dy;
 for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x){double px=x+.5,py=y+.5,t=v?std::clamp(((px-ax)*dx+(py-ay)*dy)/v,0.0,1.0):0;if(std::hypot(px-ax-dx*t,py-ay-dy*t)<=width/2)im.setPixel(x,y,c);}}
void box(QImage& im,int x0,int y0,int x1,int y1,QRgb c){for(int y=y0;y<y1;++y)for(int x=x0;x<x1;++x)im.setPixel(x,y,c);}
void ring(QImage& im,double cx,double cy,double r,double width,QRgb c){for(int y=0;y<im.height();++y)for(int x=0;x<im.width();++x)if(std::abs(std::hypot(x+.5-cx,y+.5-cy)-r)<=width/2)im.setPixel(x,y,c);}
unsigned lcg(unsigned& s){s=s*1664525u+1013904223u;return s>>8;}
QImage paper(int w,int h,QRgb c=qRgb(255,255,255)){QImage im(w,h,QImage::Format_ARGB32);im.fill(c);return im;}
/* Frame sizes match renderScene's composite frames at the default target: 826 wide for landscape and square, 1101 tall for portrait. */
tomink::IllustrationOptions frame(int w,int h,tomink::Detail d,tomink::IllustrationStats* stats,bool lineArt=true){tomink::IllustrationOptions o;o.nativeTools=true;o.lineArt=lineArt;o.detail=d;o.contourTool="fineliner";o.stats=stats;double a=double(w)/h;o.width=a>=1?825.6:1101.12*a;o.height=a>=1?825.6/a:1101.12;o.x=24+(1400-o.width)/2;o.y=24;return o;}
/* Independent observable regression: render the selected result on its decoded source grid and count retained visible source lines. */
double referenceLineRecall(const QImage& source,const nativeink::Group& group,const tomink::IllustrationOptions& o){int w=source.width(),h=source.height();double scale=std::min(o.width/w,o.height/h),ox=o.x+(o.width-w*scale)/2,oy=o.y+(o.height-h*scale)/2;styled::Matrix m;m.value={1/scale,0,0,1/scale,-ox/scale,-oy/scale};styled::Raster raster(w,h,w);for(auto v:group.strokes){auto stroke=v.toObject();if(stroke["shape"]=="polygon")raster.fill(stroke,m);else{check(raster.begin(styled::parseStyle(stroke["style"].toObject()),m));bool first=true;for(auto q:stroke["points"].toArray()){auto p=q.toArray();check(raster.point({p[0].toDouble(),p[1].toDouble()},first));first=false;}raster.end();}}std::vector<unsigned char> lum(std::size_t(w)*h);for(int y=0;y<h;++y)for(int x=0;x<w;++x){auto p=source.pixel(x,y);int a=qAlpha(p),r=(qRed(p)*a+255*(255-a))/255,g=(qGreen(p)*a+255*(255-a))/255,b=(qBlue(p)*a+255*(255-a))/255;lum[std::size_t(y)*w+x]=static_cast<unsigned char>(std::lround(.2126*r+.7152*g+.0722*b));}auto at=[&](int x,int y){return int(lum[std::size_t(std::clamp(y,0,h-1))*w+std::clamp(x,0,w-1)]);};const int dx[]{1,0,1,1},dy[]{0,1,1,-1};unsigned expected=0,hit=0;for(int y=0;y<h;++y)for(int x=0;x<w;++x){int c=at(x,y),contrast=0;if(c>=235)continue;for(int k=0;k<4;++k)contrast=std::max(contrast,std::min(std::max(at(x-dx[k],y-dy[k]),at(x-2*dx[k],y-2*dy[k])),std::max(at(x+dx[k],y+dy[k]),at(x+2*dx[k],y+2*dy[k])))-c);if(contrast<32)continue;++expected;auto p=styled::compositeInk(0xffffffff,raster.pixels()[std::size_t(y)*w+x]);hit+=std::min({qRed(p),qGreen(p),qBlue(p)})<236;}check(expected>0);return double(hit)/expected;}
const QJsonObject anchor{{"document",QString(36,'a')},{"revision",QString(64,'b')},{"format","epub"},{"page",999},{"pageId",QString(64,'c')},{"layoutId",QString(64,'d')},{"viewId",QString(64,'e')},{"version",1},{"scope","page-location"}};
/* Every stroke must replay through the reader's own renderer, also at a 1.1x device scale, and the group must fit the Note-size target the converter promises. */
void deliverable(const nativeink::Group& g){int h=std::max(64,int(std::ceil(g.bounds.bottom()))+24);styled::Raster raster(1480,h,1480);
 for(auto v:g.strokes){auto s=v.toObject();if(s["shape"]=="polygon"){raster.fill(s,{});nativeink::polygonSpans(s["points"].toArray(),1640,int(h*1.1)+2,{1.1,0,0,1.1,0,0},[](int,int,int){},s["holes"].toArray());}
  else{check(raster.begin(styled::parseStyle(s["style"].toObject()),{}));bool first=true;for(auto p:s["points"].toArray()){auto a=p.toArray();raster.point({a[0].toDouble(),a[1].toDouble()},first);first=false;}raster.end();}}
 check(product::estimatedGeneratedInkBytes(g.strokes,anchor)<=product::GeneratedInkByteCap*85/100);}
struct Census {unsigned lines{},fills{},blackPen{};QSet<QString> lineColors,fillColors;bool tint(QString c)const{return tints.contains(c);}QSet<QString> tints,shaders,full;double largestFill{};};
Census census(const nativeink::Group& g){Census c;for(auto v:g.strokes){auto s=v.toObject();auto st=s["style"].toObject();if(s["shape"]=="polygon"){++c.fills;c.fillColors.insert(st["color"].toString());if(s.contains("opacity")&&s["opacity"].toDouble()>0&&s["opacity"].toDouble()<1&&st["tool"]=="marker")c.tints.insert(st["color"].toString());if(st["tool"]=="shader")c.shaders.insert(st["color"].toString());else if(st["color"]=="black")++c.blackPen;else if(!s.contains("opacity")||s["opacity"].toDouble()>=1)c.full.insert(st["color"].toString());auto b=nativeink::bounds(QJsonArray{s});c.largestFill=std::max(c.largestFill,b.width()*b.height());}else{++c.lines;c.lineColors.insert(st["color"].toString());}}return c;}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=2)return 64;
 try{const auto root=QString::fromLocal8Bit(argv[1]);
  /* 1. Long thin linework the 20u native converter refuses, plus hue mapping: light blue becomes a blue tint, orange-red stays red, mid-gray becomes shading. */
  auto scene=paper(900,600);unsigned seed=7;double x=40,y=40;for(int i=0;i<3200;++i){double nx=std::clamp(x+double(int(lcg(seed)%9)-4),20.0,560.0),ny=std::clamp(y+double(int(lcg(seed)%9)-3),20.0,580.0);if(ny>=575)ny=40;line(scene,x,y,nx,ny,2,qRgb(20,20,24));x=nx;y=ny;}
  ring(scene,300,300,150,3,qRgb(20,20,24));box(scene,620,60,860,180,qRgb(170,200,230));box(scene,620,220,860,340,qRgb(230,80,50));box(scene,620,380,860,500,qRgb(120,120,120));
  auto bytes=encode(scene);tomink::IllustrationStats stats;nativeink::Group g;
  check(failure([&]{g=tomink::illustrate(bytes,frame(900,600,tomink::Detail::Detailed,&stats));}).empty());
  auto c=census(g);check(c.lines>0&&c.lineColors.contains("black"));check(c.tint("blue"));check(c.fillColors.contains("red")&&!c.tint("red"));check(c.shaders.contains("black"));check(!c.fillColors.contains("orange"));
  { /* 20z exact color: traced fills reproduce their source colors (through the tool's own opacity over paper), not the nearest ink; every stroke keeps a valid ink name for older readers */
   auto shown=[](const QJsonObject& s){const auto st=s["style"].toObject();bool ok=false;const unsigned rgb=st["rgb"].toString().mid(1).toUInt(&ok,16);check(ok&&st["rgb"].toString().size()==7);const auto tool=st["tool"].toString();const double a=(tool=="highlighter"?191:tool=="shader"?153:tool=="marker"?242:255)/255.0*s.value("opacity").toDouble(1);std::array<double,3> out{};for(int k=0;k<3;++k)out[std::size_t(k)]=a*((rgb>>(16-8*k))&255)+(1-a)*255;return out;};
   auto near=[&](QRgb want){for(auto v:g.strokes){auto s=v.toObject();if(s["shape"]!="polygon")continue;auto got=shown(s);if(std::abs(got[0]-qRed(want))<=14&&std::abs(got[1]-qGreen(want))<=14&&std::abs(got[2]-qBlue(want))<=14)return true;}return false;};
   check(near(qRgb(170,200,230))&&near(qRgb(230,80,50))&&near(qRgb(120,120,120)));
   for(auto v:g.strokes){auto st=v.toObject()["style"].toObject();check(st.contains("rgb")&&QStringList{"black","pink","green","orange","yellow","purple","blue","red"}.contains(st["color"].toString()));}
   std::puts("EXACT_COLOR_TRACE_PASS");}
  check(stats.lineCoverage>=.95&&stats.fillRecall>=.95&&stats.fillPrecision>=.85&&stats.lineRetention>=.9);deliverable(g);
  for(auto v:g.strokes)check(v.toObject()["points"].toArray().size()<=int(nativeink::MaxPathPoints));
  /* 2. The old native path is untouched when lineArt is off: it still refuses this drawing, and never emits open pen strokes. */
  tomink::IllustrationStats unused;auto oldError=failure([&]{auto old=tomink::illustrate(bytes,frame(900,600,tomink::Detail::Detailed,&unused,false));for(auto v:old.strokes)check(v.toObject()["shape"]=="polygon");});
  check(!oldError.empty()&&oldError.rfind("ink trace line",0)!=0);
  /* 3. Paper detection: a pale tone dominating the border is paper, not a fill over the whole frame. */
  auto tinted=paper(600,400,qRgb(245,235,200));ring(tinted,300,200,120,3,qRgb(20,20,24));tomink::IllustrationStats ps;nativeink::Group pg;
  check(failure([&]{pg=tomink::illustrate(encode(tinted),frame(600,400,tomink::Detail::Standard,&ps));}).empty());check(ps.paper>0);check(census(pg).largestFill<825.6*550.4*.5);deliverable(pg);
  /* 4. Dense hatching trims to the stroke, point and Note-size budgets instead of refusing. */
  auto dense=paper(900,600);seed=11;for(int i=0;i<900;++i){double ax=lcg(seed)%900,ay=lcg(seed)%600,len=20+lcg(seed)%100,ang=(lcg(seed)%628)/100.0;line(dense,ax,ay,ax+len*std::cos(ang),ay+len*std::sin(ang),1.5,qRgb(25,25,30));}
  for(int k=0;k<200;k+=6){line(dense,100+k,350,100+k,550,1.2,qRgb(25,25,30));line(dense,100,350+k,300,350+k,1.2,qRgb(25,25,30));}
  tomink::IllustrationStats ds;nativeink::Group dg;auto denseError=failure([&]{dg=tomink::illustrate(encode(dense),frame(900,600,tomink::Detail::HighlyDetailed,&ds));});
  check(denseError.empty());check(dg.strokes.size()<=int(nativeink::MaxVisualStrokes)&&dg.points<=nativeink::MaxVisualPoints);deliverable(dg);
  { /* room reserved for text the caller draws after the image (labels, leaders, map caption) is honoured */auto reserved=frame(900,600,tomink::Detail::HighlyDetailed,nullptr);reserved.reserveStrokes=300;reserved.reservePoints=20000;nativeink::Group rg;check(failure([&]{rg=tomink::illustrate(encode(dense),reserved);}).empty());check(int(rg.strokes.size())+300<=int(nativeink::MaxVisualStrokes)&&rg.points+20000<=nativeink::MaxVisualPoints&&!rg.strokes.isEmpty());}
  /* 5. Real image-model output (Stage 1, prompt v2) at the two default detail levels. */
  struct Real {const char* name;double retention,recall;};const Real reals[]={{"stage1-v2-sci-heart",.95,.68},{"stage1-v2-animal-cat",.95,.75},{"stage1-v2-arch-cathedral",.85,.85},{"stage1-v2-object-bicycle",.95,.85},{"stage1-v1-animal-cat",.95,.73}}; /* retention floors sit just under the values measured when the fixtures were added (converter r1 report); recall floors (lineRecall, every visibly line-like source pixel) about 0.05 under the converter2 values (heart 0.73, cat 0.81, cathedral 0.90, bicycle 0.90, v1 cat 0.78 at their lower detail level), see its report */
  unsigned realRuns=0;for(const auto& real:reals){auto data=file(root+"/fixtures/visual/real/"+real.name+".png");QImage probe;check(probe.loadFromData(data));
   for(auto d:{tomink::Detail::Standard,tomink::Detail::Detailed}){tomink::IllustrationStats rs;nativeink::Group rg;auto error=failure([&]{rg=tomink::illustrate(data,frame(probe.width(),probe.height(),d,&rs));});
    if(!error.empty())std::printf("REAL_FIXTURE_FAIL %s %s\n",real.name,error.c_str());check(error.empty());
    check(rs.lineCoverage>=.95&&rs.fillRecall>=.95&&rs.fillPrecision>=.85);check(rs.lineRetention>=real.retention);check(rs.lineRecall>=real.recall);deliverable(rg);auto rc=census(rg);
    if(QString(real.name).contains("bicycle"))check(rc.fillColors.contains("orange")&&!rc.fillColors.contains("red")); /* brown leather saddle: orange family, not red */
    if(QString(real.name).contains("v1-animal-cat"))check(!rc.fillColors.contains("orange")&&rc.shaders.contains("black")); /* warm gray-brown tabby stripes are gray shading */
    if(QString(real.name).contains("heart"))check(rc.fillColors.contains("red")&&rc.fillColors.contains("blue"));
    if(QString(real.name).contains("cat")){unsigned black=0;for(auto v:rg.strokes)black+=v.toObject()["style"].toObject()["color"]=="black";check(black*2>unsigned(rg.strokes.size()));}
    std::printf("REAL_FIXTURE name=%s detail=%s strokes=%d lines=%u fills=%u retention=%.3f ink_recall=%.3f line_recall=%.3f raster=%ux%u coarsen=%g\n",real.name,d==tomink::Detail::Standard?"standard":"detailed",int(rg.strokes.size()),rs.lines,rs.fills,rs.lineRetention,rs.inkRecall,rs.lineRecall,rs.rasterWidth,rs.rasterHeight,rs.coarsen);++realRuns;}}
  { /* The old cross-resolution score selected a coarse Detailed cat that lost more source lines. The selected output must retain the actual lines on one fixed grid. */auto data=file(root+"/fixtures/visual/real/stage1-v2-animal-cat.png");QImage original;check(original.loadFromData(data));auto o=frame(original.width(),original.height(),tomink::Detail::Detailed,nullptr);auto budget=tomink::visualBudget(o.detail,std::max(o.width,o.height),o.workingMemory);auto size=original.size().scaled(int(budget.rasterEdge),int(budget.rasterEdge),Qt::KeepAspectRatio);QBuffer buffer(&data);check(buffer.open(QIODevice::ReadOnly));QImageReader reader(&buffer);reader.setScaledSize(size);auto source=reader.read().scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_ARGB32);auto selected=tomink::illustrate(data,o);double retained=referenceLineRecall(source,selected,o);std::printf("COMMON_GRID_DETAIL_CONTROL line_recall=%.6f minimum=.70 source=REAL_SAVED_PIXELS renderer=ACTUAL_SOFTWARE\n",retained);check(retained>=.70);deliverable(selected);}
  /* 6. Memory admission: the lineArt raster shrinks to fit the working-memory budget the reader grants. */
  auto heart=file(root+"/fixtures/visual/real/stage1-v2-sci-heart.png");tomink::IllustrationStats full,tight;
  check(failure([&]{(void)tomink::illustrate(heart,frame(768,768,tomink::Detail::Standard,&full));}).empty());check(full.rasterWidth>640&&full.rasterWidth<=960&&full.rasterHeight==full.rasterWidth);
  auto small=frame(768,768,tomink::Detail::Standard,&tight);small.workingMemory=16*1024*1024;check(failure([&]{(void)tomink::illustrate(heart,small);}).empty());
  check(double(tight.rasterWidth)*tight.rasterHeight<=double(small.workingMemory)/tomink::InkWorkingBytesPerPixel&&tight.rasterWidth<640);
  /* 8. Colour on matched lightness: warm gray-brown becomes gray shading (not orange), yellow-green becomes green, tan becomes orange (not red), pure red stays red (not black), pale pink becomes a pink tint, golden yellow stays yellow. */
  struct Swatch {QRgb rgb;const char* color;char kind;};const Swatch swatches[]={{qRgb(168,154,140),"black",'s'},{qRgb(200,220,150),"green",'a'},{qRgb(162,103,68),"orange",'f'},{qRgb(255,0,0),"red",'f'},{qRgb(253,225,228),"pink",'t'},{qRgb(231,200,76),"yellow",'f'}};
  for(const auto& sw:swatches){auto im=paper(300,200);box(im,60,40,240,160,sw.rgb);tomink::IllustrationStats ss;nativeink::Group sg;check(failure([&]{sg=tomink::illustrate(encode(im),frame(300,200,tomink::Detail::Standard,&ss));}).empty());auto sc=census(sg);
   check(sc.fillColors==QSet<QString>{sw.color}&&sc.blackPen==0);if(sw.kind=='s')check(sc.shaders.contains("black"));if(sw.kind=='t')check(sc.tint(sw.color));if(sw.kind=='f')check(sc.full.contains(sw.color));deliverable(sg);}
  /* 9. Faint thin outlines survive downscaling as a few long strokes, not dashes: a 1.2 px mid-gray ring and diagonal on a 1500x1000 page are about half a pixel wide at the Standard raster. */
  auto faint=paper(1500,1000);ring(faint,750,500,380,1.2,qRgb(150,150,150));line(faint,100,900,1400,120,1.2,qRgb(150,150,150));tomink::IllustrationStats fs;nativeink::Group fg;
  check(failure([&]{fg=tomink::illustrate(encode(faint),frame(1500,1000,tomink::Detail::Standard,&fs));}).empty());auto fc=census(fg);std::printf("FAINT_OUTLINES lines=%u fills=%u line_recall=%.3f\n",fc.lines,fc.fills,fs.lineRecall);
  check(fc.lines>=2&&fc.lines<=12&&fs.lineRecall>=.85);deliverable(fg);
  /* Thin outlines meeting at a junction must not turn into opaque fill blobs. A separate compact dot remains a fill. */
  {auto joined=paper(600,400);line(joined,100,200,500,200,6,qRgb(20,20,20));line(joined,300,60,300,340,6,qRgb(20,20,20));auto o=frame(600,400,tomink::Detail::Standard,nullptr);o.contourTool="fineliner";o.width=600;o.height=400;auto g=tomink::illustrate(encode(joined),o);auto c=census(g);std::printf("JUNCTION_CONTROL lines=%u fills=%u\n",c.lines,c.fills);check(c.lines>=2&&c.fills==0);deliverable(g);auto dot=paper(600,400);box(dot,295,195,305,205,qRgb(20,20,20));auto dg=tomink::illustrate(encode(dot),o);check(census(dg).fills==1);deliverable(dg);}
  /* 10. Shading islands join their enclosing tone: small full-pink blotches inside a pale pink area come out as one pink region, not a scatter of patches. */
  auto shaded=paper(900,600);box(shaded,150,100,750,500,qRgb(246,200,215));for(int k=0;k<6;++k)box(shaded,230+k*80,200+(k%2)*120,260+k*80,230+(k%2)*120,qRgb(223,108,156));tomink::IllustrationStats ts;nativeink::Group tg;
  check(failure([&]{tg=tomink::illustrate(encode(shaded),frame(900,600,tomink::Detail::Standard,&ts));}).empty());auto tc=census(tg);std::printf("SHADING_ISLANDS fills=%u lines=%u\n",tc.fills,tc.lines);check(tc.fills==1&&tc.lines==0&&tc.tint("pink"));deliverable(tg);
  /* 7. Cancellation is honoured inside the lineArt path. */
  std::atomic<bool> stop{true};check(failure([&]{(void)tomink::illustrate(heart,frame(768,768,tomink::Detail::Standard,nullptr),&stop);})=="tom-ink-canceled");
  std::printf("INK_TRACE_CONTROLS_PASS checks=%u real_fixtures=%u old_native_refused=%s dense_trimmed_lines=%u live_model=FIXTURES_ONLY physical=NOT_RUN\n",checks,realRuns,oldError.c_str(),ds.droppedLines);return 0;}
 catch(const std::exception& e){std::printf("INK_TRACE_CONTROLS_FAIL %s checks=%u\n",e.what(),checks);return 1;}}
