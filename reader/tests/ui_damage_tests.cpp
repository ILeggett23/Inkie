// SPDX-License-Identifier: AGPL-3.0-only
#include "panel_damage.h"
#ifndef INKBOUND_DAMAGE_ONLY
#include "vendor.h"
#endif
#include <cstdio>
#include <stdexcept>
#include <vector>

namespace {
void check(bool value){if(!value)throw std::runtime_error("UI damage regression");}
bool covers(spike::Rect a,spike::Rect b){return a.x0<=b.x0&&a.y0<=b.y0&&a.x1>=b.x1&&a.y1>=b.y1;}
#ifndef INKBOUND_DAMAGE_ONLY
struct Lease final:spike::RecoveryLease {
 bool valid()const noexcept override{return true;}
 spike::Ns deadline()const noexcept override{return spike::monotonic()+10000000000ull;}
 bool stopRequested()const noexcept override{return false;}
};
struct Calls final:spike::VendorCalls {
 std::vector<std::uint32_t> pixels=std::vector<std::uint32_t>(1632*2160,0xffffffffu);
 int content{-1},mode{-1},flags{-1};spike::Rect rect{};
 bool acquire(spike::Pixels& out)noexcept override{out={reinterpret_cast<unsigned char*>(pixels.data()),1620,2160,4,6528};return true;}
 void region(spike::Rect r,int c,int m,int f)noexcept override{rect=r;content=c;mode=m;flags=f;}
 void events()noexcept override{}
};
#endif
}
int main(){try{
 std::array<spike::Rect,8> regions{};unsigned count=0;
 std::vector<spike::Rect> inputs;
 // Enough independent updates to exceed the real Shell queue repeatedly.
 for(int n=0;n<100;++n){const int x=100+(n%10)*120,y=200+(n/10)*120;spike::Rect r{x,y,x+8,y+8};inputs.push_back(r);ux::accumulateDamage(regions,count,r);check(count<=regions.size());}
 for(auto r:inputs){bool found=false;for(unsigned i=0;i<count;++i)found|=covers(regions[i],r);check(found);}
 for(unsigned i=0;i<count;++i)check(regions[i].y0>=200&&regions[i].y1<=1288);
 const auto previous=count;ux::accumulateDamage(regions,count,inputs.front());check(count==previous);
 std::array<spike::Rect,1> one{};unsigned used=0;
 ux::accumulateDamage(one,used,{10,20,40,50});ux::accumulateDamage(one,used,{80,100,90,110});check(used==1&&covers(one[0],{10,20,90,110}));
#ifndef INKBOUND_DAMAGE_ONLY
 Lease lease;Calls calls;spike::VendorDisplay display(calls);check(display.initialize(lease));
 const spike::Rect panel{50,200,1570,1400}; // larger than the live-ink area cap
 check(display.clean(panel)==spike::Submit::Accepted&&calls.mode==3&&calls.flags==0&&covers(calls.rect,panel));
 const spike::Rect erase{400,500,460,560};
 for(int cycle=0;cycle<30;++cycle){
  calls.pixels[520*1632+420]=0xffc02020u;
  const auto old=display.removalContent(erase);check(old==3);
  calls.pixels[520*1632+420]=0xffffffffu;
  check(display.cleanRemoval(erase,old)==spike::Submit::Accepted);
  check(calls.content==3&&calls.mode==3&&calls.flags==0&&covers(calls.rect,erase));
 }
 std::puts("UI_DAMAGE_PASS bounded_queue_coverage=true large_panel_partial=true consecutive_color_removal=30 vendor=FAKE optical=NOT_RUN");
#else
 std::puts("UI_DAMAGE_QUEUE_PASS bounded_queue_coverage=true vendor=NOT_RUN optical=NOT_RUN");
#endif
 return 0;
}catch(const std::exception& e){std::fprintf(stderr,"UI_DAMAGE_FAIL %s\n",e.what());return 1;}}
