// SPDX-License-Identifier: AGPL-3.0-only
#include "sand_transition.h"
#include "fixtures/sand_golden.h"
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
namespace {bool watching=false,rejectAllocation=false;std::size_t allocations=0;}
void* operator new(std::size_t n){if(rejectAllocation){rejectAllocation=false;throw std::bad_alloc();}if(watching)++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
namespace {
unsigned checks=0;void check(bool c,const char* what){++checks;if(!c)throw std::runtime_error(what);}
constexpr int W=640,H=480;using Pixels=std::vector<std::uint32_t>;
sand::Policy policy(){sand::Policy p;p.quality=sand::Quality::Full;return p;}
sand::Request request(){return {{180,100,540,400},{0,0,W,H},{3,7},sand::Direction::LeftToRight,12345,false,280};}
bool inside(sand::Rect r,int x,int y){return x>=r.x0&&x<r.x1&&y>=r.y0&&y<r.y1;}
Pixels background(){Pixels p(W*H,0xffffffffu);for(int y=30;y<H;y+=25)for(int x=20;x<W-20;++x)if(x%37<24)p[y*W+x]=0xff000000u;for(int y=220;y<235;++y)for(int x=130;x<560;++x)p[y*W+x]=0xffffd900;return p;}
Pixels card(const Pixels& bg){auto p=bg;for(int y=100;y<400;++y)for(int x=180;x<540;++x){int dx=std::min(x-180,539-x),dy=std::min(y-100,399-y);if(dx<18&&dy<18&&(dx-18)*(dx-18)+(dy-18)*(dy-18)>18*18)continue;p[y*W+x]=(dx<2||dy<2)?0xff000000u:0xffffffffu;}for(int y=142;y<360;y+=42)for(int x=208;x<510;++x)if(x%31<24)for(int dy=0;dy<3;++dy)p[(y+dy)*W+x]=0xff000000u;return p;}
std::uint64_t fingerprint(const Pixels& p){std::uint64_t h=1469598103934665603ULL;for(auto v:p)for(unsigned shift:{16u,8u,0u}){h^=(v>>shift)&255;h*=1099511628211ULL;}return h;}
void ppm(const std::filesystem::path& path,const Pixels& p){std::ofstream o(path,std::ios::binary);o<<"P6\n"<<W<<' '<<H<<"\n255\n";for(auto v:p){char rgb[]{char(v>>16),char(v>>8),char(v)};o.write(rgb,3);}}
void paint(sand::Transition& t,Pixels& pixels,const Pixels& canonical,sand::Update u){pixels=canonical;check(t.paint(u.stamp,pixels,W,H,W,{0,0,W,H}),"paint accepted current source");}
void finish(sand::Transition& t,const Pixels& canonical){auto u=t.advance(request().identity,policy(),400);check(u&&!u.intermediate,"final cleanup issued");auto p=canonical;check(t.paint(u.stamp,p,W,H,W,{0,0,W,H}),"canonical cleanup composed");check(p==canonical,"final has exact normal pixels");check(t.acknowledge(u.stamp,u.damage)&&!t.pending(),"cleanup accepted and retired");}
std::uint64_t now(){return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();}
}
int main(int argc,char**argv){try{
 const auto bg=background(),final=card(bg);Pixels pixels,other;const auto original=request();
 for(auto direction:{sand::Direction::LeftToRight,sand::Direction::RightToLeft}){
  sand::Transition a,b;auto req=request();req.direction=direction;check(a.start(req,policy(),0)&&b.start(req,policy(),0),"both directions start");check(a.capture(bg,W,H,W,0)&&b.capture(bg,W,H,W,0),"cropped old scene captures");check(a.buildSilhouette(final,W,H,W)&&b.buildSilhouette(final,W,H,W),"real component silhouette destinations");
  if(argc>1){std::filesystem::create_directories(argv[1]);ppm(std::filesystem::path(argv[1])/(direction==sand::Direction::LeftToRight?"ltr-0.ppm":"rtl-0.ppm"),bg);}
  const auto& golden=SandGolden[direction==sand::Direction::LeftToRight?0:1];check(fingerprint(bg)==golden[0],"initial golden snapshot");
  unsigned n=0;for(auto ms:{0u,70u,140u,210u}){auto u=a.advance(req.identity,policy(),ms),v=b.advance(req.identity,policy(),ms);check(u&&v&&u.stage==++n,"four bounded stages");paint(a,pixels,final,u);paint(b,other,final,v);check(pixels==other,"seed reproduces pixels");check(fingerprint(pixels)==golden[n],"directional stage matches reviewed golden pixels");
   for(int y=0;y<H;++y)for(int x=0;x<W;++x)if(!inside(u.damage,x,y))check(pixels[y*W+x]==final[y*W+x],"no escaped particles or background overwrite");
   if(ms==0){unsigned count=0;long long sx=0;for(int y=0;y<H;++y)for(int x=0;x<W;++x)if(pixels[y*W+x]!=bg[y*W+x]){++count;sx+=x;}check(count&&((direction==sand::Direction::LeftToRight)?sx/count<205:sx/count>515),"gust enters from correct edge");}
   if(argc>1)ppm(std::filesystem::path(argv[1])/((direction==sand::Direction::LeftToRight?"ltr-":"rtl-")+std::to_string(n)+".ppm"),pixels);
   check(!a.acknowledge(u.stamp,{0,0,1,1}),"partial acceptance retains debt");check(a.acknowledge(u.stamp,u.damage)&&b.acknowledge(v.stamp,v.damage),"full source acceptance retires current stage");check(!a.advance(req.identity,policy(),ms+1),"no repaint within same stage");
  }
  finish(a,final);finish(b,final);check(fingerprint(final)==golden[5],"canonical final matches reviewed golden");if(argc>1)ppm(std::filesystem::path(argv[1])/(direction==sand::Direction::LeftToRight?"ltr-5.ppm":"rtl-5.ppm"),final);
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
 // Partitioned compositor calls must produce the same source as one compose,
 // and an acknowledgement cannot cover pixels this stage never painted.
 {sand::Transition a,b;check(a.start(request(),policy(),0)&&b.start(request(),policy(),0),"partition trial starts");a.capture(bg,W,H,W,0);b.capture(bg,W,H,W,0);a.buildSilhouette(final,W,H,W);b.buildSilhouette(final,W,H,W);auto u=a.advance(request().identity,policy(),0),v=b.advance(request().identity,policy(),0);pixels=final;other=final;check(a.paint(u.stamp,pixels,W,H,W,{0,0,W,H}),"whole source paints");const auto r=v.damage;const int mid=(r.y0+r.y1)/2;const sand::Rect top{r.x0,r.y0,r.x1,mid},bottom{r.x0,mid,r.x1,r.y1};check(b.paint(v.stamp,other,W,H,W,top),"top source paints");check(!b.acknowledge(v.stamp,r),"unpainted bottom cannot retire source");check(!b.acknowledge(v.stamp,top),"top alone retains bottom debt");check(b.paint(v.stamp,other,W,H,W,bottom)&&b.acknowledge(v.stamp,bottom)&&pixels==other,"partition coverage and pixels match whole compose");a.acknowledge(u.stamp,u.damage);finish(a,final);finish(b,final);}
 {sand::Transition t;check(t.start(request(),policy(),100),"preparation can start");t.cancel();check(!t.pending()&&t.metrics().cacheBytes==0,"contact before capture creates no pixels or allocation");check(t.start(request(),policy(),100)&&t.capture(bg,W,H,W,100),"clock trial captures");auto u=t.advance(request().identity,policy(),99);check(u&&!u.intermediate&&t.metrics().reason==sand::Reason::Clock,"clock regression only cleans");paint(t,pixels,final,u);check(t.acknowledge(u.stamp,u.damage),"regressed clock cleanup retires");}
 {sand::Transition t;check(t.start(request(),policy(),0),"low-memory trial starts");rejectAllocation=true;const bool captured=t.capture(bg,W,H,W,0);check(!captured&&!rejectAllocation&&!t.pending()&&t.metrics().reason==sand::Reason::Memory,"actual allocation failure falls back without painting");check(t.start(request(),policy(),0),"uncaptured deadline starts");check(!t.advance(request().identity,policy(),1000)&&!t.pending(),"late preparation expires rather than animating stale UI");}
 {sand::Transition t;check(t.start(request(),policy(),0),"opening reset identity trial");auto id=request().identity;++id.page;check(t.prepareIdentity(id)&&t.identity()==id,"initial component reset aligns before capture");auto newer=id;++newer.ui;check(!t.prepareIdentity(newer),"different UI cannot retarget an opening");check(t.capture(bg,W,H,W,0)&&!t.prepareIdentity(id),"captured frames keep immutable identity");auto update=t.advance(request().identity,policy(),1);check(update&&!update.intermediate,"post-capture version change only cleans");paint(t,pixels,final,update);check(t.acknowledge(update.stamp,update.damage),"identity cleanup retires");}
 // Real UI snapshots exposed neutral antialias fringes after dark glyph cores
 // were withheld. Intermediate structure must not contain those grey outlines;
 // consolidation and the final source restore the exact normal typography.
 {auto aa=final;for(int y=190;y<203;++y)for(int x=220;x<225;++x)aa[y*W+x]=0xff999999u;sand::Transition t;check(t.start(request(),policy(),0)&&t.capture(bg,W,H,W,0)&&t.buildSilhouette(aa,W,H,W),"antialias fixture prepares");auto u=t.advance(request().identity,policy(),0);paint(t,pixels,aa,u);t.acknowledge(u.stamp,u.damage);u=t.advance(request().identity,policy(),140);paint(t,pixels,aa,u);for(int y=190;y<203;++y)for(int x=220;x<225;++x)check(pixels[y*W+x]!=0xff999999u,"no grey glyph fringe in structural stage");t.acknowledge(u.stamp,u.damage);u=t.advance(request().identity,policy(),210);paint(t,pixels,aa,u);for(int y=190;y<203;++y)for(int x=220;x<225;++x)check(pixels[y*W+x]==aa[y*W+x],"consolidation restores complete antialias pixels");t.acknowledge(u.stamp,u.damage);finish(t,aa);}
 // The wind's padding must not hold an unrelated neighboring control at its
 // old pixels. Flying black grains may cross it; its new canonical colour stays.
 {auto updated=final;for(int y=110;y<390;++y)for(int x=155;x<165;++x)updated[y*W+x]=0xffcc2255u;sand::Transition t;check(t.start(request(),policy(),0)&&t.capture(bg,W,H,W,0)&&t.buildSilhouette(updated,W,H,W),"neighbor update fixture prepares");auto u=t.advance(request().identity,policy(),0);paint(t,pixels,updated,u);for(int y=110;y<390;++y)for(int x=155;x<165;++x)check(pixels[y*W+x]==0xffcc2255u||pixels[y*W+x]==0xff000000u,"padding preserves current neighbor pixels");t.acknowledge(u.stamp,u.damage);finish(t,updated);}
 // Warm a bounded cache; profile every intermediate stage and final cleanup,
 // not just the cheapest initial gust. These are CPU-only host measurements.
 std::vector<std::uint64_t> renderNs,staticNs,generationNs;std::array<std::vector<std::uint64_t>,5> perStage;
 renderNs.reserve(500);staticNs.reserve(500);generationNs.reserve(100);for(auto& samples:perStage)samples.reserve(100);
 sand::Transition stress;pixels=final;other=final;
 check(stress.start(request(),policy(),0)&&stress.capture(bg,W,H,W,0),"cache warmup");finish(stress,final);allocations=0;
 for(unsigned run=0;run<100;++run){auto r=request();r.dismiss=run%2;const auto& from=r.dismiss?final:bg;const auto& to=r.dismiss?bg:final;
  watching=true;check(stress.start(r,policy(),0),"stress starts");const auto g=now();check(stress.capture(from,W,H,W,0),"stress captures");check(stress.buildSilhouette(to,W,H,W),"stress silhouette builds");generationNs.push_back(now()-g);
  unsigned stage=0;for(unsigned ms:{0u,70u,140u,210u,280u}){auto u=stress.advance(r.identity,policy(),ms);check(bool(u),"complete staged stress source");
   const auto base=now();std::copy(to.begin(),to.end(),other.begin());staticNs.push_back(now()-base);
   pixels=to;const auto begin=now();check(stress.paint(u.stamp,pixels,W,H,W,{0,0,W,H}),"stress paints");const auto cost=now()-begin;renderNs.push_back(cost);perStage[stage++].push_back(cost);
   check(stress.acknowledge(u.stamp,u.damage),"stress source accepted");
  }
  check(!stress.pending(),"stress has no stranded stage");watching=false;check(pixels==to,"repeated close restores underlying page");
 }
 check(allocations==0,"warmed snapshot generation and playback allocate zero");
 auto report=[](const char* name,std::vector<std::uint64_t> samples){std::sort(samples.begin(),samples.end());std::cout<<name<<"_ns median="<<samples[samples.size()/2]<<" p95="<<samples[samples.size()*95/100]<<" worst="<<samples.back()<<'\n';};
 report("host_canonical_copy_baseline",staticNs);report("host_sand_composite_increment_all_stages",renderNs);report("host_snapshot_and_field",generationNs);
 for(unsigned n=0;n<5;++n)report(("host_stage_"+std::to_string(n+1)).c_str(),perStage[n]);
 // Paper Pro-sized default lower sheet. This is still a host CPU model,
 // not a measurement of vendor submission or physical settlement.
 {constexpr int sw=1620,sh=2160,ss=1632;Pixels old(std::size_t(ss)*sh,0xffffffffu),next=old,scratch=old;
  for(int y=1160;y<sh;++y)for(int x=0;x<sw;++x){old[std::size_t(y)*ss+x]=(y%27<2)?0xff000000u:0xffffffffu;next[std::size_t(y)*ss+x]=(x<4||y<1164)?0xff000000u:0xffffffffu;}
  sand::Transition sheet;sand::Request r{{0,1160,sw,sh},{0,0,sw,sh},{1,2},sand::Direction::RightToLeft,55,false,280};std::vector<std::uint64_t> cost,copy,gen;
  for(unsigned run=0;run<20;++run){check(sheet.start(r,policy(),0),"lower sheet starts under cap");auto a=now();check(sheet.capture(old,sw,sh,ss,0),"lower sheet cropped capture");check(sheet.buildSilhouette(next,sw,sh,ss),"lower sheet silhouette builds");gen.push_back(now()-a);check(sheet.metrics().quality==sand::Quality::Minimal,"large sheet automatically minimizes sources");
   auto u=sheet.advance(r.identity,policy(),0);a=now();std::copy(next.begin(),next.end(),scratch.begin());copy.push_back(now()-a);a=now();check(sheet.paint(u.stamp,scratch,sw,sh,ss,{0,0,sw,sh}),"large sheet paints");cost.push_back(now()-a);check(sheet.acknowledge(u.stamp,u.damage),"large sheet accepts source");
   auto clear=sheet.advance(r.identity,policy(),300);scratch=next;check(clear&&!clear.intermediate&&sheet.paint(clear.stamp,scratch,sw,sh,ss,{0,0,sw,sh})&&sheet.acknowledge(clear.stamp,clear.damage)&&scratch==next,"lower sheet ends canonically");}
  report("host_lower_sheet_copy_baseline",copy);report("host_lower_sheet_composite_increment",cost);report("host_lower_sheet_snapshot_and_field",gen);check(sheet.metrics().cacheBytes<=sand::Transition::MaxCacheBytes,"lower sheet hard cache cap");
  r.target={0,0,sw,sh};check(!sheet.start(r,policy(),0),"full scene over cache cap appears immediately");}
 check(original.target==request().target&&original.identity==request().identity,"caller model untouched");
 std::cout<<"SAND_TRANSITION_PASS checks="<<checks<<" stress=100 warm_allocations="<<allocations<<" cache_bytes="<<stress.metrics().cacheBytes<<" max_frames=5 completion=SOURCE_ACCEPTANCE physical=NOT_RUN\n";return 0;
 }catch(const std::exception& e){std::cerr<<"SAND_TRANSITION_FAIL "<<e.what()<<'\n';return 1;}}
