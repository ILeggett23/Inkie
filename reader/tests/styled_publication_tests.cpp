#include "styled_ink.h"
#include "product_store.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <cstdio>
#include <iostream>
using Path=std::filesystem::path;
static unsigned checks=0;
static void check(bool b,const char* label){if(!b){std::fprintf(stderr,"STYLED_PUBLICATION_CHECK_FAILED %s\n",label);std::fflush(stderr);throw std::runtime_error(label);}++checks;}
template<class F>static void wait(F predicate){auto end=std::chrono::steady_clock::now()+std::chrono::seconds(5);while(!predicate()){check(std::chrono::steady_clock::now()<end,"bounded_wait");std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
static QByteArray pixels(const styled::Ink& ink){auto p=ink.pixels();return underlay::digest(QByteArray(reinterpret_cast<const char*>(p.data()),p.size_bytes()));}
static int strokes(styled::Ink& ink,const QString& id){return int(ink.snapshot()["documents"].toObject()[id].toObject()["strokes"].toArray().size());}
static void ready(styled::Ink& ink){wait([&]{ink.tick();check(!ink.status().failed,"worker_remains_healthy");return ink.status().ready&&!ink.status().busy&&!ink.unsaved();});}
static void stroke(styled::Ink& ink,int y){check(ink.reserve(),"stroke_admitted");check(ink.append({20,y,spike::Phase::Down,1,false})&&ink.append({90,y,spike::Phase::Move,2,false})&&ink.append({90,y,spike::Phase::Up,3,false}),"stroke_accepted");}
static void published(styled::Ink& ink,const QString& id,int count){
 // Intentionally do not tick: force the durable-state/publication window rather
 // than hoping a scheduling race happens during a UI polling loop.
 wait([&]{check(!ink.status().failed,"publication_worker_healthy");return ink.status().queued==0&&strokes(ink,id)==count;});
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);QTemporaryDir temp;try{check(temp.isValid(),"owned_fixture_root");temp.setAutoRemove(false);Path root=Path(temp.path().toStdString())/"library";underlay::privateDirectory(root);underlay::atomicFile(root/"authored.pdf","%PDF-1.7 publication ordering fixture");library::Document doc;{library::Store store(root);doc=store.importDocument(root/"authored.pdf","Publication ordering",[](const Path&,const QString&){return library::Inspection{1,"pdf-source-v1",{}};});}
 underlay::Prepared prepared;prepared.metadata={{"schema",2},{"document",doc.id},{"pdf",doc.sha256},{"documentFormat",doc.format},{"layout",doc.layout},{"pages",1},{"page",0},{"bounds",QJsonArray{0,0,160,160}},{"pageToSurface",QJsonArray{1,0,0,1,0,0}}};prepared.binding=underlay::digest(QJsonDocument(prepared.metadata).toJson(QJsonDocument::Compact));auto page=product::pageSnapshot(doc,prepared),binding=product::featureBinding(doc);auto features=product::featureDirectory(root,doc);std::vector<std::uint32_t> legacy(160*160,0xffffffffu);
 {
  styled::Ink ink(features,features/"legacy/publication.inkbase",page,legacy,160,160,160);ready(ink);auto blank=pixels(ink);stroke(ink,50);ready(ink);auto written=pixels(ink);check(written!=blank&&strokes(ink,doc.id)==1,"canonical_stroke_and_visible_frame_ready");
  check(ink.undo(),"undo_queued");published(ink,doc.id,0);auto status=ink.status();std::fprintf(stderr,"PUBLICATION_WINDOW phase=undo queued=%u busy=%d admission=%d canonical_strokes=%d old_frame=%d\n",status.queued,status.busy,ink.admissionOpen(),strokes(ink,doc.id),pixels(ink)==written);std::fflush(stderr);check(pixels(ink)==written,"undo_frame_waits_for_owner_tick");check(status.busy,"undo_published_but_unapplied_raster_remains_busy");check(!ink.admissionOpen()&&!ink.reserve(),"undo_unapplied_raster_blocks_new_contact");check(ink.tick(),"undo_owner_installs_raster");check(!ink.status().busy&&ink.admissionOpen()&&pixels(ink)==blank,"undo_install_reopens_correct_blank_frame");
  check(ink.redo(),"redo_queued");published(ink,doc.id,1);status=ink.status();std::fprintf(stderr,"PUBLICATION_WINDOW phase=redo queued=%u busy=%d admission=%d canonical_strokes=%d old_frame=%d\n",status.queued,status.busy,ink.admissionOpen(),strokes(ink,doc.id),pixels(ink)==blank);std::fflush(stderr);check(pixels(ink)==blank,"redo_frame_waits_for_owner_tick");check(status.busy&&!ink.admissionOpen()&&!ink.reserve(),"redo_unapplied_raster_blocks_new_contact");check(ink.tick(),"redo_owner_installs_raster");check(!ink.status().busy&&ink.admissionOpen()&&pixels(ink)==written,"redo_install_restores_exact_frame");
  {
   // Hold the canonical writer only long enough to make an ordinary stroke
   // backlog deterministic. Ordinary queued strokes must not close admission.
   product::Store held(features,binding);stroke(ink,85);check(ink.status().queued>0&&ink.admissionOpen(),"normal_stroke_backlog_keeps_contact_admission");stroke(ink,115);check(ink.status().queued>=2,"second_stroke_accepted_during_first_save");
  }
  ready(ink);check(strokes(ink,doc.id)==3,"ordinary_stroke_backlog_persists_once_each");check(ink.finish(2000)&&ink.unsaved()==0,"clean_publication_drain");
 }
 check(temp.remove(),"owned_fixture_removed");std::cout<<"STYLED_PUBLICATION_PASS undo_redo_publication_guard=true normal_stroke_backlog_admissible=true physical=NOT_RUN\n";return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"STYLED_PUBLICATION_FAIL %s data=%s\n",e.what(),temp.path().toUtf8().constData());return 1;}}
