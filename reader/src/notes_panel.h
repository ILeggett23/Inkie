// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <algorithm>
#include "notes_runtime.h"
#include "layer_panel.h"
#include "text_input.h"
#include "semantic.h"
#include "ib/notes_screen.h"
#include "note_favorites.h"
#include <deque>
namespace notes {
enum class Mode {Hub,Note,Layers,Highlight};
// Presentation ownership outlives transient worker snapshots. A busy empty
// publication must not erase the fact that this Note view displayed a note.
// A new, still-empty Note creation has no prior owner and must remain open.
class NotePresence {
 bool seen_{};
public:
 void reset()noexcept{seen_=false;}
 bool removed(Mode mode,const View& fresh)noexcept{
  if(mode!=Mode::Note){reset();return false;}
  if(!fresh.ready||!fresh.error.isEmpty())return false;
  if(!fresh.selected.isEmpty()&&!fresh.note.isEmpty()){seen_=true;return false;}
  if(seen_&&!fresh.busy&&fresh.selected.isEmpty()&&fresh.note.isEmpty()){reset();return true;}
  return false;
 }
};
class Panel final:public spike::OperationSink {
 NotePresence notePresence_;
 Canvas canvas_;Runtime runtime_;bool selectInk_{},inkActions_{},eraser_{};struct InkGesture {std::array<styled::Coordinate,128> path{};unsigned count{};bool reserved{},started{},ready{},ignored{},overflow{},move{},resize{};styled::Coordinate first{},last{};QRectF bounds;};InkGesture inkGesture_;bool inkGestureTick();bool selectInkAt(int,int);bool editInk(QJsonArray matrix={},QString color={},bool remove=false);
 tomink::StrokePlayback playback_;std::shared_ptr<const tomink::PlaybackPlan> retiredPlayback_;bool finishingPlayback_{},finishCommandSent_{},waitingBinding_{},bindingRejected_{};QString requestedNote_,playingGroup_,finishedGroup_,finishedNote_;QJsonObject finishedBinding_;bool canvasChanged_{},canvasContentChanged_{};tomink::PlaybackSpeed playbackSpeed_{tomink::PlaybackSpeed::Normal};bool playbackTick();void finishPlayback();View view_;Layers layers_;ux::TextInput keyboard_;std::vector<std::uint32_t> paper_,backdrop_;ib::Targets targets_;std::deque<QJsonObject> followUps_;ib::notes::Filter filter_{ib::notes::Filter::All};int sheetTop_{},markedTop_{};bool highlightPending_{};QString title_;
 QJsonObject page_,selection_,source_,keyboardCommand_,tomRequest_,lastGeneration_;bool confirmInkRetry_{};int sourceTarget_{-1};Mode mode_{Mode::Hub};styled::Style style_{};unsigned color_{4},hubOffset_{},textOffset_{};QString notice_;bool open_{},close_{},reset_{},pendingCommand_{},closeAfterSave_{},confirmDelete_{},sourceColor_{},textView_{};
 std::uint64_t canvasRevision_{~0ull},contentRevision_{},revealSceneEpoch_{};bool documentChanged_{};bool closeDeferred_{};bool picker_{}; /* the note page's pen picker (tool and width; notes write in dark ink) */ux::NoteFavorites* favorites_{};
 bool starred(const QJsonObject& note)const{return favorites_&&favorites_->contains(note["id"].toString());}
 bool keep(const QJsonObject& note)const;
 std::array<spike::Rect,5> damage_{};unsigned damageCount_{};std::vector<std::uint32_t> shown_; /* the paper as last marked, for changed-region damage */
 void paint();void mark();bool advanceClose();void requestSource(QJsonObject);bool send(QJsonObject);void keyboard(QString,QString,QJsonObject,unsigned=256);
public:
 bool selectionEnabled()const noexcept{return open_&&selectInk_&&mode_==Mode::Note&&!textView_&&!keyboard_.active()&&!pendingCommand_&&!view_.busy&&!playback_.active()&&!finishingPlayback_&&view_.editable;}bool selectionDrag(int,int,int,int);
 QJsonObject takeTomRequest(){auto v=tomRequest_;tomRequest_={};return v;}
 void prepareSleep()noexcept{runtime_.cancelGeneration();followUps_.erase(std::remove_if(followUps_.begin(),followUps_.end(),[](const QJsonObject& c){return c["op"]=="native.generate";}),followUps_.end());if(playback_.active())finishPlayback();}
 bool localEraser()const noexcept{return eraser_;}
 bool pickerVisible()const noexcept{return picker_;}styled::Style localStyle()const noexcept{return style_;}

