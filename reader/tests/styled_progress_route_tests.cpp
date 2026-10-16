// SPDX-License-Identifier: AGPL-3.0-only
#include "ui_targets.h"
#include "ux_support.h"
#include "ux_display.h"
#include "work_tracking.h"
#include <QCoreApplication>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <string_view>
#include <cstdlib>
#include <new>
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
namespace progress=recovery::v2::progress;
namespace {
constexpr std::uint64_t Generation=71;
struct NoHardwareCalls final:spike::VendorCalls {unsigned calls{};bool acquire(spike::Pixels&)noexcept override{++calls;return false;}void region(spike::Rect,int,int,int)noexcept override{++calls;}void events()noexcept override{++calls;}};
progress::Snapshot snapshot(progress::WorkLedger& ledger){auto s=ledger.snapshot();CHECK(s.fault==progress::Fault::None&&s.health.accepted>=s.health.completed&&s.health.backlog==s.health.accepted-s.health.completed);if(s.health.backlog)CHECK(s.health.oldest.generation==Generation&&s.health.oldest.monotonic>0&&s.health.oldest.boottime>0);else CHECK(s.health.oldest==recovery::v2::ClockPair{});return s;}
void pump(ux::Shell& shell,Canvas& canvas){shell.tick();canvas.changes();CHECK(!shell.failed());}
void settled(ux::Shell& shell,Canvas& canvas,progress::WorkLedger& ledger,std::uint64_t jobs){until([&]{pump(shell,canvas);auto s=snapshot(ledger);CHECK(s.health.accepted==jobs);return s.health.completed==jobs&&shell.writable()&&!shell.styledInk()->status().busy&&shell.unsaved()==0;});}
QJsonObject record(ux::Shell& shell){return shell.styledInk()->snapshot()["documents"].toObject()[shell.currentDocumentId()].toObject();}
void stroke(ux::Shell& shell,progress::WorkLedger& ledger,int y,std::uint64_t before){allocationCount=0;countAllocations=true;bool reserved=shell.reserve();bool down=reserved&&shell.append({250,y,spike::Phase::Down,1,false});countAllocations=false;CHECK(reserved&&down&&allocationCount==0);auto admitted=snapshot(ledger);CHECK(admitted.health.accepted==before+1);countAllocations=true;bool moved=shell.append({300,y+30,spike::Phase::Move,2,false});bool up=moved&&shell.append({350,y+60,spike::Phase::Up,3,false});countAllocations=false;CHECK(moved&&up&&allocationCount==0);CHECK(snapshot(ledger).health.accepted==before+1);}
template<class Accept>void blockedJob(ux::Shell& shell,Canvas& canvas,progress::WorkLedger& ledger,const Path& features,Accept accept){auto before=snapshot(ledger);CHECK(before.health.backlog==0);auto lease=std::make_unique<product::Services>(features);CHECK(accept());auto blocked=snapshot(ledger);CHECK(blocked.health.accepted==before.health.accepted+1&&blocked.health.completed==before.health.completed&&blocked.health.backlog==1);for(unsigned n=0;n<32;++n){shell.tick();canvas.changes();auto again=snapshot(ledger);CHECK(again.health==blocked.health);}lease.reset();settled(shell,canvas,ledger,blocked.health.accepted);}
int partialChild(const Path& app,const Path& root){auto book=library::documents(root)[0];auto features=product::featureDirectory(root,book),docroot=library::documentRoot(root,book.id);progress::WorkLedger ledger(recovery::v2::WorkerKind::DurablePrefix,32);ux::Shell shell(app,root,true,false,false,{&ledger,Generation});Canvas canvas(shell);CHECK(shell.choose(0));canvas.ready();settled(shell,canvas,ledger,1);auto before=record(shell)["strokes"].toArray();auto lease=std::make_unique<product::Services>(features);allocationCount=0;countAllocations=true;bool reserved=shell.reserve();bool down=reserved&&shell.append({300,1200,spike::Phase::Down,10,false});bool moved=down&&shell.append({400,1250,spike::Phase::Move,11,false});countAllocations=false;CHECK(reserved&&down&&moved&&allocationCount==0);auto accepted=snapshot(ledger);CHECK(accepted.health.accepted==2&&accepted.health.completed==1&&accepted.health.backlog==1);
 NoHardwareCalls calls;spike::VendorDisplay vendor(calls);ux::Display display(vendor,shell);display.operationSink()->cancel();CHECK(calls.calls==0&&snapshot(ledger).health==accepted.health);lease.reset();ux::Shell::Shutdown state;until([&]{state=shell.pollQuiesce();return state.drained;});auto done=snapshot(ledger);CHECK(done.health.accepted==2&&done.health.completed==2&&done.health.backlog==0);CHECK(!state.saved&&state.failed&&shell.unsaved()==1&&!reader::readViewResume(docroot,3,{}).clean);auto after=record(shell)["strokes"].toArray();CHECK(after.size()==before.size()+1&&after.last().toObject()["interrupted"]==true&&after.last().toObject()["points"].toArray().size()==2);for(int i=0;i<before.size();++i)CHECK(after[i]==before[i]);underlay::atomicFile(root/"tracked-interruption.json",QJsonDocument(QJsonObject{{"before",before},{"after",after},{"accepted",2},{"completed",2},{"unclean",true}}).toJson(QJsonDocument::Compact));for(unsigned n=0;n<16;++n){state=shell.pollQuiesce();CHECK(state.drained&&!state.saved&&state.failed&&!shell.finish(0)&&snapshot(ledger).health==done.health);}display.shutdown();CHECK(calls.calls==0);std::puts("TRACKED_PARTIAL_DURABLE_PREFIX_LOGICAL_UNCLEAN");std::fflush(stdout);return 0; // Shell destructor must reject clean completion and exit 78.
}
}
int main(int argc,char** argv){QCoreApplication application(argc,argv);try{if(argc==4&&std::string_view(argv[1])=="--tracked-partial")return partialChild(argv[2],argv[3]);if(argc!=5)return 64;Path source=argv[1],parent=argv[2],build=argv[3],sys=argv[4];underlay::privateDirectory(parent);QTemporaryDir owned(QString::fromStdString((parent/"styled-progress-XXXXXX").string()));CHECK(owned.isValid());owned.setAutoRemove(false);Path data=owned.path().toStdString();std::cout<<"STYLED_PROGRESS_DATA "<<data<<'\n'<<std::flush;auto app=stage(source,data/"stage",build,sys),root=data/"library";underlay::privateDirectory(root/"inbox");std::filesystem::copy_file(source/"fixtures/library/authored-three-pages.pdf",root/"inbox/authored.pdf");std::atomic<bool> stop{false};reader::syncLibrary(app,root,stop);auto book=library::documents(root)[0];auto features=product::featureDirectory(root,book),docroot=library::documentRoot(root,book.id);QJsonArray durable;
 {
  progress::WorkLedger ledger(recovery::v2::WorkerKind::DurablePrefix,32);CHECK(snapshot(ledger).health.accepted==0);ux::Shell shell(app,root,true,false,false,{&ledger,Generation});Canvas canvas(shell);CHECK(shell.choose(0));canvas.ready();settled(shell,canvas,ledger,1);CHECK(record(shell)["strokes"].toArray().isEmpty());auto boot=snapshot(ledger);CHECK(boot.health.accepted==1&&boot.health.completed==1);
  auto lease=std::make_unique<product::Services>(features);stroke(shell,ledger,1000,1);auto first=snapshot(ledger);CHECK(first.health.accepted==2&&first.health.completed==1&&first.health.backlog==1);stroke(shell,ledger,1400,2);auto second=snapshot(ledger);CHECK(second.health.accepted==3&&second.health.completed==1&&second.health.backlog==2&&second.health.oldest==first.health.oldest);for(unsigned n=0;n<64;++n){shell.tick();canvas.changes();CHECK(snapshot(ledger).health==second.health);}CHECK(lease->snapshot()["documents"].toObject()[book.id].toObject()["strokes"].toArray().isEmpty());lease.reset();settled(shell,canvas,ledger,3);durable=record(shell)["strokes"].toArray();CHECK(durable.size()==2&&durable[0].toObject()["points"].toArray().size()==3&&durable[1].toObject()["points"].toArray().size()==3);
  // All job acceptance below originates in the real writer's production API.
  // No test creates, completes, alters or injects a work token.
  CHECK(shell.action(ux::Action::Show));canvas.changes();CHECK(uitest::openTools(shell));canvas.changes();auto hit=[&](spike::Rect r){return shell.finger((r.x0+r.x1)/2,(r.y0+r.y1)/2,false);};
  blockedJob(shell,canvas,ledger,features,[&]{return hit(ux::ToolsView::toolRect(unsigned(styled::Tool::Fineliner)));});CHECK(shell.styledInk()->style().tool==styled::Tool::Fineliner);
  blockedJob(shell,canvas,ledger,features,[&]{return hit(ux::ToolsView::widthRect(unsigned(styled::Width::Thick)));});CHECK(shell.styledInk()->style().width==styled::Width::Thick);
  blockedJob(shell,canvas,ledger,features,[&]{return hit(ux::ToolsView::colorRect(6));});CHECK(shell.styledInk()->style().color==6);
  // Quiet effects live in Document settings > Reading (same writer API, same job).
  CHECK(hit(uitest::toolsClose(shell)));canvas.changes();CHECK(uitest::openSettings(shell));canvas.changes();CHECK(uitest::setting(shell,ib::sheets::TabBase+1));canvas.changes();
  blockedJob(shell,canvas,ledger,features,[&]{return uitest::setting(shell,ib::sheets::Quiet)&&uitest::setting(shell,ib::sheets::SettingsClose);});CHECK(shell.quietEffectsEnabled());
  canvas.changes();CHECK(uitest::openTools(shell));canvas.changes();
  blockedJob(shell,canvas,ledger,features,[&]{return hit(ux::ToolsView::undo());});CHECK(record(shell)["strokes"].toArray().size()==1);
  blockedJob(shell,canvas,ledger,features,[&]{return hit(ux::ToolsView::redo());});CHECK(record(shell)["strokes"].toArray()==durable);
  CHECK(hit(uitest::toolsClose(shell)));canvas.changes();CHECK(shell.action(ux::Action::Hide));canvas.changes();auto count=snapshot(ledger).health.accepted;CHECK(count==9);blockedJob(shell,canvas,ledger,features,[&]{return shell.styledInk()->reload();});CHECK(record(shell)["strokes"].toArray()==durable&&snapshot(ledger).health.completed==10);CHECK(shell.finish(5000)&&shell.unsaved()==0);auto final=snapshot(ledger);CHECK(final.health.accepted==10&&final.health.completed==10&&final.health.backlog==0&&reader::readViewResume(docroot,3,{}).clean);
 }
 // A partial pen job uses its already-admitted token when shutdown queues it.
 // Durable prefix completion remains distinct from the logical unclean state.
 QProcess child;child.setProgram(QString::fromStdString((sys/"usr/lib/ld-linux-aarch64.so.1").string()));child.setArguments({"--library-path",QString::fromStdString((sys/"usr/lib").string()),QString::fromUtf8(argv[0]),"--tracked-partial",QString::fromStdString(app.string()),QString::fromStdString(root.string())});child.start();CHECK(child.waitForStarted(5000)&&child.waitForFinished(45000));auto out=child.readAllStandardOutput(),err=child.readAllStandardError();if(child.exitCode()!=78||!out.contains("TRACKED_PARTIAL_DURABLE_PREFIX_LOGICAL_UNCLEAN")){std::cerr<<out.constData()<<err.constData();CHECK(false);}CHECK(child.exitStatus()==QProcess::NormalExit);auto manifest=QJsonDocument::fromJson(underlay::readBounded(root/"tracked-interruption.json",1024*1024)).object();CHECK(manifest["before"].toArray()==durable&&!reader::readViewResume(docroot,3,{}).clean);
 {progress::WorkLedger ledger(recovery::v2::WorkerKind::DurablePrefix,32);ux::Shell shell(app,root,true,false,false,{&ledger,Generation});Canvas canvas(shell);CHECK(shell.choose(0));canvas.ready();settled(shell,canvas,ledger,1);CHECK(record(shell)["strokes"].toArray()==manifest["after"].toArray()&&!reader::readViewResume(docroot,3,{}).clean);CHECK(shell.finish(5000)&&shell.unsaved()==0&&snapshot(ledger).health.accepted==1&&snapshot(ledger).health.completed==1);}
 CHECK(owned.remove());std::cout<<"STYLED_PROGRESS_ROUTE_PASS actual_shell=true main_owned_durable_ledger_capacity=32 bootstrap=true blocked_two_strokes_no_completion=true main_tracked_contact_cpp_allocations=0 oldest_preserved=true exact_accept_count=true settings_undo_redo_refresh_reveal=true token_slot_reuse=true partial_existing_token=true durable_prefix_distinct_from_unclean=true child_reopen=true fake_tokens=NONE provider_calls=NONE vendor_calls=0 physical=NOT_RUN application_domain_mapping=NOT_QUALIFIED\n";return 0;
 }catch(const std::exception& e){std::cerr<<"STYLED_PROGRESS_ROUTE_FAIL "<<e.what()<<'\n';return 1;}}
