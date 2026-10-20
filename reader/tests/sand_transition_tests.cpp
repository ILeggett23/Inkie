// SPDX-License-Identifier: AGPL-3.0-only
#include "sand_transition.h"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>
#include <string>
namespace {bool watching=false;std::size_t allocations=0;}
void* operator new(std::size_t n){if(watching)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
namespace {
unsigned checks=0;void check(bool c,const char* what){++checks;if(!c)throw std::runtime_error(what);}
constexpr int W=640,H=480;using Pixels=std::vector<std::uint32_t>;
sand::Policy policy(){sand::Policy p;p.quality=sand::Quality::Full;return p;}
sand::Request request(){return {{180,100,540,400},{0,0,W,H},{3,7},sand::Direction::LeftToRight,12345,false,280};}
bool inside(sand::Rect r,int x,int y){return x>=r.x0&&x<r.x1&&y>=r.y0&&y<r.y1;}
Pixels background(){Pixels p(W*H,0xffffffffu);for(int y=30;y<H;y+=25)for(int x=20;x<W-20;++x)if(x%37<24)p[y*W+x]=0xff000000u;for(int y=220;y<235;++y)for(int x=130;x<560;++x)p[y*W+x]=0xffffd900;return p;}
Pixels card(const Pixels& bg){auto p=bg;for(int y=100;y<400;++y)for(int x=180;x<540;++x){int dx=std::min(x-180,539-x),dy=std::min(y-100,399-y);if(dx<18&&dy<18&&(dx-18)*(dx-18)+(dy-18)*(dy-18)>18*18)continue;p[y*W+x]=(dx<2||dy<2)?0xff000000u:0xffffffffu;}for(int y=142;y<360;y+=42)for(int x=208;x<510;++x)if(x%31<24)for(int dy=0;dy<3;++dy)p[(y+dy)*W+x]=0xff000000u;return p;}
void ppm(const std::filesystem::path& path,const Pixels& p){std::ofstream o(path,std::ios::binary);o<<"P6\n"<<W<<' '<<H<<"\n255\n";for(auto v:p){char rgb[]{char(v>>16),char(v>>8),char(v)};o.write(rgb,3);}}
void paint(sand::Transition& t,Pixels& pixels,const Pixels& canonical,sand::Update u){pixels=canonical;check(t.paint(u.stamp,pixels,W,H,W,{0,0,W,H}),"paint accepted current source");}
void finish(sand::Transition& t,const Pixels& canonical){auto u=t.advance(request().identity,policy(),400);check(u&&!u.intermediate,"final cleanup issued");auto p=canonical;check(t.paint(u.stamp,p,W,H,W,{0,0,W,H}),"canonical cleanup composed");check(p==canonical,"final has exact normal pixels");check(t.acknowledge(u.stamp,u.damage)&&!t.pending(),"cleanup accepted and retired");}
std::uint64_t now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}
int main(int argc,char**argv){try{
 const auto bg=background(),final=card(bg);Pixels pixels,other;const auto original=request();
 for(auto direction:{sand::Direction::LeftToRight,sand::Direction::RightToLeft}){
  sand::Transition a,b;auto req=request();req.direction=direction;check(a.start(req,policy(),0)&&b.start(req,policy(),0),"both directions start");check(a.capture(bg,W,H,W,0)&&b.capture(bg,W,H,W,0),"cropped old scene captures");
  if(argc>1){std::filesystem::create_directories(argv[1]);ppm(std::filesystem::path(argv[1])/(direction==sand::Direction::LeftToRight?"ltr-0.ppm":"rtl-0.ppm"),bg);}
  unsigned n=0;for(auto ms:{0u,70u,140u,210u}){auto u=a.advance(req.identity,policy(),ms),v=b.advance(req.identity,policy(),ms);check(u&&v&&u.stage==++n,"four bounded stages");paint(a,pixels,final,u);paint(b,other,final,v);check(pixels==other,"seed reproduces pixels");
   for(int y=0;y<H;++y)for(int x=0;x<W;++x)if(!inside(u.damage,x,y))check(pixels[y*W+x]==final[y*W+x],"no escaped particles or background overwrite");
   if(ms==0){unsigned count=0;long long sx=0;for(int y=0;y<H;++y)for(int x=0;x<W;++x)if(pixels[y*W+x]!=bg[y*W+x]){++count;sx+=x;}check(count&&((direction==sand::Direction::LeftToRight)?sx/count<205:sx/count>515),"gust enters from correct edge");}
   if(argc>1)ppm(std::filesystem::path(argv[1])/((direction==sand::Direction::LeftToRight?"ltr-":"rtl-")+std::to_string(n)+".ppm"),pixels);
   check(!a.acknowledge(u.stamp,{0,0,1,1}),"partial acceptance retains debt");check(a.acknowledge(u.stamp,u.damage)&&b.acknowledge(v.stamp,v.damage),"full source acceptance retires current stage");check(!a.advance(req.identity,policy(),ms+1),"no repaint within same stage");
  }
  finish(a,final);finish(b,final);if(argc>1)ppm(std::filesystem::path(argv[1])/(direction==sand::Direction::LeftToRight?"ltr-5.ppm":"rtl-5.ppm"),final);
  check(a.metrics().accepted==5&&a.metrics().cacheBytes<=sand::Transition::MaxCacheBytes,"frame and memory budgets");
 }
 for(int failure=0;failure<11;++failure){sand::Transition t;auto p=policy();check(t.start(request(),p,0)&&t.capture(bg,W,H,W,0),"cancellation trial starts");auto u=t.advance(request().identity,p,0);paint(t,pixels,final,u);auto identity=request().identity;auto time=1ull;
  if(failure==0)p.contact=true;if(failure==1)p.selecting=true;if(failure==2)p.reducedMotion=true;if(failure==3)p.heavy=true;if(failure==4)p.power=true;if(failure==5)p.congested=true;if(failure==6)p.memoryTight=true;if(failure==7)++identity.page;if(failure==8)++identity.ui;if(failure==9)time=std::numeric_limits<std::uint64_t>::max();if(failure==10)t.cancel();
  auto clear=t.advance(identity,p,time);check(clear&&!clear.intermediate&&t.cleanupPending(),"interruption always reaches canonical cleanup");check(!t.acknowledge(u.stamp,u.damage),"stale intermediate ack cannot retire cleanup");paint(t,pixels,final,clear);check(pixels==final,"cancel uses current canonical scene");check(t.acknowledge(clear.stamp,clear.damage)&&!t.pending(),"cancel has no stranded stage");
 }
 {sand::Transition t;check(t.start(request(),policy(),0)&&t.capture(bg,W,H,W,0),"partial-cover trial starts");auto u=t.advance(request().identity,policy(),0);paint(t,pixels,final,u);auto left=u.damage;left.x1=(left.x0+left.x1)/2;auto right=u.damage;right.x0=left.x1;check(!t.acknowledge(u.stamp,left)&&t.acknowledge(u.stamp,right),"disjoint accepted bands jointly cover source");check(!t.advance(request().identity,policy(),1),"one outstanding stage only");auto skipped=t.advance(request().identity,policy(),210);check(skipped.stage==4&&t.metrics().skipped==2,"congestion skips stale intermediate stages");paint(t,pixels,final,skipped);t.acknowledge(skipped.stamp,skipped.damage);finish(t,final);}
 {sand::Transition t;auto p=policy();check(t.start(request(),p,0)&&t.capture(bg,W,H,W,0),"timeout trial starts");auto u=t.advance(request().identity,p,0);paint(t,pixels,final,u);auto clear=t.advance(request().identity,p,1000);check(clear&&!clear.intermediate&&clear.stamp!=u.stamp,"unaccepted overdue stage replaced by cleanup only");paint(t,pixels,final,clear);check(t.acknowledge(clear.stamp,clear.damage),"timeout final accepted");}
 for(unsigned mode=0;mode<4;++mode){sand::Transition t;auto p=policy();p.quality=sand::Quality(mode);const bool start=t.start(request(),p,0);check(start==(mode!=0),"quality mode enabled or immediate");if(!start)continue;t.capture(bg,W,H,W,0);unsigned frames=0;for(unsigned ms=0;ms<=300;ms+=10){auto u=t.advance(request().identity,p,ms);if(!u)continue;paint(t,pixels,final,u);t.acknowledge(u.stamp,u.damage);++frames;}check(frames==(mode==1?2u:mode==2?3u:5u)&&!t.pending(),"mode frame budgets including final");}
 {sand::Transition t;auto r=request();r.target={-1,0,10,10};check(!t.start(r,policy(),0),"offscreen geometry refuses");r=request();r.target={0,0,W,H};check(t.start(r,policy(),0),"edge clipping starts");check(t.region()==sand::Rect{0,0,W,H},"trail clipped at display edge");check(!t.capture(std::span<const std::uint32_t>(bg.data(),1),W,H,W,0)&&!t.pending(),"invalid surface fails before borrowing pixels");}
 // Warm a bounded cache; playback, acknowledgement and reuse allocate nothing.
 std::vector<std::uint64_t> renderNs,staticNs,generationNs;renderNs.reserve(200);staticNs.reserve(200);generationNs.reserve(200);sand::Transition stress;pixels=final;other=final;
 check(stress.start(request(),policy(),0)&&stress.capture(bg,W,H,W,0),"cache warmup");finish(stress,final);allocations=0;
 for(unsigned run=0;run<100;++run){auto r=request();r.dismiss=run%2;const auto& from=r.dismiss?final:bg;const auto& to=r.dismiss?bg:final;
  watching=true;check(stress.start(r,policy(),0),"stress starts");const auto g=now();check(stress.capture(from,W,H,W,0),"stress captures");const auto gen=now()-g;
  auto u=stress.advance(r.identity,policy(),0);pixels=to;const auto start=now();check(stress.paint(u.stamp,pixels,W,H,W,{0,0,W,H}),"stress paints");const auto cost=now()-start;stress.acknowledge(u.stamp,u.damage);stress.cancel();auto clear=stress.advance(r.identity,policy(),1);pixels=to;stress.paint(clear.stamp,pixels,W,H,W,{0,0,W,H});check(stress.acknowledge(clear.stamp,clear.damage)&&!stress.pending(),"stress clears");watching=false;
  const auto base=now();std::copy(to.begin(),to.end(),other.begin());staticNs.push_back(now()-base);renderNs.push_back(cost);generationNs.push_back(gen);check(pixels==to,"repeated close restores underlying page");
 }
 check(allocations==0,"warmed snapshot generation and playback allocate zero");
 auto report=[](const char* name,std::vector<std::uint64_t> samples){std::sort(samples.begin(),samples.end());std::cout<<name<<"_ns median="<<samples[samples.size()/2]<<" p95="<<samples[samples.size()*95/100]<<" worst="<<samples.back()<<'\n';};
 report("host_canonical_copy_baseline",staticNs);report("host_sand_composite_increment",renderNs);report("host_snapshot_and_field",generationNs);
 check(original.target==request().target&&original.identity==request().identity,"caller model untouched");
 std::cout<<"SAND_TRANSITION_PASS checks="<<checks<<" stress=100 warm_allocations="<<allocations<<" cache_bytes="<<stress.metrics().cacheBytes<<" max_frames=5 completion=SOURCE_ACCEPTANCE physical=NOT_RUN\n";return 0;
 }catch(const std::exception& e){std::cerr<<"SAND_TRANSITION_FAIL "<<e.what()<<'\n';return 1;}}
