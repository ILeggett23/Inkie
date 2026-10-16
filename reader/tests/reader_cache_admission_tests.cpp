// SPDX-License-Identifier: AGPL-3.0-only
// Test-only: link cache-reader-test-lib's unchanged production Reader translation
// unit against document-underlay-test BEFORE reader-lib. UNDERLAY_TEST_FAULTS
// enables the existing IoFault.delayMs in Pipeline; it is never enabled in the
// production helper, owner, reader payload or release package. No device opened.
#include "reader.h"
#include "epub_stage_helper.h"
#include "epub_view_store.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonDocument>
#include <QJsonArray>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <sys/stat.h>
#ifndef UNDERLAY_TEST_FAULTS
#error reader_cache_admission_tests requires the existing test-only underlay fault library
#endif
using Path=std::filesystem::path;
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"CACHE_CHECK_FAILED %s:%d: %s\n",__FILE__,__LINE__,#x);std::fflush(stderr);throw std::runtime_error(#x);}}while(0)
namespace {
unsigned cases{};
void proof(const char* name){++cases;std::cout<<"PASS "<<name<<std::endl;}
template<class F>void until(F f){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!f()){CHECK(std::chrono::steady_clock::now()<end);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
QString quote(const Path& p){auto s=QString::fromStdString(p.string());s.replace("'","'\\''");return "'"+s+"'";}
Path stage(const Path& root,const Path& build,const Path& sys){
 auto app=root/"application";underlay::privateDirectory(app/"bin");
 std::filesystem::copy_file(build/"reader-experience1-prepare",app/"bin/helper-real");
 auto wrapper=QString("#!/bin/sh\nexec %1 --library-path %2 %3 \"$@\"\n").arg(quote(sys/"usr/lib/ld-linux-aarch64.so.1"),quote(sys/"usr/lib"),quote(app/"bin/helper-real"));
 underlay::atomicFile(app/"bin/reader-experience1-prepare",wrapper.toUtf8());CHECK(chmod((app/"bin/reader-experience1-prepare").c_str(),0700)==0);return app;
}
library::Document importBook(const Path& app,const Path& source,const Path& root){
 underlay::privateDirectory(root/"inbox");std::filesystem::copy_file(source/"fixtures/library/authored-three-chapters.epub",root/"inbox/authored.epub");
 std::atomic<bool> stop{false};reader::syncLibrary(app,root,stop);auto books=library::documents(root);CHECK(books.size()==1&&books[0].format=="epub"&&books[0].pages==3);return books[0];
}
reader::epubviews::ViewSet derivedView(const Path& app,const Path& document){
 std::atomic<bool> stop{false};auto staged=reader::stageEpubViewViaHelper(app,document,{},200,36,0,stop);CHECK(!staged.viewId.isEmpty());reader::epubviews::activate(document,staged.viewId,{});auto view=reader::epubviews::activeSet(document);CHECK(view.id==staged.viewId&&view.receipt["pages"].toInt()>=9);return view;
}
QByteArray inkHash(const underlay::Prepared& p){return underlay::digest(QByteArray(reinterpret_cast<const char*>(p.ink.data()),p.ink.size()*sizeof(std::uint32_t)));}
void bounded(reader::Reader& r){CHECK(!r.failed()&&r.resident()<=4);}
void turn(reader::Reader& r,int target,bool install){
 CHECK(r.navigateTo(target));until([&]{bounded(r);bool changed=r.tick();CHECK(r.takeNavigationRefusal()==reader::NavigationRefusal::None);if(!changed)return false;CHECK(r.page()==target);if(install)r.installed();r.activate();return true;});bounded(r);
}
void stroke(reader::Reader& r){
 CHECK(r.writable()&&r.reserve());spike::Point previous{280,630,spike::Phase::Down,1,false};
 for(auto p:std::array{previous,spike::Point{360,630,spike::Phase::Move,2,false},spike::Point{360,630,spike::Phase::Up,3,false}}){
  auto& ink=r.active().prepared.ink;spike::segment({ink,underlay::Width,underlay::Height,underlay::Stride},previous,p,2,0xff000000u);CHECK(r.append(p));previous=p;
 }
 CHECK(!r.contact());
}
// Failure unwinding must give the intentionally delayed, already accepted write
// the same ordinary 15s test budget. Otherwise Reader's qualified 100ms destructor
// intentionally exits78, hiding the assertion from the outer failure diagnostic.
struct DrainOnFailure {
 reader::Reader& r;
 ~DrainOnFailure(){if(r.drained())return;r.beginQuiesce();const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(15);while(!r.pollQuiesce(true,true)&&std::chrono::steady_clock::now()<end)std::this_thread::sleep_for(std::chrono::milliseconds(1));}
};
}
int main(int argc,char** argv){QCoreApplication application(argc,argv);if(argc!=5)return 64;
 try{
  const Path source=argv[1],parent=argv[2],build=argv[3],sys=argv[4];underlay::privateDirectory(parent);QTemporaryDir owned(QString::fromStdString((parent/"reader-cache-admission-XXXXXX").string()));CHECK(owned.isValid());owned.setAutoRemove(false);const Path data=owned.path().toStdString();std::cout<<"READER_CACHE_ADMISSION_DATA "<<data<<std::endl;
  const auto app=stage(data/"stage",build,sys),root=data/"library";auto book=importBook(app,source,root);auto document=library::documentRoot(root,book.id);const auto descriptor=underlay::readBounded(document/"document.json",8192),sourceHash=underlay::digest(underlay::readBounded(document/"source.epub",library::MaxSourceBytes));auto view=derivedView(app,document);const int total=view.receipt["pages"].toInt();std::atomic<bool> stop{false};
  // Real helper/parser preparation before the delayed write avoids spending its
  // hold interval on first-time disk/raster work. Reader still prepares/adopts
  // each requested page through its actual helper/result/cache route below.
  for(int page:{0,1,2}){auto p=reader::prepareViewPage(app,document,page,stop,view.id);CHECK(p.metadata["schema"]==4&&p.metadata["document"]==book.id&&p.metadata["view_id"]==view.id&&p.metadata["page"]==page);}
  proof("real imported EPUB reflow receipt and at least nine source-bound pages; no fixture picker or fake helper");
  QByteArray savedInk,binding0;Path page0Storage;unsigned shutdownPage=6;
  {
   auto p=reader::prepareViewPage(app,document,0,stop,view.id);reader::Reader r(app,document,std::move(p),view.id);DrainOnFailure cleanup{r};r.activate();bounded(r);CHECK(r.page()==0&&r.active().prepared.lastId==0);auto* page0=&r.active();page0Storage=page0->storage;binding0=page0->prepared.binding;
   // Finish/destroy the original empty writer first, releasing its journal lock.
   // The replacement is the real Pipeline; only its existing test fault delays
   // the durable append. No queued/unsaved counters or page metadata are faked.
   until([&]{return page0->saves->finish(0);});page0->saves.reset();underlay::IoFault fault;fault.delayMs=10000;page0->saves=std::make_unique<underlay::Pipeline>(page0Storage/"annotations.ink",page0->prepared,fault);
   auto blank=inkHash(page0->prepared);stroke(r);savedInk=inkHash(page0->prepared);CHECK(savedInk!=blank&&page0->saves->queued.load()==1&&page0->saves->unsaved()==1&&r.unsaved()==1&&page0->saves->committed.load()==0);proof("complete accepted Reader stroke is genuinely queued and unsaved in delayed real Pipeline");
   turn(r,1,true);auto* page1=&r.active();const auto binding1=page1->prepared.binding,ink1=inkHash(page1->prepared);turn(r,2,false);auto* page2=&r.active();const auto binding2=page2->prepared.binding,ink2=inkHash(page2->prepared);const auto viewBefore=r.view();
   // Deliberately retain previous1 until owner-thread installation. Cache is
   // now exactly [0 unsaved, 1 previous, 2 active]; none can safely be evicted.
   CHECK(r.resident()==3&&page0->saves->unsaved()==1&&page0->saves->queued.load()==1&&r.unsaved()==1&&page0->prepared.binding==binding0&&page1->index==1&&page2->index==2);
   CHECK(!r.navigateTo(6));CHECK(r.takeNavigationRefusal()==reader::NavigationRefusal::Busy);CHECK(r.state()==reader::State::Active&&r.page()==2&&r.view()==viewBefore&&!r.exiting());
   for(unsigned i=0;i<64;++i){CHECK(!r.tick());bounded(r);CHECK(r.page()==2&&r.view()==viewBefore&&r.takeNavigationRefusal()==reader::NavigationRefusal::None);}
   CHECK(page0->index==0&&page0->prepared.binding==binding0&&inkHash(page0->prepared)==savedInk&&page0->saves->unsaved()==1&&page0->saves->queued.load()==1);CHECK(page1->prepared.binding==binding1&&inkHash(page1->prepared)==ink1&&page2->prepared.binding==binding2&&inkHash(page2->prepared)==ink2);proof("full protected cache immediately refuses demanded far page; active previous accepted ink IDs and geometry retained with resident cap");
   until([&]{bounded(r);r.tick();CHECK(r.page()==2&&r.view()==viewBefore);return page0->saves->queued.load()==0&&page0->saves->unsaved()==0&&page0->saves->committed.load()==1;});CHECK(r.committed()==1&&r.unsaved()==0);r.installed();
   // Incoming6 is farther than either safe idle0/1. The original distance-only
   // room rule repeatedly rejected this demanded page; successful demand must
   // now evict a safely idle nonactive/nonprevious entry instead of reprepare.
   turn(r,6,true);CHECK(r.active().prepared.metadata["document"]==book.id&&r.active().prepared.metadata["view_id"]==view.id&&r.committed()==1&&r.unsaved()==0);proof("actual durable prefix drains and demanded far-page retry completes through bounded real cache eviction");
   CHECK(r.navigateTo(8));CHECK(r.request(reader::Action::Exit));CHECK(r.exiting()&&r.page()==6&&!r.tick()&&!r.navigateTo(0)&&!r.reserve());r.beginQuiesce();until([&]{bounded(r);CHECK(!r.tick()&&r.page()==6);return r.pollQuiesce(true,true);});CHECK(r.drained()&&r.shutdownSaved()&&!r.failed()&&r.unsaved()==0);auto resume=reader::readViewResume(document,total,view.id);CHECK(resume.clean&&resume.page==int(shutdownPage));const auto final=underlay::readBounded(document/"view-resume"/(view.id.toStdString()+".json"),8192);
   for(unsigned i=0;i<32;++i){CHECK(r.pollQuiesce(true,true)&&!r.tick()&&r.page()==6&&underlay::readBounded(document/"view-resume"/(view.id.toStdString()+".json"),8192)==final);}proof("Exit cancels pending far demand; late results never install and final clean resume remains last displayed page");
  }
  // Old Reader and every page writer must release their actual journal locks
  // before fresh production helper replay and Reader ownership acquisition.
  {
   auto reopened=reader::prepareViewPage(app,document,0,stop,view.id);CHECK(reopened.lastId==1&&reopened.binding==binding0&&inkHash(reopened)==savedInk&&reopened.metadata["document"]==book.id&&reopened.metadata["view_id"]==view.id);reader::Reader r(app,document,std::move(reopened),view.id);DrainOnFailure cleanup{r};r.activate();CHECK(r.page()==0&&inkHash(r.active().prepared)==savedInk);r.beginQuiesce();until([&]{bounded(r);return r.pollQuiesce(true,true);});CHECK(r.shutdownSaved()&&reader::readViewResume(document,total,view.id).clean);proof("fresh real helper and Reader reopen exact accepted journal prefix ink document and EPUB view binding");
  }
  {
   auto otherRoot=data/"other-library";auto other=importBook(app,source,otherRoot);auto otherData=library::documentRoot(otherRoot,other.id);CHECK(other.id!=book.id&&other.sha256==book.sha256);auto p=reader::preparePage(app,otherData,0,stop);CHECK(p.lastId==0&&p.metadata["document"]==other.id&&p.binding!=binding0&&std::all_of(p.ink.begin(),p.ink.end(),[](auto pixel){return pixel==0xffffffffu;}));reader::Reader r(app,otherData,std::move(p));DrainOnFailure cleanup{r};r.activate();r.beginQuiesce();until([&]{bounded(r);return r.pollQuiesce(true,true);});CHECK(r.shutdownSaved());proof("fresh different stable document identity cannot inherit old cached or persisted ink");
  }
  {namespace pv=recovery::v2::progress;pv::WorkLedger preparation(recovery::v2::WorkerKind::CancelableHelperPrefix,32);reader::lifecycle::Tracking tracked{&preparation,95,recovery::v2::WorkerKind::CancelableHelperPrefix};auto p=reader::prepareViewPage(app,document,0,stop,view.id);reader::Reader r(app,document,std::move(p),view.id,tracked);DrainOnFailure cleanup{r};auto health=[&]{auto state=preparation.snapshot();CHECK(state.fault==pv::Fault::None);return state.health;};auto settle=[&]{until([&]{r.tick();bounded(r);return health().backlog==0;});};r.activate();settle();for(int page:{1,2,3}){turn(r,page,true);settle();}turn(r,2,true);const auto allowed=health().accepted;until([&]{r.tick();bounded(r);return health().completed>=allowed;});const auto actual=health().accepted;std::cout<<"PREFETCH_CHURN_PROBE page=2 navigation=0,1,2,3,2 allowed_jobs="<<allowed<<" actual_jobs="<<actual<<" extra_jobs="<<actual-allowed<<" resident_cap=4 actual_epub_helpers=true physical=NOT_RUN\n"<<std::flush;CHECK(actual==allowed);for(unsigned n=0;n<1000;++n){r.tick();bounded(r);CHECK(health().accepted==allowed);}r.beginQuiesce();until([&]{return r.pollQuiesce(true,true);});CHECK(r.shutdownSaved()&&health().completed==allowed);proof("stable adjacent prefetch never replaces an equally near neighbor while an obsolete farther page occupies the cache");}
  CHECK(underlay::readBounded(document/"document.json",8192)==descriptor&&underlay::digest(underlay::readBounded(document/"source.epub",library::MaxSourceBytes))==sourceHash&&reader::epubviews::activeSet(document).id==view.id);CHECK(owned.remove());std::cout<<"READER_CACHE_ADMISSION_PASS cases="<<cases<<" actual_reader=true actual_imported_epub=true real_queued_journal=true test_only_delay_ms=10000 resident_cap=4 recoverable_busy=true durable_retry=true pending_exit=true fresh_reopen=true document_isolation=true input_device=NONE physical=NOT_RUN\n";return 0;
 }catch(const std::exception& e){std::cerr<<"READER_CACHE_ADMISSION_FAIL "<<e.what()<<std::endl;return 1;}
}
