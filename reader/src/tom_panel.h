// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "tom_runtime.h"
#include "tom_reply_ink.h"
#include "view_zoom.h"
#include <chrono>
#include <algorithm>
#include "semantic.h"
#include "text_input.h"
#include "ib/tom_screen.h"
namespace tom {
enum class OutputMode:unsigned {Typed,Handwritten,Annotated,Illustrated};
struct PresentationStamp {std::uint64_t ui{},ink{};};
class Panel final:public spike::OperationSink {
 friend struct PanelTestAccess;bool picker_{},outputPicker_{},detailedInk_{};OutputMode outputMode_{OutputMode::Handwritten};QString liveImageRequest_,previewRequest_,stoppedVisualRequest_,pendingVisualPrevious_;bool pendingVisualConsent_{};QString autoInkRequest_,illustrationConfirmation_,autoInkPrevious_,illustrationConsentModel_;bool autoInkPending_{};OutputMode pendingOutput_{OutputMode::Handwritten};bool prepareInk(OutputMode);
 ux::ViewZoom replyZoom_{ReplyInk::Area};mutable std::vector<std::uint32_t> zoomPixels_;spike::Rect replyCleanup_{};std::uint64_t cleanedReplyRevision_{};bool zoomedEver_{};
 Canvas canvas_;Runtime runtime_;ReplyInk reply_;View view_;bool inline_{},visualReply_{};bool nativeReply()const noexcept{return outputMode_!=OutputMode::Typed||visualReply_;}bool syncReply();std::vector<std::uint32_t> paper_,backdrop_;ux::TextInput input_;ib::Targets targets_;QString title_;QJsonObject saveRequest_;bool composing_{true};
 QJsonObject page_,sourceRequest_,selectedPassages_,nativeOutputRequest_;QJsonArray words_;unsigned first_{},last_{},scope_{4},scroll_{},historyOffset_{},conversationOffset_{};QString notice_,deleteThread_,retryRequest_;
 bool open_{},close_{},history_{},confirmDelete_{},confirmRetry_{},bound_{},commandPending_{},expanded_{},lowerHalf_{},correcting_{},freshCanvas_{},newWanted_{},selectionOpen_{};QString freshFrom_;bool userSized_{};int markedTop_{0};QString visibleRequest_; /* tall vs compact chosen by the user; the last sheet top refreshed */
 // Turned (docs/LANDSCAPE.md, PR 3): laid out for ib::layout::SheetMap (tall sheets start at TurnedSheetTop, the zoom badge
 // sits in the header). Set by the shell before each open; the reader cannot turn while a sheet is open.
 bool turned_{};
 int sheetTop()const noexcept{return ib::layout::sheetTop((inline_||(expanded_&&composing_))&&!history_?ib::tom::ExpandedSheetTop:ib::tom::SheetTop,turned_);}
 QString retryAfterConsent_;bool writingBack_{true},sendAfterConsent_{}; /* writingBack_: a fresh question is being written (else the reply shows); sendAfterConsent_: Send consents, then sends */
 int historyTurn_{-1};std::uint64_t canvasRevision_{~0ull},damageRevision_{},revealSceneEpoch_{},gestureEpoch_{};
 /* The writing hint goes as the pen touches the paper, with the first ink (before, it stayed until the stroke was saved and the
    sheet repainted): its strip is paper again in paper_ and owed to the ink's live damage until presented. */
 void clearHint()noexcept;spike::Rect hintOwed_{};
 std::array<spike::Rect,4> damage_{};unsigned damageCount_{};spike::Rect replyOwed_{};std::uint64_t replyAckRevision_{};bool reset_{},paperChanged_{true};
 void paint();bool send(Command);bool reviewConsent(bool);void updateWriting()noexcept;void context();QJsonObject frozenContext()const;bool decideNew();int beginNew();void hideOld();void mark(bool canvas=false);
 // Handwritten reveal: complete lines appear one at a time for the visible request; Show all, the typed view
 // and the size only change presentation. RevealInterval is a provisional cadence until measured on the panel.
 QString revealRequest_;unsigned revealLines_{},answerTotal_{},answerComplete_{},answerRows_{1};float revealInk_{},inkTotal_{};bool followReply_{true},showAll_{},typed_{},answerShown_{},reveal_{true};unsigned handSize_{1};
 std::chrono::steady_clock::time_point revealAt_{};std::vector<std::uint32_t> shown_; /* the paper as last marked, for changed-region damage */bool advanceReveal();void markLine(unsigned line);bool addDamage(spike::Rect);ib::tom::Model answerModel_;
public:
 OutputMode outputMode()const noexcept{return outputMode_;}void setOutputMode(OutputMode mode)noexcept{outputMode_=mode;}
 bool pickerVisible()const noexcept{return picker_;}styled::Style localStyle()const noexcept{return canvas_.style();}bool localEraser()const noexcept{return canvas_.eraser();}

