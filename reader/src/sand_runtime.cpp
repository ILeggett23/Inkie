// SPDX-License-Identifier: AGPL-3.0-only
#include "ux_shell.h"
#include <limits>
#ifdef INKBOUND_SAND_DEBUG
#include <cstdio>
#endif
namespace ux {namespace {
std::uint64_t sandNow()noexcept{const auto ns=spike::monotonic();return ns?ns/1000000:std::numeric_limits<std::uint64_t>::max();}
sand::Rect box(spike::Rect r)noexcept{return {r.x0,r.y0,r.x1,r.y1};}
}
sand::Policy Shell::sandPolicy()const noexcept{
 sand::Policy p;p.quality=sand::Quality(std::min(prefs_.sandMode,3u));p.reducedMotion=reducedMotion_;
 p.contact=(document_&&document_->contact())||(notes_&&notes_->contact())||(tom_&&tom_->contact());
 p.selecting=penSelect_.active||selectMode_;p.power=powerConstrained_||stop_||quiescing_||lock_!=Lock::None;
 p.heavy=turned()||changingView_||returning_||syncing_||companionBusy_||pdfPreviewActive_||epubZoomActive()||generatedPlaying()||
  (state_!=State::Clean&&state_!=State::Chrome&&state_!=State::Picker)||(styled_&&styled_->status().busy)||semantics_->pending()||unsaved()!=0;
 p.congested=sandCongested_;p.expensive=sand_.pending()&&sand_.metrics().lastPaintNs>12000000;return p;
}
void Shell::sandSceneChanged(){
 if(!prefs_.sandMode&&!sand_.pending()){sandScene_=0;sandKey_.clear();sandTarget_={};return;}
 // Surface identity only: streamed content, worker revisions, scrolling and
 // ordinary page changes never start decorative frames.
 int scene=0;QString key;spike::Rect target;auto direction=sand::Direction::RightToLeft;
 if(tom_&&tom_->active()){scene=1;target=sheetPhysical(tom_->repaintArea());}
 else if(notes_&&notes_->active()){scene=2;target=sheetPhysical(notes_->repaintArea());}
 else if(toolsView_.active()){scene=3;target=physical(ToolsView::panel());direction=leftHanded_?sand::Direction::RightToLeft:sand::Direction::LeftToRight;}
 else if(semantics_->view()==semantic::View::Definition&&!semantics_->pending()){
  scene=4;target=semanticVisual_.panel;if(auto selection=semantics_->selection())key=selection->id();
  direction=target.x0<810?sand::Direction::LeftToRight:sand::Direction::RightToLeft;
 }else if(state_==State::Picker&&libraryView_.effectScene()==2){scene=5;target=libraryView_.menuContent();direction=sand::Direction::LeftToRight;}
 if(scene==sandScene_&&key==sandKey_)return;
 const auto oldTarget=sandTarget_;const auto oldDirection=sandDirection_;const bool wasVisible=sandScene_!=0;
 sandScene_=scene;sandKey_=key;sandTarget_=target;sandDirection_=direction;++sandEpoch_;
 if(sand_.pending()){sand_.cancel(sand::Reason::Scene);return;} // newer UI wins; no transition queue
 if(!prefs_.sandMode||turned()||!reveal_.quiescent())return;
 const bool dismissal=wasVisible&&scene==0;
 if(!scene&&!dismissal)return;
 if(dismissal){target=oldTarget;direction=oldDirection==sand::Direction::RightToLeft?sand::Direction::LeftToRight:sand::Direction::RightToLeft;}
 // No full-screen decoration, or decoration on security/navigation installs.
 if(change_.full||change_.pageTurn||target.empty())return;
 const auto seed=std::uint32_t((scene?scene:17)*2654435761u)^std::uint32_t(target.x0*31+target.y0);
 const bool started=sand_.start({box(target),{0,0,1620,2160},{presentedVersion_,sandEpoch_},direction,seed,dismissal,scene==4?180u:280u},sandPolicy(),sandNow());
#ifdef INKBOUND_SAND_DEBUG
 if(started)std::fprintf(stderr,"SAND_START scene=%d dismiss=%d quality=%u x=%d y=%d w=%d h=%d\n",scene,dismissal,unsigned(sand_.metrics().quality),target.x0,target.y0,target.x1-target.x0,target.y1-target.y0);
#else
 (void)started;
#endif
}
void Shell::prepareSand(spike::Surface old){
 if(sand_.needsCapture()){
  // A real scene install bypasses the effect. Capture is bounded and occurs
  // once off-contact, before Display modifies its borrowed source pixels.
  if(change_.full||change_.pageTurn)sand_.cancel(sand::Reason::Scene);
  else {auto policy=sandPolicy();sand_.advance({presentedVersion_,sandEpoch_},policy,sandNow());
   if(sand_.needsCapture()&&sand_.capture(old.pixels,old.width,old.height,old.stridePixels,sandNow())){
    // Display has already retained the old pigment class. Borrowed source reuse
    // is serial; this CPU compose does not submit an update or change ownership.
    const auto r=sand_.region();try{composeCanonical(old,{r.x0,r.y0,r.x1,r.y1});
     if(!sand_.buildSilhouette(old.pixels,old.width,old.height,old.stridePixels))sand_.cancel(sand::Reason::Geometry);
    }catch(...){sand_.cancel(sand::Reason::Memory);}
   }}
 }
 sandTick();
}
void Shell::sandTick(){
 if(!sand_.pending())return;
 if(change_.full||change_.pageTurn)sand_.cancel(sand::Reason::Scene);
 auto update=sand_.advance({presentedVersion_,sandEpoch_},sandPolicy(),sandNow());
 // Contact cancels logically without introducing display work on the pen path.
 // The first off-contact boundary restores the complete canonical footprint.
 if(!update||sandPolicy().contact||change_.full)return;
#ifdef INKBOUND_SAND_DEBUG
 if(update.stamp!=sandDebugStamp_){sandDebugStamp_=update.stamp;const auto& m=sand_.metrics();std::fprintf(stderr,"SAND_STAGE stamp=%llu stage=%u intermediate=%d quality=%u reason=%u x=%d y=%d w=%d h=%d masks=%llu generation_ns=%llu last_paint_ns=%llu requests=%llu refused=%llu accepted=%llu skipped=%llu cleanup=%llu cache_bytes=%zu fence=SOURCE_ONLY\n",(unsigned long long)update.stamp,update.stage,update.intermediate,unsigned(m.quality),unsigned(m.reason),update.damage.x0,update.damage.y0,update.damage.x1-update.damage.x0,update.damage.y1-update.damage.y0,(unsigned long long)m.masks,(unsigned long long)m.generationNs,(unsigned long long)m.lastPaintNs,(unsigned long long)m.requests,(unsigned long long)m.refused,(unsigned long long)m.accepted,(unsigned long long)m.skipped,(unsigned long long)m.cleanups,m.cacheBytes);}
#endif
 damage({update.damage.x0,update.damage.y0,update.damage.x1,update.damage.y1});
 change_.preview=true; // existing regional UI quality, never new flags/modes
}
void Shell::cancelSandForInput()noexcept{if(sand_.pending())sand_.cancel(sand::Reason::Contact);}
}
