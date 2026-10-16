// SPDX-License-Identifier: AGPL-3.0-only
// Actual policy + a narrowly modeled, independently inspected bit0 expansion.
// This is not vendor execution, optical qualification, or a complete backend model.
#include "refresh_scheduler.h"
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
unsigned checks{};
void check(bool condition,const char* message){++checks;if(!condition)throw std::runtime_error(message);}
bool same(spike::Rect a,spike::Rect b){return a.x0==b.x0&&a.y0==b.y0&&a.x1==b.x1&&a.y1==b.y1;}
struct Lease final:spike::RecoveryLease {
 bool live{true};spike::Ns end{spike::monotonic()+10000000000ULL};
 bool valid()const noexcept override{return live;}bool stopRequested()const noexcept override{return !live;}
 spike::Ns deadline()const noexcept override{return end;}
};
struct Calls final:spike::VendorCalls {
 struct Request {spike::Rect requested,effective;int content,mode,flags;};
 std::vector<std::uint32_t> pixels=std::vector<std::uint32_t>(1632*2160);
 std::vector<Request> requests;unsigned maskPixels{},maskChanged{},maskUnchanged{};
 bool acquire(spike::Pixels& p)noexcept override{p={reinterpret_cast<unsigned char*>(pixels.data()),1620,2160,4,6528};return true;}
 void region(spike::Rect r,int c,int m,int f)noexcept override{
  // Exact pinned wrapper 0x32fdc -> 0x33248. Private force state is not modeled.
  requests.push_back({r,(f&1)?spike::Rect{0,0,1620,2160}:r,c,m,f});
 }
 bool eraseRegion(spike::Rect r,int c,int m,int f,std::span<const unsigned char> mask,int stride)noexcept override{if(stride!=1632||mask.size()<std::size_t(1632)*2160)return false;maskPixels=0;for(int y=r.y0;y<r.y1;++y)for(int x=r.x0;x<r.x1;++x)maskPixels+=mask[std::size_t(y)*1632+x]!=0;maskChanged=mask[std::size_t(800)*1632+100];maskUnchanged=mask[std::size_t(800)*1632+101];region(r,c,m,f);return true;}
 void events()noexcept override{}
};
}
int main(){try{
 {
  std::vector<unsigned char> mask(1632*2160,23),next(1632*2160,0),backup(1632*2160);const auto original=mask;const spike::Rect r{1600,100,1620,103};next[std::size_t(101)*1632+1605]=255;
  spike::Pixels p{mask.data(),1620,2160,24,1632};check(p.validFerrariMask(),"mask shape");{spike::ScopedEraseMask scope(p,next,1632,r,backup);check(bool(scope),"mask scope admission");check(mask[std::size_t(101)*1632+1605]==255&&mask[std::size_t(101)*1632+1604]==0&&mask[std::size_t(101)*1632+1620]==23,"mask scope ROI/padding");}check(mask==original,"mask bytes not restored");
  {spike::ScopedEraseMask scope(p,next,1620,r,backup);check(!scope&&mask==original,"invalid mask stride mutated bytes");}{spike::ScopedEraseMask scope(p,next,1632,{-1,0,1,1},backup);check(!scope&&mask==original,"invalid mask bounds mutated bytes");}{spike::ScopedEraseMask scope(p,next,1632,r,std::span<unsigned char>(backup).first(10));check(!scope&&mask==original,"short backup mutated bytes");}
  spike::CaptureTable table;spike::Pixels selected;table.observe(p);check(table.selectMask(selected)&&selected.data==p.data,"separate mask capture failed");std::vector<unsigned char> other(mask.size());p.data=other.data();table.observe(p);check(!table.selectMask(selected),"ambiguous mask accepted");
 }
 Lease lease;Calls calls;spike::VendorDisplay display(calls);check(display.initialize(lease),"initialize");
 const spike::Rect local{100,800,200,850};
 for(auto intent:{ux::RefreshIntent::Cleanup,ux::RefreshIntent::UIUpdate}){
  check(ux::applyRefresh(display,local,intent)==spike::Submit::Accepted,"local quality submit");
  const auto& q=calls.requests.back();
  check(same(q.effective,local),"regional quality became full image at modeled vendor boundary");
  check(q.content==0&&q.mode==3&&q.flags==0,"UI/cleanup must retain regional UI quality");
 }
 // Keep live handwriting and streamed-content mechanics unchanged.
 for(auto intent:{ux::RefreshIntent::FastInk,ux::RefreshIntent::ContentUpdate}){
  check(ux::applyRefresh(display,local,intent)==spike::Submit::Accepted,"fast submit");
  const auto& q=calls.requests.back();check(same(q.effective,local)&&q.content==0&&q.mode==0&&q.flags==0,"fast path changed");
 }
  check(display.erase(local)==spike::Submit::Failed,"unprepared erase accepted");check(display.beginErase(local),"erase before-capture refused");calls.pixels[std::size_t(800)*1632+100]=0xffffffffu;
  check(display.erase(local)==spike::Submit::Accepted,"live erase refused");const auto erased=calls.requests.back();check(same(erased.effective,local)&&erased.content==0&&erased.mode==0&&erased.flags==2,"live erase conversion or extent changed");check(calls.maskPixels==1&&calls.maskChanged==255&&calls.maskUnchanged==0,"erase mask touched unchanged neighboring pixels");check(display.erase({0,0,1620,2160})==spike::Submit::Failed,"oversized erase bypassed live bound");
 // Separated restoration areas do not become a bounding union at this boundary.
 const spike::Rect other{1400,100,1450,160};
 check(display.clean(local)==spike::Submit::Accepted&&display.clean(other)==spike::Submit::Accepted,"two cleanup requests");
 check(same(calls.requests[calls.requests.size()-2].effective,local)&&same(calls.requests.back().effective,other),"separated cleanup scope");
 // UI quality can exceed the live-ink area guard without becoming complete.
 const spike::Rect large{112,112,1510,1000};
 check(display.ui(large)==spike::Submit::Accepted&&same(calls.requests.back().effective,large),"large UI remains regional");
 const auto prior=calls.requests.size();check(display.submit(large)==spike::Submit::Failed&&calls.requests.size()==prior,"live area guard changed");
 check(ux::applyRefresh(display,local,ux::RefreshIntent::PageTransition)==spike::Submit::Accepted,"page transition");
 const auto& page=calls.requests.back();check(same(page.effective,{0,0,1620,2160})&&page.mode==3&&page.flags==1,"intentional full page changed");
 // Color and grayscale must retain the same local scope and source RGB.
 const spike::Rect aligned{101,801,105,803};
 calls.pixels[800*1632+100]=0xffff0000u;
 check(display.clean(aligned)==spike::Submit::Accepted,"aligned red cleanup");
 check(calls.requests.back().content==3&&calls.requests.back().mode==3&&calls.requests.back().flags==0&&same(calls.requests.back().effective,aligned),"color must not promote local cleanup or select Mono screen mode");
 check(calls.pixels[800*1632+100]==0xffff0000u,"color source must remain unchanged");
 calls.pixels[800*1632+100]=0xff808080u;
 check(display.clean(aligned)==spike::Submit::Accepted&&calls.requests.back().content==1&&same(calls.requests.back().effective,aligned),"local grayscale classification");
 calls.pixels[20*1632+1400]=0xff0000ffu;
 check(display.clean(aligned)==spike::Submit::Accepted&&calls.requests.back().content==1,"distant color must not change unrelated classification or extent");
 calls.pixels[2160*1632-1]=0xff00ff00u;
 check(display.clean({1619,2159,1620,2160})==spike::Submit::Accepted&&calls.requests.back().content==0,"stride padding is not visible content");
 const auto guarded=calls.requests.size();
 check(display.clean({-1,800,5,810})==spike::Submit::Failed&&display.clean({1619,2159,1621,2160})==spike::Submit::Failed&&calls.requests.size()==guarded,"invalid regions rejected before pixel inspection");
 // Failed authority does not reach the vendor boundary for either quality route.
 const auto accepted=calls.requests.size();lease.live=false;
 check(display.clean(local)==spike::Submit::Failed&&display.ui(local)==spike::Submit::Failed&&calls.requests.size()==accepted,"lease-loss gate changed");
 lease.live=true;lease.end=1;
 check(display.clean(local)==spike::Submit::Failed&&calls.requests.size()==accepted,"deadline gate changed");
 std::printf("REFRESH_EXTENT_PASS checks=%u actual_policy=true full_bit_model=true optical=NOT_RUN vendor_runtime=NOT_RUN\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"REFRESH_EXTENT_FAIL %s\n",e.what());return 1;}}