 // Tom's pen: one small partial refresh per RevealInterval, at PenSpeed pixels of pen travel per second (a quick
 // hand, ~6-8 words a second at the medium size). Provisional until measured on the panel.
 inline static const QString ExplainQuestion=QStringLiteral("Explain this passage in plain words.");bool explainPending_{};void sendNow();
 static constexpr std::chrono::milliseconds RevealInterval{70};static constexpr float PenSpeed=2000.f;
 Panel(std::filesystem::path applicationPath,std::filesystem::path libraryRoot,bool networkAllowed=true,reader::lifecycle::Tracking tracking={},reader::lifecycle::Tracking previewTracking={},std::shared_ptr<tomink::IllustrationProvider> images={});~Panel();
 // Reveal adornment anchors: the history overlay's blank band, otherwise the screen top.
 static constexpr spike::Rect HistoryContent{66,ib::tom::SheetTop+ib::tom::HeaderHeight+64,1554,2140} /* the first history row: the gap above it is blank */,SheetContent{0,15,1620,2160};
 void open(QJsonObject immutablePage,semantic::Selection selected={},bool startNew=false);
 // Settings > Tom (and Accessibility > Reduce motion): how replies are presented when the panel opens. The panel's own
 // controls still change them for the session.
 void setPresentation(bool typed,unsigned handSize,bool reveal)noexcept{typed_=typed;outputMode_=typed?OutputMode::Typed:OutputMode::Handwritten;handSize_=handSize%3;reveal_=reveal;}
 void setTitle(QString title){title_=std::move(title);}
 void setTurned(bool turned)noexcept{turned_=turned;}bool turned()const noexcept{return turned_;}
 void showNotice(QString value){notice_=std::move(value);paint();mark();}
 void setBackdrop(std::span<const std::uint32_t> pixels){backdrop_.assign(pixels.begin(),pixels.end());}
 // "Save as note": {question, answer, context}; the shell creates the linked note.
 QJsonObject takeSaveRequest(){auto v=saveRequest_;saveRequest_={};return v;}
 const ib::Targets& targets()const noexcept{return targets_;}const ib::Targets& keyboardTargets()const noexcept{return input_.targets();}bool typing()const noexcept{return input_.active();}QString notice()const{return notice_;}bool closeRequested()const{return close_;}
 bool revealReady()const noexcept{return open_&&bound_&&view_.ready&&!view_.busy&&!view_.unsaved&&!view_.active&&!view_.helperBusy&&!commandPending_&&!canvas_.contact()&&!canvas_.unsaved()&&!canvas_.failed()&&!input_.active()&&(history_||historyTurn_<0)&&!close_;}
 spike::Rect revealContent()const noexcept;
 std::uint64_t revealSceneEpoch()const noexcept{return revealSceneEpoch_;}std::uint64_t gestureEpoch()const noexcept{return gestureEpoch_;}
 bool inkPending()const noexcept{return canvas_.pending()!=0||canvas_.unsaved()!=0;}
 bool active()const noexcept{return open_;}unsigned unsaved()const noexcept{return runtime_.unsaved();}bool failed()const noexcept{return canvas_.failed()||runtime_.failed();}
 bool canLeave()const noexcept{return !canvas_.contact()&&!unsaved()&&!failed()&&!view_.busy&&!commandPending_&&!input_.active();}
 void closed(){if(canLeave()){reply_.reset();open_=false;close_=false;autoInkPending_=false;sendAfterConsent_=false;retryAfterConsent_.clear();canvas_.enable(false);}}
 bool pinch(float,int,int);bool viewportPan(int,int);double zoom()const noexcept{return replyZoom_.scale();}spike::Rect takeReplyCleanup();
 bool finger(int,int);bool tick();bool pan(int,int,int,int);bool panAvailable()const noexcept{return open_&&inline_&&!history_&&!picker_&&!canvas_.contact();}bool generatedPlaying()const noexcept{return !history_&&historyTurn_<0&&reply_.playing();}bool generatedQuiet()const noexcept{return !generatedPlaying()||reply_.quiet();}spike::Rect repaintArea()const noexcept{return {0,zoomedEver_?0:std::min(markedTop_,sheetTop()),1620,2160};}std::size_t inkRoom()const noexcept{return canvas_.room();}bool contact()const noexcept{return canvas_.contact();}bool writable()const noexcept{return open_&&!close_&&!picker_&&!outputPicker_&&composing_&&!input_.active()&&!history_&&historyTurn_<0&&canvas_.writable();}
 bool reserve()noexcept override{if(!writable())return false;++gestureEpoch_;(void)reply_.tick(true);return canvas_.reserve();}bool append(const spike::Point& p)noexcept override{if(p.phase==spike::Phase::Down)clearHint();return canvas_.append(p);}void cancel()noexcept override{canvas_.cancel();}
 spike::Surface ink()noexcept{return canvas_.surface();}std::span<const std::uint32_t> background()const noexcept{return paper_;}
 void composite(spike::Surface,spike::Rect)const noexcept;
 // Non-consuming: only presentationCovered after accepted submit retires matching generations.
 bool takeReset()const{return reset_;}unsigned takeDamage(std::array<spike::Rect,4>& out)const{out=damage_;return damageCount_;}
 spike::Rect inkDamage()const noexcept{auto r=canvas_.damage();if(!hintOwed_.empty()){if(r.empty())r=hintOwed_;else r.include(hintOwed_);}return r;}PresentationStamp presentationStamp()const noexcept{return {damageRevision_,canvas_.damageGeneration()};}
 void presentationCovered(spike::Rect accepted,PresentationStamp)noexcept;
 bool renderSettled()const noexcept{return !canvas_.contact()&&!canvas_.pending()&&canvasRevision_==view_.canvasRevision;}
 bool drained()const noexcept{return runtime_.drained()&&reply_.drained();}
 bool finish(unsigned ms=100)noexcept;bool retire(unsigned ms=2000)noexcept;View view()const{return view_;}QJsonObject source()const;
 QJsonObject takeNativeOutputRequest(){auto value=nativeOutputRequest_;nativeOutputRequest_={};return value;}
 QJsonObject takeSourceRequest(){auto value=sourceRequest_;sourceRequest_={};return value;}void sourceRejected(){notice_="Source unavailable, changed, or still saving";paint();mark();}
};
}
