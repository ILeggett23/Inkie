// SPDX-License-Identifier: AGPL-3.0-only
// D2 user-ended sessions: one very long pen stroke (far beyond any saver's per-stroke bound) on a
// real imported PDF page, through the real ux::Display sink the session uses. The stroke must be
// accepted end to end (never refused, so the session would continue), saved as consecutive pieces
// that join at the same point, and leave nothing unsaved.
#include "ux_support.h"
#include "ux_display.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
using namespace ux;
namespace {
input_event ev(int type,int code,int value){input_event e{};e.type=type;e.code=code;e.value=value;return e;}
struct NoCalls final:spike::VendorCalls {
 bool acquire(spike::Pixels&)noexcept override{return false;}void region(spike::Rect,int,int,int)noexcept override{}void events()noexcept override{}
};
struct SustainedLease final:spike::RecoveryLease {
 bool valid()const noexcept override{return true;}spike::Ns deadline()const noexcept override{return spike::monotonic()+3600000000000ull;}
 bool stopRequested()const noexcept override{return false;}spike::SustainedPolicy sustained()const noexcept override{return {1000000000ull,1000,1000000};}
};
// Records of one page journal: [count of points, first point, last point] per record.
struct Piece {int count{},x0{},y0{},x1{},y1{};};
std::vector<Piece> pieces(const QByteArray& raw){
 std::vector<Piece> out;qsizetype at=40;
 while(at+12<=raw.size()){CHECK(raw.mid(at,8)=="INKOP001");quint32 n=0;for(int i=3;i>=0;--i)n=(n<<8)|quint8(raw[at+8+i]); // little-endian (underlay frame)
  auto o=QJsonDocument::fromJson(raw.mid(at+12,n)).object();auto pts=o["points"].toArray();Piece p;p.count=int(pts.size());
  if(p.count){auto a=pts.first().toArray(),b=pts.last().toArray();p.x0=a[0].toInt();p.y0=a[1].toInt();p.x1=b[0].toInt();p.y1=b[1].toInt();}
  out.push_back(p);at+=12+n+32;}
 return out;
}
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=5)return 64;
 const std::filesystem::path source=argv[1],data=argv[2],binaries=argv[3],sys=argv[4];constexpr int Points=40000;
 try{underlay::privateDirectory(data);auto root=stage(source,data,binaries,sys);
  Shell s(root,data/"main");Canvas c(s);CHECK(c.submit({0,0,1620,2160})==spike::Submit::Accepted);
  CHECK(s.choose(0));c.changes();c.ready();CHECK(s.state()==State::Clean&&s.inkAdmission());
  NoCalls calls;spike::VendorDisplay vendor(calls);ux::Display display(vendor,s);spike::OperationSink* sink=display.operationSink();SustainedLease lease;
  auto engine=std::make_unique<spike::Engine>(spike::Transform{{0,1619},{0,2159},1620,2160,0,false},s.ink(),c);
  spike::FrameAssembler a;spike::Scheduler scheduler;spike::SessionStats stats;spike::Ns now=1;
  auto feed=[&](std::span<const input_event> e){now+=2000000;CHECK(spike::consumeEvents(e,a,*engine,scheduler,stats,now,&lease,sink,nullptr));
   if(!engine->dirty().empty())CHECK(engine->flush()==spike::Submit::Accepted);if(engine->retirable())engine->retireSubmitted();};
  auto x=[](int i){int k=i%2800;return 100+(k<1400?k:2799-k);};auto y=[](int i){return 300+(i/1400)*2;};
  std::array down={ev(EV_KEY,BTN_TOOL_PEN,1),ev(EV_KEY,BTN_TOOL_RUBBER,0),ev(EV_ABS,ABS_X,x(0)),ev(EV_ABS,ABS_Y,y(0)),ev(EV_KEY,BTN_TOUCH,1),ev(EV_SYN,SYN_REPORT,0)};feed(down);
  for(int i=1;i<Points;++i){std::array move={ev(EV_ABS,ABS_X,x(i)),ev(EV_ABS,ABS_Y,y(i)),ev(EV_SYN,SYN_REPORT,0)};feed(move);}
  std::array up={ev(EV_KEY,BTN_TOUCH,0),ev(EV_SYN,SYN_REPORT,0)};feed(up);
  CHECK(!stats.failed&&!stats.cancelled&&stats.contacts==1&&stats.released==1);
  until([&]{s.tick();return s.document()->unsaved()==0;});CHECK(!s.failed()&&s.document()->committed()>=3);
  auto journal=s.document()->active().storage/"annotations.ink";auto list=pieces(underlay::readBounded(journal,64*1024*1024));
  int total=0;CHECK(list.size()>=3);for(std::size_t i=0;i<list.size();++i){total+=list[i].count;CHECK(list[i].count>=2&&list[i].count<=int(underlay::MaxPoints));if(i)CHECK(list[i].x0==list[i-1].x1&&list[i].y0==list[i-1].y1);}
  CHECK(total>=Points&&total<=Points+int(list.size()));
  CHECK(s.action(Action::Exit));CHECK(s.finish(5000));
  std::printf("LONG_STROKE_ROUTE_PASS points=%d pieces=%zu saved_points=%d joined=1 failed=0\n",Points,list.size(),total);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"LONG_STROKE_ROUTE_FAIL %s\n",e.what());return 1;}}
