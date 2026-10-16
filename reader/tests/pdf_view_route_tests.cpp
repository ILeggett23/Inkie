#include "touch.h"
#include "ui_targets.h"
#include "bookmarks.h"
#include "ux_support.h"
#include "document_view_store.h"
#include "document_controls.h"
#include "ib/notes_screen.h"
#include "ib/tom_screen.h"
#include "ib/status_screen.h"
#include "ux_paint.h"
#include <sys/stat.h>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QJsonArray>
#include <iostream>
#include <cmath>
namespace vs=reader::viewstore;namespace pv=reader::pdfview;using Path=std::filesystem::path;
static unsigned cases=0;
static void proof(const char* label){++cases;std::cout<<"PASS "<<label<<'\n';std::cout.flush();}
static QByteArray hashFile(const Path& p,std::size_t cap=64*1024*1024){return underlay::digest(underlay::readBounded(p,cap));}
static library::Document importBook(const Path& app,const Path& source,const Path& root){underlay::privateDirectory(root/"inbox");std::filesystem::copy_file(source/"fixtures/library/authored-three-pages.pdf",root/"inbox/authored.pdf");std::atomic<bool> stop{false};reader::syncLibrary(app,root,stop);auto docs=library::documents(root);CHECK(docs.size()==1&&docs[0].pages==3);return docs[0];}
static void settled(ux::Shell& s,Canvas& c){until([&]{s.tick();c.changes();CHECK(!s.failed());return s.writable()&&s.styledInk()&&!s.styledInk()->status().busy&&!s.unsaved()&&!s.semantics().pending();});}
static styled::Matrix transform(const QJsonObject& metadata){styled::Matrix out;auto a=metadata["pageToSurface"].toArray();CHECK(a.size()==6);for(int n=0;n<6;++n)out.value[n]=a[n].toDouble();CHECK(out.valid());return out;}
static QJsonArray strokes(ux::Shell& s,const QString& id){return s.styledInk()->snapshot()["documents"].toObject()[id].toObject()["strokes"].toArray();}
static input_event event(unsigned short type,unsigned short code,int value){input_event e{};e.type=type;e.code=code;e.value=value;return e;}
static void canonicalStroke(ux::Shell& s,Canvas& c){auto m=transform(s.document()->active().prepared.metadata);double ax,ay,bx,by;CHECK(m.map(300,400,ax,ay)&&m.map(330,400,bx,by));spike::Engine engine({{0,1619},{0,2159},1620,2160,0,false},s.ink(),c);spike::FrameAssembler assembler;spike::Scheduler scheduler;spike::SessionStats stats;auto down=std::array{event(EV_KEY,BTN_TOOL_PEN,1),event(EV_ABS,ABS_X,int(ax)),event(EV_ABS,ABS_Y,int(ay)),event(EV_KEY,BTN_TOUCH,1),event(EV_SYN,SYN_REPORT,0)};CHECK(spike::consumeEvents(down,assembler,engine,scheduler,stats,1,nullptr,&s));pv::View zoom;zoom.zoom=1.25;CHECK(!s.applyPdfView(zoom));auto move=std::array{event(EV_ABS,ABS_X,int(bx)),event(EV_ABS,ABS_Y,int(by)),event(EV_SYN,SYN_REPORT,0)};CHECK(spike::consumeEvents(move,assembler,engine,scheduler,stats,8000001,nullptr,&s));auto up=std::array{event(EV_KEY,BTN_TOUCH,0),event(EV_SYN,SYN_REPORT,0)};CHECK(spike::consumeEvents(up,assembler,engine,scheduler,stats,16000001,nullptr,&s));CHECK(stats.contacts==1&&stats.released==1&&engine.flush()==spike::Submit::Accepted);settled(s,c);}
static void preservedInk(ux::Shell& s,Canvas& c,const library::Document& book,const QJsonArray& saved){CHECK(strokes(s,book.id)==saved&&saved.size()==1);auto m=transform(s.document()->active().prepared.metadata);auto points=saved[0].toObject()["points"].toArray();CHECK(points.size()>=2);auto a=points.first().toArray(),b=points.last().toArray();double x,y;CHECK(m.map((a[0].toDouble()+b[0].toDouble())/2,(a[1].toDouble()+b[1].toDouble())/2,x,y));int sx=int(std::round(x)),sy=int(std::round(y));CHECK(sx>3&&sx<1616&&sy>3&&sy<2056);auto ink=s.visibleInk();bool painted=false;for(int dy=-2;dy<=2;++dy)for(int dx=-2;dx<=2;++dx){auto at=std::size_t(sy+dy)*underlay::Stride+sx+dx;auto pixel=ink[at];if((pixel>>24)&&pixel!=0xffffffffu){painted=true;CHECK(c.pixels[at]==pixel);}}CHECK(painted);auto style=s.styledInk()->style();CHECK(style.tool==styled::Tool::Fineliner&&style.width==styled::Width::Thick&&style.color==3);}
// A pen stroke in surface coordinates (start, end, a few points between), through the real engine and shell.
static void surfaceStroke(ux::Shell& s,Canvas& c,double ax,double ay,double bx,double by){spike::Engine engine({{0,1619},{0,2159},1620,2160,0,false},s.ink(),c);spike::FrameAssembler assembler;spike::Scheduler scheduler;spike::SessionStats stats;spike::Ns at=1;
 auto down=std::array{event(EV_KEY,BTN_TOOL_PEN,1),event(EV_ABS,ABS_X,int(ax)),event(EV_ABS,ABS_Y,int(ay)),event(EV_KEY,BTN_TOUCH,1),event(EV_SYN,SYN_REPORT,0)};CHECK(spike::consumeEvents(down,assembler,engine,scheduler,stats,at,nullptr,&s));
 for(int i=1;i<=6;++i){at+=8000000;auto move=std::array{event(EV_ABS,ABS_X,int(ax+(bx-ax)*i/6)),event(EV_ABS,ABS_Y,int(ay+(by-ay)*i/6)),event(EV_SYN,SYN_REPORT,0)};CHECK(spike::consumeEvents(move,assembler,engine,scheduler,stats,at,nullptr,&s));}
 at+=8000000;auto up=std::array{event(EV_KEY,BTN_TOUCH,0),event(EV_SYN,SYN_REPORT,0)};CHECK(spike::consumeEvents(up,assembler,engine,scheduler,stats,at,nullptr,&s));CHECK(stats.contacts==1&&stats.released==1&&engine.flush()==spike::Submit::Accepted);settled(s,c);}
