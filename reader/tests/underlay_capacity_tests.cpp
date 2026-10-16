// D2 user-ended sessions: page ink capacity must never end a session.
// 1. With every save slot busy (delayed real writer) the saver reports it cannot take a stroke
//    (canReserve() false) so the reader closes ink admission and the session gate skips the
//    pen-down; once the writer commits, admission reopens.
// 2. A page holds far more than the old 128-stroke cap, and the journal reloads intact.
#include "underlay.h"
#include <QCoreApplication>
#include <chrono>
#include <cstdio>
#include <thread>
using namespace underlay;
static int fails=0;
#define CHECK(c) do{if(!(c)){std::fprintf(stderr,"UNDERLAY_CAPACITY_FAIL line=%d %s\n",__LINE__,#c);++fails;}}while(0)
static bool stroke(Pipeline& p,int y){
 if(!p.reserve())return false;
 spike::Point a{100,y,spike::Phase::Down,1,false},b{140,y,spike::Phase::Move,2,false},c{180,y,spike::Phase::Up,3,false};
 return p.append(a)&&p.append(b)&&p.append(c);
}
template<class F> static bool until(F f,int ms=20000){for(int i=0;i<ms;++i){if(f())return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return f();}
int main(int argc,char** argv){
 QCoreApplication app(argc,argv);if(argc!=3)return 64;const std::filesystem::path root=argv[1],work=argv[2];
 constexpr int Many=300;
 try{
  {auto d=work/"gate";auto prep=prepare(root,d);IoFault fault;fault.delayMs=200;Pipeline p(d/"annotations.ink",prep,fault);
   for(int i=0;i<int(Slots);++i){CHECK(p.canReserve());CHECK(stroke(p,200+i*10));}
   CHECK(!p.canReserve());CHECK(!p.reserve()); // gate closed: the stroke is skipped, nothing is refused mid-session
   CHECK(until([&]{return p.committed.load()>=Slots;}));
   CHECK(p.canReserve());CHECK(stroke(p,300));CHECK(p.finish(10000)&&p.unsaved()==0&&!p.failed());}
  {auto d=work/"many";
   {auto prep=prepare(root,d);Pipeline p(d/"annotations.ink",prep);
    for(int i=0;i<Many;++i){CHECK(until([&]{return p.canReserve();}));CHECK(stroke(p,200+(i%1400)));}
    CHECK(until([&]{return p.committed.load()>=unsigned(Many);}));CHECK(p.finish(10000)&&p.unsaved()==0&&!p.failed());}
   {auto prep=prepare(root,d);CHECK(prep.lastId==std::uint64_t(Many));Pipeline p(d/"annotations.ink",prep);CHECK(stroke(p,600));CHECK(p.finish(10000)&&p.unsaved()==0);}
   {auto prep=prepare(root,d);CHECK(prep.lastId==std::uint64_t(Many+1));}}
 }catch(const std::exception& e){std::fprintf(stderr,"UNDERLAY_CAPACITY_FAIL %s\n",e.what());return 1;}
 if(fails)return 1;
 std::printf("UNDERLAY_CAPACITY_PASS slots=%u gate=closed_then_reopened strokes=%d reload=%d\n",unsigned(Slots),Many+1,Many+1);return 0;
}
