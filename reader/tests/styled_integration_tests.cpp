#include "styled_ink.h"
#include "product_bridge.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>
#include <set>
#include <cmath>
#include <cstdlib>
#include <new>
#include <limits>
#include <cstring>
#include <algorithm>
#include <cstdio>
#include <map>
#include <vector>
// Probe C++ allocation entry points on this test thread only. This does not
// claim to intercept libc allocation or physical-device/driver allocation.
static thread_local bool countAllocations=false;
static thread_local std::size_t allocationCount=0;
[[gnu::noinline]] void* operator new(std::size_t n){if(countAllocations)++allocationCount;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
[[gnu::noinline]] void* operator new[](std::size_t n){return ::operator new(n);}[[gnu::noinline]] void operator delete(void*p)noexcept{std::free(p);}[[gnu::noinline]] void operator delete[](void*p)noexcept{std::free(p);}[[gnu::noinline]] void operator delete(void*p,std::size_t)noexcept{std::free(p);}[[gnu::noinline]] void operator delete[](void*p,std::size_t)noexcept{std::free(p);}
[[gnu::noinline]] void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
[[gnu::noinline]] void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
[[gnu::noinline]] void operator delete(void*p,const std::nothrow_t&)noexcept{::operator delete(p);}
[[gnu::noinline]] void operator delete[](void*p,const std::nothrow_t&)noexcept{::operator delete(p);}
[[gnu::noinline]] void* operator new(std::size_t n,std::align_val_t a){if(countAllocations)++allocationCount;void* p{};if(!::posix_memalign(&p,std::size_t(a),n?n:1))return p;throw std::bad_alloc();}
[[gnu::noinline]] void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
[[gnu::noinline]] void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
[[gnu::noinline]] void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
[[gnu::noinline]] void operator delete(void*p,std::align_val_t)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete[](void*p,std::align_val_t)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete(void*p,std::size_t,std::align_val_t)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete[](void*p,std::size_t,std::align_val_t)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete(void*p,std::align_val_t,const std::nothrow_t&)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete[](void*p,std::align_val_t,const std::nothrow_t&)noexcept{std::free(p);}
using Path=std::filesystem::path;
static void check(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
template<class F>static void until(F f){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(!f()){check(std::chrono::steady_clock::now()<end,"wait-timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
static void ready(styled::Ink& ink){until([&]{ink.tick();check(!ink.status().failed,"ink-failed");return ink.status().ready&&!ink.status().busy;});}
static QByteArray pixels(styled::Ink& ink){auto p=ink.pixels();return underlay::digest(QByteArray(reinterpret_cast<const char*>(p.data()),p.size_bytes()));}
static void stroke(styled::Ink& ink,int y,bool erase=false){check(ink.reserve(),"reserve");for(auto p:std::array{spike::Point{20,y,spike::Phase::Down,1,erase},spike::Point{100,y,spike::Phase::Move,2,erase},spike::Point{100,y,spike::Phase::Up,3,erase}})check(ink.append(p),"append");ready(ink);}
static constexpr int RW=192,RH=192,RS=200;
static constexpr std::array<styled::Coordinate,4> Trace{{{24.5,24.5},{60.5,40.5},{90.5,95.5},{145.5,130.5}}};
static QByteArray rasterHash(const styled::Raster& raster){auto p=raster.pixels();return underlay::digest(QByteArray(reinterpret_cast<const char*>(p.data()),p.size_bytes()));}
static QByteArray shapeHash(const styled::Raster& raster){QByteArray alpha(RW*RH,Qt::Uninitialized);for(int y=0;y<RH;++y)for(int x=0;x<RW;++x)alpha[y*RW+x]=char(raster.pixels()[y*RS+x]>>24);return underlay::digest(alpha);}
static void draw(styled::Raster& raster,styled::Style style,styled::Matrix matrix={}){check(raster.begin(style,matrix),"raster-begin");for(unsigned n=0;n<Trace.size();++n)check(raster.point(Trace[n],n==0),"raster-point");raster.end();}
static unsigned allRasterCombinations(){
 unsigned combinations=0;std::set<QByteArray> allPixels;std::array<std::set<QByteArray>,3> toolGeometry;
 std::vector<std::uint32_t> paper(RS*RH,0xffffffffu),out(RS*RH,0xff123456u);
 for(unsigned tool=0;tool<9;++tool){std::set<QByteArray> widthGeometry;
  for(unsigned width=0;width<3;++width){QByteArray shape;std::set<QByteArray> colorPixels;
   for(unsigned color=tool==8?1:0;color<8;++color){styled::Style style{styled::Tool(tool),styled::Width(width),color};
    auto encoded=QJsonDocument(styled::jsonStyle(style)).toJson(QJsonDocument::Compact);auto decoded=styled::parseStyle(QJsonDocument::fromJson(encoded).object());
    check(decoded.tool==style.tool&&decoded.width==style.width&&decoded.color==style.color,"all-style-json-roundtrip");
    styled::Raster raster(RW,RH,RS);std::fill(out.begin(),out.end(),0xff123456u);allocationCount=0;countAllocations=true;
    bool accepted=raster.begin(decoded,{});for(unsigned n=0;n<Trace.size();++n)accepted=raster.point(Trace[n],n==0)&&accepted;raster.end();raster.composite({out,RW,RH,RS},paper,{0,0,RW,RH});
    countAllocations=false;check(accepted,"all-style-contact-accepted");check(allocationCount==0,"raster-contact-composite-no-cpp-allocation");
    unsigned covered=0;for(int y=0;y<RH;++y){for(int x=0;x<RW;++x){auto pixel=raster.pixels()[y*RS+x];if(pixel>>24){++covered;check((pixel&0xffffffu)==styled::colorRgb(color),"all-style-palette-rgb");}}for(int x=RW;x<RS;++x)check(out[y*RS+x]==0xff123456u,"raster-composite-preserves-padding");}
    check(covered>0&&!raster.damage().empty(),"all-style-visible-shape");auto alpha=shapeHash(raster);if(shape.isEmpty())shape=alpha;else check(shape==alpha,"palette-does-not-change-geometry");
    auto hash=rasterHash(raster);check(colorPixels.insert(hash).second,"each-color-distinct-output");check(allPixels.insert(hash).second,"each-legal-style-distinct-output");++combinations;
   }
   check(widthGeometry.insert(shape).second,"each-width-distinct-geometry");check(toolGeometry[width].insert(shape).second,"nine-position-driven-tools-distinct-geometry");
  }
  check(widthGeometry.size()==3,"all-three-widths");
 }
 check(combinations==213&&allPixels.size()==213,"all-legal-tool-width-color-combinations");for(auto& geometry:toolGeometry)check(geometry.size()==9,"nine-tool-geometry-signatures");
 for(unsigned width=0;width<3;++width){styled::Raster r(RW,RH,RS);check(!r.begin({styled::Tool::Highlighter,styled::Width(width),0},{}),"highlighter-black-refused");bool rejected=false;try{styled::jsonStyle({styled::Tool::Highlighter,styled::Width(width),0});}catch(...){rejected=true;}check(rejected,"illegal-style-serialization-refused");}
 { /* Highlighter on the Paper Pro: ink multiplies the page, so text under a highlight stays black (photo of 19x showed
     washed-out text with "over" blending); on white paper the highlight keeps its tint. */
  std::vector<std::uint32_t> page(std::size_t(RS)*RH,0xffffffffu),shown(std::size_t(RS)*RH,0);for(int y=0;y<RH;++y)for(int x=0;x<RW/2;++x)page[std::size_t(y)*RS+x]=0xff000000u;
  styled::Raster marker(RW,RH,RS);check(marker.begin({styled::Tool::Highlighter,styled::Width::Thick,4},{}),"highlight-begin");
  for(int x=4;x<RW-4;x+=4)check(marker.point({double(x),96.5},x==4),"highlight-point");marker.end();marker.composite({shown,RW,RH,RS},page,{0,0,RW,RH});
  const auto text=shown[std::size_t(96)*RS+RW/4],paper=shown[std::size_t(96)*RS+3*RW/4];
  check(text==0xff000000u,"highlight-keeps-text-black");check(paper!=0xffffffffu&&(paper&0xff)<(paper>>16&0xff),"highlight-tints-white-paper");}
 std::cout<<"STYLED_RASTER_MATRIX_PASS combinations="<<combinations<<" serialization_roundtrips="<<combinations<<" per_contact_cpp_allocations=0 sensors=POSITION_ONLY\n";return combinations;
}
static void transforms(){
 const std::array<styled::Matrix,5> matrices{{{},{{1,0,0,1,17,23}},{{2,0,0,2,4,6}},{{0,1,-1,0,192,0}},{{2,0,0,1,5,11}}}};
 for(auto m:matrices){check(m.valid(),"matrix-valid");for(auto point:Trace){double x,y,backX,backY;check(m.map(point.x,point.y,x,y)&&m.map(x,y,backX,backY,true),"matrix-forward-inverse");check(std::abs(backX-point.x)<1e-9&&std::abs(backY-point.y)<1e-9,"matrix-roundtrip-coordinate");}}
 for(unsigned tool=0;tool<9;++tool){styled::Style style{styled::Tool(tool),styled::Width::Medium,6};styled::Raster original(RW,RH,RS),pan(RW,RH,RS),rotation(RW,RH,RS);draw(original,style);draw(pan,style,matrices[1]);draw(rotation,style,matrices[3]);
  for(int y=0;y<RH;++y)for(int x=0;x<RW;++x){auto p=original.pixels()[y*RS+x];check(rotation.pixels()[x*RS+(191-y)]==p,"quarter-turn-raster-coordinate-equivalence");if(x+17<RW&&y+23<RH)check(pan.pixels()[(y+23)*RS+x+17]==p,"pan-raster-coordinate-equivalence");else check((p>>24)==0,"pan-fixture-not-clipped");}
 }
 styled::Raster scaled(RW,RH,RS);check(scaled.begin({styled::Tool::Marker,styled::Width::Thick,6},matrices[2])&&scaled.point({40.25,50.25},true),"scaled-point");scaled.end();check((scaled.pixels()[106*RS+84]>>24)>0&&scaled.pixels()[50*RS+40]==0,"scale-and-pan-map-raster-location");
 for(auto bad:std::array<styled::Matrix,3>{{{{1,0,0,0,0,0}},{{17,0,0,17,0,0}},{{1,0,0,1,std::numeric_limits<double>::infinity(),0}}}}){double x=123,y=456;check(!bad.valid()&&!bad.map(1,2,x,y),"invalid-transform-refused");styled::Raster r(RW,RH,RS);check(!r.begin({styled::Tool::Ballpoint,styled::Width::Medium,0},bad),"invalid-transform-no-raster-admission");}
 std::cout<<"STYLED_TRANSFORM_PASS forward_inverse=5 pan_and_rotation_tools=9 scale_location=1 invalid=3\n";
}
static void allDurableCombinations(){QTemporaryDir temp;check(temp.isValid(),"matrix-temp");Path root=Path(temp.path().toStdString())/"library";underlay::privateDirectory(root);underlay::atomicFile(root/"matrix.pdf","%PDF-1.7 authored style matrix");library::Document doc;{library::Store store(root);doc=store.importDocument(root/"matrix.pdf","Durable matrix",[](const Path&,const QString&){return library::Inspection{1,"pdf-source-v1",{}};});}underlay::Prepared prepared;prepared.metadata={{"schema",2},{"document",doc.id},{"pdf",doc.sha256},{"documentFormat",doc.format},{"layout",doc.layout},{"pages",1},{"page",0},{"bounds",QJsonArray{0,0,160,160}},{"pageToSurface",QJsonArray{1,0,0,1,0,0}}};prepared.binding=underlay::digest(QJsonDocument(prepared.metadata).toJson(QJsonDocument::Compact));auto page=product::pageSnapshot(doc,prepared);auto feature=product::featureDirectory(root,doc);auto baseline=feature/"legacy/matrix.inkbase";std::vector<std::uint32_t> legacy(160*160,0xffffffffu);QByteArray before;unsigned count=0;
 {styled::Ink ink(feature,baseline,page,legacy,160,160,160);ready(ink);for(unsigned t=0;t<9;++t){check(ink.selectTool(styled::Tool(t)),"matrix-tool");ready(ink);for(unsigned w=0;w<3;++w){check(ink.selectWidth(styled::Width(w)),"matrix-width");ready(ink);for(unsigned c=t==8?1:0;c<8;++c){check(ink.selectColor(c),"matrix-color");ready(ink);stroke(ink,10+int(count%9)*15);++count;}}}check(count==213&&ink.snapshot()["documents"].toObject()[doc.id].toObject()["strokes"].toArray().size()==213,"matrix-no-eviction");before=pixels(ink);check(ink.finish(5000),"matrix-drain");}
 {styled::Ink ink(feature,baseline,page,legacy,160,160,160);ready(ink);check(pixels(ink)==before,"matrix-reopened-render-identical");auto records=ink.snapshot()["documents"].toObject()[doc.id].toObject()["strokes"].toArray();std::set<QByteArray> styles;std::set<QString> ids;for(auto value:records){auto record=value.toObject();styles.insert(QJsonDocument(record["style"].toObject()).toJson(QJsonDocument::Compact));ids.insert(record["id"].toString());check(record["anchor"].toObject()["document"]==doc.id&&record["points"].toArray().size()==3,"matrix-geometry-reopened");}check(records.size()==213&&styles.size()==213&&ids.size()==213,"matrix-all-identities-and-styles-reopened");check(ink.finish(5000),"matrix-reopen-drain");}std::cout<<"STYLED_DURABLE_MATRIX_PASS combinations=213 geometry_style_reopen=true no_eviction=true physical=NOT_RUN\n";}
// D2 page-turn latency: a page without legacy pixels freezes as a 40-byte blank marker; legacy pixels keep the full
// INKBASE1 copy; an existing full copy that is blank is rewritten once as the marker; a corrupt marker fails closed.
static void legacyBaselines(){QTemporaryDir temp;check(temp.isValid(),"baseline-temp");Path root=Path(temp.path().toStdString())/"library";underlay::privateDirectory(root);underlay::atomicFile(root/"base.pdf","%PDF-1.7 baseline");library::Document doc;{library::Store store(root);doc=store.importDocument(root/"base.pdf","Baselines",[](const Path&,const QString&){return library::Inspection{1,"pdf-source-v1",{}};});}underlay::Prepared prepared;prepared.metadata={{"schema",2},{"document",doc.id},{"pdf",doc.sha256},{"documentFormat",doc.format},{"layout",doc.layout},{"pages",1},{"page",0},{"bounds",QJsonArray{0,0,160,160}},{"pageToSurface",QJsonArray{1,0,0,1,0,0}}};prepared.binding=underlay::digest(QJsonDocument(prepared.metadata).toJson(QJsonDocument::Compact));auto page=product::pageSnapshot(doc,prepared);auto feature=product::featureDirectory(root,doc);
 const std::vector<std::uint32_t> blank(160*160,0xffffffffu);auto withInk=blank;for(int y=40;y<44;++y)for(int x=30;x<90;++x)withInk[std::size_t(y)*160+x]=0xff202020u;const qint64 full=160*160*4+40;
 auto size=[](const Path& p){return qint64(std::filesystem::file_size(p));};
 {auto path=feature/"legacy/blank.inkbase";QByteArray first;{styled::Ink ink(feature,path,page,blank,160,160,160);ready(ink);first=pixels(ink);check(!ink.hasLegacyInk(),"blank-no-legacy");}
  auto bytes=underlay::readBounded(path,full);check(bytes.size()==40&&bytes.first(8)=="INKBASE0","blank-page-writes-40-byte-marker");
  {styled::Ink ink(feature,path,page,withInk,160,160,160);ready(ink);check(pixels(ink)==first&&!ink.hasLegacyInk(),"blank-marker-stays-frozen-blank");}}
 {auto path=feature/"legacy/inked.inkbase";QByteArray first;{styled::Ink ink(feature,path,page,withInk,160,160,160);ready(ink);first=pixels(ink);check(ink.hasLegacyInk(),"legacy-ink-present");}
  check(size(path)==full&&underlay::readBounded(path,full).first(8)=="INKBASE1","legacy-pixels-keep-full-copy");
  {styled::Ink ink(feature,path,page,blank,160,160,160);ready(ink);check(pixels(ink)==first&&ink.hasLegacyInk(),"full-copy-stays-frozen");}}
 {auto path=feature/"legacy/old-blank.inkbase";auto bytes=QByteArray(reinterpret_cast<const char*>(blank.data()),qsizetype(blank.size()*4));underlay::atomicFile(path,QByteArray("INKBASE1",8)+underlay::digest(bytes)+bytes);
  {styled::Ink ink(feature,path,page,withInk,160,160,160);ready(ink);check(!ink.hasLegacyInk(),"old-blank-copy-read-blank");}
  auto now=underlay::readBounded(path,full);check(now.size()==40&&now.first(8)=="INKBASE0","old-blank-copy-rewritten-as-marker");}
 {auto path=feature/"legacy/bad.inkbase";underlay::atomicFile(path,QByteArray("INKBASE0",8)+QByteArray(32,'x'));std::fflush(nullptr);
  /* A failed Ink ends its process on destruction (fail-safe), so the corrupt marker is exercised in a child. */
  const pid_t child=fork();check(child>=0,"corrupt-marker-fork");
  if(child==0){try{styled::Ink ink(feature,path,page,blank,160,160,160);auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(!ink.status().failed&&!ink.status().ready&&std::chrono::steady_clock::now()<end){ink.tick();std::this_thread::sleep_for(std::chrono::milliseconds(1));}std::fflush(nullptr);std::_Exit(ink.status().failed&&!ink.status().ready?42:1);}catch(...){std::_Exit(2);}}
  int status=0;check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&WEXITSTATUS(status)==42,"corrupt-marker-fails-closed");}
 std::cout<<"STYLED_BASELINE_PASS blank_marker=40 legacy_full=true old_blank_rewritten=true corrupt_fails=true\n";
}
static void unifiedTextHighlights(){QJsonArray first{QJsonArray{10,10,60,10,10,28,60,28}},second{QJsonArray{10,30,60,30,10,48,60,48}};styled::Raster unioned(100,80,100);unioned.beginHighlights();unioned.highlight(first,{},4,{},true);unioned.highlight(second,{},4,{},true);auto pixels=unioned.pixels();check(pixels[15*100+20]==pixels[29*100+20],"adjacent record overlap is one tint");const auto once=rasterHash(unioned);unioned.highlight(first,{},4,{},true);check(rasterHash(unioned)==once,"duplicate same-color semantic record is idempotent");unioned.highlight(first,{},6,{},true);check(rasterHash(unioned)!=once,"different color retains blending");unioned.beginHighlights();unioned.highlight(second,{},4,{},true);check(unioned.pixels()[35*100+20]>>24>128,"separate layer retains blending");std::vector<std::uint32_t> page(8000,0xffffffffu),shown(8000);page[15*100+20]=0xff000000u;unioned.composite({shown,100,80,100},page,{0,0,100,80});check(shown[15*100+20]==0xff000000u,"unified highlights keep dark text");}
static void actualHighlightReplay(){QTemporaryDir temp;check(temp.isValid(),"highlight-replay-temp");Path root=Path(temp.path().toStdString())/"library";underlay::privateDirectory(root);underlay::atomicFile(root/"highlights.pdf","%PDF-1.7 authored overlap regression");library::Document doc;{library::Store store(root);doc=store.importDocument(root/"highlights.pdf","Highlight replay",[](const Path&,const QString&){return library::Inspection{1,"pdf-source-v1",{}};});}underlay::Prepared prepared;prepared.metadata={{"schema",2},{"document",doc.id},{"pdf",doc.sha256},{"documentFormat",doc.format},{"layout",doc.layout},{"pages",1},{"page",0},{"bounds",QJsonArray{0,0,160,160}},{"pageToSurface",QJsonArray{1,0,0,1,0,0}}};prepared.binding=underlay::digest(QJsonDocument(prepared.metadata).toJson(QJsonDocument::Compact));auto page=product::pageSnapshot(doc,prepared);auto feature=product::featureDirectory(root,doc);QJsonArray records;
 {product::Services service(root,doc);auto state=service.snapshot()["documents"].toObject()[doc.id].toObject();for(int y:{10,30}){QJsonObject anchor{{"document",doc.id},{"revision",doc.sha256},{"format","pdf"},{"page",0},{"pageId",page["pageId"]},{"layoutId",doc.layout},{"version",1},{"scope","selection"},{"excerpt","Sentence"},{"start",0},{"end",8},{"quads",QJsonArray{QJsonArray{10,y,60,y,10,y+18,60,y+18}}}};service.perform({{"op","highlight.add"},{"document",doc.id},{"layer",state["activeLayer"]},{"color","yellow"},{"anchor",anchor}});}records=service.snapshot()["documents"].toObject()[doc.id].toObject()["highlights"].toArray();check(records.size()==2,"two independent saved sentence records");}
 std::vector<std::uint32_t> legacy(160*160,0xffffffffu);QByteArray saved;for(int pass=0;pass<2;++pass){styled::Ink ink(feature,feature/"legacy/highlights.inkbase",page,legacy,160,160,160);ready(ink);check(ink.pixels()[15*160+20]==ink.pixels()[29*160+20],"actual worker replay unions adjacent records");check(ink.snapshot()["documents"].toObject()[doc.id].toObject()["highlights"].toArray()==records,"render does not rewrite saved highlight records");if(pass==0){saved=pixels(ink);check(ink.undo(),"highlight undo admitted");ready(ink);check(ink.snapshot()["documents"].toObject()[doc.id].toObject()["highlights"].toArray().size()==1,"highlight undo retains one record");check(ink.redo(),"highlight redo admitted");ready(ink);check(pixels(ink)==saved,"highlight redo restores union pixels");}else check(pixels(ink)==saved,"highlight reopen restores union pixels");check(ink.finish(2000),"highlight replay drain");}std::cout<<"HIGHLIGHT_RECORD_REPLAY_PASS records=2 undo_redo=exact reopen=exact no_record_merge=true\n";}
int main(int argc,char**argv){QCoreApplication app(argc,argv);try{actualHighlightReplay();unifiedTextHighlights();legacyBaselines();allDurableCombinations();allRasterCombinations();transforms();QTemporaryDir temp;check(temp.isValid(),"temp");Path root=Path(temp.path().toStdString())/"library";underlay::privateDirectory(root);underlay::atomicFile(root/"synthetic.pdf","%PDF-1.7 authored");library::Document doc;{library::Store store(root);doc=store.importDocument(root/"synthetic.pdf","Styled",[](const Path&,const QString&){return library::Inspection{2,"pdf-source-v1",{}};});}
 underlay::Prepared p;p.metadata={{"schema",2},{"document",doc.id},{"pdf",doc.sha256},{"documentFormat",doc.format},{"layout",doc.layout},{"pages",2},{"page",0},{"bounds",QJsonArray{0,0,160,160}},{"pageToSurface",QJsonArray{1,0,0,1,0,0}}};p.binding=underlay::digest(QJsonDocument(p.metadata).toJson(QJsonDocument::Compact));auto page=product::pageSnapshot(doc,p);auto feature=product::featureDirectory(root,doc),baseline=feature/"legacy/test.inkbase";std::vector<std::uint32_t> legacy(160*160,0xffffffffu);QByteArray saved;
 {styled::Ink ink(feature,baseline,page,legacy,160,160,160);ready(ink);auto blank=pixels(ink);for(unsigned t=0;t<9;++t){check(ink.selectTool(styled::Tool(t)),"select-tool");ready(ink);check(ink.selectWidth(styled::Width::Thick),"select-width");ready(ink);check(ink.selectColor(t==8?4:6),"select-color");ready(ink);stroke(ink,12+int(t)*15);}
 saved=pixels(ink);check(saved!=blank,"rendered-colors");auto document=ink.snapshot()["documents"].toObject()[doc.id].toObject();check(document["strokes"].toArray().size()==9,"single-canonical-record-per-stroke");for(auto v:document["strokes"].toArray()){auto s=v.toObject();check(s["sensors"]=="position-only; pressure and tilt unavailable","sensor-provenance");check(s["anchor"].toObject()["document"]==doc.id,"document-identity");}
 check(ink.undo(),"undo");ready(ink);check(pixels(ink)!=saved,"undo-visible");check(ink.redo(),"redo");ready(ink);check(pixels(ink)==saved,"redo-identical");
 check(ink.reserve(),"cancel-reserve");check(ink.append({30,150,spike::Phase::Down,4,false}),"cancel-down");check(!ink.selectTool(styled::Tool::Pencil),"no-tool-switch-midcontact");ink.cancel();check(pixels(ink)==saved,"cancel-restores");check(ink.finish(2000),"finish");}
 {styled::Ink ink(feature,baseline,page,legacy,160,160,160);ready(ink);check(pixels(ink)==saved,"durable-replay-identical");check(ink.style().tool==styled::Tool::Highlighter&&ink.style().width==styled::Width::Thick&&ink.style().color==4,"tool-settings-reopen");stroke(ink,132,true);auto erased=pixels(ink);check(erased!=saved,"back-eraser-visible");check(ink.undo(),"undo-eraser");ready(ink);check(pixels(ink)==saved,"undo-eraser-identical");check(ink.setEraser(true),"toolbar-eraser");stroke(ink,132);check(pixels(ink)==erased,"both-erasers-same-canonical-path");check(ink.finish(2000),"reopen-finish");}
 // D2 precise eraser: a short eraser pass across the middle of a line removes only the crossed part; both ends stay as
 // two pieces with the original endpoints, and one undo restores the single original stroke.
 {styled::Ink ink(feature,baseline,page,legacy,160,160,160);ready(ink);check(ink.selectTool(styled::Tool::Ballpoint),"precise-tool");ready(ink);check(ink.selectWidth(styled::Width::Thin),"precise-width");ready(ink);
  auto strokes=[&]{std::map<QString,QJsonArray> all;for(auto dv:ink.snapshot()["documents"].toObject())for(auto sv:dv.toObject()["strokes"].toArray())all[sv.toObject()["id"].toString()]=sv.toObject()["points"].toArray();return all;};
  auto added=[](const std::map<QString,QJsonArray>& now,const std::map<QString,QJsonArray>& before){std::vector<QJsonArray> fresh;for(const auto& [key,pts]:now)if(!before.count(key))fresh.push_back(pts);return fresh;};
  auto before=strokes();stroke(ink,48);auto drawn=strokes();auto line=added(drawn,before);check(line.size()==1&&drawn.size()==before.size()+1,"precise-line-drawn");auto first=line[0].first().toArray()[0].toDouble(),last=line[0].last().toArray()[0].toDouble();
  check(ink.reserve(),"precise-reserve");for(auto p:std::array{spike::Point{60,30,spike::Phase::Down,1,true},spike::Point{60,66,spike::Phase::Move,2,true},spike::Point{60,66,spike::Phase::Up,3,true}})check(ink.append(p),"precise-append");ready(ink);
  /* earlier cases left other strokes on this page that the pass may also cut: only the pieces on this line's row count */
  auto cut=strokes();auto row=line[0].first().toArray()[1].toDouble();std::vector<QJsonArray> pieces;for(auto& pts:added(cut,drawn)){bool same=true;for(auto pv:pts)same&=pv.toArray()[1].toDouble()==row;if(same)pieces.push_back(pts);}
  check(pieces.size()==2,"precise-two-pieces");
  std::sort(pieces.begin(),pieces.end(),[](const QJsonArray& a,const QJsonArray& b){return a.first().toArray()[0].toDouble()<b.first().toArray()[0].toDouble();});
  double leftEnd=pieces[0].last().toArray()[0].toDouble(),rightStart=pieces[1].first().toArray()[0].toDouble();
  check(pieces[0].first().toArray()[0].toDouble()==first&&pieces[1].last().toArray()[0].toDouble()==last,"precise-endpoints-kept");
  check(leftEnd<rightStart&&rightStart-leftEnd<(last-first)*0.6,"precise-gap-only-where-crossed");
  check(ink.undo(),"precise-undo");ready(ink);check(strokes()==drawn,"precise-undo-restores");  /* E2 session 20: a long eraser scribble across many strokes ended the session (worker work capacity). It must complete. */
  for(int y=8;y<152;y+=4)stroke(ink,y);auto dense=strokes();check(ink.reserve(),"heavy-reserve");
  for(unsigned n=0;n<1200;++n){int x=10+int((n*7)%140),y=6+int(n*146/1200);auto phase=n==0?spike::Phase::Down:n==1199?spike::Phase::Up:spike::Phase::Move;check(ink.append(spike::Point{x,y,phase,spike::Ns(n+1),true}),"heavy-append");}
  ready(ink);check(!ink.status().failed&&strokes()!=dense,"heavy-erase-completes");
check(ink.finish(2000),"precise-finish");}
 {auto pinned=feature/"legacy/pinned.inkbase";auto withLegacy=legacy;withLegacy[150*160+150]=0xff000000u;QByteArray pinnedPixels;
  {styled::Ink ink(feature,pinned,page,withLegacy,160,160,160);ready(ink);check(ink.hasLegacyInk(),"legacy-ink-detected");pinnedPixels=pixels(ink);auto state=ink.snapshot();auto style=ink.style();
   for(auto transform:std::array<styled::Matrix,3>{{{{1,0,0,1,10,5}},{{0,1,-1,0,160,0}},{{2,0,0,2,0,0}}}}){check(!ink.setTransform(transform),"legacy-viewport-change-refused");check(pixels(ink)==pinnedPixels&&ink.snapshot()==state&&ink.status().queued==0,"legacy-refusal-preserves-pixels-and-state");check(ink.style().tool==style.tool&&ink.style().width==style.width&&ink.style().color==style.color,"legacy-refusal-preserves-style");}
   check(std::string(ink.status().label)=="LEGACY INK - VIEW IS PINNED","legacy-pin-visible");check(ink.finish(2000),"legacy-finish");}
  {styled::Ink ink(feature,pinned,page,legacy,160,160,160);ready(ink);check(ink.hasLegacyInk()&&pixels(ink)==pinnedPixels,"legacy-baseline-reopen-preserves-ink");check(ink.finish(2000),"legacy-reopen-finish");}
 }
 std::cout<<"styled integration passed raster213 styles, nine-tool worker durable settings/replay, undo/redo, both erasers, cancellation, transforms, legacy pin; all213 durable permutations PASS; physical NOT_RUN\n";return 0;}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
