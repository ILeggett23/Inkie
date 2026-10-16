#include "reader.h"
#include "touch.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <fstream>
#include <cstdio>
#include <sys/stat.h>
using namespace reader;
using namespace underlay;
#define CHECK(x) do{if(!(x))throw std::runtime_error(#x);}while(0)
template<class F>void rejects(F f){bool yes=false;try{f();}catch(...){yes=true;}CHECK(yes);}
template<class F>void until(F f){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!f()){CHECK(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(2));}}
void turn(Reader& r,Action a){CHECK(r.request(a));until([&]{if(r.tick()){r.installed();r.activate();return true;}return false;});}
void operation(Reader& r,bool erase,int x,int y,int end){CHECK(r.reserve());spike::Point prev{x,y,spike::Phase::Down,1,erase};
 for(auto p:std::array{prev,spike::Point{end,y,spike::Phase::Move,2,erase},spike::Point{end,y,spike::Phase::Up,3,erase}}){auto& ink=r.active().prepared.ink;spike::segment({ink,Width,Height,Stride},prev,p,erase?4:2,erase?0xffffffffu:0xff000000u);CHECK(r.append(p));prev=p;}}
QByteArray hashInk(const Prepared& p){return digest(QByteArray(reinterpret_cast<const char*>(p.ink.data()),Pixels*4)).toHex();}
input_event ev(int type,int code,int value){input_event e{};e.type=type;e.code=code;e.value=value;return e;}
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=5)return 64;std::filesystem::path source=argv[1],data=argv[2],build=argv[3],sys=argv[4];unsigned count=0;
 auto test=[&](const char* name,auto fn){fn();++count;std::printf("PASS %s\n",name);std::fflush(stdout);};
 try{privateDirectory(data);auto stage=data/"application";privateDirectory(stage/"bin");privateDirectory(stage/"fixtures");
 std::filesystem::copy_file(source/"fixtures/inkbound-device-test.pdf",stage/"fixtures/inkbound-device-test.pdf");
 std::filesystem::copy_file(build/"reader-experience1-prepare",stage/"bin/helper-real");
 // Host SDK loader adapter only, outside production package. Runs the actual
 // production helper CLI, which discovers the staged fixture from its ELF path.
 auto wrapper=QByteArray("#!/bin/sh\nexec '")+(sys/"usr/lib/ld-linux-aarch64.so.1").string().c_str()+"' --library-path '"+(sys/"usr/lib").string().c_str()+"' '"+(stage/"bin/helper-real").string().c_str()+"' \"$@\"\n";
 atomicFile(stage/"bin/reader-experience1-prepare",wrapper);chmod((stage/"bin/reader-experience1-prepare").c_str(),0700);
 auto root=data/"main";auto first=initialPrepare(stage,root);
 test("actual_two_page_document_and_distinct_binding",[&]{auto second=prepare(stage,pagePath(data/"geometry",1),1,false);CHECK(first.metadata["pages"].toInt()==2&&second.metadata["page"].toInt()==1&&first.binding!=second.binding);CHECK(first.background!=second.background);for(auto* p:{&first,&second}){std::ofstream image(data/(p==&first?"page0.pgm":"page1.pgm"),std::ios::binary);image<<"P5\n1620 2160\n255\n";for(int y=0;y<Height;++y)for(int x=0;x<Width;++x)image.put(char(p->background[y*Stride+x]&255));}});
 test("invalid_page_and_document_rejected",[&]{rejects([&]{prepare(stage,data/"badpage",2,false);});rejects([&]{prepare(stage,data/"negative",-1,false);});rejects([&]{initialPrepare(data/"missing",data/"missing-data");});});
 QByteArray zero,one;
 test("navigation_contact_page_return_and_pending_commits",[&]{Reader r(stage,root,std::move(first));CHECK(!r.writable());r.activate();CHECK(!r.request(Action::Previous));CHECK(!r.request(Action::Next,true));CHECK(r.page()==0);auto background=r.active().prepared.background;
  operation(r,false,80,200,900);operation(r,true,300,200,600);zero=hashInk(r.active().prepared);CHECK(r.active().prepared.ink[200*Stride+450]==0xffffffffu&&r.active().prepared.ink[200*Stride+100]==0xff000000u);CHECK(r.active().prepared.background==background);
  CHECK(r.request(Action::Next));CHECK(!r.request(Action::Next));CHECK(!r.reserve());until([&]{if(r.tick()){r.installed();r.activate();return true;}return false;});CHECK(r.page()==1);CHECK(!r.request(Action::Next));CHECK(r.active().prepared.ink[200*Stride+100]==0xffffffffu);
  operation(r,false,100,500,700);one=hashInk(r.active().prepared);turn(r,Action::Previous);CHECK(r.page()==0&&hashInk(r.active().prepared)==zero&&r.active().prepared.background==background);turn(r,Action::Next);CHECK(hashInk(r.active().prepared)==one);CHECK(r.resident()<=4);CHECK(r.request(Action::Exit));CHECK(r.finish(2000)&&!r.failed()&&r.committed()==3&&r.unsaved()==0);CHECK(r.finish());
 });
 atomicFile(data/"expected0",zero);atomicFile(data/"expected1",one);
 test("last_page_resume_replay_and_return",[&]{auto p=initialPrepare(stage,root);CHECK(p.metadata["page"].toInt()==1&&hashInk(p)==one);Reader r(stage,root,std::move(p));r.activate();turn(r,Action::Previous);CHECK(hashInk(r.active().prepared)==zero);turn(r,Action::Next);CHECK(r.finish(2000));});
 test("resume_checksum_identity_and_invalid_page",[&]{auto valid=readBounded(root/"resume.json",4096);CHECK(readResume(root,2).page==1);CHECK(readResume(root,3).page==0);writeResume(root,99,2,3,true);CHECK(readResume(root,2).page==0);writeResume(root,1,2,3,true);auto object=QJsonDocument::fromJson(readBounded(root/"resume.json",4096)).object();auto payload=object["payload"].toObject();payload["document"]="different";object["payload"]=payload;object["checksum"]=QString::fromLatin1(digest(QJsonDocument(payload).toJson(QJsonDocument::Compact)).toHex());atomicFile(root/"resume.json",QJsonDocument(object).toJson());CHECK(readResume(root,2).page==0);atomicFile(root/"resume.json",valid);});
 test("page_binding_rejects_other_page_journal",[&]{auto p=prepare(stage,pagePath(data/"binding",0),0,false);rejects([&]{Journal j(pagePath(root,1)/"annotations.ink",p.binding);});});
 test("failed_prefetch_preserves_active_page",[&]{auto dir=data/"failed";auto p=initialPrepare(stage,dir);Reader r(data/"no-helper",dir,std::move(p));r.activate();CHECK(r.request(Action::Next));until([&]{r.tick();return r.state()==State::Active;});CHECK(r.page()==0&&r.writable());CHECK(!r.request(Action::Next));CHECK(r.finish(2000));});
 test("close_during_actual_helper_prefetch",[&]{auto dir=data/"close";auto p=initialPrepare(stage,dir);Reader r(stage,dir,std::move(p));r.activate();until([&]{return r.preparing();});CHECK(r.finish(2000));CHECK(r.state()==State::Closed);});
 test("term_ignoring_helper_cleanup_after_reap_and_stale_refusal",[&]{auto helper=stage/"bin/reader-experience1-prepare";auto original=readBounded(helper,4096);auto dir=data/"hung-helper";auto p=initialPrepare(stage,dir);auto target=pagePath(dir,1);privateDirectory(target);
  QByteArray fake="#!/bin/sh\numask 077\ntrap '' TERM\nprintf 'incomplete' > \"$2/";fake+=FixtureHash;fake+="/page-$3/prepared.bin.tmp\"\nexec /bin/sleep 60\n";atomicFile(helper,fake);chmod(helper.c_str(),0700);
  {Reader r(stage,dir,std::move(p));r.activate();until([&]{return std::filesystem::exists(target/"prepared.bin.tmp");});CHECK(r.finish(2000));CHECK(!std::filesystem::exists(target/"prepared.bin.tmp"));}
  atomicFile(helper,original);chmod(helper.c_str(),0700);atomicFile(target/"prepared.bin.tmp",QByteArray("retained-stale"));std::atomic<bool> stop{false};rejects([&]{preparePage(stage,dir,1,stop);});CHECK(readBounded(target/"prepared.bin.tmp",64)=="retained-stale");
 });
 test("mailbox_moves_and_releases_consumed_ownership",[&]{Mailbox<std::shared_ptr<int>,3> m;auto p=std::make_shared<int>(1);std::weak_ptr<int> weak=p;CHECK(m.push(std::move(p)));std::shared_ptr<int> q;CHECK(m.pop(q));CHECK(!p&&q&&*q==1);q.reset();CHECK(weak.expired());});
 test("touch_single_tap_only_no_marker_or_palm_navigation",[&]{Touch t;t.testAxes({0,1619},{0,2159});auto tap=[&](bool busy,spike::Ns duration,bool multiple){spike::Ns now=1000000000;for(auto e:std::array{ev(EV_ABS,ABS_MT_SLOT,0),ev(EV_ABS,ABS_MT_TRACKING_ID,1),ev(EV_ABS,ABS_MT_POSITION_X,600),ev(EV_ABS,ABS_MT_POSITION_Y,2100),ev(EV_SYN,SYN_REPORT,0)})t.feed(e,busy,now);if(multiple){t.feed(ev(EV_ABS,ABS_MT_SLOT,1),busy,now);t.feed(ev(EV_ABS,ABS_MT_TRACKING_ID,2),busy,now);t.feed(ev(EV_SYN,SYN_REPORT,0),busy,now);t.feed(ev(EV_ABS,ABS_MT_TRACKING_ID,-1),busy,now);t.feed(ev(EV_ABS,ABS_MT_SLOT,0),busy,now);}t.feed(ev(EV_ABS,ABS_MT_TRACKING_ID,-1),busy,now+duration);return t.feed(ev(EV_SYN,SYN_REPORT,0),busy,now+duration);};CHECK(tap(false,100000000,false)==Action::Next);CHECK(tap(true,100000000,false)==Action::None);CHECK(tap(false,700000000,false)==Action::None);CHECK(tap(false,100000000,true)==Action::None);CHECK(t.feed(ev(EV_KEY,BTN_TOOL_PEN,1),false,1)==Action::None);});
 std::printf("READER_TESTS_PASS cases=%u helper=actual-with-host-loader-adapter vendor=NONE input_devices=NONE\n",count);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"READER_TEST_FAIL %s\n",e.what());return 1;}
}
