// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
// Tom, the contextual reading companion (concept #4): a bottom sheet over the
// page. The passage stays visible above it and is quoted at the top of the
// sheet. The handwriting area keeps the Tom canvas geometry exactly (stored
// question ink is relative to it).
#include "ib/components.h"
#include "ib/layout.h"
#include <string>
#include <vector>
namespace ib::tom {
// A compact sheet over the lower half of the page: the book stays visible above it. Strokes are stored in
// area-relative logical coordinates, so the writing area keeps its size (1480 x 680, or 1360 tall) wherever it sits.
// Native lower sheet: header (title, subtitle, tools), the quoted passage, ruled paper, then a footer row.
inline constexpr Rect WritingArea{70,1360,1550,2040},ExpandedWritingArea{70,680,1550,2040};
inline constexpr int SheetTop=layout::SheetTop,ExpandedSheetTop=480,HeaderHeight=layout::SheetHeader,FooterTop=2048;
inline constexpr unsigned HistoryRows=4;
struct Turn {std::string question,answer;bool operator==(const Turn&)const=default;};
struct Thread {std::string title,metadata;bool operator==(const Thread&)const=default;};
struct Model {
 std::string sceneIdentity;
 std::string sourceTitle,sourceMeta,excerpt;bool hasSource{};bool sourceMissing{};
 unsigned scope{4};bool scopesAvailable{true};bool historyTurn{};
 bool composing{true};bool expanded{},lowerHalf{};bool writable{};bool picker{},eraser{};unsigned pen{1},width{1};
 std::string question,interpretation,answer,draftQuestion;bool inlineComposer{};bool streaming{},answered{};
 bool configured{},consent{};std::string provider,model;int outputLimit{};
 bool canSend{},canInterpret{},canStop{},canRetry{},confirmRetry{},canClear{},busy{},saved{};
 std::string status;bool failed{};
 unsigned answerScroll{};int zoomPercent{};
 // Turned (docs/LANDSCAPE.md, PR 3): laid out in sheet pixels as upright, shown through layout::SheetMap. The tall
 // sheets start at TurnedSheetTop (the writing and answer areas keep their place); the one line between header and
 // paper, when it no longer fits, moves into the header; the zoom badge sits in the header, left of the tools.
 bool turned{};
 // History overlay
 bool history{};std::vector<Thread> threads;std::vector<Turn> turns;unsigned threadOffset{},turnOffset{};bool confirmDelete{};
 bool outputPicker{},detailedInk{},nativeReply{},hasNativeOutput{};unsigned outputMode{};std::string illustrationModel;bool canSaveNote{};bool canExplain{}; /* a selected passage and nothing written yet: ask Tom to explain it */
 // Writing-to-Tom page: one quiet header line, the quoted passage, paper, then Tom's reply in his hand.
 std::string contextLine,disclosure;bool showAnswer{},empty{true};
 // Tom writes his reply with a pen (ib/handwriting): revealInk is how much pen travel, in pixels, has been drawn so
 // far (-1 = all of it). While the reply is still streaming only finished words are written. typed: an accessible
 // typed view of the same canonical text. handSize: 0 small, 1 medium, 2 large. revealLines is kept for the typed view.
 unsigned revealLines{~0u};bool revealing{},typed{};unsigned handSize{1};float revealInk{-1.f};bool operator==(const Model&)const=default;
};
enum Id:int {Close=1,History,More,Source,ScopeBase=10,RangeStartLess=20,RangeStartMore,RangeEndLess,RangeEndMore,
 Type=30,Interpret,Clear,Send,Stop,Retry,FollowUp,SaveNote,Consent,Expand,Half,AnswerUp,AnswerDown,
 ShowAll=50,TypedView,HandSize,Explain,InkTools,InkReply,InkTyped,InkWrite,InkAnnotated,InkIllustrated,
 InkDetail=70,HistoryBack=60,HistoryDelete,ThreadsPrev,ThreadsNext,TurnsPrev,TurnsNext,NewChat,ThreadBase=100,TurnBase=200};
// Paints the sheet into a full-screen buffer that already holds the page.
// Answer lines that fit, used by the caller to clamp scrolling.
int answerLines(const Model&);
int answerRows(const Model&); // lines that fit on the paper at once
// Pen travel of the writable reply (finished words only while streaming) and the line the pen is on at `ink`.
struct HandProgress {float total{};int line{};};
HandProgress handProgress(const Model&,float ink);
// Where answer line i (absolute) is drawn, for line-sized damage during the reveal.
Rect answerLineBox(const Model&,unsigned line);
int sheetTop(const Model&); // where the sheet starts for this model (compact, or tall for long writing)
// Where the reply zoom badge is drawn and tapped: the top chrome row above the sheet, or (turned) the sheet's header.
Rect zoomBadgeRect(const Model&);
void paint(Canvas&,Targets&,const Model&);
}
namespace ib {
// Original on-screen keyboard: letters, shift, numbers/punctuation layer.
struct KeyboardModel {std::string title,value,hint;bool shift{},symbols{};bool multiline{},paste{};};
enum KeyboardId:int {KeyBase=1000,KeyShift=1300,KeySymbols,KeyBackspace,KeySpace,KeyReturn,KeyDone,KeyCancel,KeyPaste,KeyClear};
inline constexpr Rect KeyboardRect{0,1260,1620,2160};
void keyboard(Canvas&,Targets&,const KeyboardModel&,Rect area=KeyboardRect);
// Maps a key id to its character for the current layer (0 for special keys).
char32_t keyCharacter(int id,bool shift)noexcept;
}
