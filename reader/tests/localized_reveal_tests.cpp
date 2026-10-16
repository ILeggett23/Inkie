#include "localized_reveal.h"
#include "product_services.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <new>
namespace {std::atomic<unsigned long long> allocations{0};bool watch=false;unsigned checks=0,failures=0;}
void* operator new(std::size_t n){if(watch)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
void* operator new(std::size_t n,std::align_val_t alignment){if(watch)++allocations;void* p=nullptr;if(posix_memalign(&p,std::size_t(alignment),n?n:1)==0)return p;throw std::bad_alloc();}
void* operator new[](std::size_t n,std::align_val_t a){return ::operator new(n,a);}
void operator delete(void* p,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void* p,std::align_val_t)noexcept{std::free(p);}
void operator delete(void* p,std::size_t,std::align_val_t)noexcept{std::free(p);}
void operator delete[](void* p,std::size_t,std::align_val_t)noexcept{std::free(p);}
void* operator new(std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,const std::nothrow_t&)noexcept{try{return ::operator new(n);}catch(...){return nullptr;}}
void operator delete(void* p,const std::nothrow_t&)noexcept{::operator delete(p);}
void operator delete[](void* p,const std::nothrow_t&)noexcept{::operator delete(p);}
void* operator new(std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void* operator new[](std::size_t n,std::align_val_t a,const std::nothrow_t&)noexcept{try{return ::operator new(n,a);}catch(...){return nullptr;}}
void operator delete(void* p,std::align_val_t a,const std::nothrow_t&)noexcept{::operator delete(p,a);}
void operator delete[](void* p,std::align_val_t a,const std::nothrow_t&)noexcept{::operator delete(p,a);}
namespace {
constexpr int Width=320,Height=240;constexpr std::uint32_t Static=0xfff0f0f0;
using Pixels=std::array<std::uint32_t,Width*Height>;
void check(bool okay,const char* name){++checks;if(!okay)++failures;std::printf("%s=%s\n",name,okay?"PASS":"FAIL");}
localized::Request request(){return {{40,80,280,220},{0,0,Width,Height},{7,9},true};}
bool inside(localized::Rect r,int x,int y){return x>=r.x0&&x<r.x1&&y>=r.y0&&y<r.y1;}
unsigned area(localized::Rect r){return unsigned((r.x1-r.x0)*(r.y1-r.y0));}
void restore(Pixels& pixels,localized::Rect r){for(int y=r.y0;y<r.y1;++y)std::fill(pixels.begin()+y*Width+r.x0,pixels.begin()+y*Width+r.x1,Static);}
void finish(localized::Reveal& effect,Pixels& pixels,const product::AnimationPolicy& policy,unsigned time){auto clear=effect.advance(request().identity,policy,time);if(clear){restore(pixels,clear.damage);check(!clear.paint&&effect.acknowledgePresented(clear.stamp,clear.damage),"cleanup_accepted");}check(effect.quiescent(),"cleanup_quiescent");}
}
int main(){product::AnimationPolicy policy;Pixels pixels;pixels.fill(Static);
 {
  localized::Reveal e;localized::Request off=request();off.userEnabled=false;
  check(!e.start(off,policy,0)&&e.quiescent(),"default_user_preference_is_disabled");
  auto limited=policy;limited.maxFrames=1;check(!e.start(request(),limited,0),"one_frame_uses_static_fallback");limited=policy;limited.maxParticles=0;check(!e.start(request(),limited,0),"zero_particle_limit_refuses");limited=policy;limited.durationMs=0;check(!e.start(request(),limited,0),"zero_duration_refuses");limited=policy;limited.maxArea=1;check(!e.start(request(),limited,0),"tight_damage_budget_refuses");
  for(int flag=0;flag<5;++flag){auto blocked=policy;if(flag==0)blocked.enabled=false;if(flag==1)blocked.reducedMotion=true;if(flag==2)blocked.contact=true;if(flag==3)blocked.heavyRendering=true;if(flag==4)blocked.constrainedPower=true;check(!e.start(request(),blocked,0)&&e.quiescent(),"policy_denial_is_immediate_static_fallback");}
  auto invalid=request();invalid.popup.y0=2;check(!e.start(invalid,policy,0),"no_outside_content_edge_room_refuses");invalid=request();invalid.screen.x1=std::numeric_limits<int>::max();check(!e.start(invalid,policy,0),"oversized_screen_refuses_safely");check(!e.start(request(),policy,std::numeric_limits<std::uint64_t>::max()),"invalid_clock_refuses");
  invalid=request();invalid.screen={0,0,40,15};invalid.popup={0,10,40,15};check(!e.start(invalid,policy,0),"quarter_screen_damage_limit_refuses");auto loose=policy;loose.maxParticles=24;loose.maxFrames=4;loose.durationMs=250;check(e.start(request(),loose,0)&&e.particleCount()==18&&e.frameCount()==3&&e.durationMs()==180,"looser_product_policy_clamped_to_local_limits");loose.enabled=false;e.advance(request().identity,loose,1);check(e.quiescent(),"cancel_before_paint_needs_no_cleanup");
 }
 {
  localized::Reveal e;check(e.start(request(),policy,0),"bounded_reveal_starts");check(e.particleCount()<=18&&e.frameCount()<=3&&e.durationMs()<=180&&area(e.region())<=300000&&area(e.region())<=Width*Height/4,"all_local_budgets_hold");check(!e.start(request(),policy,1),"active_effect_cannot_be_replaced");auto a=e.advance(request().identity,policy,0);check(a&&a.paint&&!e.acknowledgePresented(a.stamp,a.damage),"unpainted_frame_cannot_be_acknowledged");check(e.paint(a.stamp,pixels,Width,Height,Width),"first_frame_paints");bool contentUntouched=true,hasDots=false;for(int y=0;y<Height;++y)for(int x=0;x<Width;++x)if(pixels[y*Width+x]!=Static){hasDots=true;contentUntouched&=inside(a.damage,x,y)&&!inside(request().popup,x,y);}check(hasDots&&contentUntouched,"static_popup_content_never_masked");auto partial=a.damage;--partial.x1;check(!e.acknowledgePresented(a.stamp,partial),"partial_frame_acceptance_does_not_retire");check(e.acknowledgePresented(a.stamp,a.damage)&&!e.acknowledgePresented(a.stamp,a.damage),"only_current_full_frame_ack_accepted");check(!e.advance(request().identity,policy,1),"same_phase_does_not_add_damage");auto b=e.advance(request().identity,policy,90);check(b&&b.paint&&b.stamp!=a.stamp&&!e.paint(a.stamp,pixels,Width,Height,Width),"second_phase_rejects_stale_paint");restore(pixels,b.damage);check(e.paint(b.stamp,pixels,Width,Height,Width)&&e.acknowledgePresented(b.stamp,b.damage),"second_frame_acceptance");auto c=e.advance(request().identity,policy,180);check(c&&!c.paint&&!c.active&&c.stamp==b.stamp+1&&e.cleanupPending(),"third_and_final_frame_is_cleanup");check(!e.acknowledgePresented(a.stamp,c.damage)&&!e.acknowledgePresented(c.stamp,partial)&&!e.quiescent(),"stale_or_partial_cleanup_preserves_obligation");restore(pixels,c.damage);check(e.acknowledgePresented(c.stamp,c.damage)&&e.quiescent()&&!e.advance(request().identity,policy,500),"full_cleanup_ack_retires_effect");check(std::all_of(pixels.begin(),pixels.end(),[](auto p){return p==Static;}),"finished_effect_leaves_no_pixels");
 }
 {
  localized::Reveal e;e.start(request(),policy,0);auto frame=e.advance(request().identity,policy,0);e.paint(frame.stamp,pixels,Width,Height,Width);auto contact=policy;contact.contact=true;auto stopped=e.advance(request().identity,contact,1);check(!stopped&&!e.active()&&e.cleanupPending(),"contact_cancels_without_any_damage");check(!e.paint(frame.stamp,pixels,Width,Height,Width)&&!e.acknowledgePresented(frame.stamp,frame.damage),"contact_rejects_paint_and_late_ack");check(!e.start(request(),policy,2),"unacknowledged_paint_requires_cleanup_before_restart");auto cleanup=e.advance({88,99},policy,2);check(cleanup&&!cleanup.paint&&cleanup.damage==frame.damage,"off_contact_restores_old_dirty_region_for_current_scene");check(!e.advance({88,99},contact,3)&&!e.acknowledgePresented(cleanup.stamp,cleanup.damage),"contact_during_cleanup_defers_submission");auto resumed=e.advance({88,99},policy,4);check(resumed.stamp==cleanup.stamp&&resumed.damage==cleanup.damage,"repeated_contact_does_not_add_cleanup_phases");restore(pixels,resumed.damage);check(e.acknowledgePresented(resumed.stamp,resumed.damage)&&e.quiescent(),"unacknowledged_paint_cleaned_conservatively");
 }
 for(int cancellation=0;cancellation<9;++cancellation){localized::Reveal e;e.start(request(),policy,100);auto a=e.advance(request().identity,policy,100);e.paint(a.stamp,pixels,Width,Height,Width);e.acknowledgePresented(a.stamp,a.damage);auto changed=policy;auto identity=request().identity;std::uint64_t now=101;if(cancellation==0)++identity.page;if(cancellation==1)++identity.ui;if(cancellation==2)changed.enabled=false;if(cancellation==3)changed.reducedMotion=true;if(cancellation==4)changed.heavyRendering=true;if(cancellation==5)changed.constrainedPower=true;if(cancellation==6)now=99;if(cancellation==7)now=std::numeric_limits<std::uint64_t>::max();if(cancellation==8)changed.maxFrames=1;auto cleanup=e.advance(identity,changed,now);check(cleanup&&!cleanup.paint&&!e.active()&&e.cleanupPending(),"changed_identity_policy_or_clock_only_cleans");restore(pixels,cleanup.damage);check(e.acknowledgePresented(cleanup.stamp,cleanup.damage)&&e.quiescent(),"cancellation_full_cleanup_retires");}
 {
  localized::Reveal e;auto two=policy;two.maxFrames=2;two.maxParticles=4;two.durationMs=20;check(e.start(request(),two,0),"two_frame_policy_starts_with_single_paint");auto a=e.advance(request().identity,two,0);e.paint(a.stamp,pixels,Width,Height,Width);e.acknowledgePresented(a.stamp,a.damage);check(!e.advance(request().identity,two,19),"two_frame_policy_has_no_second_paint");finish(e,pixels,two,20);
 }
 {
  localized::Reveal e;e.start(request(),policy,0);auto a=e.advance(request().identity,policy,0);check(!e.paint(a.stamp,std::span<std::uint32_t>(pixels.data(),1),Width,Height,Width)&&e.quiescent(),"invalid_borrowed_surface_expires_before_touching_memory");e.start(request(),policy,0);a=e.advance(request().identity,policy,0);e.paint(a.stamp,pixels,Width,Height,Width);e.acknowledgePresented(a.stamp,a.damage);auto b=e.advance(request().identity,policy,90);check(!e.paint(b.stamp,pixels,Width,Height,1)&&e.cleanupPending(),"later_invalid_surface_preserves_earlier_cleanup");finish(e,pixels,policy,91);
 }
 {
  const auto original=request();const auto originalPolicy=policy;allocations=0;watch=true;for(unsigned run=0;run<128;++run){localized::Reveal e;e.start(original,policy,0);for(unsigned now:{0u,90u,180u}){auto update=e.advance(original.identity,policy,now);if(update){restore(pixels,update.damage);if(update.paint)e.paint(update.stamp,pixels,Width,Height,Width);e.acknowledgePresented(update.stamp,update.damage);}}}watch=false;check(allocations==0,"start_advance_paint_acknowledge_allocate_zero");check(original.identity==request().identity&&original.popup==request().popup&&policy.enabled==originalPolicy.enabled&&policy.maxFrames==originalPolicy.maxFrames,"caller_identity_policy_and_model_not_mutated");check(std::all_of(pixels.begin(),pixels.end(),[](auto p){return p==Static;}),"stress_sequence_leaves_only_static_pixels");
 }
 std::printf("LOCALIZED_REVEAL checks=%u failures=%u\n",checks,failures);return failures?1:0;
}
