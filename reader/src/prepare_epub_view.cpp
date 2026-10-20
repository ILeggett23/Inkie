#include <chrono>
#include <cstdio>
#include "source_file.h"
#include "prepare_epub_view.h"
#include "turn_fit.h"
#include "library.h"
#include "semantic.h"
#include "chrome.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cstdio>
#include <memory>
#include <optional>
namespace reader {namespace {
void need(bool b,const char* why){if(!b)throw std::runtime_error(why);}
epub::Layout parameters(const QJsonObject& layout){return {layout["width"].toInt(),layout["height"].toInt(),layout["fontSize"].toInt(),layout["userCss"].toString()};}
void verify(const epub::ReflowDocument& doc,const epubviews::ViewSet& view){need(doc.sourceSha256()==view.receipt["source_sha"]&&doc.pageCount()==view.receipt["pages"].toInt()&&doc.layoutDescriptor()==view.receipt["layout"].toObject(),"epub-view-actual-layout-mismatch");if(view.id.isEmpty()){const auto l=doc.layoutDescriptor();need(l["width"]==600&&l["height"]==800&&l["fontSize"]==12,"epub-view-baseline-parameters");}else need(doc.layoutId()==view.receipt["layout_id"],"epub-view-actual-layout-identity");}
// Page-server helper: a verified source is not re-hashed while its file identity is unchanged, and the laid-out
// view document is kept for the next page of the same view (see underlay::prepare). One-shot helpers are unchanged.
struct FileIdentity {dev_t dev{};ino_t ino{};off_t size{};long long ms{},mn{},cs{},cn{};bool operator==(const FileIdentity&)const=default;};
bool fileIdentity(const std::filesystem::path& p,FileIdentity& i){struct stat st{};if(::stat(p.c_str(),&st)||!S_ISREG(st.st_mode))return false;i={st.st_dev,st.st_ino,st.st_size,st.st_mtim.tv_sec,st.st_mtim.tv_nsec,st.st_ctim.tv_sec,st.st_ctim.tv_nsec};return true;}
struct Verified {std::filesystem::path path;QString sha;FileIdentity id;};std::optional<Verified> verified;
struct OpenView {std::filesystem::path path;QString document,sha;QJsonObject layout;FileIdentity id;std::unique_ptr<epub::ReflowDocument> doc;};std::unique_ptr<OpenView> openView;
void verifySourceFully(const std::filesystem::path& source,const QString& expected);
void verifySource(const std::filesystem::path& source,const QString& expected){FileIdentity id{};const bool known=fileIdentity(source,id);
 if(known&&verified&&verified->path==source&&verified->sha==expected&&verified->id==id)return;verified.reset();verifySourceFully(source,expected);FileIdentity after{};if(known&&fileIdentity(source,after)&&after==id)verified=Verified{source,expected,id};}
epub::ReflowDocument& reflow(const std::filesystem::path& source,const QString& document,const QString& sha,const QJsonObject& layout,const epubviews::ViewSet& view){FileIdentity id{};const bool known=fileIdentity(source,id);
 if(!(openView&&known&&openView->path==source&&openView->document==document&&openView->sha==sha&&openView->layout==layout&&openView->id==id)){openView.reset();auto doc=std::make_unique<epub::ReflowDocument>(source,document,sha,parameters(layout));verify(*doc,view);FileIdentity after{};need(known&&fileIdentity(source,after)&&after==id,"epub-view-source-changed");openView=std::make_unique<OpenView>(OpenView{source,document,sha,layout,id,std::move(doc)});}
 else verify(*openView->doc,view);return *openView->doc;}
void verifySourceFully(const std::filesystem::path& source,const QString& expected){auto checked=reader::source::hashFile(source,library::MaxSourceBytes);need(checked.identity.size>0&&checked.digest.toHex()==expected.toLatin1(),"epub-view-source-changed");}
semantic::Geometry geometry(epub::ReflowDocument& doc,int page,const underlay::Prepared& prepared){auto native=doc.page(page);auto chars=native["chars"].toArray();QJsonArray words;auto lexical=[&](int i){auto c=chars[i].toObject();uint u=uint(c["u"].toInt());if(QChar::isLetterOrNumber(u)||QChar::isMark(u))return true;if((u=='\''||u==0x2019)&&i>0&&i+1<chars.size())return QChar::isLetter(uint(chars[i-1].toObject()["u"].toInt()))&&QChar::isLetter(uint(chars[i+1].toObject()["u"].toInt()));return false;};for(int i=0;i<chars.size();){if(!lexical(i)){++i;continue;}int start=i,line=chars[i].toObject()["line"].toInt();while(i<chars.size()&&chars[i].toObject()["line"].toInt()==line&&lexical(i))++i;words.append(QJsonArray{start,i});need(words.size()<=semantic::MaxWords,"epub-view-word-capacity");}
 const auto& m=prepared.metadata;QJsonObject out{{"version",1},{"document",m["document"]},{"pageId",QString::fromLatin1(prepared.binding.toHex())},{"page",page},{"status",chars.isEmpty()?"NO TEXT LAYER":"READY"},{"transform",m["pageToSurface"]},{"transformVersion",m["render"]},{"viewId",m["view_id"]},{"visibleSourceRect",m["crop"]},{"layoutId",doc.layoutId()},{"sourceSha256",doc.sourceSha256()},{"publicationLocation",QJsonObject{{"chapter",native["chapter"]},{"chapterPage",native["chapterPage"]},{"spineItem",native["spineItem"]}}},{"text",native["text"]},{"chars",chars},{"lines",native["lines"]},{"blocks",native["blocks"]},{"words",words}};
 out["transformId"]=QString::fromLatin1(underlay::digest(QJsonDocument(QJsonObject{{"matrix",out["transform"]},{"version",out["transformVersion"]}}).toJson(QJsonDocument::Compact)).toHex());return semantic::decodeGeometry(out,prepared.binding);
}
}
StagedEpubView stageEpubView(const std::filesystem::path& application,const std::filesystem::path& root,const QString& current,epub::Layout requested,int currentPage){(void)application;const auto started=std::chrono::steady_clock::now();auto phase=[&](const char* name){std::fprintf(stderr,"EPUB_STAGE_TIMING phase=%s ms=%lld\n",name,(long long)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started).count());}; /* where a turn or layout change spends its time (journal) */auto old=epubviews::readSet(root,current);auto descriptor=library::descriptor(root);auto source=library::sourcePath(root,descriptor);auto id=old.receipt["document"].toString(),sha=old.receipt["source_sha"].toString();QJsonObject anchor;
 epub::ReflowDocument::ChapterFraction where{};
 {epub::ReflowDocument previous(source,id,sha,parameters(old.receipt["layout"].toObject()));verify(previous,old);where=previous.chapterFraction(currentPage);auto captured=previous.captureReadingPosition(currentPage);if(captured["status"]!="anchored"&&currentPage==0)captured=previous.captureSpineStart(currentPage);if(captured["status"]=="anchored")anchor=captured["anchor"].toObject();else std::fprintf(stderr,"EPUB_STAGE_POSITION_APPROXIMATE capture=%s\n",captured["reason"].toString().toUtf8().constData());}
 epub::ReflowDocument next(source,id,sha,requested);int target=-1;
 // The exact word is found when the new layout extracts the same chapter text; otherwise (hyphenation and line breaks
 // differ between sizes) the same share of the same chapter is used, so a layout change is never refused for this.
 if(!anchor.isEmpty()){auto resolved=next.resolve(anchor);if(resolved["status"]=="resolved"&&resolved["page"].isDouble())target=resolved["page"].toInt(-1);else std::fprintf(stderr,"EPUB_STAGE_POSITION_APPROXIMATE resolve=%s\n",resolved["reason"].toString().toUtf8().constData());}
 if(target<0)target=next.pageAtFraction(where.chapter,where.fraction);need(target>=0&&target<next.pageCount(),"epub-view-reading-target");phase("layouts");
 // Reject an unrenderable fit before leaving behind even an inactive receipt. The book's own layout (turning back upright)
 // is always renderable: it is what the book opens in.
 if(next.layoutId()!=old.receipt["base_layout"].toString())(void)next.renderPage(target);phase("render");verifySource(source,sha);auto view=epubviews::stage(root,next.layoutDescriptor(),next.layoutId(),next.pageCount());phase("store");return {view,target,anchor};
}
underlay::Prepared prepareEpubView(const std::filesystem::path& application,const std::filesystem::path& root,int page,const epubviews::ViewSet& immutable,unsigned orientation){auto checked=epubviews::readSet(root,immutable.id);need(checked.receipt==immutable.receipt,"epub-view-receipt-changed");need(orientation==0||((orientation==90||orientation==270)&&!checked.id.isEmpty()),"epub-view-turn");auto storage=epubviews::turnedStoragePath(root,checked,page,orientation);if(checked.id.isEmpty())return underlay::prepare(application,storage,page,true,root);auto descriptor=library::descriptor(root);auto source=library::sourcePath(root,descriptor);auto expected=checked.receipt["source_sha"].toString();auto& doc=reflow(source,checked.receipt["document"].toString(),expected,checked.receipt["layout"].toObject(),checked);auto raster=orientation?doc.renderPage(page,orientation):doc.renderPage(page);const auto port=pdfview::viewport(orientation);need(raster.width==port.width&&raster.height==port.height&&raster.stride==port.width&&raster.rgb32.size()==std::size_t(port.width)*port.height,"epub-view-raster-contract");underlay::Prepared prepared;prepared.background.assign(underlay::Pixels,0xffffffffu);prepared.ink.assign(underlay::Pixels,0xffffffffu);for(int y=0;y<raster.height;++y)for(int x=0;x<raster.width;++x){auto color=raster.rgb32[std::size_t(y)*raster.stride+x];need(color>>24==255,"epub-view-raster-alpha");prepared.background[std::size_t(y+port.y)*underlay::Stride+x+port.x]=color;} /* turned: the raster is the landscape viewport, at its place on the panel */
 int total=doc.pageCount();prepared.metadata={{"schema",4},{"generation",1},{"page",page},{"pages",total},{"pdf",expected},{"document",checked.receipt["document"]},{"documentFormat","epub"},{"layout",checked.receipt["layout_id"]},{"width",underlay::Width},{"height",underlay::Height},{"stride",underlay::Stride*4},{"format","RGB32-LE"},{"raster","qualified-circle-bresenham-v1"},{"render","reader-epub-view-mupdf1.28.3-rgb32-v1"},{"view_id",checked.id},{"view_receipt",checked.receipt},{"rotation",0},{"bounds",raster.bounds},{"crop",raster.bounds},{"pageToSurface",raster.forward},{"surfaceToPage",raster.inverse}};
 if(orientation)prepared.metadata["orientation"]=int(orientation);else chrome({prepared.background,underlay::Width,underlay::Height,underlay::Stride},page,total); /* turned: the shell paints the landscape footer */prepared.metadata["backgroundSha256"]=QString::fromLatin1(underlay::digest(QByteArray(reinterpret_cast<const char*>(prepared.background.data()),underlay::Pixels*4)).toHex());prepared.binding=underlay::digest(QJsonDocument(prepared.metadata).toJson(QJsonDocument::Compact));prepared.text=geometry(doc,page,prepared);verifySource(source,expected);underlay::privateDirectory(storage);underlay::Journal journal(storage/"annotations.ink",prepared.binding);journal.replay({prepared.ink,underlay::Width,underlay::Height,underlay::Stride});prepared.lastId=journal.lastId();prepared.journalDigest=journal.fingerprint();semantic::saveGeometry(storage/"semantic-text.json",prepared.text);underlay::savePrepared(storage/"prepared.bin",prepared);return prepared;
}
}
