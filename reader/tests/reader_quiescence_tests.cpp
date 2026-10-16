// SPDX-License-Identifier: AGPL-3.0-only
#include "ux_support.h"
#include "ui_targets.h"
#include "ux_display.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QProcess>
#include <QTemporaryDir>
#include <iostream>
#include <string_view>
using Path=std::filesystem::path;
namespace {
unsigned cases{};
struct NoHardwareCalls final:spike::VendorCalls {
 unsigned calls{};
 bool acquire(spike::Pixels&)noexcept override{++calls;return false;}
 void region(spike::Rect,int,int,int)noexcept override{++calls;}
 void events()noexcept override{++calls;}
};
void proof(const char* label){++cases;std::cout<<"PASS "<<label<<'\n'<<std::flush;}
library::Document importBook(const Path& app,const Path& source,const Path& root){
 underlay::privateDirectory(root/"inbox");std::filesystem::copy_file(source/"fixtures/library/authored-three-pages.pdf",root/"inbox/authored.pdf");std::atomic<bool> stop{false};reader::syncLibrary(app,root,stop);auto books=library::documents(root);CHECK(books.size()==1&&books[0].pages==3);return books[0];
}
void settle(ux::Shell& shell,Canvas& canvas){until([&]{shell.tick();canvas.changes();CHECK(!shell.failed());return shell.writable()&&shell.styledInk()&&!shell.styledInk()->status().busy&&!shell.unsaved()&&!shell.semantics().pending();});}
void stroke(spike::OperationSink& sink){CHECK(sink.reserve());CHECK(sink.append({300,600,spike::Phase::Down,1,false}));CHECK(sink.append({350,620,spike::Phase::Move,2,false}));CHECK(sink.append({380,640,spike::Phase::Up,3,false}));}
void assertClosed(ux::Shell& s){
 CHECK(!s.writable()&&!s.reserve()&&s.exiting());CHECK(!s.choose(0));CHECK(!s.action(ux::Action::Show));CHECK(!s.action(ux::Action::Next));CHECK(!s.finger(350,1980,false));CHECK(!s.finger(1180,2080,false));CHECK(!s.navigateTo(1));CHECK(!s.returnToLibrary());CHECK(!s.applyPdfView({}));CHECK(!s.applyEpubView({}));CHECK(!s.selectionDrag(200,300,400,300,s.version()));CHECK(!s.tick());
}
int interruptedChild(const Path& app,const Path& root){
 ux::Shell s(app,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settle(s,c);
 CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::menu(s,ux::MenuNotes));
 until([&]{s.tick();c.changes();return s.notesPanel()&&s.notesPanel()->view().ready&&!s.notesPanel()->view().busy;});
 CHECK(uitest::notes(s,ib::notes::HubCreate));until([&]{s.tick();c.changes();return s.notesPanel()->writable()&&!s.notesPanel()->view().busy;});
 CHECK(s.reserve());CHECK(s.append({250,1500,spike::Phase::Down,1,false})); /* inside the compact note sheet's writing area */CHECK(s.notesPanel()->contact());
 // Use the exact terminal cancellation route used by runDirectSession.
 // Do not initialize Display or VendorDisplay: no touch/device/vendor access.
 NoHardwareCalls calls;spike::VendorDisplay vendor(calls);ux::Display display(vendor,s);
 CHECK(display.operationSink());display.operationSink()->cancel();
 CHECK(calls.calls==0);assertClosed(s);CHECK(s.notesPanel()->failed()&&s.notesPanel()->unsaved()==1);
 ux::Shell::Shutdown state;until([&]{state=s.pollQuiesce();return state.drained;});CHECK(!state.saved&&state.failed);CHECK(s.notesPanel()->unsaved()==1);
 const auto docroot=library::documentRoot(root,s.currentDocumentId());CHECK(!reader::readViewResume(docroot,3,{}).clean);
 for(unsigned i=0;i<32;++i){auto again=s.pollQuiesce();CHECK(again.drained&&!again.saved&&again.failed);CHECK(!s.finish(0));}
 display.shutdown();CHECK(calls.calls==0);
 std::puts("INTERRUPTED_NOTE_RETAINED_DRAINED_NOT_SAVED");std::fflush(stdout);
 return 0; // Shell destructor must refuse clean shutdown with exit 78.
}
int resumeFailureChild(const Path& app,const Path& root){
 ux::Shell s(app,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settle(s,c);
 const auto docroot=library::documentRoot(root,s.currentDocumentId()),resume=docroot/"resume.json",original=docroot/"resume-before-final-failure.json";
 until([&]{return reader::readViewResume(docroot,3,{}).generation>0;});
 CHECK(!reader::readViewResume(docroot,3,{}).clean&&s.unsaved()==0&&!s.failed());
 const auto initial=underlay::readBounded(resume,4096);CHECK(!std::filesystem::exists(original));
 // Preserve the initial unclean fence. An exact test-owned directory makes
 // final atomic replacement fail deterministically, including when run as root.
 std::filesystem::rename(resume,original);CHECK(std::filesystem::create_directory(resume));
 s.beginQuiesce();assertClosed(s);ux::Shell::Shutdown state;
 until([&]{state=s.pollQuiesce();return state.drained;});
 CHECK(!state.saved&&state.failed&&s.unsaved()==0&&s.document()->drained());
 CHECK(s.document()->failed()&&!s.document()->shutdownSaved());
 CHECK(std::filesystem::is_directory(resume)&&std::filesystem::is_empty(resume));
 CHECK(underlay::readBounded(original,4096)==initial);
 // Remove only the synthetic empty directory, then restore the original fence.
 // Even with the obstruction gone, polling must not retry or upgrade the result.
 CHECK(std::filesystem::remove(resume));std::filesystem::rename(original,resume);
 for(unsigned i=0;i<32;++i){auto again=s.pollQuiesce();CHECK(again.drained&&!again.saved&&again.failed);CHECK(!s.finish(0));CHECK(!reader::readViewResume(docroot,3,{}).clean);CHECK(underlay::readBounded(resume,4096)==initial);}
 std::puts("FINAL_RESUME_WRITE_FAILED_DRAINED_NOT_SAVED");std::fflush(stdout);
 return 0; // Shell destructor must refuse clean shutdown with exit 78.
}
}
int main(int argc,char** argv){QCoreApplication application(argc,argv);
 try{
  if(argc==4&&std::string_view(argv[1])=="--interrupted-note")return interruptedChild(argv[2],argv[3]);
  if(argc==4&&std::string_view(argv[1])=="--resume-write-failure")return resumeFailureChild(argv[2],argv[3]);
  if(argc!=5)return 64;const Path source=argv[1],parent=argv[2],build=argv[3],sys=argv[4];underlay::privateDirectory(parent);
  QTemporaryDir owned(QString::fromStdString((parent/"reader-quiescence-XXXXXX").string()));CHECK(owned.isValid());owned.setAutoRemove(false);Path data=owned.path().toStdString();std::cout<<"READER_QUIESCENCE_DATA "<<data<<'\n'<<std::flush;auto app=stage(source,data/"stage",build,sys);
  {
   auto root=data/"reader-held-fence";
   {auto prepared=reader::initialPrepare(app,root);reader::Reader r(app,root,std::move(prepared));r.activate();
   until([&]{return reader::readResume(root,2).generation>0;});auto initial=underlay::readBounded(root/"resume.json",4096);CHECK(!reader::readResume(root,2).clean);
   stroke(r);r.beginQuiesce();CHECK(!r.writable()&&!r.reserve()&&!r.navigateTo(1));
   // Canonical drain is a deterministic external latch. No timing threshold
   // asserts that the worker must have run between particular polling calls.
   for(unsigned i=0;i<128;++i){CHECK(!r.pollQuiesce(false,true));CHECK(!r.drained()&&!r.shutdownSaved());CHECK(underlay::readBounded(root/"resume.json",4096)==initial);}
   until([&]{CHECK(!r.pollQuiesce(false,true));return r.committed()==1&&r.unsaved()==0;});CHECK(underlay::readBounded(root/"resume.json",4096)==initial);
   until([&]{return r.pollQuiesce(true,true);});CHECK(r.drained()&&r.shutdownSaved()&&!r.failed());auto final=underlay::readBounded(root/"resume.json",4096);CHECK(reader::readResume(root,2).clean);
   for(unsigned i=0;i<32;++i){r.beginQuiesce();CHECK(r.pollQuiesce(true,true));CHECK(underlay::readBounded(root/"resume.json",4096)==final);}
   } // Release the journal lock with Reader ownership before external replay.
   std::atomic<bool> stop{false};auto reopened=reader::preparePage(app,root,0,stop);CHECK(reopened.lastId==1);CHECK(reopened.ink[600*underlay::Stride+300]!=0xffffffffu);proof("canonical fence withheld across repeated polls; accepted journal write durable and replayable; final fence stable");
  }
  {
   auto root=data/"reader-canonical-failure";reader::Reader r(app,root,reader::initialPrepare(app,root));r.beginQuiesce();until([&]{return r.pollQuiesce(true,false);});CHECK(r.drained()&&!r.shutdownSaved()&&!reader::readResume(root,2).clean);auto final=underlay::readBounded(root/"resume.json",4096);
   for(unsigned i=0;i<32;++i){CHECK(r.pollQuiesce(true,true));CHECK(!r.shutdownSaved()&&underlay::readBounded(root/"resume.json",4096)==final);}proof("unclean canonical result cannot be upgraded by later successful polling");
  }
  {
   auto root=data/"reader-prefetch-ownership";reader::Reader r(app,root,reader::initialPrepare(app,root));r.activate();until([&]{return r.resident()>1;});
   // Never tick: any prefetched page is owned by the preparer/result mailbox,
   // not cache_. Drain must retire it before claiming all Reader workers drained.
   r.beginQuiesce();until([&]{return r.pollQuiesce(true,true);});CHECK(r.drained()&&r.resident()==1);proof("uninstalled prefetched page workers are retired before reader drain");
  }
  const auto root=data/"library";auto book=importBook(app,source,root);const auto docroot=library::documentRoot(root,book.id);QJsonArray stored;
  {
   ux::Shell s(app,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settle(s,c);stroke(s);s.beginQuiesce();assertClosed(s);
   ux::Shell::Shutdown state;until([&]{state=s.pollQuiesce();return state.drained;});CHECK(state.saved&&!state.failed&&s.unsaved()==0);CHECK(s.styledInk()->drained()&&s.semantics().drained()&&s.document()->drained());CHECK(reader::readViewResume(docroot,3,{}).clean);
   {product::Services services(product::featureDirectory(root,book));stored=services.snapshot()["documents"].toObject()[book.id].toObject()["strokes"].toArray();}CHECK(stored.size()==1);auto final=underlay::readBounded(docroot/"resume.json",4096);
   for(unsigned i=0;i<32;++i){state=s.pollQuiesce();CHECK(state.drained&&state.saved&&!state.failed);assertClosed(s);CHECK(underlay::readBounded(docroot/"resume.json",4096)==final);}CHECK(s.finish(0));proof("Shell closes every public admission path and drains accepted canonical stroke before clean resume");
  }
  {
   ux::Shell s(app,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settle(s,c);CHECK(s.styledInk()->snapshot()["documents"].toObject()[book.id].toObject()["strokes"].toArray()==stored);CHECK(s.finish(5000));proof("fresh Shell reopens exact durably drained canonical stroke");
  }
  {
   auto interrupted=data/"interrupted-library";importBook(app,source,interrupted);QProcess child;child.setProgram(QString::fromStdString((sys/"usr/lib/ld-linux-aarch64.so.1").string()));child.setArguments({"--library-path",QString::fromStdString((sys/"usr/lib").string()),QString::fromUtf8(argv[0]),"--interrupted-note",QString::fromStdString(app.string()),QString::fromStdString(interrupted.string())});child.start();CHECK(child.waitForStarted(5000)&&child.waitForFinished(45000));auto out=child.readAllStandardOutput(),err=child.readAllStandardError();if(child.exitCode()!=78||!out.contains("INTERRUPTED_NOTE_RETAINED_DRAINED_NOT_SAVED")){std::cerr<<out.constData()<<err.constData();CHECK(false);}CHECK(child.exitStatus()==QProcess::NormalExit);proof("interrupted Notes contact remains unsaved after drain; repeated finish and destructor refuse clean exit");
  }
  {
   auto failedResume=data/"resume-failure-library";importBook(app,source,failedResume);QProcess child;child.setProgram(QString::fromStdString((sys/"usr/lib/ld-linux-aarch64.so.1").string()));child.setArguments({"--library-path",QString::fromStdString((sys/"usr/lib").string()),QString::fromUtf8(argv[0]),"--resume-write-failure",QString::fromStdString(app.string()),QString::fromStdString(failedResume.string())});child.start();CHECK(child.waitForStarted(5000)&&child.waitForFinished(45000));auto out=child.readAllStandardOutput(),err=child.readAllStandardError();if(child.exitCode()!=78||!out.contains("FINAL_RESUME_WRITE_FAILED_DRAINED_NOT_SAVED")){std::cerr<<out.constData()<<err.constData();CHECK(false);}CHECK(child.exitStatus()==QProcess::NormalExit);proof("actual final resume-write failure drains without saved outcome; restored unclean fence stays unchanged and destructor refuses clean exit");
  }
  CHECK(owned.remove());std::cout<<"READER_QUIESCENCE_PASS cases="<<cases<<" real_helpers=true provider_calls=NONE owner_protocol=UNCHANGED physical=NOT_RUN\n";return 0;
 }catch(const std::exception& e){std::cerr<<"READER_QUIESCENCE_FAIL "<<e.what()<<'\n';return 1;}
}
