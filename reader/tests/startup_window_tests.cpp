// SPDX-License-Identifier: AGPL-3.0-only
// V2 start-up window (E2 phase E finding): the vendor display start blocks the main thread for
// seconds on the Paper Pro. Before the first pump a helper is the sole main-progress publisher,
// capped by the start-up budget; past the cap nothing publishes; the V1 route is unchanged.
#include "ux_support.h"
#include "ux_display.h"
#include <QCoreApplication>
#include <QTemporaryDir>
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>
using Path=std::filesystem::path;
using namespace recovery::v2;
namespace lc=reader::lifecycle;
namespace {
constexpr spike::Ns Ms=1000000,S=1000*Ms;
struct SlowCalls final:spike::VendorCalls {
 spike::Ns block{};std::vector<std::uint32_t> buffer=std::vector<std::uint32_t>(std::size_t(6528/4)*2160);
 bool acquire(spike::Pixels& p)noexcept override{std::this_thread::sleep_for(std::chrono::nanoseconds(block));p.data=reinterpret_cast<unsigned char*>(buffer.data());p.width=1620;p.height=2160;p.format=4;p.stride=6528;return true;}
 void region(spike::Rect,int,int,int)noexcept override{}void events()noexcept override{}
};
struct OpenLease final:spike::RecoveryLease {
 bool valid()const noexcept override{return true;}spike::Ns deadline()const noexcept override{return spike::monotonic()+3600*S;}bool stopRequested()const noexcept override{return false;}
};
Timing timing(){
 Timing t;t.healthCadence=50*Ms;t.challengeLifetime=200*Ms;t.healthMaxAge=300*Ms;t.grantLifetime=6500*Ms;t.drainLead=4500*Ms;
 t.startupBudget=30*S;t.stopGrace=2*S;t.killGrace=2*S;t.proofWindow=10*S;t.sleepAllowance=50*S;t.clockSkew=10*Ms;t.workers=lc::DomainCount;
 for(std::uint32_t n=0;n<lc::DomainCount;++n)t.workerPolicies[n]={lc::kind(lc::Domain(n)),32,10*S};
 return t;
}
// Samples the channel while `run` executes; returns {publications, largest gap between publications}.
template<class F> std::pair<unsigned,spike::Ns> sample(progress::MainProgressChannel& channel,F&& run){
 std::atomic<bool> stop{false};unsigned count=0;spike::Ns gap=0;
 std::thread sampler([&]{std::uint64_t last=0;spike::Ns at=spike::monotonic();
  while(!stop.load()){auto r=channel.read();if(r.status==progress::ReadStatus::Ready&&r.health.mainSequence!=last){auto now=spike::monotonic();if(last&&now-at>gap)gap=now-at;last=r.health.mainSequence;at=now;++count;}std::this_thread::sleep_for(std::chrono::milliseconds(2));}
  auto now=spike::monotonic();if(now-at>gap)gap=now-at;});
 run();stop=true;sampler.join();return {count,gap};
}
unsigned checks{};
void check(bool b,const char* what){++checks;if(!b)throw std::runtime_error(what);}
}
int main(int argc,char** argv){QCoreApplication app(argc,argv);if(argc!=5)return 64;try{
 Path source=argv[1],parent=argv[2],build=argv[3],sys=argv[4];underlay::privateDirectory(parent);
 QTemporaryDir temp(QString::fromStdString((parent/"startup-window-XXXXXX").string()));check(temp.isValid(),"temp");
 Path data=temp.path().toStdString();auto application=stage(source,data/"stage",build,sys),root=data/"library";underlay::privateDirectory(root);
 ux::Shell shell(application,root,true,false,false);OpenLease lease;const auto t=timing();
 {// 1. A 1.5 s vendor start keeps main progress flowing (no gap near healthMaxAge) and succeeds.
  lc::Domains domains(t,11);progress::MainProgressChannel channel;lc::Foreground foreground(domains,channel);
  SlowCalls calls;calls.block=1500*Ms;spike::VendorDisplay vendor(calls);ux::Display display(vendor,shell,&foreground);bool ok=false;
  // Display::initialize also opens the real touch device (absent here); judge the vendor start itself.
  auto [count,gap]=sample(channel,[&]{ok=display.initialize(lease);});(void)ok;
  check(!vendor.surface().pixels.empty(),"slow vendor start succeeds");check(count>=30,"helper published throughout the start-up");check(gap<100*Ms,"no main-progress gap near healthMaxAge during start-up");
  // The helper must never touch the owner-only work ledgers: no fault anywhere, and the next
  // main-thread publication is clean (on the tablet a ledger fault became a session-ending error).
  check(!foreground.failed()&&channel.fault()==progress::Fault::None,"foreground and channel healthy after the window");
  for(std::uint32_t n=0;n<lc::DomainCount;++n)check(domains.snapshot(lc::Domain(n)).fault==progress::Fault::None,"work ledgers untouched by the helper");
  check(foreground.publish(false),"main thread publishes after reclaim");{auto r=channel.read();check(r.status==progress::ReadStatus::Ready&&!r.health.saveError,"health carries no save error");}
  std::printf("STARTUP_WINDOW covered block_ms=1500 publications=%u max_gap_ms=%llu\n",count,(unsigned long long)(gap/Ms));
 }
 {// 2. Past the budget nothing publishes: a hung start-up still goes stale (the owner then ends the session).
  lc::Domains domains(t,11);progress::MainProgressChannel channel;lc::Foreground foreground(domains,channel);
  SlowCalls calls;calls.block=1200*Ms;spike::VendorDisplay vendor(calls);ux::Display display(vendor,shell,&foreground,nullptr,300*Ms);
  auto [count,gap]=sample(channel,[&]{display.initialize(lease);});
  check(gap>=700*Ms,"publication stops at the start-up budget");
  std::printf("STARTUP_WINDOW capped budget_ms=300 block_ms=1200 publications=%u max_gap_ms=%llu\n",count,(unsigned long long)(gap/Ms));
 }
 {// 3. V1 route (no Foreground): nothing publishes, behaviour unchanged.
  progress::MainProgressChannel channel;SlowCalls calls;calls.block=200*Ms;spike::VendorDisplay vendor(calls);ux::Display display(vendor,shell);
  auto [count,gap]=sample(channel,[&]{display.initialize(lease);});(void)gap;check(count==0,"V1 route publishes nothing");
 }
 std::printf("STARTUP_WINDOW_PASS checks=%u vendor=FAKE_BLOCKING_ACQUIRE device_display=NOT_TOUCHED\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"STARTUP_WINDOW_FAIL %s checks=%u\n",e.what(),checks);return 1;}}
