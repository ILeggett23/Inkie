#include "reader.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <cstdio>
using namespace reader;using namespace underlay;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
static std::atomic<bool> stall{false},entered{false},failPage{false};
namespace reader {
// Deterministic eight-page cache fixture only; never linked into production.
underlay::Prepared preparePage(const std::filesystem::path&,const std::filesystem::path& data,int page,const std::atomic<bool>& stop){
 entered=true;while(stall&&!stop)std::this_thread::sleep_for(std::chrono::milliseconds(1));if(stop||failPage)throw std::runtime_error("test-preparation-cancelled");
 Prepared p;p.metadata={{"page",page},{"pages",8},{"pdf",FixtureHash}};p.binding=digest(QJsonDocument(p.metadata).toJson());p.background.assign(Pixels,0xff000000u|unsigned(page));p.ink.assign(Pixels,0xffffffffu);privateDirectory(pagePath(data,page));{Journal j(pagePath(data,page)/"annotations.ink",p.binding);j.replay({p.ink,Width,Height,Stride});p.lastId=j.lastId();p.journalDigest=j.fingerprint();}return p;
}
}
template<class F>void until(F f){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(10);while(!f()){CHECK(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=2)return 64;try{std::filesystem::path data=argv[1];privateDirectory(data);std::atomic<bool> no{false};
 {auto p=preparePage({},data/"cache",0,no);Reader r({},data/"cache",std::move(p));r.activate();unsigned peak=0;
 for(int page=1;page<8;++page){auto* old=r.active().prepared.background.data();auto oldPixel=*old;CHECK(r.request(Action::Next));until([&]{peak=std::max(peak,r.resident());return r.tick();});CHECK(*old==oldPixel);CHECK(r.page()==page);r.installed();r.activate();CHECK(r.resident()<=4);}
 for(int page=6;page>=0;--page){CHECK(r.request(Action::Previous));until([&]{peak=std::max(peak,r.resident());return r.tick();});r.installed();r.activate();CHECK(r.page()==page);CHECK(r.active().prepared.background[0]==(0xff000000u|unsigned(page)));}CHECK(r.finish(2000));std::printf("PASS synthetic_8_page_eviction_active_buffer_pinning peak_pages=%u limit=4\n",peak);}
 {auto p=preparePage({},data/"stall",0,no);stall=true;entered=false;Reader r({},data/"stall",std::move(p));r.activate();until([&]{return entered.load();});CHECK(r.reserve());CHECK(!r.request(Action::Next));CHECK(!r.tick());CHECK(r.writable()&&r.page()==0);r.cancel();CHECK(r.finish(2000));stall=false;std::puts("PASS stalled_prefetch_contact_rejection_and_cancelled_close");}
 std::puts("CACHE_TESTS_PASS fixture=SYNTHETIC_NOT_PDF vendor_calls=0");return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"CACHE_TEST_FAIL %s\n",e.what());return 1;}}
