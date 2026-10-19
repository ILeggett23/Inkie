#pragma once
#include "chrome.h"
#include "gesture_policy.h"
#include <linux/input.h>
namespace reader {
// reMarkable-style finger gestures, recognised when every finger has lifted: a fast horizontal swipe turns the page,
// a still two-finger tap undoes and a three-finger tap redoes, a swipe down from the top edge opens recents, and a
// swipe up from the bottom edge opens quick browse; two fingers that spread or close pinch-zoom, and two fingers
// that move together pan (or, from the top edge, open the recent drawer).
enum class Gesture {None,SwipeNext,SwipePrevious,Undo,Redo,EdgeDown,EdgeUp,Pinch,TwoFingerPan,VerticalScroll};
enum class DragMode {None,Text,Highlight,Ink,Scroll};
class Touch {
 public: struct Tap {bool valid{};int x{},y{};bool longPress{},cancelled{},drag{};int startX{},startY{};std::uint64_t epoch{};Gesture gesture{Gesture::None};
  unsigned contacts{};spike::Ns startedAt{},eligibleAt{},readAt{},classifiedAt{};float scale{1};int focalX{},focalY{},deltaX{},deltaY{},originY{};DragMode mode{DragMode::None};}; /* pinch: scale about the focal point; pan: centroid delta from originY */
 private: Tap lastTap_{};friend struct TouchTestAccess;
 std::array<input_event,64> staged_{};std::size_t stagedAt_{},stagedCount_{};bool cancellationSent_{},caughtUp_{};
 void cancelLongPress()noexcept;bool snapshotSlots(bool recover)noexcept;
 std::array<int,16> tools_{};std::array<bool,16> knownX_{},knownY_{};std::array<spike::Rect,2> handles_{};
 bool lost_{},nonFingerChanged_{},handleDrag_{},penReleased_{};int slotCount_{16};spike::Ns penCutoff_{},lastRead_{},lastFrame_{};
public:struct Counters {std::uint64_t readEvents{},consumedEvents{},frames{},outputs{},cancellations{},losses{},palmFrames{},rejectedPen{},rejectedInvalid{},rejectedMulti{},rejectedOther{};};
private:Counters counters_{};
 int fd_{-1};int slot_{};std::array<int,16> ids_{};spike::Axis x_{},y_{};
 spike::Rect toolbar_{};bool toolbarStart_{};unsigned turn_{};bool statusCorner_{},gesture_{},blocked_{},bad_{};int startX_{},startY_{},lastX_{},lastY_{};spike::Ns start_{};
 DragMode mode_{DragMode::None},gestureMode_{DragMode::None};bool intentional_{},motionStarted_{};bool longFired_{},longCancelled_{},dragEnabled_{},gestureDrag_{},moved_{},penBlocked_{};unsigned maxTouches_{};int travelX_{},travelY_{};std::array<int,16> fingerX_{},fingerY_{},originX_{},originY_{};std::uint64_t epoch_{},gestureEpoch_{};std::uint32_t usedSlots_{};spike::Ns multiAt_{}; /* when a second contact joined the gesture */
 struct Pair {float d0{},d1{},cx0{},cy0{},cx1{},cy1{},ax{},ay{},bx{},by{};}; /* a*, b*: each finger's own motion */bool pair(Pair&)const noexcept; // the two fingers of this gesture: spread and centroid, landing vs lift
public:
 Touch(){ids_.fill(-1);originX_.fill(-1);originY_.fill(-1);}~Touch();bool open(bool permitted)noexcept;
 Action feed(const input_event&,bool markerBusy,spike::Ns now)noexcept;
 Action poll(bool markerBusy)noexcept;
 Tap takeTap()noexcept{auto t=lastTap_;lastTap_={};return t;}
 Tap pollTap(bool markerBusy)noexcept{lastTap_={};(void)poll(markerBusy);if(caughtUp_&&!lastTap_.valid)expire(spike::monotonic(),markerBusy);if(lastTap_.valid){lastTap_.contacts=maxTouches_;if(!lastTap_.drag&&lastTap_.gesture==Gesture::None){lastTap_.startX=startX_;lastTap_.startY=startY_;}lastTap_.startedAt=start_;lastTap_.eligibleAt=lastTap_.longPress?start_+semantic::LongPressNs:lastFrame_;lastTap_.readAt=lastRead_;lastTap_.classifiedAt=spike::monotonic();++counters_.outputs;if(lastTap_.cancelled)++counters_.cancellations;}return takeTap();}
 void toolbar(spike::Rect r)noexcept{toolbar_=r;}
 // The screen's turn in degrees (docs/LANDSCAPE.md): 0, 90 (top edge turned clockwise) or 270. Gestures are recognised
 // in the reader's upright frame; tap coordinates stay in panel pixels.
 void turn(unsigned degrees)noexcept{turn_=degrees==90||degrees==270?degrees:0;}
 void statusCorner(bool enabled)noexcept{statusCorner_=enabled;}
 void interactionMode(DragMode mode,std::uint64_t epoch)noexcept{const bool promotion=gesture_&&longFired_&&(mode_==DragMode::None||mode_==DragMode::Highlight)&&mode==DragMode::Text&&epoch==epoch_;if(gesture_&&!promotion&&(mode!=mode_||epoch!=epoch_))blocked_=bad_=true;if(promotion){gestureMode_=mode;gestureDrag_=intentional_=true;}mode_=mode;dragEnabled_=mode!=DragMode::None;epoch_=epoch;}
 void selectionMode(bool enabled,std::uint64_t epoch)noexcept{interactionMode(enabled?DragMode::Text:DragMode::None,epoch);}
 void expire(spike::Ns now,bool busy=false)noexcept;
 Gesture recognise(spike::Ns now,bool busy)const noexcept;
 void block()noexcept{blocked_=true;penBlocked_=true;penReleased_=false;}
 void penReleased(spike::Ns cutoff)noexcept{if(cutoff){penReleased_=true;penCutoff_=cutoff;}}
 void selectionHandles(std::array<spike::Rect,2> handles)noexcept{handles_=handles;}
 Counters counters()const noexcept{return counters_;}
 void quiesce()noexcept{(void)poll(true);bool live=false;for(int id:ids_)live|=id>=0;if(!live)blocked_=penBlocked_=false;}
 void testAxes(spike::Axis x,spike::Axis y){x_=x;y_=y;}
};
}
