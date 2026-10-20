#pragma once
#include "ux_shell.h"
#include "panel_damage.h"
#include "touch.h"
#include "refresh_plan.h"
#include "refresh_scheduler.h"
#include "lifecycle_v2.h"
#include "session_power_v2.h"
#include "ib/sleep_screen.h"
#include "e2_provisional_v2.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <chrono>
#include <thread>
namespace ux {
class Display final:public spike::SessionDisplay,public spike::OperationSink {
 friend struct DisplayTestAccess;spike::VendorDisplay& vendor_;Shell& shell_;reader::Touch touch_;bool drainTouch_{};const spike::RecoveryLease* lease_{};
 // V2 only: the sole main progress producer. Null retains the V1 route unchanged.
 reader::lifecycle::Foreground* foreground_{};recovery::v2::e2::Cadence cadence_;
 // V2 only: session-scoped wake lock and power-key/cover/idle session end. Null on V1.
 recovery::v2::sessionpower::SessionPower* sessionPower_{};bool powerEnd_{};
 // V2 start-up window: the vendor display start and first full page block the main thread for
 // seconds before the first pump (no input is read yet). Only then, and for at most StartupBudget in
 // total, a helper is the SOLE main-progress publisher; it is joined before the main thread resumes.
 // Past the budget nothing publishes, so a hung start-up still ends the session.
 // 20 s: the Paper Pro measured 9.23 s vendor start + 0.85 s first page (E2 phase E run 3).
 static constexpr spike::Ns StartupBudget=20000000000ull;spike::Ns startupBudget_{StartupBudget},startupUsed_{};bool pumped_{};
 // D2 long strokes: a stroke never ends the session because a saver piece is full. Before a piece
 // reaches its bound it is closed (Up) at the current point and the stroke continues in a new piece
 // from that same point. Only if no new piece can be opened is the rest of that one contact
 // left unsaved (drawn, logged); the session always continues.
 bool detached_{};std::uint64_t splits_{},tailPoints_{};
 // D2 eraser: when the eraser lifts, its area gets one regional quality cleanup (liftCleanup_, as stock does). Optical removal of ghosting still requires device qualification. (A ring cursor drawn with the fast pen waveform left
 // ghost trails on the colour panel, so the live feedback is the erased band itself.)
 static constexpr int EraserRing=22;
 bool pageTurnPending_{};std::uint64_t installedVersion_{};RefreshPlan uiCleanup_;RefreshController refresh_;std::array<std::uint32_t,1620*64> sceneRows_;bool penContact_{},erasing_{},eraseSubmission_{};int ringX_{-1},ringY_{-1};spike::Rect eraseArea_{},cleanup_{},liftCleanup_{},strokeArea_{};RefreshPlan inkPlan_,removal_;bool colorStroke_{};
 // Keep the removed material through both the immediate lift cleanup and the
 // final saved/rendered erase result. Paying a grid cell at lift is not a panel
 // completion fence and must not downgrade the later cleanup to white/Mono.
 spike::Rect erasedBounds_{};int erasedClass_{-1};spike::Ns eraseLiftAt_{};
 // TEST BUILD (user request 2026-10-20): once an erase has settled (pen up, saved, its removal cleanups sent) and
 // GhostDelay has passed since the last of them, the panel driver's own ghost clearing runs once (VendorDisplay::
 // clearGhosting, Xochitl's GhostBuster call). The user checks whether it removes the yellow left by the fast erase
 // and whether it flashes; `ui:ghost-clear` journals each run (1 ran, 0 unavailable).
 static constexpr spike::Ns GhostDelay=1000000000ull;spike::Ns lastRemovalAt_{},ghostAt_{};
 // What the Gallery 3 panel still holds. A fast (pen-mode) update, or one whose class came only from its new pixels,
 // leaves the pigment of what it replaced: colour ink erased to white, a coloured sheet closed over a page. A later
 // quality update of that area must drive the richest class the area showed, or the residue stays until a complete
 // refresh. One byte per 60 px cell (27 x 36): -1 nothing owed, else the vendor content class (Mono 0, Grayscale 1,
 // Color 3) still owed there. Cells are cleared only by a quality update that covers them whole.
 static constexpr int Cell=60,CellColumns=27,CellRows=36;
 std::array<std::int8_t,CellColumns*CellRows> owed_=[]{std::array<std::int8_t,CellColumns*CellRows> a{};a.fill(-1);return a;}();
 template<class F> void cells(spike::Rect r,F&& f)noexcept{if(r.empty())return;const int c0=std::clamp(r.x0/Cell,0,CellColumns-1),c1=std::clamp((r.x1-1)/Cell,0,CellColumns-1),r0=std::clamp(r.y0/Cell,0,CellRows-1),r1=std::clamp((r.y1-1)/Cell,0,CellRows-1);for(int y=r0;y<=r1;++y)for(int x=c0;x<=c1;++x)f(owed_[std::size_t(y)*CellColumns+x],spike::Rect{x*Cell,y*Cell,std::min(1620,(x+1)*Cell),std::min(2160,(y+1)*Cell)});}
 void owe(spike::Rect r,int content)noexcept{if(content<0)return;cells(r,[&](std::int8_t& c,spike::Rect){c=std::int8_t(std::max<int>(c,content));});}
 int owedIn(spike::Rect r)noexcept{int most=-1;cells(r,[&](std::int8_t& c,spike::Rect){most=std::max<int>(most,c);});return most;}
 void paid(spike::Rect r)noexcept{cells(r,[&](std::int8_t& c,spike::Rect cell){if(r.x0<=cell.x0&&r.y0<=cell.y0&&r.x1>=cell.x1&&r.y1>=cell.y1)c=-1;});}
 // The class a quality update of r must keep: what r shows now (read before compose) or still owes. cleanRemoval()
 // takes the larger of this and the new pixels' class, so a removal is never driven with a poorer waveform class.
 int prior(spike::Rect r)noexcept{int content=std::max(owedIn(r),vendor_.removalContent(r));
  if(erasedClass_>=0&&std::max(r.x0,erasedBounds_.x0)<std::min(r.x1,erasedBounds_.x1)&&std::max(r.y0,erasedBounds_.y0)<std::min(r.y1,erasedBounds_.y1))content=std::max(content,erasedClass_);
  return content;}
 static spike::Rect around(int x,int y,int r)noexcept{return {std::max(0,x-r-3),std::max(0,y-r-3),std::min(1620,x+r+4),std::min(2160,y+r+4)};}
 static void grow(spike::Rect& a,spike::Rect b)noexcept{if(b.empty())return;if(a.empty())a=b;else a.include(b);}
 static void join(RefreshPlan& p,spike::Rect r)noexcept{if(r.empty()||p.add(r))return;spike::Rect joined=r;for(unsigned n=0;n<p.count;++n)joined.include(p.regions[n]);p={};(void)p.add(joined);}
 // D2: the vendor refuses one fast (pen-mode) update larger than a quarter page. Live ink joined with
 // the re-presented area of erased strokes can exceed that (E2 session 16 ended mid-erase), so a larger
 // area is sent as consecutive full-width bands, each within the bound. Never a reason to end the session.
 // Only pen-mode work is banded: quality (UI) updates have no such bound, and banding them made one screen
 // change finish in visible stages (a closing drawer's lower part stayed up for a second after its upper part).
 // keep >= 0: the class of what a quality update removes (prior()); cleanRemoval keeps the richer of it and the new pixels.
 static constexpr int LiveArea=1620*2160/4;
 spike::Submit live(spike::Rect r,const char* what,RefreshIntent intent=RefreshIntent::FastInk,bool generated=false,bool erase=false,int keep=-1)noexcept{
  if(r.empty())return spike::Submit::Accepted;
  const bool penMode=erase||mechanics(intent)==RefreshMechanics::FastRegion;
  const int rows=penMode?std::max(1,LiveArea/std::max(1,r.x1-r.x0)):r.y1-r.y0;
  if(r.y1-r.y0>rows)recovery::v2::e2::mark("reader:live-banded",unsigned((r.y1-r.y0+rows-1)/rows));
  for(int y=r.y0;y<r.y1;y+=rows){spike::Rect band{r.x0,y,r.x1,std::min(r.y1,y+rows)};if((erase?vendor_.erase(band):!penMode&&keep>=0?vendor_.cleanRemoval(band,keep):applyRefresh(vendor_,band,intent))!=spike::Submit::Accepted){recovery::v2::e2::mark(what,unsigned(std::uint64_t(r.x1-r.x0)*unsigned(r.y1-r.y0)));return spike::Submit::Failed;}refresh_.accepted(band,intent,generated);}
  return spike::Submit::Accepted;}
 void track(const spike::Point& p)noexcept{if(p.phase==spike::Phase::Down)eraseSubmission_=p.eraser||shell_.eraseMode();
  // Coloured ink: stock gives each coloured stroke a local quality update after it (the red-ink pilot: fast 1/14, then
  // local quality 2/16, 5/16; red appears after it). Inkie gave one only to grid cells that passed the 96-update counter,
  // so a stroke across two cells came out half in fast-mode colour. Each stroke's live damage is kept (strokeArea_) and
  // queued once the pen is up and ink is settled. Black ink keeps no extra update, as in stock's black-dot pilot.
  if(!eraseSubmission_){if(p.phase==spike::Phase::Down){join(inkPlan_,strokeArea_);strokeArea_={};colorStroke_=shell_.colorInk();}return;}if(p.phase==spike::Phase::Down){erasing_=true;eraseArea_={};}ringX_=p.x;ringY_=p.y;grow(eraseArea_,shell_.inputArea(around(p.x,p.y,int(std::ceil((EraserRing+10)*shell_.inputScale()))))); /* p is in the pen's frame (inputFrame) */if(p.phase==spike::Phase::Up){erasing_=false;grow(liftCleanup_,eraseArea_);eraseArea_={};if(!eraseLiftAt_)eraseLiftAt_=spike::monotonic();}}
 void detach(const char* why)noexcept{detached_=true;std::fprintf(stderr,"INK_STROKE_TAIL_UNSAVED reason=%s splits=%llu\n",why,(unsigned long long)splits_);}
 bool appendSplit(const spike::Point& p)noexcept{
  penContact_=p.phase!=spike::Phase::Up;track(p);
  if(detached_){++tailPoints_;if(p.phase==spike::Phase::Up)detached_=false;return true;}
  if(p.phase==spike::Phase::Up)return shell_.append(p);
  const std::size_t room=shell_.inkRoom();
  if(p.phase==spike::Phase::Down){if(room>=2)return shell_.append(p);shell_.cancel();detach("no-room-at-down");return true;}
  if(room>=2)return shell_.append(p);
  spike::Point close=p;close.phase=spike::Phase::Up;if(!shell_.append(close))return false;
  if(!shell_.inkAdmission()||!shell_.reserve()){detach("no-piece");return true;}
  if(shell_.inkRoom()<2){shell_.cancel();detach("no-room-in-piece");return true;}
  ++splits_;recovery::v2::e2::mark("reader:ink-split",unsigned(splits_));spike::Point open=p;open.phase=spike::Phase::Down;return shell_.append(open);}
 // D2 sleep (Inkbound settings): apply setting changes; when sleep is pending, wait (bounded) for durable ink, notes
 // and Tom work, then release the wake lock so autosleep suspends; while asleep, input and redraws are skipped.
 static constexpr spike::Ns SleepQuiesceBudget=5000000000ull;
 spike::Ns sleepPendingAt_{},sleepInputCutoff_{};unsigned appliedSleep_{~0u};bool restoreSleep_{};recovery::v2::sessionpower::WakeCause pendingWake_{recovery::v2::sessionpower::WakeCause::None};recovery::v2::sessionpower::DoubleTapWake sleepTaps_;
 bool asleep()const noexcept{return sessionPower_&&sessionPower_->state()==recovery::v2::sessionpower::PowerState::Sleeping;}
 // boundary: called from readerBoundary(), the only place full-page swaps happen. pump() (inside the session loop,
 // possibly between vendor pump steps) only applies settings, logs resume/wake and records the pending sleep; the
 // passcode presentation and the sleep transition itself run at the next reader boundary (at most one tick later).
 void sleepTick(bool boundary)noexcept{
  namespace sp=recovery::v2::sessionpower;namespace e2=recovery::v2::e2;if(!sessionPower_)return;const spike::Ns now=spike::monotonic();
  if(appliedSleep_!=shell_.sleepRevision()){appliedSleep_=shell_.sleepRevision();const auto& s=shell_.sleepSettings();
   sessionPower_->configure({s.cover,spike::Ns(s.idleMinutes)*60000000000ull},now);e2::mark("power:settings",(s.cover?1000u:0u)+unsigned(s.idleMinutes)+(sessionPower_->coverAvailable()?0u:10000u));}
  if(pendingWake_!=sp::WakeCause::None){const auto cause=pendingWake_;pendingWake_=sp::WakeCause::None;(void)sessionPower_->inputWake(cause,now);}
  if(sessionPower_->takeResume())e2::mark("power:device-resumed",sessionPower_->wakes());
  if(sessionPower_->takeWake()){e2::mark("power:inkbound-restored",sessionPower_->wakes());drainTouch_=true;restoreSleep_=true;}
  if(boundary&&restoreSleep_&&!asleep()){compose({0,0,1620,2160});if(vendor_.readerPage(FullRefreshReason::Wake)){refresh_.accepted({0,0,1620,2160},RefreshIntent::PageTransition);shell_.presentationAccepted();(void)shell_.takeChange();restoreSleep_=false;e2::mark("power:scene-restored");}else e2::mark("power:scene-restore-pending");}
  if(sessionPower_->state()==sp::PowerState::SleepPending){
   if(!sleepPendingAt_){shell_.prepareSleep();sleepPendingAt_=now?now:1;const bool cover=sessionPower_->reason()==sp::SleepReason::Cover;e2::mark(cover?"power:cover-closed":"power:idle-timeout");e2::mark("power:suspend-requested",cover?1u:2u);}
   const bool ready=shell_.unsaved()==0&&shell_.inkSettled()&&!erasing_;
   if(boundary&&(ready||now-sleepPendingAt_>=SleepQuiesceBudget)){
    sessionPower_->prepareSleepDisplay();(void)shell_.lockForSleep();ib::Canvas sleeping(vendor_.surface());ib::sleep::paint(sleeping);if(vendor_.readerPage(FullRefreshReason::Sleep)){refresh_.accepted({0,0,1620,2160},RefreshIntent::PageTransition);e2::mark("power:sleep-screen-presented");}else e2::mark("power:sleep-screen-failed");sleepInputCutoff_=now;sleepTaps_.reset(sp::bootTime());touch_.quiesce();
    e2::mark("power:inkbound-quiesced",ready?1u:0u);e2::mark("power:wake-lock-released",sessionPower_->sleep(now)?sessionPower_->sleeps():0u);sleepPendingAt_=0;}
  }else sleepPendingAt_=0;
 }
 // E2 build only: every 5 s, each non-empty work queue's backlog and oldest-item age (finds WorkerAge sources).
 spike::Ns workerLogAt_{};
 void logWorkers()noexcept{
  if constexpr(recovery::v2::e2::provisional){
   const spike::Ns now=spike::monotonic();if(!foreground_||now-workerLogAt_<5000000000ull)return;workerLogAt_=now;
   static constexpr const char* Age[]={"reader:worker0-age-ms","reader:worker1-age-ms","reader:worker2-age-ms","reader:worker3-age-ms","reader:worker4-age-ms"};
   static constexpr const char* Backlog[]={"reader:worker0-backlog","reader:worker1-backlog","reader:worker2-backlog","reader:worker3-backlog","reader:worker4-backlog"};
   const auto touch=touch_.counters();recovery::v2::e2::mark("touch:read-events",touch.readEvents);recovery::v2::e2::mark("touch:consumed-events",touch.consumedEvents);recovery::v2::e2::mark("touch:frames",touch.frames);recovery::v2::e2::mark("touch:outputs",touch.outputs);recovery::v2::e2::mark("touch:cancellations",touch.cancellations);recovery::v2::e2::mark("touch:lost-packets",touch.losses);recovery::v2::e2::mark("touch:palm-frames",touch.palmFrames);recovery::v2::e2::mark("touch:rejected-pen",touch.rejectedPen);recovery::v2::e2::mark("touch:rejected-invalid",touch.rejectedInvalid);recovery::v2::e2::mark("touch:rejected-multi",touch.rejectedMulti);recovery::v2::e2::mark("touch:rejected-other",touch.rejectedOther);
   const auto& h=foreground_->lastHealth();
   for(unsigned n=0;n<5&&n<h.workerCount;++n){const auto& w=h.workers[n];if(!w.backlog)continue;
    recovery::v2::e2::mark(Backlog[n],w.backlog);recovery::v2::e2::mark(Age[n],now>w.oldest.monotonic?(now-w.oldest.monotonic)/1000000:0);}
  }
 }
 template<class F> bool startupWindow(const char* what,F&& f){
  if(!foreground_||pumped_)return f();
  // Main thread: capture a fresh worker snapshot first; the helper only re-stamps it (heartbeat).
  foreground_->publish(shell_.failed());
  const spike::Ns begin=spike::monotonic();std::atomic<bool> done{false};const spike::Ns limit=startupBudget_>startupUsed_?startupBudget_-startupUsed_:0;
  // The channel accepts one producer: delegate to the helper only after it has named itself, and
  // reclaim only after it is joined. Without a successful delegation nothing publishes (fail-safe).
  std::atomic<std::uint64_t> helperId{0};std::atomic<bool> go{false};std::thread helper;
  try{helper=std::thread([&]{helperId.store(recovery::v2::progress::currentThread(),std::memory_order_release);
   while(!go.load(std::memory_order_acquire)&&!done.load(std::memory_order_acquire))std::this_thread::yield();
   while(go.load(std::memory_order_acquire)&&!done.load(std::memory_order_acquire)&&spike::monotonic()-begin<limit){foreground_->heartbeat();
    // Main is blocked, so the helper is also the sole user of the session wake lock: keep it renewed.
    if(sessionPower_&&!sessionPower_->tick(spike::monotonic(),lease_&&lease_->valid()))powerEnd_=true;
    std::this_thread::sleep_for(std::chrono::milliseconds(20));}});}catch(...){}
  bool delegated=false;
  if(helper.joinable()){for(unsigned n=0;n<100000&&!helperId.load(std::memory_order_acquire);++n)std::this_thread::yield();
   if(auto id=helperId.load(std::memory_order_acquire))delegated=foreground_->delegate(id);
   if(delegated)go.store(true,std::memory_order_release);else done.store(true,std::memory_order_release);}
  bool ok=false;try{ok=f();}catch(...){ok=false;}
  done.store(true,std::memory_order_release);if(helper.joinable())helper.join();
  if(delegated)foreground_->reclaim();
  const spike::Ns used=spike::monotonic()-begin;startupUsed_+=used;recovery::v2::e2::mark(what,used/1000);
  foreground_->publish(shell_.failed());return ok;
 }
 void compose(spike::Rect r){shell_.compose(vendor_.surface(),r);}
 // Compose and present r. Its class is read before compose: a quality update keeps what it removes (and settles the
 // cells it covers); a fast update leaves that class owed to the area's later cleanup.
 spike::Submit present(spike::Rect r,const char* what,RefreshIntent intent,bool generated)noexcept{const int before=prior(r);compose(r);const bool quality=mechanics(intent)==RefreshMechanics::QualityRegion;const auto result=live(r,what,intent,generated,false,quality?before:-1);if(result==spike::Submit::Accepted){if(quality)paid(r);else owe(r,before);}return result;}
public:
 Display(spike::VendorDisplay& v,Shell& s,reader::lifecycle::Foreground* foreground=nullptr,recovery::v2::sessionpower::SessionPower* sessionPower=nullptr,spike::Ns startupBudget=StartupBudget):vendor_(v),shell_(s),foreground_(foreground),sessionPower_(sessionPower),startupBudget_(startupBudget){}
 bool initialize(const spike::RecoveryLease& l)noexcept override{lease_=&l;
  // Take the session wake lock before the start-up window so autosleep cannot intervene.
  if(sessionPower_&&!sessionPower_->tick(spike::monotonic(),l.valid()))powerEnd_=true;
  return startupWindow("reader:display-initialize-us",[&]{return vendor_.initialize(l);})&&touch_.open(l.valid());}
 spike::Surface surface()const noexcept override{return shell_.ink();}
 // A turned Notes or Inkie sheet (docs/LANDSCAPE.md, PR 3): pen points arrive in its sheet pixels (Shell::inputFrame).
 void inputFrame(spike::Transform& t)noexcept override{shell_.inputFrame(t);}
 bool initialPage()noexcept override{try{return startupWindow("reader:initial-page-us",[&]{compose({0,0,1620,2160});const bool accepted=vendor_.initialPreparedPage(vendor_.surface().pixels);if(accepted)refresh_.accepted({0,0,1620,2160},RefreshIntent::PageTransition);return accepted;});}catch(...){return false;}}
 spike::Submit submit(spike::Rect r)noexcept override{try{r=shell_.presentationDamage(r);const int shown=eraseSubmission_?prior(r):-1;if(eraseSubmission_&&!vendor_.beginErase(r))return spike::Submit::Failed;compose(r);auto result=live(r,"reader:submit-failed",RefreshIntent::FastInk,false,eraseSubmission_);if(result==spike::Submit::Accepted){if(eraseSubmission_){grow(liftCleanup_,r);grow(erasedBounds_,r);erasedClass_=std::max(erasedClass_,std::max(0,shown));owe(r,std::max(0,shown));}else if(colorStroke_)grow(strokeArea_,r);shell_.presentationCovered(r);}return result;}catch(...){return spike::Submit::Failed;}}
 void pump()noexcept override{pumped_=true;cadence_.tick("reader:loop");vendor_.pump();if(foreground_)foreground_->publish(shell_.failed());logWorkers();if(sessionPower_&&!sessionPower_->tick(spike::monotonic(),lease_&&lease_->valid()))powerEnd_=true;sleepTick(false);}void shutdown()noexcept override{shell_.beginQuiesce();vendor_.shutdown();lease_=nullptr;}
 bool diagnosticSegment()const noexcept override{return false;}spike::OperationSink* operationSink()noexcept override{return this;}
 bool reserve()noexcept override{if(asleep()||restoreSleep_||pageTurnPending_)return false;if(sessionPower_)sessionPower_->activity(spike::monotonic());touch_.block();return shell_.reserve();}
 bool append(const spike::Point& p)noexcept override{const bool result=appendSplit(p);if(p.phase==spike::Phase::Up)touch_.penReleased(p.receipt);return result;} /* SessionDisplay cancellation is terminal. */
 void cancel()noexcept override{shell_.beginQuiesce();}
 void wakeInput(const spike::Sample& sample)noexcept override{if(asleep()&&sample.phase==spike::Phase::Down)pendingWake_=recovery::v2::sessionpower::WakeCause::PenTip;}
 bool admissionOpen()const noexcept override{return !asleep()&&!restoreSleep_&&!pageTurnPending_&&shell_.inkAdmission();}std::uint64_t pageVersion()const noexcept override{return pageTurnPending_?installedVersion_:shell_.version();}
 void pageInstalled()noexcept override{installedVersion_=shell_.version();shell_.installed();}
 bool readerBoundary()noexcept override{try{if(sessionPower_&&(powerEnd_||sessionPower_->endRequested(spike::monotonic()))){recovery::v2::e2::mark("reader:end-power",powerEnd_?2u:1u);return false;}sleepTick(true);if(asleep()){touch_.selectionMode(false,shell_.version());touch_.statusCorner(false);const auto tap=touch_.pollTap(false);if(tap.valid){namespace sp=recovery::v2::sessionpower;const auto offset=sp::suspendOffset(nullptr);const bool still=!tap.cancelled&&!tap.longPress&&!tap.drag&&tap.gesture==reader::Gesture::None&&tap.startedAt>=sleepInputCutoff_&&std::abs(tap.x-tap.startX)<=25&&std::abs(tap.y-tap.startY)<=25;if(sleepTaps_.tap(tap.contacts,still,tap.x,tap.y,tap.startedAt+offset,tap.eligibleAt+offset,sp::bootTime()))pendingWake_=sp::WakeCause::DoubleTap;}return true;}if(restoreSleep_)return true;if(shell_.quietEffectsEnabled()||shell_.revealPending()){auto now=spike::monotonic(),end=lease_?lease_->deadline():0;shell_.setPowerConstrained(!lease_||!lease_->valid()||lease_->stopRequested()||!now||now>=end||end-now<=localized::Reveal::MaxDurationMs*1000000ULL);}if(drainTouch_){touch_.quiesce();drainTouch_=false;}touch_.statusCorner(shell_.statusAvailable());touch_.interactionMode(shell_.touchMode(),shell_.version());touch_.selectionHandles(shell_.selectionHandles());touch_.toolbar(shell_.touchToolbar());touch_.turn(shell_.turnDegrees());auto tap=touch_.pollTap(false);if(tap.valid){recovery::v2::e2::mark("touch:started-kernel-us",tap.startedAt/1000);recovery::v2::e2::mark("touch:eligible-kernel-us",tap.eligibleAt/1000);recovery::v2::e2::mark("touch:read-us",tap.readAt/1000);recovery::v2::e2::mark("touch:classified-us",tap.classifiedAt/1000);recovery::v2::e2::mark("reader:tap",tap.cancelled?3u:tap.gesture!=reader::Gesture::None?4u:tap.drag?2u:tap.longPress?1u:0u);if(sessionPower_)sessionPower_->activity(spike::monotonic());bool accepted=false;if(tap.gesture!=reader::Gesture::None)accepted=shell_.gesture(tap);else if(tap.cancelled){shell_.semantics().retain();accepted=true;}else if(tap.drag)accepted=shell_.touchDrag(tap);else accepted=shell_.finger(tap.x,tap.y,tap.longPress);recovery::v2::e2::mark("touch:application-handled",accepted?1:0);}{const spike::Ns tickStart=spike::monotonic();shell_.tick();const auto tickUs=(spike::monotonic()-tickStart)/1000;if(tickUs>=20000)recovery::v2::e2::mark("reader:slow-tick-us",tickUs);} /* E2 timing: UI work (page backgrounds, chrome) outside compose */if(shell_.exiting()){recovery::v2::e2::mark("reader:end-shell",unsigned(shell_.state()));return false;}auto change=shell_.takeChange();
  // Scene replacement and page navigation share canonical pixel comparison.
  // Keep a pending request while contact/saving is active; one 64-row strip
  // bounds memory even in the library where no input scratch frame exists.
  if(change.full){refresh_.requestScene(change.fullReason);if(change.fullReason==FullRefreshReason::DocumentBinding)pageTurnPending_=true;}
  if(change.pageTurn){pageTurnPending_=true;refresh_.requestScene();}
  const RefreshConditions conditions{penContact_,erasing_,!shell_.cleanupSettled()||shell_.unsaved()!=0,shell_.generatedPlaying(),shell_.generatedQuiet()};
  auto presentationConditions=conditions;if(pageTurnPending_&&shell_.unsaved()==0)presentationConditions.saving=false;
  const auto scene=refresh_.scene(presentationConditions);
  change.full=scene==SceneRefresh::Complete;
  if(change.full)change.fullReason=refresh_.sceneReason();
  if(scene==SceneRefresh::Deferred){change.regions=0;change.extraCount=0;}
  if(scene==SceneRefresh::Regional){
   const auto surface=vendor_.surface();if(!surface.valid())return false;
   const auto oldContent=vendor_.removalContent({0,0,1620,2160});
   std::array<spike::Rect,8> changed{};unsigned count{};
   if(!recomposeDamage(surface,sceneRows_,[&](spike::Rect r){compose(r);},changed,count)){shell_.fail(21);return false;}
   change.regions=0;change.extraCount=change.uiCleanupCount=0;
   // One scene replacement (a page turn, the page returning under a panel) presents as one region. Its pieces sent
   // as separate quality updates finish one after another on the panel, showing old and new scenes together.
   spike::Rect scene{};for(unsigned i=0;i<count;++i)grow(scene,changed[i]);
   if(!scene.empty()){change.extra[change.extraCount++]=scene;change.uiCleanup[change.uiCleanupCount++]=scene;owe(scene,oldContent);}
   change.preview=true;
   refresh_.sceneComposed();recovery::v2::e2::mark("reader:scene-regions",count);
  }
  if(change.full){if(!erasing_){cleanup_={};liftCleanup_={};removal_={};owed_.fill(-1);erasedBounds_={};erasedClass_=-1;eraseLiftAt_=0;}strokeArea_={};inkPlan_={};uiCleanup_={};}else{if(!change.cleanup.empty()){owe(change.cleanup,vendor_.removalContent(change.cleanup));grow(cleanup_,change.cleanup);}for(unsigned i=0;i<change.uiCleanupCount;++i)owe(change.uiCleanup[i],vendor_.removalContent(change.uiCleanup[i]));for(unsigned i=0;i<change.uiCleanupCount;++i)if(!uiCleanup_.add(change.uiCleanup[i])){spike::Rect joined=change.uiCleanup[i];for(unsigned n=0;n<uiCleanup_.count;++n)joined.include(uiCleanup_.regions[n]);uiCleanup_={};(void)uiCleanup_.add(joined);}}
  // Stock erase cleanup (Xochitl three-dot erase pilot, docs/refresh-reverse-engineering/STOCK_REFRESH_TRACE.md): in the
  // first safe boundary after lift, one regional quality update covers the erased damage. Stock queue
  // observations on 2026-10-20 were around 23–37 ms after input lift; these are lossy observations, not a timer.
  // No dark fill, fixed padding inferred from stock, or new waiting interval is introduced. Inkie used to wait until the stroke was saved (0.5-2 s). The removed class is kept
  // (prior); a refused update joins the ordinary removal cleanup and is retried with it. Strokes removed beyond the
  // eraser's path arrive with the saved result and keep the existing removal cleanup.
  RefreshPlan removalNow;if(!penContact_&&!erasing_&&!liftCleanup_.empty()&&refresh_.qualityAllowed({},spike::monotonic())){recovery::v2::e2::mark("ui:erase-lift-cleanup",unsigned(std::min<std::uint64_t>(RefreshPlan::area(liftCleanup_),UINT32_MAX)));join(removalNow,liftCleanup_);
   /* The erased area leaves any colour-stroke refresh still pending (UI mode, inkPlan_/strokeArea_): sent after this
      cleanup it would redraw the erased highlight's area last, in the waveform that leaves its yellow. */
   join(inkPlan_,strokeArea_);strokeArea_={};if(inkPlan_.count&&!inkPlan_.subtract(liftCleanup_))inkPlan_={};liftCleanup_={};}
  const bool settled=refreshSettled(conditions);if(settled&&!penContact_){join(inkPlan_,strokeArea_);strokeArea_={};for(unsigned i=0;i<inkPlan_.count;++i)join(uiCleanup_,inkPlan_.regions[i]);inkPlan_={};}if(settled&&!cleanup_.empty()){join(removal_,cleanup_);cleanup_={};}const auto adaptive=refresh_.cleanup({penContact_,erasing_,!shell_.cleanupSettled()||shell_.unsaved()!=0,shell_.generatedPlaying(),shell_.generatedQuiet()},spike::monotonic());RefreshPlan quality;if(!change.full&&removal_.count&&refresh_.qualityAllowed(presentationConditions,spike::monotonic())){for(unsigned i=0;i<removal_.count;++i)join(removalNow,removal_.regions[i]);removal_={};}
  if(!change.full&&refresh_.qualityAllowed(presentationConditions,spike::monotonic())){quality=uiCleanup_;for(unsigned i=0;i<adaptive.count;++i)if(!quality.add(adaptive.regions[i])){spike::Rect joined=adaptive.regions[i];for(unsigned n=0;n<quality.count;++n)joined.include(quality.regions[n]);quality={};(void)quality.add(joined);}}
  // The removal cleanup is also the last word over UI quality debt in the same area: a coloured stroke's own refresh
  // (UI mode) sent with the erase of that stroke (Content mode) could land last and leave the yellow of an erased
  // highlight. Removed areas leave the UI set; if they cannot be cut out, the UI debt is cleaned with the removal.
  if(removalNow.count&&quality.count){auto remaining=quality;bool split=true;for(unsigned i=0;i<removalNow.count;++i)if(!remaining.subtract(removalNow.regions[i])){split=false;break;}if(split)quality=remaining;else{for(unsigned i=0;i<quality.count;++i)join(removalNow,quality.regions[i]);quality={};}uiCleanup_={};}
  if(change.full){const spike::Ns drawStart=spike::monotonic();compose({0,0,1620,2160});const spike::Ns composed=spike::monotonic();if(applyRefresh(vendor_,{0,0,1620,2160},RefreshIntent::PageTransition,change.fullReason)!=spike::Submit::Accepted){recovery::v2::e2::mark("reader:end-page-failed");shell_.fail(21);return false;}shell_.presentationAccepted();pageTurnPending_=false;refresh_.sceneComposed();refresh_.accepted({0,0,1620,2160},RefreshIntent::PageTransition);const spike::Ns swapped=spike::monotonic(); /* E2 timing: software compose vs. vendor full-page swap */recovery::v2::e2::mark("reader:page-compose-us",(composed-drawStart)/1000);recovery::v2::e2::mark("reader:page-swap-us",(swapped-composed)/1000);recovery::v2::e2::mark("reader:full-page-us",(swapped-drawStart)/1000);}
  else{const bool generated=shell_.generatedPlaying();const auto feedback=RefreshController::feedback(change.content,change.preview,generated,conditions);const bool deferredUi=feedback.cleanupDebt;const auto intent=feedback.intent;RefreshPlan plan;const auto regions=ux::regions();for(unsigned i=0;i<regions.size();++i)if(change.regions&(1u<<i))(void)plan.add(regions[i]);for(unsigned i=0;i<change.extraCount;++i)(void)plan.add(change.extra[i]);
  // UI quality never interrupts contact or durable saving. Fast feedback while
  // unsettled retains debt in the existing retirement queue, including toolbar
  // changes which have no old/new popup retirement of their own.
  if(deferredUi){auto owe=[&](spike::Rect r){if(!uiCleanup_.add(r)){spike::Rect joined=r;for(unsigned n=0;n<uiCleanup_.count;++n)joined.include(uiCleanup_.regions[n]);uiCleanup_={};(void)uiCleanup_.add(joined);}};if(plan.overflow){for(unsigned i=0;i<regions.size();++i)if(change.regions&(1u<<i))owe(regions[i]);for(unsigned i=0;i<change.extraCount;++i)owe(change.extra[i]);}else for(unsigned i=0;i<plan.count;++i)owe(plan.regions[i]);
   // Off-contact tool/settings saves need only one quality presentation once
   // durable. During contact preserve immediate fast feedback and clean later.
   if(feedback.defer)plan={};}
  // A removal's own damage (the saved erase result, an undo) arrives with its cleanup. Sent as a fast update just before
  // the Content-mode cleanup, the fast one can land last (fast and quality updates are not applied in submission order),
  // leaving the removed pigment, e.g. a snapped highlight's yellow. The cleanup alone draws those areas.
  if(!plan.overflow&&removalNow.count){auto remaining=plan;bool split=true;for(unsigned i=0;i<removalNow.count;++i)if(!remaining.subtract(removalNow.regions[i])){split=false;break;}if(split)plan=remaining;else{for(unsigned i=0;i<plan.count;++i)join(removalNow,plan.regions[i]);plan={};}}
  if(!plan.overflow&&quality.count){auto remaining=plan;bool split=true;for(unsigned i=0;i<quality.count;++i)if(!remaining.subtract(quality.regions[i])){split=false;break;}if(split)plan=remaining;else{spike::Rect joined{};for(unsigned i=0;i<quality.count;++i)joined.include(quality.regions[i]);for(unsigned i=0;i<plan.count;++i)joined.include(plan.regions[i]);quality={};(void)quality.add(joined);plan={};}}if(plan.overflow){/* Existing change has at most sixteen inputs; preserve every region if that invariant ever changes. */for(unsigned i=0;i<regions.size();++i)if(change.regions&(1u<<i)){if(present(regions[i],"reader:region-failed",intent,generated)!=spike::Submit::Accepted){shell_.fail(22);return false;}shell_.presentationCovered(regions[i]);}for(unsigned i=0;i<change.extraCount;++i){if(present(change.extra[i],"reader:extra-failed",intent,generated)!=spike::Submit::Accepted){shell_.fail(23);return false;}shell_.presentationCovered(change.extra[i]);}}else{for(unsigned i=0;i<plan.count;++i){if(present(plan.regions[i],"reader:region-failed",intent,generated)!=spike::Submit::Accepted){shell_.fail(22);return false;}shell_.presentationCovered(plan.regions[i]);}}}
  // D2: remove a stale eraser ring, then give a finished erase one high-quality refresh (failures
  // here only leave ghosting; they never end the session).
  // Removals the shell reports (erase results, undo/redo, snap, highlight/layer changes): once ink is settled and the
  // pen is up they get one quality refresh together (the eraser's own area was cleaned when it lifted). A full-page swap already cleaned everything.
  // Removals (erase results, undo/redo, snap, highlight/layer changes) are cleaned in Content mode, as stock cleans
  // erased ink (cleanContent); UI debt stays in UI mode below.
  if(removalNow.count){const auto now=removalNow;for(unsigned i=0;i<now.count;++i){const auto rect=now.regions[i];const bool ready=vendor_.surface().valid();const int before=ready?prior(rect):-1;if(ready)compose(rect);const auto submitAt=spike::monotonic();
   if(ready&&vendor_.cleanContent(rect,std::max(0,before))==spike::Submit::Accepted){lastRemovalAt_=spike::monotonic();
    if(eraseLiftAt_&&std::max(rect.x0,erasedBounds_.x0)<std::min(rect.x1,erasedBounds_.x1)&&std::max(rect.y0,erasedBounds_.y0)<std::min(rect.y1,erasedBounds_.y1)){recovery::v2::e2::mark("ui:erase-lift-to-submit-us",(submitAt-eraseLiftAt_)/1000);eraseLiftAt_=0;}paid(rect);shell_.presentationCovered(rect);refresh_.accepted(rect,RefreshIntent::Cleanup);recovery::v2::e2::mark("ui:removal-content-cleanup",unsigned(std::min<std::uint64_t>(RefreshPlan::area(rect),UINT32_MAX)));}
   else{for(unsigned n=i;n<now.count;++n)join(removal_,now.regions[n]);refresh_.failed(spike::monotonic());break;}}}
bool presentationDone=uiCleanup_.count==0;if(quality.count){uiCleanup_={};bool accepted=true;for(unsigned i=0;i<quality.count;++i){const auto rect=quality.regions[i];const bool sourceReady=vendor_.surface().valid();const int before=sourceReady?prior(rect):-1;if(sourceReady)compose(rect);if(sourceReady&&vendor_.cleanRemoval(rect,std::max(0,before))==spike::Submit::Accepted){paid(rect);shell_.presentationCovered(rect);refresh_.accepted(rect,RefreshIntent::Cleanup);recovery::v2::e2::mark("ui:retirement-quality-submit",RefreshPlan::area(rect));}else{accepted=presentationDone=false;for(unsigned n=i;n<quality.count;++n)(void)uiCleanup_.add(quality.regions[n]);refresh_.failed(spike::monotonic());break;}}if(accepted){uiCleanup_={};presentationDone=true;}}if(presentationDone&&refresh_.scene(conditions)==SceneRefresh::None){shell_.uiPresentationAccepted();pageTurnPending_=false;}
  if(settled&&presentationDone&&removal_.count==0&&liftCleanup_.empty()&&cleanup_.empty()){
   if(!erasedBounds_.empty())ghostAt_=(lastRemovalAt_?lastRemovalAt_:spike::monotonic())+GhostDelay;
   erasedBounds_={};erasedClass_=-1;eraseLiftAt_=0;
   if(ghostAt_&&!penContact_&&!erasing_&&spike::monotonic()>=ghostAt_){ghostAt_=0;recovery::v2::e2::mark("ui:ghost-clear",vendor_.clearGhosting()?1u:0u);}
  }
  return true;
 }catch(...){recovery::v2::e2::mark("reader:end-exception");shell_.fail(24);return false;}}
};
}
