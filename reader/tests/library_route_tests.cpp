// Project-authored integration regression: real helper/parser, no vendor/input.
#include "ui_targets.h"
#include "touch.h"
#include "ux_support.h"
#include "library.h"
#include "ib/library_screen.h"
#include <QCoreApplication>
#include <QProcess>
#include <QImage>
#include <QRegularExpression>
#include <algorithm>
#include <chrono>
#include <thread>
#include <fstream>
#include <cstdio>
#include <string_view>
using Path=std::filesystem::path;
static int importViaHelper(const Path& application,const Path& source,const Path& root){
 QProcess child;child.start(QString::fromStdString((application/"bin/reader-experience1-prepare").string()),
   {"--import",QString::fromStdString(source.string()),QString::fromStdString(root.string())});
 CHECK(child.waitForStarted(2000));
 if(!child.waitForFinished(30000)){child.kill();child.waitForFinished(2000);throw std::runtime_error("library-import-helper-timeout");}
 auto output=child.readAllStandardOutput()+child.readAllStandardError();
 std::fwrite(output.data(),1,output.size(),stdout);std::fflush(stdout);
 CHECK(child.exitStatus()==QProcess::NormalExit);return child.exitCode();
}
static unsigned indexOf(const Path& root,const QString& id){auto docs=library::documents(root);for(int i=0;i<docs.size();++i)if(docs[i].id==id)return unsigned(i);throw std::runtime_error("document-id-not-in-picker");}
static library::Document byFormat(const Path& root,const QString& format){for(auto d:library::documents(root))if(d.format==format)return d;throw std::runtime_error("format-not-imported");}
static void mark(ux::Shell& shell,int y){
 CHECK(shell.reserve());spike::Point previous{100,y,spike::Phase::Down,1,false};
 for(auto point:std::array{previous,spike::Point{350,y,spike::Phase::Move,2,false},spike::Point{350,y,spike::Phase::Up,3,false}}){
  spike::segment(shell.ink(),previous,point,2);CHECK(shell.append(point));previous=point;
 }
}
static void next(ux::Shell& shell,Canvas& canvas){CHECK(shell.action(ux::Action::Show));canvas.changes();CHECK(shell.action(ux::Action::Next));canvas.ready();}
static void highlightWord(ux::Shell& shell,Canvas& canvas,const QString& word){
 auto g=shell.semantics().geometry();CHECK(g&&g->available());auto words=g->data["words"].toArray();int index=-1;
 for(int i=0;i<words.size();++i){auto selected=semantic::select(g,unsigned(i),unsigned(i),1);if(selected&&selected->data["normalized"].toString()==word){index=i;break;}}
 CHECK(index>=0);auto w=words[index].toArray();auto quad=g->data["chars"].toArray()[w[0].toInt()].toObject()["quad"].toArray();double x=0,y=0;
 for(int i=0;i<8;i+=2){x+=quad[i].toDouble()/4;y+=quad[i+1].toDouble()/4;}
 auto m=g->data["transform"].toArray();auto sx=m[0].toDouble()*x+m[2].toDouble()*y+m[4].toDouble();auto sy=m[1].toDouble()*x+m[3].toDouble()*y+m[5].toDouble();
 auto prior=shell.styledInk()->style().tool;CHECK(shell.styledInk()->selectTool(styled::Tool::Highlighter));until([&]{shell.tick();canvas.changes();return !shell.styledInk()->status().busy;});
 CHECK(selectForActions(shell,int(sx),int(sy)));CHECK(shell.semantics().action(semantic::Action::Highlight));
 until([&]{shell.tick();canvas.changes();CHECK(!shell.failed());return shell.semantics().highlightAcks()==1&&shell.semantics().pending()==0;});
 CHECK(shell.semantics().highlights().size()==1);CHECK(shell.styledInk()->selectTool(prior));until([&]{shell.tick();canvas.changes();return !shell.styledInk()->status().busy;});
}
int main(int argc,char**argv){QCoreApplication core(argc,argv);if(argc!=5&&(argc!=7||std::string_view(argv[5])!="--case"))return 64;
 Path source=argv[1],data=argv[2],build=argv[3],sys=argv[4];unsigned cases=0;const std::string_view filter=argc==7?std::string_view(argv[6]):std::string_view{};
 // Diagnostic filtering preserves the shared import prerequisite. Stateful
 // cross-case assertions still require the normal unfiltered run.
 auto test=[&](const char* name,auto f){bool selected=filter.empty()||filter==name;bool setup=!filter.empty()&&std::string_view(name)=="actual_parser_imports_authored_pdf_and_epub";if(!selected&&!setup)return;f();if(selected)++cases;std::printf("%s %s\n",selected?"PASS":"SETUP",name);std::fflush(stdout);};
 try{
  underlay::privateDirectory(data);auto application=stage(source,data/"stage",build,sys);
  std::filesystem::copy_file(source/"fixtures/dictionary-test.json",application/"fixtures/dictionary-test.json");
  auto root=data/"library";auto fixtures=source/"fixtures/library";
  test("normal_startup_inbox_import_is_durable_and_reports_rejections",[&]{
   auto inboxRoot=data/"startup-library";underlay::privateDirectory(inboxRoot/"inbox");
   for(auto name:{"authored-three-pages.pdf","authored-three-chapters.epub","malformed.pdf"})std::filesystem::copy_file(fixtures/name,inboxRoot/"inbox"/name);
   std::atomic<bool> stop{false};reader::syncLibrary(application,inboxRoot,stop);CHECK(library::documents(inboxRoot).size()==2);
   auto rejected=QJsonDocument::fromJson(underlay::readBounded(inboxRoot/"import-errors.json",16384)).array();CHECK(rejected.size()==1);
   auto saved=underlay::readBounded(inboxRoot/"catalogue.json",2*1024*1024);reader::syncLibrary(application,inboxRoot,stop);CHECK(underlay::readBounded(inboxRoot/"catalogue.json",2*1024*1024)==saved);
   CHECK(std::filesystem::exists(inboxRoot/"inbox/malformed.pdf"));ux::Shell startup(application,inboxRoot,true);CHECK(startup.libraryDocuments().size()==2&&startup.finish(2000));
  });
  test("automatic_inbox_import_without_ui_trigger",[&]{auto automatic=data/"automatic-inbox";underlay::privateDirectory(automatic/"inbox");std::filesystem::copy_file(fixtures/"authored-three-pages.pdf",automatic/"inbox/new-payload.pdf");ux::Shell shell(application,automatic,true,false,false);Canvas canvas(shell);until([&]{shell.tick();canvas.changes();CHECK(!shell.failed());return shell.libraryDocuments().size()==1;});CHECK(!std::filesystem::exists(automatic/"inbox/new-payload.pdf"));CHECK(std::distance(std::filesystem::directory_iterator(automatic/"inbox/imported"),std::filesystem::directory_iterator{})==2);CHECK(shell.finish(2000));});
  test("automatic_inbox_waits_for_an_arriving_file",[&]{auto arriving=data/"arriving-inbox";underlay::privateDirectory(arriving/"inbox");
   const auto bytes=underlay::readBounded(fixtures/"authored-three-pages.pdf",library::MaxSourceBytes);const auto target=arriving/"inbox/arriving.pdf";
   ux::Shell shell(application,arriving,true,false,false);Canvas canvas(shell);
   auto noErrors=[&]{return !std::filesystem::exists(arriving/"import-errors.json")||QJsonDocument::fromJson(underlay::readBounded(arriving/"import-errors.json",16384)).array().isEmpty();};
   /* the copy grows for 12 s: no automatic pass may read it, so nothing is imported and nothing is rejected */
   for(int part=1;part<=8;++part){{std::ofstream out(target,std::ios::binary|std::ios::trunc);out.write(bytes.constData(),std::streamsize(bytes.size()*part/8));CHECK(out.good());}
    const auto end=std::chrono::steady_clock::now()+std::chrono::milliseconds(1500);while(std::chrono::steady_clock::now()<end){shell.tick();canvas.changes();CHECK(!shell.failed());std::this_thread::sleep_for(std::chrono::milliseconds(20));}
    CHECK(shell.libraryDocuments().isEmpty()&&noErrors());}
   until([&]{shell.tick();canvas.changes();CHECK(!shell.failed());return shell.libraryDocuments().size()==1;});
   CHECK(shell.libraryDocuments()[0].sha256==QString::fromLatin1(underlay::digest(bytes).toHex())&&noErrors()&&!std::filesystem::exists(target));CHECK(shell.finish(2000));});
  test("actual_parser_imports_authored_pdf_and_epub",[&]{
   CHECK(importViaHelper(application,fixtures/"authored-three-pages.pdf",root)==0);
   CHECK(importViaHelper(application,fixtures/"authored-three-chapters.epub",root)==0);
   auto docs=library::documents(root);CHECK(docs.size()==2);CHECK(byFormat(root,"pdf").pages==3);CHECK(byFormat(root,"epub").pages>=3);
   for(auto& d:docs){CHECK(library::validId(d.id)&&d.id!=d.sha256&&QRegularExpression("^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$").match(d.id).hasMatch());auto document=library::documentRoot(root,d.id);CHECK(std::filesystem::exists(document/"cover.png"));CHECK(!library::descriptor(document).isEmpty());}
  });
  const auto pdf=byFormat(root,"pdf"),epub=byFormat(root,"epub");CHECK(pdf.id!=epub.id);
  test("default_pdf_epub_color_and_highlighter_snap_context",[&]{for(const auto& id:{pdf.id,epub.id}){ux::Shell shell(application,root,true,false,false);Canvas canvas(shell);auto docs=shell.libraryDocuments();auto found=std::find_if(docs.begin(),docs.end(),[&](const auto& d){return d.id==id;});CHECK(found!=docs.end()&&shell.choose(unsigned(found-docs.begin())));canvas.ready();CHECK(std::count_if(shell.background().begin(),shell.background().end(),[](auto p){return (p>>24)==255&&((p>>16)&255)>210&&((p>>8)&255)<70&&(p&255)<70;})>100);CHECK(shell.action(ux::Action::Show));canvas.changes();auto h=ib::reader::toolbarSlot(ib::reader::SlotHighlighter);CHECK(shell.finger((h.x0+h.x1)/2,(h.y0+h.y1)/2));until([&]{shell.tick();canvas.changes();return !shell.styledInk()->status().busy;});if(!shell.toolsView().active())CHECK(shell.finger((h.x0+h.x1)/2,(h.y0+h.y1)/2));canvas.changes();CHECK(shell.toolsView().active()&&shell.styledInk()->style().tool==styled::Tool::Highlighter);auto snap=ib::reader::snapCell();const bool before=shell.snapHighlights();CHECK(shell.finger((snap.x0+snap.x1)/2,(snap.y0+snap.y1)/2));CHECK(shell.snapHighlights()!=before&&shell.styledInk()->style().tool==styled::Tool::Highlighter);canvas.changes();CHECK(shell.finish(2000));ux::Shell reopened(application,root,true,false,false);CHECK(reopened.snapHighlights()!=before);CHECK(reopened.finish(2000));}});
  test("epub_zoom_keeps_layout_and_maps_stylus_to_original_source",[&]{auto zoomRoot=data/"zoom-input";CHECK(importViaHelper(application,fixtures/"authored-three-chapters.epub",zoomRoot)==0);ux::Shell shell(application,zoomRoot,true,false,false);Canvas canvas(shell);CHECK(shell.choose(0));canvas.ready();const auto before=shell.styledInk()->snapshot();auto g=shell.semantics().geometry();CHECK(g);const auto matrix=g->data["transform"].toArray();CHECK(shell.pinch(2,810,300)&&shell.pdfZoom()==2);canvas.changes();CHECK(shell.styledInk()->snapshot()==before&&shell.semantics().geometry()->data==g->data);{std::vector<std::uint32_t> pixels(underlay::Pixels,0xffabcdefu);const spike::Rect damage{400,800,600,1000};shell.compose({pixels,1620,2160,1632},damage);for(int y=0;y<2160;++y)for(int x=0;x<1632;++x)if(x<damage.x0||x>=damage.x1||y<damage.y0||y>=damage.y1)CHECK(pixels[std::size_t(y)*1632+x]==0xffabcdefu);}mark(shell,900);until([&]{shell.tick();canvas.changes();CHECK(!shell.failed());return shell.styledInk()->status().saved==1&&!shell.styledInk()->status().busy;});auto id=byFormat(zoomRoot,"epub").id;const auto doc=shell.styledInk()->snapshot()["documents"].toObject()[id].toObject();auto strokes=doc["strokes"].toArray();CHECK(strokes.size()==1);const auto point=strokes.last().toObject()["points"].toArray().first().toArray();const double logicalX=matrix[0].toDouble()*point[0].toDouble()+matrix[2].toDouble()*point[1].toDouble()+matrix[4].toDouble(),logicalY=matrix[1].toDouble()*point[0].toDouble()+matrix[3].toDouble()*point[1].toDouble()+matrix[5].toDouble();CHECK(std::abs(logicalX-455)<1&&std::abs(logicalY-600)<1);CHECK(shell.inputScale()==2&&shell.styledInk()->setEraser(true));mark(shell,900);until([&]{shell.tick();canvas.changes();CHECK(!shell.failed());return shell.styledInk()->status().saved>=2&&!shell.styledInk()->status().busy;});CHECK(shell.styledInk()->snapshot()["documents"].toObject()[id].toObject()["strokes"].toArray().isEmpty());CHECK(shell.styledInk()->setEraser(false)&&shell.styledInk()->undo());until([&]{shell.tick();canvas.changes();return !shell.styledInk()->status().busy;});CHECK(shell.styledInk()->snapshot()["documents"].toObject()[id].toObject()["strokes"].toArray()==strokes);CHECK(shell.twoFingerPan(-200,100,500));canvas.changes();CHECK(shell.resetZoom()&&shell.pdfZoom()==1);canvas.changes();CHECK(shell.styledInk()->snapshot()["documents"].toObject()[id].toObject()["strokes"].toArray()==strokes);CHECK(shell.finish(2000));});
  test("duplicate_import_is_stable_idempotent",[&]{
   auto before=underlay::readBounded(root/"catalogue.json",2*1024*1024);
   CHECK(importViaHelper(application,fixtures/"authored-three-pages.pdf",root)==0);
   CHECK(library::documents(root).size()==2&&byFormat(root,"pdf").id==pdf.id);
   CHECK(underlay::readBounded(root/"catalogue.json",2*1024*1024)==before);
  });
  test("invalid_imports_do_not_mutate_catalogue_or_escape",[&]{
   auto before=underlay::readBounded(root/"catalogue.json",2*1024*1024);
   for(auto name:{"malformed.pdf","malformed.epub","traversal.epub","oversized-entry.epub"}){
    CHECK(importViaHelper(application,fixtures/name,root)!=0);
    CHECK(underlay::readBounded(root/"catalogue.json",2*1024*1024)==before);
   }
   auto disguised=data/"disguised.pdf";std::filesystem::copy_file(fixtures/"authored-three-chapters.epub",disguised);CHECK(importViaHelper(application,disguised,root)!=0);CHECK(underlay::readBounded(root/"catalogue.json",2*1024*1024)==before);
   CHECK(!std::filesystem::exists(root/"escape.txt")&&!std::filesystem::exists(root/"documents/escape.txt"));
   library::Store reopen(root);CHECK(reopen.all().size()==2);
  });
  test("failed_catalogue_commit_keeps_memory_uncommitted_and_recovers_receipt",[&]{
   auto recoveryRoot=data/"commit-recovery";QString recoveredId;
   {
    library::Store store(recoveryRoot);bool failed=false;
    library::Inspector interrupted=[&](const Path&,const QString&){
     std::filesystem::rename(recoveryRoot/"catalogue.json",recoveryRoot/"catalogue.kept");
     std::filesystem::create_directory(recoveryRoot/"catalogue.json");
     return library::Inspection{pdf.pages,pdf.layout,{}};
    };
    try{store.importDocument(fixtures/"authored-three-pages.pdf","Interrupted import",interrupted);}catch(...){failed=true;}
    // Restore the old durable catalogue before inspecting the failed transaction.
    std::filesystem::remove(recoveryRoot/"catalogue.json");
    std::filesystem::rename(recoveryRoot/"catalogue.kept",recoveryRoot/"catalogue.json");
    CHECK(failed&&library::documents(recoveryRoot).isEmpty());CHECK(store.all().isEmpty());
   }
   {library::Store recovered(recoveryRoot);CHECK(recovered.all().size()==1);recoveredId=recovered.all()[0].id;}
   {library::Store again(recoveryRoot);CHECK(again.all().size()==1&&again.all()[0].id==recoveredId);}
  });
  test("invalid_tombstone_refuses_recovery_without_catalogue_mutation",[&]{
   auto recoveryRoot=data/"commit-recovery";auto docs=library::documents(recoveryRoot);CHECK(docs.size()==1);
   auto marker=recoveryRoot/"tombstones"/(docs[0].id.toStdString()+".json");
   auto before=underlay::readBounded(recoveryRoot/"catalogue.json",2*1024*1024);underlay::atomicFile(marker,QByteArray("torn"));
   bool denied=false;try{library::Store broken(recoveryRoot);}catch(...){denied=true;}
   auto after=underlay::readBounded(recoveryRoot/"catalogue.json",2*1024*1024);std::filesystem::remove(marker);
   CHECK(denied&&after==before);
  });
  test("valid_tombstone_recovery_prevents_silent_reimport",[&]{
   auto recoveryRoot=data/"commit-recovery";auto d=library::documents(recoveryRoot)[0];QByteArray beforeDeletion;
   {library::Store store(recoveryRoot);store.trash(d.id,true);beforeDeletion=underlay::readBounded(recoveryRoot/"catalogue.json",2*1024*1024);}
   QJsonObject tombstone{{"version",1},{"id",d.id},{"sha256",d.sha256},{"purge",true}};
   auto payload=QJsonDocument(tombstone).toJson(QJsonDocument::Compact);
   underlay::atomicFile(recoveryRoot/"tombstones"/(d.id.toStdString()+".json"),QJsonDocument(QJsonObject{{"payload",tombstone},{"sha256",QString::fromLatin1(underlay::digest(payload).toHex())}}).toJson(QJsonDocument::Compact));
   // Simulate interrupted catalogue removal after the tombstone committed.
   underlay::atomicFile(recoveryRoot/"catalogue.json",beforeDeletion);
   {library::Store recovered(recoveryRoot);CHECK(recovered.all().isEmpty());}
   CHECK(importViaHelper(application,fixtures/"authored-three-pages.pdf",recoveryRoot)!=0);
   CHECK(library::documents(recoveryRoot).isEmpty());
   // Durable explicit purge approval finishes exact app-owned removal on restart.
   CHECK(!std::filesystem::exists(library::documentRoot(recoveryRoot,d.id)));
   {library::Store secondRecovery(recoveryRoot);CHECK(secondRecovery.all().isEmpty());}
  });
  test("confirmed_purge_requires_trash_and_no_reader_lease_and_preserves_original",[&]{
   auto purgeRoot=data/"purge";auto original=fixtures/"authored-three-pages.pdf";
   auto originalHash=underlay::digest(underlay::readBounded(original,library::MaxSourceBytes));
   CHECK(importViaHelper(application,original,purgeRoot)==0);auto d=library::documents(purgeRoot)[0];auto object=library::documentRoot(purgeRoot,d.id);
   library::Store store(purgeRoot);
   auto denied=[&](auto action){bool failed=false;try{action();}catch(...){failed=true;}CHECK(failed);};
   denied([&]{store.permanentlyDelete(d.id,false);});denied([&]{store.permanentlyDelete(d.id,true);});
   store.trash(d.id,true);
   {library::Lease active(object);denied([&]{store.permanentlyDelete(d.id,true);});}
   CHECK(std::filesystem::exists(object/"source.pdf")&&!std::filesystem::exists(purgeRoot/"tombstones"/(d.id.toStdString()+".json")));
   std::filesystem::create_symlink(original,object/"unsafe-link");denied([&]{store.permanentlyDelete(d.id,true);});
   CHECK(std::filesystem::exists(object/"source.pdf")&&!std::filesystem::exists(purgeRoot/"tombstones"/(d.id.toStdString()+".json")));std::filesystem::remove(object/"unsafe-link");
   store.permanentlyDelete(d.id,true);CHECK(!std::filesystem::exists(object)&&store.all().isEmpty());
   CHECK(underlay::digest(underlay::readBounded(original,library::MaxSourceBytes))==originalHash);
  });
  test("imported_descriptor_cannot_rebind_another_document_directory",[&]{
   auto target=library::documentRoot(root,pdf.id)/"document.json";
   auto original=underlay::readBounded(target,8192);
   auto other=underlay::readBounded(library::documentRoot(root,epub.id)/"document.json",8192);
   underlay::atomicFile(target,other);bool denied=false;
   try{library::descriptor(target.parent_path());}catch(...){denied=true;}
   underlay::atomicFile(target,original);CHECK(denied);
  });
  test("imported_route_missing_descriptor_never_opens_legacy_fixture",[&]{
   auto document=library::documentRoot(root,pdf.id);auto descriptor=document/"document.json";auto held=document/"document.held";
   std::filesystem::rename(descriptor,held);bool refused=false;
   try{
    ux::Shell shell(application,root,true);Canvas canvas(shell);
    if(!shell.choose(indexOf(root,pdf.id)))refused=true;
    else{until([&]{shell.tick();canvas.changes();return shell.failed()||shell.writable();});refused=shell.failed()&&!shell.writable();}
    CHECK(shell.finish(2000));
   }catch(...){std::filesystem::rename(held,descriptor);throw;}
   std::filesystem::rename(held,descriptor);CHECK(refused);
  });
  QByteArray pdfInk,epubInk,pdfBinding,epubBinding;QString pdfPageId;
  test("picker_pdf_route_draw_highlight_and_resume",[&]{
   ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(shell.choose(indexOf(root,pdf.id)));canvas.ready();CHECK(shell.page()==0&&shell.document()->total()==3);
   next(shell,canvas);CHECK(shell.page()==1);CHECK(shell.semantics().highlights().isEmpty());mark(shell,1300);pdfInk=inkHash(shell);
   pdfBinding=shell.document()->active().prepared.binding;highlightWord(shell,canvas,"river");pdfPageId=shell.semantics().geometry()->pageId();
   CHECK(shell.finish(2000)&&shell.unsaved()==0&&!shell.failed());CHECK(shell.committed()==1);
  });
  test("picker_epub_route_starts_without_pdf_state",[&]{
   ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(shell.choose(indexOf(root,epub.id)));canvas.ready();
   CHECK(shell.page()==0&&shell.semantics().highlights().isEmpty());CHECK(shell.document()->active().prepared.lastId==0);
   CHECK(shell.ink().pixels[1300*underlay::Stride+200]==0xffffffffu);
   next(shell,canvas);CHECK(shell.page()==1);CHECK(shell.semantics().highlights().isEmpty());
   epubBinding=shell.document()->active().prepared.binding;CHECK(epubBinding!=pdfBinding);
   CHECK(shell.semantics().geometry()&&shell.semantics().geometry()->available());CHECK(shell.semantics().geometry()->pageId()!=pdfPageId);
   mark(shell,1450);epubInk=inkHash(shell);CHECK(epubInk!=pdfInk);CHECK(shell.finish(2000)&&shell.unsaved()==0&&!shell.failed());
  });
  test("reopen_pdf_restores_only_pdf_ink_highlight_and_position",[&]{
   ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(shell.choose(indexOf(root,pdf.id)));canvas.ready();
   CHECK(shell.page()==1&&inkHash(shell)==pdfInk&&shell.semantics().highlights().size()==1);
   CHECK(shell.document()->active().prepared.binding==pdfBinding);CHECK(shell.ink().pixels[1450*underlay::Stride+200]==0xffffffffu);
   CHECK(shell.finish(2000)&&shell.unsaved()==0&&!shell.failed());
  });
  test("reopen_epub_restores_only_epub_ink_and_position",[&]{
   ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(shell.choose(indexOf(root,epub.id)));canvas.ready();
   CHECK(shell.page()==1&&inkHash(shell)==epubInk&&shell.semantics().highlights().isEmpty());CHECK(shell.document()->active().prepared.binding==epubBinding);
   CHECK(shell.finish(2000)&&shell.unsaved()==0&&!shell.failed());
  });
  /* D2 user-ended sessions: the library's Exit and the document's circled close (top corner opposite the toolbar) never end the session (only the power button does) */
  test("user_ended_exit_taps_return_to_library_and_never_end_the_session",[&]{
   ux::Shell shell(application,root,true);shell.setUserEnded(true);Canvas canvas(shell);CHECK(shell.state()==ux::State::Picker);
   CHECK(uitest::wifi(shell));CHECK(!shell.exiting()&&shell.state()==ux::State::Picker);CHECK(uitest::closeWifi(shell));
   CHECK(shell.choose(indexOf(root,pdf.id)));canvas.ready();CHECK(shell.action(ux::Action::Show));canvas.changes();CHECK(shell.state()==ux::State::Chrome);
   {auto r=ux::closeTarget();CHECK(shell.finger((r.x0+r.x1)/2,(r.y0+r.y1)/2,false));}until([&]{shell.tick();canvas.changes();CHECK(!shell.failed()&&!shell.exiting());return shell.state()==ux::State::Picker;});
   CHECK(uitest::wifi(shell));CHECK(!shell.exiting());CHECK(uitest::closeWifi(shell));CHECK(shell.finish(2000)&&!shell.failed());
  });
  test("same_shell_library_round_trip_rebinds_epoch_and_document_state",[&]{
   ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(shell.choose(indexOf(root,pdf.id)));
   /* the "Opening …" line is neutral ink (no hue to linger on the panel) and ends with the open, so My Files never shows it after close */
   CHECK(shell.libraryView().notice().startsWith("Opening"));canvas.changes();
   {const auto r=ib::library::NoticeRect;bool inked=false,neutral=true;for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x){const auto p=canvas.pixels[std::size_t(y)*underlay::Stride+x];const auto R=p>>16&255,G=p>>8&255,B=p&255;inked|=R<200;neutral&=R==G&&G==B;}CHECK(inked&&neutral);}
   canvas.ready();CHECK(shell.libraryView().notice().isEmpty());
   CHECK(shell.page()==1&&inkHash(shell)==pdfInk&&shell.semantics().highlights().size()==1);
   auto pdfEpoch=shell.version();auto oldGeometry=shell.semantics().geometry();CHECK(oldGeometry);
   CHECK(shell.returnToLibrary());
   until([&]{shell.tick();canvas.changes();CHECK(!shell.failed());return shell.state()==ux::State::Picker;});
   CHECK(shell.document()==nullptr&&shell.version()>pdfEpoch&&!shell.semantics().hasSelection());
   CHECK(shell.libraryView().notice().isEmpty());{const auto r=ib::library::NoticeRect;bool blank=true;for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x)blank&=canvas.pixels[std::size_t(y)*underlay::Stride+x]==0xffffffffu;CHECK(blank);}
   CHECK(shell.semantics().highlights().isEmpty());CHECK(shell.ink().pixels[1300*underlay::Stride+200]==0xffffffffu);
   auto pickerEpoch=shell.version();CHECK(shell.choose(indexOf(root,epub.id)));canvas.ready();
   CHECK(shell.version()>pickerEpoch&&shell.page()==1&&inkHash(shell)==epubInk);
   CHECK(shell.semantics().highlights().isEmpty()&&!shell.semantics().hasSelection());
   CHECK(shell.semantics().geometry()->pageId()!=oldGeometry->pageId());CHECK(shell.document()->active().prepared.binding==epubBinding);
   CHECK(shell.returnToLibrary());until([&]{shell.tick();canvas.changes();CHECK(!shell.failed());return shell.state()==ux::State::Picker;});
   CHECK(shell.choose(indexOf(root,pdf.id)));canvas.ready();CHECK(inkHash(shell)==pdfInk&&shell.semantics().highlights().size()==1);
   CHECK(shell.document()->active().prepared.binding==pdfBinding);CHECK(shell.finish(2000)&&shell.unsaved()==0&&!shell.failed());
  });
  test("rename_keeps_document_identity_and_paths",[&]{
   auto path=library::documentRoot(root,pdf.id);auto before=reader::pagePath(path,1);
   {library::Store store(root);store.rename(pdf.id,"Renamed Orchard");auto backup=QJsonDocument::fromJson(underlay::readBounded(root/"catalogue.previous.json",2*1024*1024)).object()["payload"].toObject()["documents"].toArray();bool oldTitle=false;for(auto row:backup)if(row.toObject()["id"].toString()==pdf.id)oldTitle=row.toObject()["title"].toString()==pdf.title;CHECK(oldTitle);store.organize(pdf.id,"Authored tests");}
   auto docs=library::documents(root,false,"Renamed","Authored tests");CHECK(docs.size()==1&&docs[0].id==pdf.id);CHECK(reader::pagePath(path,1)==before);
   ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(shell.choose(indexOf(root,pdf.id)));canvas.ready();CHECK(inkHash(shell)==pdfInk&&shell.semantics().highlights().size()==1);CHECK(shell.finish(2000));
  });
  test("real_library_management_trash_restore_keeps_ink",[&]{
   ux::Shell shell(application,root,true);Canvas canvas(shell);auto ids=shell.visibleLibraryIds();CHECK(!ids.isEmpty());auto id=ids[0];
   auto background=shell.background();QImage image(reinterpret_cast<const uchar*>(background.data()),1620,2160,1632*4,QImage::Format_RGB32);CHECK(image.save(QString::fromStdString((data/"library-grid.png").string())));
   CHECK(uitest::libraryHold(shell,ib::library::DocumentBase));CHECK(uitest::manage(shell,ux::ManageTrash));auto trash=library::documents(root,true);CHECK(trash.size()==1&&trash[0].id==id);
   CHECK(uitest::library(shell,ib::library::NavBase+int(ib::library::Section::Trash)));CHECK(uitest::library(shell,ib::library::DocumentBase));CHECK(uitest::manage(shell,ux::ManageRestore));CHECK(library::documents(root,true).isEmpty());
   CHECK(shell.choose(indexOf(root,id)));canvas.ready();CHECK(inkHash(shell)==(id==pdf.id?pdfInk:epubInk));CHECK(shell.finish(2000));
  });
  test("catalogue_import_lock_cannot_block_ink_or_resume",[&]{
   library::Store importLock(root);ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(shell.choose(indexOf(root,pdf.id)));canvas.ready();mark(shell,1600);CHECK(shell.finish(2000)&&shell.unsaved()==0&&!shell.failed());auto saved=reader::readResume(library::documentRoot(root,pdf.id),pdf.pages);CHECK(saved.page==1&&saved.clean);
  });
  test("actual_touch_tools_pen_over_panel_single_commit_and_durable_reopen",[&]{
   auto toolRoot=data/"tools-route";CHECK(importViaHelper(application,fixtures/"authored-three-pages.pdf",toolRoot)==0);auto book=byFormat(toolRoot,"pdf");QByteArray written;
   {ux::Shell shell(application,toolRoot,true,false);Canvas canvas(shell);CHECK(shell.choose(0));canvas.ready();CHECK(shell.styledInk());auto blank=inkHash(shell);
    auto settle=[&]{until([&]{shell.tick();canvas.changes();CHECK(!shell.failed());return shell.unsaved()==0&&!shell.styledInk()->status().busy;});};
    auto tap=[&](spike::Rect r){CHECK(shell.finger((r.x0+r.x1)/2,(r.y0+r.y1)/2,false));canvas.changes();settle();};
    CHECK(shell.action(ux::Action::Show));canvas.changes();CHECK(uitest::openTools(shell));canvas.changes();
    tap(ux::ToolsView::toolRect(1));tap(ux::ToolsView::widthRect(2));tap(ux::ToolsView::colorRect(6));
    CHECK(shell.styledInk()->style().tool==styled::Tool::Fineliner&&shell.styledInk()->style().width==styled::Width::Thick&&shell.styledInk()->style().color==6);
    mark(shell,1200);settle();written=inkHash(shell);CHECK(written!=blank);CHECK(shell.styledInk()->snapshot()["documents"].toObject()[book.id].toObject()["strokes"].toArray().size()==1);CHECK(shell.document()->active().prepared.lastId==0&&shell.document()->active().saves->committed==0);
    tap(uitest::toolsClose(shell));CHECK(shell.action(ux::Action::Hide));canvas.changes();CHECK(canvas.pixels[1200*underlay::Stride+200]==0xff567db8u);
    CHECK(shell.action(ux::Action::Show));canvas.changes();CHECK(uitest::openTools(shell));canvas.changes();tap(ux::ToolsView::undo());CHECK(inkHash(shell)==blank);tap(ux::ToolsView::redo());CHECK(inkHash(shell)==written);tap(uitest::toolsClose(shell));CHECK(shell.finish(2000));
   }
   {ux::Shell shell(application,toolRoot,true,false);Canvas canvas(shell);CHECK(shell.choose(0));canvas.ready();CHECK(inkHash(shell)==written&&shell.styledInk()->style().color==6);CHECK(shell.finish(2000));}
  });
  test("direct_page_navigation_is_bounded_and_reopens_exact_target",[&]{
   auto jumpRoot=data/"page-jump";CHECK(importViaHelper(application,fixtures/"authored-three-pages.pdf",jumpRoot)==0);
   {ux::Shell shell(application,jumpRoot,true,false);Canvas canvas(shell);CHECK(shell.choose(0));canvas.ready();CHECK(!shell.navigateTo(-1)&&!shell.navigateTo(3));CHECK(shell.navigateTo(2));canvas.ready();CHECK(shell.page()==2);CHECK(shell.reserve());CHECK(!shell.navigateTo(0));shell.cancel();CHECK(shell.finish(2000));}
   {ux::Shell shell(application,jumpRoot,true,false);Canvas canvas(shell);CHECK(shell.choose(0));canvas.ready();CHECK(shell.page()==2);CHECK(shell.finish(2000));}
  });
  // Settings: Inkbound's own sleep rows. The cover toggle and "Sleep after" choices save through the Shell and survive a restart.
  /* Native toolbar: handedness persists in ui-toolbar.json and moves the toolbar, palette and close; the document
     menu replaces the old top bar; the active shortcut tile toggles the palette; the handle hides the toolbar. */
  test("native_toolbar_handedness_document_menu_and_palette",[&]{
   auto settings=[&](ux::Shell& sh,Canvas& cv){CHECK(uitest::library(sh,ib::library::NavBase+int(ib::library::Section::Settings)));sh.tick();cv.changes();};
   {ux::Shell shell(application,root,true);Canvas canvas(shell);settings(shell,canvas);CHECK(uitest::library(shell,955));shell.tick();canvas.changes();
    CHECK(ib::reader::toolbarSide()==ib::reader::Side::Right);CHECK(underlay::readBounded(root/"ui-toolbar.json",4096).contains("\"side\":\"right\""));CHECK(shell.finish(2000));}
   ib::reader::setToolbarSide(ib::reader::Side::Left); /* a fresh shell must restore the side from disk */
   {ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(shell.choose(indexOf(root,pdf.id)));canvas.ready();CHECK(shell.action(ux::Action::Show));canvas.changes();
    CHECK(shell.chromeTargets().rect(ib::reader::RailPen).x0>=1620-ib::reader::ToolbarWidth&&shell.chromeTargets().rect(ib::reader::Back).x0==0);
    CHECK(uitest::chrome(shell,ib::reader::RailMore)&&shell.menuOpen());canvas.changes();
    for(int id:{int(ux::MenuSettings),int(ux::MenuPages),int(ux::MenuBookmark),int(ux::MenuNotes),int(ux::MenuTom),int(ux::MenuAbout)})CHECK(!shell.menuTargets().rect(id).empty());
    CHECK(shell.menuTargets().rect(ux::MenuLayers).empty()&&shell.menuTargets().rect(ux::MenuSettings).x1<=1620-ib::reader::ToolbarWidth+2);
    CHECK(uitest::chrome(shell,ib::reader::RailMore)||!shell.menuOpen());canvas.changes();CHECK(!shell.menuOpen());
    CHECK(uitest::openTools(shell));canvas.changes();CHECK(ux::ToolsView::panel().x1<=1620-ib::reader::ToolbarWidth+2);
    CHECK(uitest::tap(shell,shell.toolsView().closeRect()));canvas.changes();CHECK(!shell.toolsView().active()&&shell.state()==ux::State::Chrome);
    CHECK(uitest::chrome(shell,ib::reader::ToolHandle));canvas.changes();CHECK(shell.state()==ux::State::Clean);
    /* Quick browse: swipe up from the bottom edge; it stays open while the page changes underneath; Return goes back. */
    {const int start=shell.page();CHECK(shell.gesture(reader::Gesture::EdgeUp)&&shell.menuOpen());canvas.changes();
     CHECK(uitest::tap(shell,shell.menuTargets().rect(ib::sheets::QuickNext)));canvas.ready();CHECK(shell.page()==start+1&&shell.menuOpen()&&!shell.menuTargets().rect(ib::sheets::QuickReturn).empty());
     CHECK(uitest::tap(shell,shell.menuTargets().rect(ib::sheets::QuickReturn)));canvas.ready();CHECK(shell.page()==start&&shell.menuOpen());
     CHECK(shell.finger(800,600,false)&&!shell.menuOpen());canvas.changes();}
    CHECK(shell.finish(2000)&&!shell.failed());}
   {ux::Shell shell(application,root,true);Canvas canvas(shell);settings(shell,canvas);CHECK(uitest::library(shell,955));shell.tick();canvas.changes();
    CHECK(ib::reader::toolbarSide()==ib::reader::Side::Left);CHECK(shell.finish(2000));}
  });
  test("settings_sleep_rows",[&]{auto root=data/"sleep-settings";underlay::privateDirectory(root);
   {ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(uitest::library(shell,ib::library::NavBase+int(ib::library::Section::Settings)));shell.tick();canvas.changes();
    CHECK(!shell.libraryView().targets().rect(953).empty()&&!shell.libraryView().targets().rect(960+5).empty()&&shell.sleepSettings().cover&&shell.sleepSettings().idleMinutes==15);
    CHECK(uitest::library(shell,953));shell.tick();canvas.changes();CHECK(!shell.sleepSettings().cover);
    CHECK(uitest::library(shell,960));shell.tick();canvas.changes();CHECK(shell.sleepSettings().idleMinutes==0&&shell.libraryView().notice().isEmpty());CHECK(shell.finish(2000));}
   {ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(!shell.sleepSettings().cover&&shell.sleepSettings().idleMinutes==0);
    CHECK(uitest::library(shell,ib::library::NavBase+int(ib::library::Section::Settings)));shell.tick();canvas.changes();CHECK(uitest::library(shell,953)&&uitest::library(shell,960+3));shell.tick();canvas.changes();CHECK(shell.sleepSettings().cover&&shell.sleepSettings().idleMinutes==15);CHECK(shell.finish(2000));}});
  // Settings pages (mockup 25): the page list switches pages through the real shell; each page carries only its own rows.
  test("settings_pages",[&]{auto root=data/"settings-pages";underlay::privateDirectory(root);namespace S=ib::settings;
   {ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(uitest::library(shell,ib::library::NavBase+int(ib::library::Section::Settings)));shell.tick();canvas.changes();
   const auto& view=shell.libraryView();CHECK(view.settingsPage()==S::Page::Reading);for(unsigned p=0;p<unsigned(S::Page::Count);++p)CHECK(!view.targets().rect(S::PageBase+int(p)).empty());
   auto open=[&](S::Page p){CHECK(uitest::library(shell,S::PageBase+int(p)));shell.tick();canvas.changes();CHECK(view.settingsPage()==p);};
   open(S::Page::Storage);CHECK(view.targets().rect(S::SleepCover).empty()&&view.targets().rect(S::Leave).empty());
   open(S::Page::WiFi);CHECK(!view.targets().rect(S::Wifi).empty()&&view.targets().rect(S::SleepCover).empty());
   open(S::Page::General);CHECK(!view.targets().rect(S::Leave).empty());
   open(S::Page::Reading);CHECK(!view.targets().rect(S::SleepCover).empty()&&!view.targets().rect(S::LeftHanded).empty()&&!view.targets().rect(S::IdleBase+5).empty());
   // Reading progress stays here; Snap to Text is now exclusively in the highlighter toolbar.
   CHECK(shell.readingProgress()&&shell.snapHighlights());CHECK(uitest::library(shell,S::ReadingProgress));shell.tick();canvas.changes();CHECK(shell.libraryView().targets().rect(S::SnapHighlights).empty());
   CHECK(!shell.readingProgress()&&shell.snapHighlights());CHECK(!shell.failed()&&shell.finish(2000));}
   {ux::Shell again(application,data/"settings-pages",true);CHECK(!again.readingProgress()&&again.snapHighlights());CHECK(again.finish(2000));}});
  // Security, Accessibility, Tom (Settings): the passcode lock through the real shell and the stored preferences.
  test("settings_security_accessibility_tom",[&]{auto root=data/"settings-security";underlay::privateDirectory(root);namespace S=ib::settings;namespace K=ib::lock;
   auto key=[&](ux::Shell& sh,Canvas& cv,char digit){auto r=sh.lockTargets().rect(K::KeyBase+(digit-'0'));CHECK(!r.empty());CHECK(sh.finger((r.x0+r.x1)/2,(r.y0+r.y1)/2,false));sh.tick();cv.changes();};
   auto code=[&](ux::Shell& sh,Canvas& cv,const char* digits){for(const char* d=digits;*d;++d)key(sh,cv,*d);};
   auto open=[&](ux::Shell& sh,Canvas& cv,S::Page p){CHECK(uitest::library(sh,ib::library::NavBase+int(ib::library::Section::Settings)));sh.tick();cv.changes();CHECK(uitest::library(sh,S::PageBase+int(p)));sh.tick();cv.changes();CHECK(sh.libraryView().settingsPage()==p);};
   {ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(!shell.locked()&&!shell.passcodeSet()&&!shell.lockForSleep());
    open(shell,canvas,S::Page::Security);CHECK(uitest::library(shell,S::PasscodeSet));shell.tick();canvas.changes();CHECK(shell.locked()&&!shell.reserve());
    code(shell,canvas,"1234");CHECK(shell.locked());code(shell,canvas,"9999");CHECK(shell.locked()&&!shell.passcodeSet()); /* mismatch: choose again */
    code(shell,canvas,"1234");code(shell,canvas,"1234");CHECK(!shell.locked()&&shell.passcodeSet());
    const auto stored=underlay::readBounded(root/"ui-passcode.json",4096);CHECK(!stored.contains("1234"));struct stat st{};CHECK(lstat((root/"ui-passcode.json").c_str(),&st)==0&&(st.st_mode&0077)==0);
    // Before sleep the passcode screen is raised (once), and it takes over input until the code is entered.
    CHECK(shell.lockForSleep()&&shell.locked()&&!shell.lockForSleep());(void)uitest::library(shell,ib::library::Menu);shell.tick();canvas.changes();CHECK(shell.locked()&&shell.libraryView().effectScene()!=2); /* the drawer does not open under the lock */
    code(shell,canvas,"1234");CHECK(!shell.locked());CHECK(shell.finish(2000));}
   {ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(shell.locked()); /* locked from the start */
    for(int attempt=0;attempt<security::MaxAttempts;++attempt)code(shell,canvas,"0000");
    CHECK(shell.locked()&&shell.lockTargets().rect(K::KeyBase+1).empty()); /* attempt limit: the keypad is inert */CHECK(shell.finish(2000));}
   {ux::Shell shell(application,root,true);Canvas canvas(shell);CHECK(shell.locked());code(shell,canvas,"1234");CHECK(!shell.locked());
    open(shell,canvas,S::Page::Security);CHECK(uitest::library(shell,S::PasscodeOff));shell.tick();canvas.changes();CHECK(shell.locked());code(shell,canvas,"1234");CHECK(!shell.locked()&&!shell.passcodeSet());
    // Accessibility and Tom preferences.
    open(shell,canvas,S::Page::Accessibility);const auto normal=ib::theme::Disabled; /* Inkie text and lines are ink in both modes: high contrast darkens disabled controls */CHECK(uitest::library(shell,S::HighContrast));shell.tick();canvas.changes();CHECK(shell.prefs().highContrast&&ib::theme::HighContrast&&ib::theme::Disabled!=normal);
    CHECK(uitest::library(shell,S::ReduceMotion));shell.tick();canvas.changes();CHECK(shell.prefs().reduceMotion&&!shell.quietEffectsEnabled());
    open(shell,canvas,S::Page::Tom);CHECK(uitest::library(shell,S::TomStyleBase+1));shell.tick();canvas.changes();CHECK(uitest::library(shell,S::TomHandBase+2));shell.tick();canvas.changes();CHECK(shell.prefs().tomTyped&&shell.prefs().tomHand==2);
    CHECK(shell.finish(2000));}
   {ux::Shell shell(application,root,true);CHECK(!shell.locked()&&shell.prefs().highContrast&&shell.prefs().reduceMotion&&shell.prefs().tomTyped&&shell.prefs().tomHand==2);CHECK(shell.finish(2000));}
   ib::theme::setHighContrast(false);});
  if(!filter.empty()){CHECK(cases==1);std::printf("LIBRARY_ROUTE_DIAGNOSTIC_PASS cases=%u filter=%s helper=actual-parser vendor=NONE input_devices=NONE\n",cases,argv[6]);}else std::printf("LIBRARY_ROUTE_TESTS_PASS cases=%u helper=actual-parser authored_documents=2 vendor=NONE input_devices=NONE\n",cases);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"LIBRARY_ROUTE_TEST_FAIL %s\n",e.what());return 1;}
}