 // Construct with product::featureDirectory(libraryRoot, document).
 explicit Panel(std::filesystem::path,reader::lifecycle::Tracking tracking={},std::filesystem::path application={});~Panel();
 bool open(Mode,QJsonObject immutablePage,semantic::Selection,styled::Style,QString note={},unsigned initialHighlightColor=4,QJsonObject expectedNative={});
 bool highlight(QJsonObject immutablePage,semantic::Selection,unsigned color,bool existing);
 // Page pixels shown around the sheets (the document stays visible).
 void setBackdrop(std::span<const std::uint32_t> pixels){backdrop_.assign(pixels.begin(),pixels.end());}
 void setTitle(QString title){title_=std::move(title);}
 // Stars on notes (hub rows, Favorites filter, the note sheet). Owned by the shell; null hides stars.
 void setFavorites(ux::NoteFavorites* favorites){favorites_=favorites;}
 // After the note exists: set its title and typed text (e.g. saved from Tom).
 void queueInk(QString title,QJsonObject generation){if(!title.isEmpty())followUps_.push_back({{"op","note.title"},{"title",title.left(256)}});generation["op"]="native.generate";generation["generation"]=product::newId();generation["binding"]=page_;followUps_.push_back(std::move(generation));}
 void queueText(QString title,QString text){if(!title.isEmpty())followUps_.push_back({{"op","note.title"},{"title",title.left(256)}});if(!text.isEmpty())followUps_.push_back({{"op","note.text"},{"text",text.left(8192)}});}
 const ib::Targets& targets()const noexcept{return targets_;}const ib::Targets& keyboardTargets()const noexcept{return keyboard_.targets();}Mode mode()const noexcept{return mode_;}
 void showNotice(QString value){notice_=std::move(value);paint();mark();}
 bool finger(int,int);bool tick();bool closeRequested()const{return close_;}void closed(){open_=close_=false;notePresence_.reset();canvas_.enable(false);}
 bool readingText()const noexcept{return textView_;}unsigned textOffset()const noexcept{return textOffset_;}
 bool revealReady()const noexcept{return open_&&view_.ready&&!view_.busy&&!pendingCommand_&&!contact()&&!canvas_.unsaved()&&!canvas_.failed()&&!keyboard_.active()&&!close_;}
 std::uint64_t revealSceneEpoch()const noexcept{return revealSceneEpoch_;}
 bool active()const noexcept{return open_;}unsigned unsaved()const noexcept{return runtime_.unsaved();}bool failed()const noexcept{return runtime_.failed();}
 bool canLeave()const noexcept{return !playback_.active()&&!finishingPlayback_&&!contact()&&!unsaved()&&!view_.busy&&!pendingCommand_&&!failed();}
 bool takeCanvasContentChanged()noexcept{bool changed=canvasContentChanged_;canvasContentChanged_=false;return changed;}bool renderSettled()const noexcept{return !waitingBinding_&&!bindingRejected_&&!pendingCommand_&&!view_.busy&&!finishingPlayback_&&!contact()&&!canvas_.pending()&&view_.error.isEmpty();}
 bool takeCanvasChanged()noexcept{bool changed=canvasChanged_;canvasChanged_=false;return changed;}bool generatedPlaying()const noexcept{return playback_.active();}bool generatedQuiet()const noexcept{return !playback_.active()||playback_.quietForCleanup();}
 std::size_t inkRoom()const noexcept{return inkGesture_.reserved?65536:canvas_.room();}bool contact()const noexcept{return inkGesture_.reserved||canvas_.contact();}bool writable()const noexcept{return open_&&!finishingPlayback_&&mode_==Mode::Note&&!textView_&&!keyboard_.active()&&!pendingCommand_&&!closeDeferred_&&!closeAfterSave_&&canvas_.writable();}
 bool reserve()noexcept override;bool append(const spike::Point&)noexcept override;void cancel()noexcept override{inkGesture_={};canvas_.cancel();}
 spike::Surface ink()noexcept{return canvas_.surface();}std::span<const std::uint32_t> background()const noexcept{return paper_;}
 void composite(spike::Surface,spike::Rect)const noexcept;spike::Rect presentationDamage(spike::Rect r)const noexcept{r.include(canvas_.damage());return r;}void presentationAccepted()noexcept{canvas_.clearDamage();}void presentationCovered(spike::Rect accepted)noexcept{canvas_.presentationCovered(accepted);}
 spike::Rect repaintArea()const noexcept{return {0,std::clamp(sheetTop_-8,0,2160),1620,2160};}
 bool takeReset(){bool reset=reset_;reset_=false;return reset;}unsigned takeDamage(std::array<spike::Rect,5>& out){out=damage_;auto n=damageCount_;damageCount_=0;return n;}bool takeDocumentChanged(){bool v=documentChanged_;documentChanged_=false;return v;}
 QJsonObject sourceRequest()const{return source_;}int sourceTarget()const noexcept{return sourceTarget_;}void sourceHandled(bool ok){source_={};if(ok)close_=true;else{notice_="Source unavailable, changed, or still saving";paint();mark();}}
 bool drained()const noexcept{return runtime_.drained();}
 bool finish(unsigned ms=100)noexcept;bool retire(unsigned ms=0)noexcept;View view()const{auto current=view_;current.window.style=style_;if(pendingCommand_){current.busy=true;current.saved=false;current.status="UPDATING NOTE";}return current;}
};
}
