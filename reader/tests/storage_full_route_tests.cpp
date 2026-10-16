// SPDX-License-Identifier: AGPL-3.0-only
// Real disk-full ladder (R05.5). The library data root is a small separate
// filesystem supplied by the runner; space is exhausted with genuine ENOSPC.
// Nothing written after exhaustion may be reported saved, previously durable
// state must survive reopen, and recovery after space returns must not be poisoned.
#include "ux_support.h"
#include "ui_targets.h"
#include "reader.h"
#include "library.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QProcess>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>
using Path=std::filesystem::path;
static input_event ev(unsigned short type,unsigned short code,int value){input_event e{};e.type=type;e.code=code;e.value=value;return e;}
static void stroke(ux::Shell& s,Canvas& c,int x,int y,int end){
 CHECK(s.writable());spike::Engine engine({{0,1619},{0,2159},1620,2160,0,false},s.ink(),c);spike::FrameAssembler assembler;spike::Scheduler scheduler;spike::SessionStats stats;
 auto down=std::array{ev(EV_KEY,BTN_TOOL_PEN,1),ev(EV_KEY,BTN_TOOL_RUBBER,0),ev(EV_ABS,ABS_X,x),ev(EV_ABS,ABS_Y,y),ev(EV_KEY,BTN_TOUCH,1),ev(EV_SYN,SYN_REPORT,0)};
 CHECK(spike::consumeEvents(down,assembler,engine,scheduler,stats,1,nullptr,&s));auto move=std::array{ev(EV_ABS,ABS_X,end),ev(EV_SYN,SYN_REPORT,0)};CHECK(spike::consumeEvents(move,assembler,engine,scheduler,stats,8000001,nullptr,&s));
 auto up=std::array{ev(EV_KEY,BTN_TOUCH,0),ev(EV_SYN,SYN_REPORT,0)};CHECK(spike::consumeEvents(up,assembler,engine,scheduler,stats,16000001,nullptr,&s));CHECK(engine.flush()==spike::Submit::Accepted&&stats.contacts==1&&stats.released==1);
}
// Writes until the filesystem itself refuses with ENOSPC; any other error fails the test.
static void exhaust(const Path& file){
 int fd=open(file.c_str(),O_CREAT|O_WRONLY|O_TRUNC|O_CLOEXEC,0600);CHECK(fd>=0);std::vector<char> block(65536,'x');
 for(std::size_t size=block.size();size;){auto n=write(fd,block.data(),size);if(n<0){CHECK(errno==ENOSPC);size/=2;}}
 CHECK(!fsync(fd)||errno==ENOSPC);close(fd);
 struct statvfs v{};CHECK(!statvfs(file.parent_path().c_str(),&v)&&v.f_bavail==0);
}
static std::size_t strokes(ux::Shell& s,const QString& id){return std::size_t(s.styledInk()->snapshot()["documents"].toObject()[id].toObject()["strokes"].toArray().size());}
static void openDocument(ux::Shell& s,Canvas& c,const QString& id){for(int i=0;i<s.libraryDocuments().size();++i)if(s.libraryDocuments()[i].id==id){CHECK(s.choose(unsigned(i)));c.ready();return;}CHECK(false);}
static void settle(ux::Shell& s,Canvas& c){until([&]{s.tick();c.changes();CHECK(!s.failed());return s.writable()&&s.styledInk()&&!s.styledInk()->status().busy&&!s.unsaved();});}
// Long-press the n-th geometry word with the Highlighter and request a highlight.
static void requestHighlight(ux::Shell& s,Canvas& c,unsigned index){
 auto g=s.semantics().geometry();CHECK(g&&g->available());auto words=g->data["words"].toArray();CHECK(int(index)<words.size());
 auto w=words[int(index)].toArray();auto quad=g->data["chars"].toArray()[w[0].toInt()].toObject()["quad"].toArray();double x=0,y=0;
 for(int i=0;i<8;i+=2){x+=quad[i].toDouble()/4;y+=quad[i+1].toDouble()/4;}
 auto m=g->data["transform"].toArray();auto sx=m[0].toDouble()*x+m[2].toDouble()*y+m[4].toDouble();auto sy=m[1].toDouble()*x+m[3].toDouble()*y+m[5].toDouble();
 if(s.styledInk()->style().tool!=styled::Tool::Highlighter){CHECK(s.styledInk()->selectTool(styled::Tool::Highlighter));until([&]{s.tick();c.changes();return !s.styledInk()->status().busy;});}
 CHECK(selectForActions(s,int(sx),int(sy)));CHECK(s.semantics().action(semantic::Action::Highlight));
}
static void openNote(ux::Shell& s,Canvas& c){
 CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::menu(s,ux::MenuNotes));until([&]{s.tick();c.changes();return s.notesPanel()&&s.notesPanel()->view().ready&&s.notesPanel()->canLeave();});
 CHECK(uitest::notes(s,ib::notes::HubCreate));until([&]{s.tick();c.changes();return s.notesPanel()->writable()&&s.notesPanel()->canLeave();});
}
// Each exhaustion child opens a fresh Shell on the durable library, exhausts the
// filesystem and attempts exactly ONE kind of write as its first write. A Shell
// holding unsaved work deliberately refuses clean destruction (_Exit(78)), so the
// child asserts, prints its result and leaves via _Exit(0), exactly like the
// existing failed-write boundary test.
static int exhaustChild(const std::string& mode,const Path& full,const Path& data){
 try{
  auto app=data/"stage/application";auto root=full/"library";auto books=library::documents(root);CHECK(books.size()==1);const QString pdf=books[0].id;
  auto* s=new ux::Shell(app,root,true,false,false);auto* c=new Canvas(*s);openDocument(*s,*c,pdf);settle(*s,*c);CHECK(s->page()==1);
  // Tool selection is itself a durable preference write, so it happens before exhaustion.
  if(mode=="highlight"){CHECK(s->styledInk()->selectTool(styled::Tool::Highlighter));settle(*s,*c);}
  /* Finish every unrelated transport before exhaustion: deleting a failed 28 MB temporary would otherwise restore space during the intended failed write. */
  until([&]{s->tick();c->changes();CHECK(!s->failed());return !s->document()->preparing()&&s->document()->resident()==unsigned(s->document()->total())&&s->semantics().ready()&&!s->semantics().busy()&&!s->semantics().pending();});
  const auto saved=s->styledInk()->status().saved;
  exhaust(full/"filler");CHECK(s->writable()&&!s->failed());
  if(mode=="ink"){
   stroke(*s,*c,250,900,450);
   until([&]{s->tick();c->changes();return !s->styledInk()->status().busy;});
   CHECK(s->failed()||s->unsaved()>0);CHECK(s->styledInk()->status().saved==saved);
  }else if(mode=="highlight"){
   requestHighlight(*s,*c,1);
   until([&]{s->tick();c->changes();return s->semantics().failed()||s->failed()||(s->semantics().pending()==0&&!s->semantics().busy());});
   CHECK(s->semantics().highlightAcks()==0);CHECK(s->semantics().failed()||s->failed());
  }else if(mode=="note"){
   CHECK(s->action(ux::Action::Show));c->changes();CHECK(uitest::menu(*s,ux::MenuNotes));
   until([&]{s->tick();c->changes();return s->failed()||(s->notesPanel()&&(s->notesPanel()->failed()||(s->notesPanel()->view().ready&&s->notesPanel()->canLeave())));});
   if(!s->failed()&&!s->notesPanel()->failed()){
    CHECK(uitest::notes(*s,ib::notes::HubCreate));
    until([&]{s->tick();c->changes();return s->failed()||s->notesPanel()->failed()||s->notesPanel()->unsaved()>0||(s->notesPanel()->writable()&&s->notesPanel()->canLeave());});
    if(!s->failed()&&!s->notesPanel()->failed()&&s->notesPanel()->writable()&&s->notesPanel()->canLeave()){
     stroke(*s,*c,250,1200,400);
     until([&]{s->tick();c->changes();return s->failed()||s->notesPanel()->failed()||(!s->notesPanel()->contact()&&!s->notesPanel()->view().busy);});
    }
   }
   // Never reported saved: a visible failure or retained unsaved work.
   CHECK(s->failed()||s->notesPanel()->failed()||s->notesPanel()->unsaved()>0);
  }else CHECK(false);
  struct statvfs pressure{};CHECK(!statvfs(full.c_str(),&pressure)&&pressure.f_bavail==0);std::printf("ENOSPC_STILL_FULL mode=%s free_blocks=0 page_helpers_idle=true\n",mode.c_str());
  CHECK(!s->finish(2000));
  std::printf("EXHAUST_%s_NOT_SAVED\n",mode.c_str());std::fflush(stdout);std::_Exit(0);
 }catch(const std::exception& e){std::fprintf(stderr,"EXHAUST_CHILD_FAIL %s %s\n",mode.c_str(),e.what());std::fflush(stderr);std::_Exit(1);}
}
int main(int argc,char**argv){QCoreApplication application(argc,argv);
 if(argc==5&&std::string_view(argv[1])=="--exhaust-child")return exhaustChild(argv[2],argv[3],argv[4]);
 if(argc!=6)return 64;try{
 const Path source=argv[1],full=argv[2],data=argv[3],build=argv[4],sys=argv[5];unsigned cases=0;
 // The runner must supply a genuinely separate, small, empty, private filesystem.
 struct stat fullStat{},dataStat{};struct statvfs v{};underlay::privateDirectory(data);
 CHECK(!stat(full.c_str(),&fullStat)&&!stat(data.c_str(),&dataStat)&&fullStat.st_dev!=dataStat.st_dev&&fullStat.st_uid==getuid());
 CHECK(!statvfs(full.c_str(),&v)&&std::uint64_t(v.f_blocks)*v.f_frsize<=256ull*1024*1024&&std::filesystem::is_empty(full));
 auto app=stage(source,data/"stage",build,sys);auto root=full/"library";auto fixtures=source/"fixtures/library";
 underlay::privateDirectory(root/"inbox");std::filesystem::copy_file(fixtures/"authored-three-pages.pdf",root/"inbox/authored-three-pages.pdf");
 std::atomic<bool> stop{false};reader::syncLibrary(app,root,stop);auto books=library::documents(root);CHECK(books.size()==1);const QString pdf=books[0].id;
 // Durable baseline with space available: one stroke, page 1, one highlight, one note; clean finish.
 {
  ux::Shell s(app,root,true);Canvas c(s);openDocument(s,c,pdf);settle(s,c);
  stroke(s,c,250,600,450);settle(s,c);CHECK(strokes(s,pdf)==1);
  CHECK(s.navigateTo(1));c.ready();settle(s,c);CHECK(s.page()==1);
  const auto prior=s.styledInk()->style().tool;requestHighlight(s,c,0);
  until([&]{s.tick();c.changes();CHECK(!s.failed());return s.semantics().highlightAcks()==1&&s.semantics().pending()==0;});
  CHECK(s.semantics().highlights().size()==1);CHECK(s.styledInk()->selectTool(prior));settle(s,c);
  openNote(s,c);stroke(s,c,250,1500,400); /* inside ib::notes::NoteArea */until([&]{s.tick();c.changes();return s.notesPanel()->canLeave()&&s.notesPanel()->view().strokeCount==1;});
  CHECK(uitest::notes(s,ib::notes::NoteDone));until([&]{s.tick();c.changes();return !s.notesPanel()->active()&&s.writable()&&!s.styledInk()->status().busy;});settle(s,c);
  CHECK(s.finish(5000));
 }
 std::printf("PASS durable_baseline_ink_page_highlight_note\n");++cases;
 for(const char* mode:{"ink","highlight","note"}){
  QProcess child;child.start(QString::fromStdString((sys/"usr/lib/ld-linux-aarch64.so.1").string()),{"--library-path",QString::fromStdString((sys/"usr/lib").string()),QString::fromStdString((build/"storage-full-route-tests").string()),"--exhaust-child",mode,QString::fromStdString(full.string()),QString::fromStdString(data.string())});
  CHECK(child.waitForStarted(5000)&&child.waitForFinished(120000));auto output=child.readAllStandardOutput()+child.readAllStandardError();std::fwrite(output.data(),1,output.size(),stdout);
  CHECK(child.exitStatus()==QProcess::NormalExit&&child.exitCode()==0&&output.contains(QByteArray("EXHAUST_")+mode+"_NOT_SAVED"));
  CHECK(!unlink((full/"filler").c_str()));
  std::printf("PASS %s_first_write_after_exhaustion_never_saved\n",mode);++cases;
 }
 {
  ux::Shell s(app,root,true);Canvas c(s);CHECK(s.libraryDocuments().size()==1);openDocument(s,c,pdf);settle(s,c);
  CHECK(strokes(s,pdf)==1);CHECK(s.page()==1);CHECK(!s.failed());
  until([&]{s.tick();c.changes();return !s.semantics().busy();});CHECK(s.semantics().highlights().size()==1);
  CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::menu(s,ux::MenuNotes));until([&]{s.tick();c.changes();return s.notesPanel()&&s.notesPanel()->view().ready&&s.notesPanel()->canLeave();});
  CHECK(s.notesPanel()->view().notes.size()==1);CHECK(uitest::notes(s,ib::notes::NoteDone));until([&]{s.tick();c.changes();return !s.notesPanel()->active()&&s.writable();});
  CHECK(s.finish(5000));
 }
 std::printf("PASS reopen_keeps_exact_durable_ink_page_highlight_and_note\n");++cases;
 // Import while full: refused explicitly without catalogue mutation; succeeds once space returns.
 std::filesystem::copy_file(fixtures/"authored-three-chapters.epub",root/"inbox/authored-three-chapters.epub");
 const auto catalogue=underlay::readBounded(root/"catalogue.json",2*1024*1024);exhaust(full/"filler");
 bool refused=false;try{reader::syncLibrary(app,root,stop);}catch(const std::runtime_error& e){refused=std::string_view(e.what())=="library-helper-failed";}
 CHECK(refused);CHECK(library::documents(root).size()==1);CHECK(underlay::readBounded(root/"catalogue.json",2*1024*1024)==catalogue);
 CHECK(std::filesystem::exists(root/"inbox/authored-three-chapters.epub"));
 std::printf("PASS import_while_full_is_refused_without_catalogue_change\n");++cases;
 CHECK(!unlink((full/"filler").c_str()));reader::syncLibrary(app,root,stop);CHECK(library::documents(root).size()==2);
 {ux::Shell s(app,root,true);CHECK(s.libraryDocuments().size()==2);CHECK(s.finish(2000));}
 std::printf("PASS import_recovers_after_space_returns\n");++cases;
 std::printf("STORAGE_FULL_ROUTE_PASS cases=%u filesystem=real_enospc physical=NOT_RUN\n",cases);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"STORAGE_FULL_ROUTE_FAIL %s\n",e.what());return 1;}
}