// Surface centre of a character of the page's text layer (its quad through the geometry's transform).
static std::pair<double,double> glyphCentre(const semantic::Geometry& g,int index){auto chars=g->data["chars"].toArray();if(index<0||index>=chars.size())throw std::runtime_error("test-glyph-missing");auto q=chars[index].toObject()["quad"].toArray(),m=g->data["transform"].toArray();double x=(q[0].toDouble()+q[6].toDouble())/2,y=(q[1].toDouble()+q[7].toDouble())/2;return {m[0].toDouble()*x+m[2].toDouble()*y+m[4].toDouble(),m[1].toDouble()*x+m[3].toDouble()*y+m[5].toDouble()};}
static void navigate(ux::Shell& s,Canvas& c,int page){CHECK(s.navigateTo(page));c.ready();settled(s,c);CHECK(s.page()==page);}
static void apply(ux::Shell& s,Canvas& c,const pv::View& v){CHECK(s.applyPdfView(v));CHECK(s.state()==ux::State::NavigationPending);c.changes();c.ready();settled(s,c);CHECK(s.state()==ux::State::Clean);}
static void rejectedOpen(const Path& app,const Path& root){ux::Shell s(app,root,true,false);Canvas c(s);CHECK(s.choose(0));until([&]{s.tick();c.changes();CHECK(!s.failed());return s.state()==ux::State::Picker;});CHECK(!s.document()&&!s.writable());CHECK(s.finish(5000));}
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=5)return 64;try{Path source=argv[1],parent=argv[2],build=argv[3],sys=argv[4];underlay::privateDirectory(parent);QTemporaryDir owned(QString::fromStdString((parent/"pdf-view-route-XXXXXX").string()));CHECK(owned.isValid());owned.setAutoRemove(false);Path data=owned.path().toStdString();std::cout<<"PDF_VIEW_ROUTE_DATA "<<data<<'\n';auto application=stage(source,data/"stage",build,sys),root=data/"library";auto book=importBook(application,source,root);auto docroot=library::documentRoot(root,book.id);auto descriptor=underlay::readBounded(docroot/"document.json",8192);auto originalSource=hashFile(docroot/"source.pdf");QJsonArray savedStrokes;QJsonObject sourceGeometry;QByteArray baselineInk,baselineJournal;QString changedSet;Path baselinePath;
 {
  ux::Shell s(application,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);baselinePath=s.document()->active().storage;CHECK(baselinePath==reader::pagePath(docroot,0)&&vs::activeSet(docroot).id.isEmpty());sourceGeometry=s.semantics().geometry()->data;
  CHECK(s.styledInk()->selectTool(styled::Tool::Fineliner));settled(s,c);CHECK(s.styledInk()->selectWidth(styled::Width::Thick));settled(s,c);CHECK(s.styledInk()->selectColor(3));settled(s,c);canonicalStroke(s,c);savedStrokes=strokes(s,book.id);CHECK(savedStrokes.size()==1&&savedStrokes[0].toObject()["coordinateSpace"]=="source-page");baselineInk=inkHash(s);baselineJournal=hashFile(baselinePath/"annotations.ink");CHECK(std::filesystem::file_size(baselinePath/"annotations.ink")==40);preservedInk(s,c,book,savedStrokes);proof("baseline canonical styled ink uses source coordinates and pen contact blocks apply");
  CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::openSettings(s));c.changes();CHECK(!s.writable()&&!s.reserve());CHECK(uitest::setting(s,ib::sheets::ZoomIn));c.changes();CHECK(vs::activeSet(docroot).id.isEmpty()&&s.document()->active().prepared.metadata["schema"]==2);CHECK(uitest::setting(s,ib::sheets::Cancel));c.changes();CHECK(s.action(ux::Action::Hide));c.changes();CHECK(vs::activeSet(docroot).id.isEmpty()&&inkHash(s)==baselineInk);proof("footer controls stage and cancel without committing");
  pv::View zoom;zoom.zoom=1.25;pv::View rotated;rotated.rotation=90;pv::View cropped;cropped.rotation=90;cropped.crop=pv::Rect{40,180,500,500};
  for(const auto& v:{zoom,rotated,cropped}){auto old=s.document()->active().prepared.binding;apply(s,c,v);auto set=vs::activeSet(docroot);CHECK(!set.id.isEmpty());changedSet=set.id;const auto& p=s.document()->active().prepared;CHECK(p.metadata["schema"]==3&&p.binding!=old&&p.metadata["view_id"]==vs::pageConfigId(set,0)&&p.metadata["document"]==book.id&&p.metadata["pdf"]==book.sha256);CHECK(s.document()->active().storage==vs::storagePath(docroot,set,0)&&s.document()->active().storage!=baselinePath&&s.document()->active().storage==reader::pageStoragePath(docroot,0,set.id));CHECK(s.semantics().geometry()->data["text"]==sourceGeometry["text"]&&s.semantics().geometry()->data["chars"]==sourceGeometry["chars"]);CHECK(s.semantics().geometry()->data["transform"]==p.metadata["pageToSurface"]);preservedInk(s,c,book,savedStrokes);CHECK(hashFile(baselinePath/"annotations.ink")==baselineJournal&&underlay::readBounded(docroot/"document.json",8192)==descriptor);}
  proof("zoom rotation crop keep canonical ink source geometry and private page storage");navigate(s,c,1);CHECK(s.document()->active().prepared.metadata["schema"]==2&&s.document()->active().storage==reader::pagePath(docroot,1)&&vs::activeSet(docroot).id==changedSet);until([&]{try{return reader::readViewResume(docroot,book.pages,changedSet).page==1&&library::documents(root)[0].position==1;}catch(...){return false;}});CHECK(s.finish(5000)&&s.unsaved()==0);proof("unmodified page keeps baseline cache while view resume and library progress advance");
 }
 {
  ux::Shell s(application,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);CHECK(s.page()==1&&s.document()->active().storage==reader::pagePath(docroot,1));navigate(s,c,0);CHECK(s.document()->active().prepared.metadata["schema"]==3);preservedInk(s,c,book,savedStrokes);navigate(s,c,1);navigate(s,c,0);preservedInk(s,c,book,savedStrokes);apply(s,c,{});CHECK(vs::activeSet(docroot).id.isEmpty()&&s.document()->active().storage==baselinePath&&s.document()->active().prepared.metadata["schema"]==2&&inkHash(s)==baselineInk);preservedInk(s,c,book,savedStrokes);CHECK(s.finish(5000));proof("fresh Shell resumes selected view and restoring fit preserves exact styles and ink");
 }
 // Receipt failures are exercised only while the reader is closed. The failing
 // open must leave the damaged receipt and all source/annotation data intact.
 vs::activate(docroot,changedSet,{});auto receipt=docroot/"view-sets"/(changedSet.toStdString()+".json");auto valid=underlay::readBounded(receipt,vs::MaxMetadataBytes);underlay::atomicFile(receipt,"{}");rejectedOpen(application,root);CHECK(underlay::readBounded(receipt,32)=="{}"&&hashFile(baselinePath/"annotations.ink")==baselineJournal);underlay::atomicFile(receipt,valid);CHECK(chmod(receipt.c_str(),0644)==0);rejectedOpen(application,root);struct stat st{};CHECK(lstat(receipt.c_str(),&st)==0&&(st.st_mode&0777)==0644);CHECK(chmod(receipt.c_str(),0600)==0);vs::activate(docroot,{},changedSet);proof("corrupt and unsafe receipts reject opening without silent repair or data loss");
 {
  auto legacyRoot=data/"legacy-library";auto legacyBook=importBook(application,source,legacyRoot);auto legacyDoc=library::documentRoot(legacyRoot,legacyBook.id);std::atomic<bool> stop{false};auto prepared=reader::preparePage(application,legacyDoc,0,stop);auto journalPath=reader::pagePath(legacyDoc,0)/"annotations.ink";{underlay::Journal journal(journalPath,prepared.binding);underlay::Operation op;op.id=1;op.count=2;op.points[0]={200,500,spike::Phase::Down};op.points[1]={350,500,spike::Phase::Up};journal.append(op);}auto pinned=hashFile(journalPath);ux::Shell s(application,legacyRoot,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);CHECK(s.styledInk()->hasLegacyInk());pv::View changed;changed.rotation=90;CHECK(!s.applyPdfView(changed)&&vs::activeSet(legacyDoc).id.isEmpty());CHECK(hashFile(journalPath)==pinned&&s.document()->active().prepared.metadata["schema"]==2);CHECK(s.finish(5000));proof("legacy baseline journal pins apply and original ink is retained");
 }
 // Bookmarks through the real shell: top-bar toggle persists; the Pages tab chip jumps there.
 {
  ux::Shell s(application,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);navigate(s,c,1);
  CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::menu(s,ux::MenuBookmark));c.changes();
  {ux::Bookmarks marks;marks.load(root/"ui-bookmarks.json");CHECK(marks.has(book.id,"pdf",1));}
  navigate(s,c,0);CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::openSettings(s));c.changes();CHECK(uitest::setting(s,ib::sheets::TabBase+2));c.changes();
  CHECK(uitest::setting(s,ib::sheets::BookmarkBase));c.changes();CHECK(uitest::setting(s,ib::sheets::Apply));c.changes();until([&]{s.tick();c.changes();CHECK(!s.failed());return s.page()==1&&s.writable();});settled(s,c);
  CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::menu(s,ux::MenuBookmark));c.changes();{ux::Bookmarks marks;marks.load(root/"ui-bookmarks.json");CHECK(!marks.has(book.id,"pdf",1));}
  CHECK(s.finish(5000));std::cout<<"PASS bookmark_toggle_persists_and_pages_tab_jumps\n";
 }
 // A bookmark owns the marked page view, independently of the later resume page and edits on other pages.
 {const auto markedRoot=data/"bookmark-views";const auto markedBook=importBook(application,source,markedRoot);const auto markedDoc=library::documentRoot(markedRoot,markedBook.id);QString savedView;reader::pdfview::View marked;marked.zoom=2;marked.rotation=90;marked.crop=reader::pdfview::Rect{30,30,500,650};marked.pan={40,-70};reader::pdfview::View elsewhere;elsewhere.zoom=1.5;elsewhere.pan={10,40};
  {ux::Shell s(application,markedRoot,true,false,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);apply(s,c,marked);savedView=vs::activeSet(markedDoc).id;CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::menu(s,ux::MenuBookmark));c.changes();navigate(s,c,1);apply(s,c,elsewhere);navigate(s,c,0);apply(s,c,{});navigate(s,c,2);CHECK(s.finish(5000));}
  {ux::Shell s(application,markedRoot,true,false,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);CHECK(s.page()==2);CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::openSettings(s));c.changes();CHECK(uitest::setting(s,ib::sheets::TabBase+2));c.changes();CHECK(uitest::setting(s,ib::sheets::BookmarkBase));c.changes();CHECK(uitest::setting(s,ib::sheets::Apply));c.changes();until([&]{s.tick();c.changes();CHECK(!s.failed());return s.page()==0&&s.writable();});settled(s,c);auto active=vs::activeSet(markedDoc);auto restored=vs::pageEntry(active,0),kept=vs::pageEntry(active,1),original=vs::pageEntry(vs::readSet(markedDoc,savedView),0);CHECK(restored&&kept&&original&&restored->view.zoom==original->view.zoom&&restored->view.rotation==original->view.rotation&&restored->view.pan.x==original->view.pan.x&&restored->view.pan.y==original->view.pan.y&&restored->view.crop&&restored->view.crop->width==original->view.crop->width);CHECK(kept->view.zoom==elsewhere.zoom&&kept->view.pan.y==elsewhere.pan.y);apply(s,c,{});CHECK(s.page()==0);CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::openSettings(s));c.changes();CHECK(uitest::setting(s,ib::sheets::TabBase+2));c.changes();CHECK(uitest::setting(s,ib::sheets::BookmarkBase));c.changes();CHECK(uitest::setting(s,ib::sheets::Apply));c.changes();until([&]{s.tick();c.changes();return s.writable()&&vs::pageEntry(vs::activeSet(markedDoc),0)&&vs::pageEntry(vs::activeSet(markedDoc),0)->view.zoom==marked.zoom;});settled(s,c);navigate(s,c,2);CHECK(s.finish(5000));}
  {ux::Shell s(application,markedRoot,true,false,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);CHECK(s.page()==2&&vs::pageEntry(vs::activeSet(markedDoc),0)->view.zoom==marked.zoom);CHECK(s.finish(5000));}proof("bookmark restores exact zoom pan crop rotation after exit elsewhere; other-page view edits and ordinary resume survive");const auto receipt=markedDoc/"view-sets"/(savedView.toStdString()+".json");const auto held=std::filesystem::path(receipt.string()+".held");std::filesystem::rename(receipt,held);{ux::Shell s(application,markedRoot,true,false,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);const auto active=vs::activeSet(markedDoc).id;CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::openSettings(s));c.changes();CHECK(uitest::setting(s,ib::sheets::TabBase+2));c.changes();CHECK(uitest::setting(s,ib::sheets::BookmarkBase));c.changes();CHECK(uitest::setting(s,ib::sheets::Apply));c.changes();CHECK(!s.failed()&&s.page()==2&&vs::activeSet(markedDoc).id==active);CHECK(uitest::setting(s,ib::sheets::Cancel));c.changes();CHECK(s.writable()&&s.page()==2);CHECK(s.finish(5000));}std::filesystem::rename(held,receipt);proof("missing bookmarked receipt refuses cleanly without leaving the current reading position");}
 // Page overview thumbnails come from the isolated helper over a pipe; none are written to the document.
 {
  ux::Shell s(application,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);overviewThumbnails(s,c,docroot);
  CHECK(s.finish(5000));std::cout<<"PASS page_overview_thumbnails_render_off_path_without_storage_writes\n";
 }
 // Snap highlights to text through the real shell and store: a highlighter stroke across the first word becomes one text
 // highlight (the freehand stroke is gone) in a single undo step; with snapping off the same stroke stays freehand.
 {
  ux::Shell s(application,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);CHECK(s.snapHighlights());
  CHECK(s.styledInk()->selectTool(styled::Tool::Highlighter));settled(s,c);CHECK(QString::fromLatin1(styled::colorId(s.styledInk()->highlighterColor()))!="black");
  auto g=s.semantics().geometry();CHECK(g&&g->available());auto word=g->data["words"].toArray().first().toArray();CHECK(word[1].toInt()>word[0].toInt());
  auto [ax,ay]=glyphCentre(g,word[0].toInt());auto [bx,by]=glyphCentre(g,word[1].toInt()-1);
  auto doc=[&]{return s.styledInk()->snapshot()["documents"].toObject()[book.id].toObject();};const auto before=doc();
  surfaceStroke(s,c,ax-30,ay,bx+30,by);
  until([&]{s.tick();c.changes();CHECK(!s.failed());return doc()["highlights"].toArray().size()==before["highlights"].toArray().size()+1;});settled(s,c);
  CHECK(doc()["strokes"].toArray()==before["strokes"].toArray());CHECK(doc()["highlights"].toArray().last().toObject()["color"].toString()==QString::fromLatin1(styled::colorId(s.styledInk()->highlighterColor())));
  CHECK(s.styledInk()->undo());settled(s,c);until([&]{s.tick();c.changes();return doc()["highlights"].toArray()==before["highlights"].toArray();});CHECK(doc()["strokes"].toArray()==before["strokes"].toArray());
  s.setSnapHighlights(false);surfaceStroke(s,c,ax-30,ay,bx+30,by);for(int i=0;i<50;++i){s.tick();c.changes();}settled(s,c);
  CHECK(doc()["strokes"].toArray().size()==before["strokes"].toArray().size()+1&&doc()["highlights"].toArray()==before["highlights"].toArray());
  s.setSnapHighlights(true);CHECK(s.finish(5000));std::cout<<"PASS snap_highlights_to_text_one_undo_step_and_off_keeps_freehand\n";
 }
 // Removals request a quality cleanup of exactly what changed: undoing a snapped highlight asks for one over its words.
 {
  ux::Shell s(application,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);CHECK(s.styledInk()->selectTool(styled::Tool::Highlighter));settled(s,c);
  auto g=s.semantics().geometry();auto word=g->data["words"].toArray().first().toArray();auto [ax,ay]=glyphCentre(g,word[0].toInt());auto [bx,by]=glyphCentre(g,word[1].toInt()-1);
  auto doc=[&]{return s.styledInk()->snapshot()["documents"].toObject()[book.id].toObject();};const auto before=doc()["highlights"].toArray().size();
  surfaceStroke(s,c,ax-30,ay,bx+30,by);until([&]{s.tick();c.changes();return doc()["highlights"].toArray().size()==before+1;});settled(s,c);
  const unsigned cleanups=c.cleanups;CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::chrome(s,ib::reader::RailUndo));c.changes();CHECK(s.action(ux::Action::Hide));c.changes();until([&]{s.tick();c.changes();return doc()["highlights"].toArray().size()==before&&c.cleanups>cleanups;});
  CHECK(c.cleaned.x0<=int(ax)&&c.cleaned.x1>=int(bx)&&c.cleaned.y0<=int(ay)&&c.cleaned.y1>=int(ay));
  // Plain strokes never ask for one: a pen stroke adds ink without a cleanup.
  CHECK(s.styledInk()->selectTool(styled::Tool::Fineliner));settled(s,c);const unsigned quiet=c.cleanups;surfaceStroke(s,c,300,1500,520,1500);for(int i=0;i<30;++i){s.tick();c.changes();}settled(s,c);CHECK(c.cleanups==quiet);
  CHECK(s.styledInk()->undo());settled(s,c);for(int i=0;i<30;++i){s.tick();c.changes();}
  CHECK(s.finish(5000));std::cout<<"PASS removals_request_cleanup_additions_do_not\n";
 }
 // The selection tool with the pen selects text (no ink is drawn or saved); a drag from a selection end moves that end.
 {
  ux::Shell s(application,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);
  CHECK(s.action(ux::Action::Show));c.changes();CHECK(!s.selectMode()&&uitest::chrome(s,ib::reader::RailSelect));c.changes();CHECK(s.selectMode()&&s.selectionEnabled()&&s.state()==ux::State::Chrome); /* 20u contract: the toolbar stays visible while selecting */
  auto g=s.semantics().geometry();auto words=g->data["words"].toArray();CHECK(words.size()>=3);
  auto doc=[&]{return s.styledInk()->snapshot()["documents"].toObject()[book.id].toObject();};const auto strokes=doc()["strokes"].toArray();
  auto [ax,ay]=glyphCentre(g,words[0].toArray()[0].toInt());auto [bx,by]=glyphCentre(g,words[0].toArray()[1].toInt()-1);
  surfaceStroke(s,c,ax-25,ay,bx+25,by);for(int i=0;i<20;++i){s.tick();c.changes();}
  CHECK(s.semantics().hasSelection()&&s.semantics().view()==semantic::View::Menu);auto first=s.semantics().selection()->data["text"].toString();CHECK(!first.isEmpty()&&!first.contains(' '));
  CHECK(doc()["strokes"].toArray()==strokes); /* nothing was written */
  // Drag the end handle to the third word: the selection grows to three words, starting where it started.
  auto [cx,cy]=glyphCentre(g,words[2].toArray()[1].toInt()-1);auto q=s.semantics().selection()->data["quads"].toArray().last().toArray();auto m=g->data["transform"].toArray();
  const double ex=m[0].toDouble()*(q[2].toDouble()-1)+m[2].toDouble()*((q[3].toDouble()+q[7].toDouble())/2)+m[4].toDouble(),ey=m[1].toDouble()*(q[2].toDouble()-1)+m[3].toDouble()*((q[3].toDouble()+q[7].toDouble())/2)+m[5].toDouble();
  surfaceStroke(s,c,ex,ey,cx,cy);for(int i=0;i<20;++i){s.tick();c.changes();}
  auto grown=s.semantics().selection()->data["text"].toString();CHECK(grown.startsWith(first)&&grown.size()>first.size()&&grown.count(' ')>=1);
  CHECK(doc()["strokes"].toArray()==strokes);
  // Collapsing chrome preserves the explicitly chosen Selection tool. A pen
  // gesture still selects text and writes nothing until a writing tool is chosen.
  CHECK(s.action(ux::Action::Hide));c.changes();CHECK(s.selectMode()&&s.state()==ux::State::Clean);settled(s,c);
  surfaceStroke(s,c,ax-25,ay,bx+25,by);CHECK(s.selectMode()&&s.semantics().hasSelection()&&doc()["strokes"].toArray()==strokes);
  CHECK(s.action(ux::Action::Show));c.changes();CHECK(s.selectMode());CHECK(uitest::chrome(s,ib::reader::RailPen));c.changes();CHECK(!s.selectMode());CHECK(s.action(ux::Action::Hide));c.changes();settled(s,c);
  surfaceStroke(s,c,300,1500,520,1500);for(int i=0;i<30;++i){s.tick();c.changes();}settled(s,c);CHECK(doc()["strokes"].toArray().size()==strokes.size()+1);
  CHECK(s.styledInk()->undo());settled(s,c);for(int i=0;i<30;++i){s.tick();c.changes();}CHECK(doc()["strokes"].toArray()==strokes);
  CHECK(s.finish(5000));std::cout<<"PASS selection_tool_pen_selects_text_without_ink_and_handles_move_ends\n";
 }
 // After the selection tool, Ask Tom and Write a note open ready to write: the pen writes in them at once and the
 // selection tool is put away (no tool to pick first).
 {
  ux::Shell s(application,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);
  /* What the device shows where the toolbar goes (the status clock excluded: its minute may change) */
  auto chromePixels=[&]{QByteArray bytes;const auto rs=ux::regions();for(unsigned i=0;i<rs.size();++i)if(ux::ChromeRegions&(1u<<i))for(int y=rs[i].y0;y<rs[i].y1;++y)for(int x=rs[i].x0;x<rs[i].x1;++x)if(!ib::status::ReaderClock.contains(x,y))bytes.append(char(c.pixels[std::size_t(y)*underlay::Stride+x]&255));return underlay::digest(bytes);};const auto cleanChrome=chromePixels();
  auto selectFirstWord=[&]{CHECK(s.action(ux::Action::Show));c.changes();CHECK(!s.selectMode()&&uitest::chrome(s,ib::reader::RailSelect));c.changes();CHECK(s.selectMode()&&s.selectionEnabled()&&s.state()==ux::State::Chrome); /* 20u contract: the toolbar stays visible while selecting */
   auto g=s.semantics().geometry();auto words=g->data["words"].toArray();auto [ax,ay]=glyphCentre(g,words[0].toArray()[0].toInt());auto [bx,by]=glyphCentre(g,words[0].toArray()[1].toInt()-1);
   surfaceStroke(s,c,ax-25,ay,bx+25,by);for(int i=0;i<20;++i){s.tick();c.changes();}CHECK(s.semantics().hasSelection()&&s.semantics().view()==semantic::View::Menu);};
  selectFirstWord();CHECK(chromePixels()!=cleanChrome);CHECK(uitest::tap(s,s.semanticVisual().targets.rect(ib::reader::SelTom)));c.changes();CHECK(s.tomPanel()&&s.tomPanel()->active()&&!s.selectMode());
  until([&]{s.tick();c.changes();CHECK(!s.failed());return s.writable();});
  surfaceStroke(s,c,300,1600,520,1630);until([&]{s.tick();c.changes();CHECK(!s.failed());return s.tomPanel()->view().handwriting.size()==1&&!s.tomPanel()->unsaved();});
  until([&]{s.tick();c.changes();CHECK(!s.failed());return s.tomPanel()->canLeave();});CHECK(uitest::tom(s,ib::tom::Close));until([&]{s.tick();c.changes();CHECK(!s.failed());return !s.tomPanel()->active()&&s.state()==ux::State::Clean;});for(int i=0;i<10;++i){s.tick();c.changes();}CHECK(chromePixels()==cleanChrome); /* closing Tom erases the toolbar it left visible: no stale, unresponsive controls remain */
  /* New text selected for Tom starts a new conversation: the earlier question's writing stays in History and nothing of it carries over */
  {const auto earlier=s.tomPanel()->view();CHECK(earlier.handwriting.size()==1);selectFirstWord();CHECK(uitest::tap(s,s.semanticVisual().targets.rect(ib::reader::SelTom)));c.changes();CHECK(s.tomPanel()->active()&&!s.selectMode());until([&]{s.tick();c.changes();CHECK(!s.failed());const auto v=s.tomPanel()->view();return v.thread!=earlier.thread&&v.ready&&!v.busy&&QStringList{"word","sentences","paragraphs"}.contains(v.context["scope"].toString())&&s.writable();});const auto v=s.tomPanel()->view();CHECK(v.handwriting.isEmpty()&&v.answer.isEmpty()&&v.question.isEmpty()&&v.request.isEmpty());bool kept=false;for(auto t:v.threads)kept|=t.toObject()["id"]==earlier.thread;CHECK(kept);until([&]{s.tick();c.changes();CHECK(!s.failed());return s.tomPanel()->canLeave();});CHECK(uitest::tom(s,ib::tom::Close));until([&]{s.tick();c.changes();CHECK(!s.failed());return !s.tomPanel()->active()&&s.state()==ux::State::Clean;});}
  selectFirstWord();CHECK(uitest::tap(s,s.semanticVisual().targets.rect(ib::reader::SelNote)));c.changes();CHECK(s.notesPanel()&&s.notesPanel()->active()&&!s.selectMode());
  until([&]{s.tick();c.changes();CHECK(!s.failed());return s.writable();});
  {const auto r=ib::notes::NoteArea;surfaceStroke(s,c,r.x0+200,r.y0+120,r.x0+420,r.y0+150);}until([&]{s.tick();c.changes();CHECK(!s.failed());return !s.notesPanel()->unsaved()&&!s.notesPanel()->contact();});
  CHECK(s.finish(5000));std::cout<<"PASS selection_then_tom_and_notes_open_ready_to_write\n";
 }
 // Deleting a note written from a selection takes the highlight made with it: one confirmed delete, and once the note
 // page closes the page is redrawn without the highlight by a refresh that clears colour (full or regional cleanup).
 {
  ux::Shell s(application,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);
  auto doc=[&]{return s.styledInk()->snapshot()["documents"].toObject()[book.id].toObject();};
  const auto highlights=doc()["highlights"].toArray().size(),notes=doc()["notes"].toArray().size();const auto before=inkHash(s);const auto beforePixels=c.pixels;
  CHECK(s.action(ux::Action::Show));c.changes();CHECK(!s.selectMode()&&uitest::chrome(s,ib::reader::RailSelect));c.changes();CHECK(s.selectMode()&&s.selectionEnabled()&&s.state()==ux::State::Chrome); /* 20u contract: the toolbar stays visible while selecting */
  {auto g=s.semantics().geometry();auto words=g->data["words"].toArray();auto [ax,ay]=glyphCentre(g,words[1].toArray()[0].toInt());auto [bx,by]=glyphCentre(g,words[1].toArray()[1].toInt()-1);surfaceStroke(s,c,ax-25,ay,bx+25,by);}
  for(int i=0;i<20;++i){s.tick();c.changes();}CHECK(s.semantics().hasSelection());const auto removedHighlight=semantic::bounds(s.semantics().selection()->data);
  CHECK(uitest::tap(s,s.semanticVisual().targets.rect(ib::reader::SelNote)));c.changes();
  until([&]{s.tick();c.changes();CHECK(!s.failed());auto v=s.notesPanel()->view();return !v.busy&&!v.note.isEmpty()&&!v.note["highlight"].toString().isEmpty()&&!s.notesPanel()->unsaved();});
  CHECK(s.notesPanel()->view().doc["highlights"].toArray().size()==highlights+1);
  CHECK(uitest::notes(s,ib::notes::NoteDelete));c.changes();CHECK(uitest::notes(s,ib::notes::NoteDelete));c.changes(); /* the second tap confirms */
  until([&]{s.tick();c.changes();CHECK(!s.failed());auto v=s.notesPanel()->view();return !v.busy&&!s.notesPanel()->unsaved()&&v.doc["notes"].toArray().size()==notes;});
  CHECK(s.notesPanel()->view().doc["highlights"].toArray().size()==highlights);
  const auto submissions=c.submissions,fulls=c.full;CHECK(uitest::notes(s,ib::notes::HubClose));c.changes();
  until([&]{s.tick();c.changes();CHECK(!s.failed());return !s.notesPanel()->active()&&s.state()==ux::State::Clean&&s.writable();});settled(s,c);
  CHECK(doc()["highlights"].toArray().size()==highlights);CHECK(doc()["notes"].toArray().size()==notes);CHECK(inkHash(s)==before);CHECK(c.submissions>submissions&&c.full==fulls); /* UI cleanup is carried separately from ink cleanup; closing restores regionally. */for(int y=removedHighlight.y0;y<removedHighlight.y1;++y)for(int x=removedHighlight.x0;x<removedHighlight.x1;++x)CHECK(c.pixels[std::size_t(y)*underlay::Stride+x]==beforePixels[std::size_t(y)*underlay::Stride+x]);
  CHECK(s.finish(5000));std::cout<<"PASS deleting_a_selection_note_removes_its_highlight_and_redraws\n";
 }
 // A Tom failure that holds nothing unsaved closes Tom with a notice; the reading session goes on (it used to end it).
 {
  auto index=root/"tom/index.json";CHECK(std::filesystem::exists(index));
  ux::Shell s(application,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);
  CHECK(chmod(index.c_str(),0644)==0); /* the Tom store refuses to open: its runtime fails as soon as Tom opens */
  CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::menu(s,ux::MenuTom));c.changes();
  until([&]{s.tick();c.changes();CHECK(!s.failed());return !s.tomPanel();});
  CHECK(!s.failed()&&s.state()==ux::State::Clean&&s.notice().startsWith("Tom stopped"));until([&]{s.tick();c.changes();CHECK(!s.failed());return s.writable();});
  CHECK(chmod(index.c_str(),0600)==0);
  CHECK(s.action(ux::Action::Show));c.changes();CHECK(uitest::menu(s,ux::MenuTom));c.changes();until([&]{s.tick();c.changes();CHECK(!s.failed());return s.tomPanel()&&s.tomPanel()->active()&&s.writable();}); /* Tom opens again afterwards */
  until([&]{s.tick();c.changes();CHECK(!s.failed());return s.tomPanel()->canLeave();});CHECK(uitest::tom(s,ib::tom::Close));until([&]{s.tick();c.changes();CHECK(!s.failed());return !s.tomPanel()->active();});
  CHECK(s.finish(5000));std::cout<<"PASS tom_failure_with_nothing_unsaved_closes_tom_not_the_session\n";
 }
 // Pinch zoom, two-finger pan and the zoom indicator through the real shell and PDF view path.
 {
  ux::Shell s(application,root,true,false);Canvas c(s);CHECK(s.choose(0));c.ready();settled(s,c);
  auto zoomed=[&]{return s.pdfView();};reader::Touch::Tap pinch;pinch.valid=true;pinch.gesture=reader::Gesture::Pinch;pinch.scale=2.f;pinch.focalX=810;pinch.focalY=1030;
  CHECK(s.gesture(pinch));c.changes();c.ready();settled(s,c);auto v=zoomed();CHECK(std::abs(v.zoom-2.)<0.01&&!s.chromeTargets().rect(ib::reader::ZoomBadge).empty());
  reader::Touch::Tap pan;pan.valid=true;pan.gesture=reader::Gesture::TwoFingerPan;pan.deltaX=120;pan.deltaY=-80;pan.originY=900;const auto before=v.pan;
  CHECK(s.gesture(pan));c.changes();c.ready();settled(s,c);v=zoomed();CHECK(std::abs(v.zoom-2.)<0.01&&(std::abs(v.pan.x-before.x)>1||std::abs(v.pan.y-before.y)>1));
  CHECK(uitest::tap(s,s.chromeTargets().rect(ib::reader::ZoomBadge)));c.changes();c.ready();settled(s,c);v=zoomed();CHECK(std::abs(v.zoom-1.)<0.001&&s.chromeTargets().rect(ib::reader::ZoomBadge).empty());
  CHECK(s.finish(5000));std::cout<<"PASS pinch_zoom_pan_and_indicator_reset\n";
 }
 CHECK(underlay::readBounded(docroot/"document.json",8192)==descriptor&&hashFile(docroot/"source.pdf")==originalSource&&hashFile(baselinePath/"annotations.ink")==baselineJournal);CHECK(owned.remove());std::cout<<"PDF_VIEW_ROUTE_PASS cases="<<cases<<" actual_parser_render=true canonical_source_ink=true view_resume=true controls_cancel=true legacy_pins=true damaged_receipts_rejected=true own_synthetic_data_removed=true physical=NOT_RUN\n";return 0;
 }catch(const std::exception& e){std::cerr<<"PDF_VIEW_ROUTE_FAIL "<<e.what()<<'\n';return 1;}}
