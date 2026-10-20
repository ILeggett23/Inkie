// SPDX-License-Identifier: AGPL-3.0-only
// Actual Shell -> Display -> VendorDisplay, fake final device boundary. No raw
// input opened, real vendor library loaded, network enabled or provider called.
#include "ux_support.h"
#include "ux_display.h"
#include <QCoreApplication>
#include <QImage>
#include <iostream>
using Path=std::filesystem::path;
namespace ux {
struct DisplayTestAccess {static void lease(Display& d,const spike::RecoveryLease& l){d.lease_=&l;}static int sandClass(const Display& d){return d.sandClass_;}static spike::Submit decorative(Display& d,spike::Rect r){return d.present(r,"test:sand-refusal",RefreshIntent::UIUpdate,false);}};
struct QolTestAccess {static void mode(Shell& s,unsigned n){s.prefs_.sandMode=n;}static bool note(Shell& s,QString id={}){return s.openNotes(notes::Mode::Note,{},id);}static void select(Shell& s,bool on){s.selectMode_=on;}static void orientationCancel(Shell& s){++s.presentedVersion_;}static void budget(Shell& s,bool on){s.sandCongested_=on;}};
}
namespace {
struct Lease final:spike::RecoveryLease {bool live{true};bool valid()const noexcept override{return live;}spike::Ns deadline()const noexcept override{return spike::monotonic()+300000000000ULL;}bool stopRequested()const noexcept override{return false;}spike::SustainedPolicy sustained()const noexcept override{return {1000000000ull,8,4096};}};
struct Calls final:spike::VendorCalls {
 std::vector<std::uint32_t> pixels=std::vector<std::uint32_t>(underlay::Pixels,0xffffffffu);unsigned full{},regions{};bool bad{};int lastQualityClass{-1},sampleClass{-1};spike::Rect sample{};bool trace{};
 bool acquire(spike::Pixels& out)noexcept override{out={reinterpret_cast<unsigned char*>(pixels.data()),1620,2160,4,6528};return true;}
 void region(spike::Rect r,int content,int mode,int flags)noexcept override{
  if(!flags&&(mode==3||mode==4)){
   lastQualityClass=content;
   if(r.x0<sample.x1&&r.x1>sample.x0&&r.y0<sample.y1&&r.y1>sample.y0){
    sampleClass=content;
    if(trace)std::fprintf(stderr,"SAND_TEST_QUALITY x=%d y=%d w=%d h=%d class=%d mode=%d\n",r.x0,r.y0,r.x1-r.x0,r.y1-r.y0,content,mode);
   }
  }
  bad|=r.empty()||r.x0<0||r.y0<0||r.x1>1620||r.y1>2160;
  if(flags&1)++full;else ++regions;
  bad|=flags!=0&&!(flags==1&&mode==3)&&!(flags==2&&mode==0);
 }
 void events()noexcept override{}
};
struct Harness {
 ux::Shell shell;Lease lease;Calls calls;spike::VendorDisplay vendor;ux::Display display;std::uint64_t version{};Path captures;
 Harness(const Path& app,const Path& root,Path out):shell(app,root,true,false,false),vendor(calls),display(vendor,shell),captures(std::move(out)){
  ux::DisplayTestAccess::lease(display,lease);CHECK(vendor.initialize(lease)&&display.initialPage());version=shell.version();CHECK(shell.choose(0));settle();
 }
 void boundary(){CHECK(display.readerBoundary());CHECK(!shell.failed()&&!calls.bad);if(display.pageVersion()!=version){version=display.pageVersion();display.pageInstalled();}}
 void settle(){until([&]{boundary();return shell.writable()&&shell.unsaved()==0&&shell.styledInk()&&shell.styledInk()->status().ready&&!shell.styledInk()->status().busy&&!shell.sandPending();});}
 void effect(const std::string& label){unsigned count=0;auto ack=shell.sandMetrics().accepted;until([&]{boundary();if(shell.sandMetrics().accepted!=ack){ack=shell.sandMetrics().accepted;if(!label.empty()){QImage im(reinterpret_cast<const uchar*>(calls.pixels.data()),1620,2160,1632*4,QImage::Format_RGB32);CHECK(im.save(QString::fromStdString((captures/(label+"-"+std::to_string(count++)+".png")).string())));}}return !shell.sandPending();});}
 void canonical(spike::Rect r){auto normal=calls.pixels;shell.composeCanonical({normal,1620,2160,1632},r);for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x)CHECK(normal[std::size_t(y)*1632+x]==calls.pixels[std::size_t(y)*1632+x]);}
 void stroke(bool erase=false){auto engine=std::make_unique<spike::Engine>(spike::engineTransform(spike::Transform{{0,1619},{0,2159},1620,2160,0,false},display),shell.ink(),display);CHECK(display.reserve());const auto t=spike::monotonic();for(unsigned i=0;i<3;++i){spike::Sample sample{350+int(i)*10,1800,i==0?spike::Phase::Down:i==2?spike::Phase::Up:spike::Phase::Move,t+i+1};sample.eraser=erase;CHECK(engine->accept(sample)&&display.append(engine->points().back())&&engine->flush()==spike::Submit::Accepted);}settle();}
};
std::pair<int,int> word(ux::Shell& s){auto g=s.semantics().geometry();CHECK(g&&g->available());auto words=g->data["words"].toArray();for(int n=0;n<words.size();++n){auto v=semantic::select(g,unsigned(n),unsigned(n),1);if(v->data["normalized"].toString()!="orchard")continue;auto q=v->data["quads"].toArray()[0].toArray();double x=0,y=0;for(int i=0;i<8;i+=2){x+=q[i].toDouble()/4;y+=q[i+1].toDouble()/4;}auto m=g->data["transform"].toArray();return {int(m[0].toDouble()*x+m[2].toDouble()*y+m[4].toDouble()),int(m[1].toDouble()*x+m[3].toDouble()*y+m[5].toDouble())};}throw std::runtime_error("word missing");}
}
int main(int argc,char**argv){QCoreApplication app(argc,argv);if(argc!=5)return 64;try{
 const Path source=argv[1],data=argv[2],build=argv[3],sys=argv[4];underlay::privateDirectory(data);auto application=stage(source,data/"stage",build,sys);underlay::privateDirectory(application/"assets/dictionary");std::filesystem::copy_file(source/"assets/dictionary/wordnet.ibd",application/"assets/dictionary/wordnet.ibd");const auto root=data/"library";underlay::privateDirectory(root/"inbox");std::filesystem::copy_file(source/"fixtures/library/authored-three-pages.pdf",root/"inbox/test.pdf");std::atomic<bool> stop{};reader::syncLibrary(application,root,stop);
 CHECK(ux::Prefs::load(root).sandMode==0);ux::Prefs prefs;prefs.sandMode=3;prefs.tomHand=2;prefs.autoRotate=false;prefs.save(root);CHECK(ux::Prefs::load(root)==prefs);
 // Exercise the visible control and persistence through the actual Shell route.
 {const auto prefRoot=data/"preferences";underlay::privateDirectory(prefRoot);ux::Shell s(application,prefRoot,true,false,false);Canvas c(s);c.changes();CHECK(uitest::library(s,ib::library::NavBase+int(ib::library::Section::Settings)));c.changes();CHECK(uitest::library(s,ib::settings::PageBase+int(ib::settings::Page::Accessibility)));c.changes();for(unsigned n=1;n<=4;++n){CHECK(uitest::library(s,ib::settings::SandMode));c.changes();CHECK(s.prefs().sandMode==n%4&&ux::Prefs::load(prefRoot).sandMode==n%4);}CHECK(s.finish(5000));}
 const auto captures=data/"sand-captures";underlay::privateDirectory(captures);Harness h(application,root,captures);CHECK(h.shell.sandEnabled());const auto ink=inkHash(h.shell);CHECK(h.shell.action(ux::Action::Show));h.boundary();h.settle();const auto full=h.calls.full;
 // Real toolbox opening + dismissal, preservation of semantic tool identity.
 // Synthetic prior-panel pigment in the borrowed source tests the real vendor
 // classification contract; it is deliberately distinct from stored page ink.
 const auto panel=ux::ToolsView::panel();const int sx=(panel.x0+panel.x1)/2,sy=(panel.y0+panel.y1)/2;h.calls.sample={sx-20,sy-12,sx+20,sy+13};h.calls.trace=true;
 for(int y=h.calls.sample.y0;y<h.calls.sample.y1;++y)for(int x=h.calls.sample.x0;x<h.calls.sample.x1;++x)h.calls.pixels[std::size_t(y)*1632+x]=0xffffd900u;
 CHECK(h.vendor.removalContent(panel)==3);std::fprintf(stderr,"SAND_TEST_OLD panel=%d,%d,%d,%d class=%d\n",panel.x0,panel.y0,panel.x1,panel.y1,h.vendor.removalContent(panel));
 auto starts=h.shell.sandMetrics().starts;CHECK(uitest::openTools(h.shell));CHECK(h.vendor.removalContent(panel)==3);h.boundary();CHECK(h.shell.sandMetrics().starts>starts);std::fprintf(stderr,"SAND_TEST_FIRST retained=%d pending=%d accepted=%llu quality=%u reason=%u\n",ux::DisplayTestAccess::sandClass(h.display),h.shell.sandPending(),(unsigned long long)h.shell.sandMetrics().accepted,unsigned(h.shell.sandMetrics().quality),unsigned(h.shell.sandMetrics().reason));h.effect("tools");std::fprintf(stderr,"SAND_TEST_FINAL sampled=%d retained=%d accepted=%llu requests=%llu reason=%u\n",h.calls.sampleClass,ux::DisplayTestAccess::sandClass(h.display),(unsigned long long)h.shell.sandMetrics().accepted,(unsigned long long)h.shell.sandMetrics().requests,unsigned(h.shell.sandMetrics().reason));CHECK(h.calls.sampleClass==3);h.calls.trace=false;h.canonical(ux::ToolsView::panel());CHECK(uitest::tap(h.shell,uitest::toolsClose(h.shell)));h.effect("tools-dismiss");h.settle();CHECK(h.calls.full==full&&inkHash(h.shell)==ink);
 // Reduce motion is a hard override, and a disabled effect does not snapshot.
 h.shell.setReducedMotion(true);starts=h.shell.sandMetrics().starts;CHECK(uitest::openTools(h.shell));h.boundary();CHECK(!h.shell.sandPending()&&h.shell.sandMetrics().starts==starts);CHECK(uitest::tap(h.shell,uitest::toolsClose(h.shell)));h.effect("");h.shell.setReducedMotion(false);
 // Definitions stay tied to the original selected word; dismissal restores page.
 CHECK(h.shell.action(ux::Action::Hide));h.boundary();auto at=word(h.shell);CHECK(h.shell.finger(at.first,at.second,true));until([&]{h.boundary();return h.shell.semantics().view()==semantic::View::Definition&&!h.shell.semantics().pending();});h.effect("definition");CHECK(h.shell.semantics().definition()["status"]=="FOUND");CHECK(h.shell.finger(1450,1800,false));h.effect("definition-dismiss");h.settle();CHECK(inkHash(h.shell)==ink);
 // Tom's real worker binds while decoration is optional. No question is sent.
 // Twenty opens/closes exercise lifetime, async binding and cancellation.
 const auto tomFramesBefore=h.shell.sandMetrics().accepted;unsigned tomStarts=0;for(unsigned i=0;i<20;++i){CHECK(h.shell.action(ux::Action::Show));h.boundary();h.settle();starts=h.shell.sandMetrics().starts;CHECK(uitest::menu(h.shell,ux::MenuTom));h.boundary();tomStarts+=h.shell.sandMetrics().starts>starts;h.effect(i==0?"tom":"");until([&]{h.boundary();return h.shell.tomPanel()->canLeave();});CHECK(h.shell.tomPanel()->view().request.isEmpty());CHECK(uitest::tom(h.shell,ib::tom::Close));until([&]{h.boundary();return !h.shell.tomPanel()->active();});h.effect(i==0?"tom-dismiss":"");h.settle();CHECK(inkHash(h.shell)==ink&&h.calls.full==full);}
 CHECK(tomStarts>0&&h.shell.sandMetrics().accepted>tomFramesBefore);CHECK(h.shell.sandMetrics().cacheBytes<=sand::Transition::MaxCacheBytes);
 // Writing and erasing during an active entrance must settle decoration, then
 // use the normal production pen route without touching a provider or input fd.
 CHECK(h.shell.action(ux::Action::Show));h.boundary();h.settle();CHECK(uitest::menu(h.shell,ux::MenuTom));h.boundary();until([&]{h.boundary();return h.shell.tomPanel()->writable();});h.stroke();h.stroke(true);CHECK(!h.shell.sandPending());CHECK(uitest::tom(h.shell,ib::tom::Close));until([&]{h.boundary();return !h.shell.tomPanel()->active();});h.effect("");h.settle();
 // A book-linked note is created by the normal explicit action only. Reopening
 // that same note for a transition must never create another object.
 CHECK(ux::QolTestAccess::note(h.shell));h.boundary();h.effect("note");until([&]{h.boundary();return h.shell.notesPanel()->canLeave();});auto id=h.shell.notesPanel()->view().note["id"].toString();CHECK(!id.isEmpty());CHECK(uitest::notes(h.shell,ib::notes::NoteDone));until([&]{h.boundary();return !h.shell.notesPanel()->active();});h.effect("");h.settle();CHECK(ux::QolTestAccess::note(h.shell,id));h.boundary();h.effect("");until([&]{h.boundary();return h.shell.notesPanel()->canLeave();});CHECK(h.shell.notesPanel()->view().note["id"].toString()==id);CHECK(uitest::notes(h.shell,ib::notes::NoteDone));until([&]{h.boundary();return !h.shell.notesPanel()->active();});h.effect("");h.settle();
 // Transient decorative submission refusal retains cleanup and keeps the UI
 // alive. Do not report false source acceptance or swallow unrelated failures.
 CHECK(h.shell.action(ux::Action::Show));h.boundary();h.settle();CHECK(uitest::openTools(h.shell));h.shell.prepareSand(h.vendor.surface());CHECK(h.shell.sandFrameFor(h.shell.sandRegion()));h.lease.live=false;CHECK(ux::DisplayTestAccess::decorative(h.display,h.shell.sandRegion())==spike::Submit::Busy);h.lease.live=true;h.effect("");CHECK(!h.shell.failed());CHECK(uitest::tap(h.shell,uitest::toolsClose(h.shell)));h.effect("");h.settle();
 // Navigation and selection cancel rather than replaying on ordinary updates.
 CHECK(h.shell.action(ux::Action::Show));h.boundary();h.settle();CHECK(uitest::openTools(h.shell));h.boundary();ux::QolTestAccess::orientationCancel(h.shell);h.effect("");CHECK(uitest::tap(h.shell,uitest::toolsClose(h.shell)));h.effect("");h.settle();CHECK(h.shell.navigateTo(1));until([&]{h.boundary();return h.shell.page()==1&&h.shell.writable();});h.effect("");CHECK(h.calls.full==full);CHECK(h.shell.finish(5000));
 std::cout<<"SAND_ROUTE_PASS tom_cycles=20 tom_starts="<<tomStarts<<" notes_identity=RETAINED definition=FOUND pen_erase=PRODUCTION_ENGINE max_cache="<<h.shell.sandMetrics().cacheBytes<<" no_new_complete_updates=true providers=NONE raw_input=NONE optics=NOT_RUN\n";return 0;
 }catch(const std::exception& e){std::cerr<<"SAND_ROUTE_FAIL "<<e.what()<<'\n';return 1;}}
