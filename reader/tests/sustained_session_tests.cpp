// SPDX-License-Identifier: AGPL-3.0-only
// D1 (approved 2026-09-30): sustained V2 admission in the protected input path.
// Drives the real consumeEvents()/Engine. A lease without a sustained policy must
// keep the historical V1 limits exactly; a V2 sustained lease writes far beyond
// them under fixed per-window quotas with fixed Engine memory.
#include "session.h"
#include <linux/input.h>
#include <cstdio>
#include <stdexcept>
#include <vector>
using namespace spike;
namespace {
unsigned checks{},cases{};
void check(bool b,const char* what){++checks;if(!b)throw std::runtime_error(what);}
constexpr Ns Ms=1000000,S=1000*Ms;
// consumeEvents() checks the lease deadline against the real monotonic clock.
Ns far(){return monotonic()+3600*S;}
struct TestLease final:RecoveryLease {
 SustainedPolicy policy{};Ns end{};bool live{true};
 bool valid()const noexcept override{return live;}
 Ns deadline()const noexcept override{return end;}
 bool stopRequested()const noexcept override{return !live;}
 SustainedPolicy sustained()const noexcept override{return policy;}
};
input_event ev(unsigned short type,unsigned short code,int value){input_event e{};e.type=type;e.code=code;e.value=value;return e;}
struct Rig {
 std::vector<std::uint32_t> pixels=std::vector<std::uint32_t>(1632u*2160u,0xffffffffu);
 FakeDisplay display;Engine engine{{{0,1619},{0,2159},1620,2160,0,false},{pixels,1620,2160,1632},display};
 FrameAssembler assembler;Scheduler scheduler;SessionStats stats;Ns now{S};unsigned retired{};
 bool frame(TestLease& lease,std::initializer_list<input_event> events){std::vector<input_event> v(events);v.push_back(ev(EV_SYN,SYN_REPORT,0));now+=Ms;return consumeEvents(v,assembler,engine,scheduler,stats,now,&lease);}
 // One pen contact: down, `moves` moves, up. Returns false at the first refusal.
 bool contact(TestLease& lease,unsigned moves,bool sustained){
  int x=200+int(stats.contacts%50)*20,y=300+int((stats.contacts/50)%80)*20;
  if(!frame(lease,{ev(EV_KEY,BTN_TOOL_PEN,1),ev(EV_KEY,BTN_TOOL_RUBBER,0),ev(EV_ABS,ABS_X,x),ev(EV_ABS,ABS_Y,y),ev(EV_KEY,BTN_TOUCH,1)}))return false;
  for(unsigned m=0;m<moves;++m)if(!frame(lease,{ev(EV_ABS,ABS_X,x+int(m%10))}))return false;
  if(!frame(lease,{ev(EV_KEY,BTN_TOUCH,0)}))return false;
  if(engine.flush()!=Submit::Accepted)return false;
  if(sustained&&engine.retirable()){engine.retireSubmitted();++retired;}
  return true;
 }
};
struct WakeObserver final:SessionDisplay {unsigned wakes{};void wakeInput(const Sample& s)noexcept override{if(s.phase==Phase::Down)++wakes;}Submit submit(Rect)noexcept override{return Submit::Accepted;}bool initialize(const RecoveryLease&)noexcept override{return false;}Surface surface()const noexcept override{return {};}bool initialPage()noexcept override{return true;}void pump()noexcept override{}void shutdown()noexcept override{}};
void wakeGate(){++cases;Rig r;TestLease lease;lease.end=far();InputGate gate;gate.open=false;WakeObserver observer;auto frame=[&](std::initializer_list<input_event> events){std::vector<input_event> v(events);v.push_back(ev(EV_SYN,SYN_REPORT,0));r.now+=Ms;return consumeEvents(v,r.assembler,r.engine,r.scheduler,r.stats,r.now,&lease,nullptr,&gate,&observer);};check(frame({ev(EV_KEY,BTN_TOOL_PEN,1),ev(EV_ABS,ABS_X,300),ev(EV_ABS,ABS_Y,400)}),"hover admitted to assembler");check(observer.wakes==0&&r.stats.samples==0,"hover never wakes or writes");check(frame({ev(EV_KEY,BTN_TOUCH,1)}),"sleeping down consumed");check(observer.wakes==1&&gate.blocked&&r.engine.points().empty(),"validated wake edge has no ink");gate.open=true;check(frame({ev(EV_ABS,ABS_X,330)})&&frame({ev(EV_KEY,BTN_TOUCH,0)}),"waking contact drains after gate reopens");check(!gate.blocked&&observer.wakes==1&&r.stats.contacts==0&&r.engine.points().empty(),"wake through release never counts a stroke");check(frame({ev(EV_KEY,BTN_TOUCH,1)})&&frame({ev(EV_KEY,BTN_TOUCH,0)}),"fresh contact after wake writes");check(r.stats.contacts==1&&r.stats.released==1&&!r.stats.failed&&observer.wakes==1,"fresh contact preserves admission");std::puts("WAKE_INPUT_GATE_PASS validated_tip_only=true wake_contact_consumed=true next_contact_accepted=true physical=NOT_RUN");}
void v1ContactCap(){++cases;
 Rig r;TestLease lease;lease.end=far();
 for(unsigned i=0;i<5;++i)check(r.contact(lease,2,false),"V1 accepts five contacts");
 check(!r.contact(lease,2,false)&&r.stats.failed&&r.stats.cancelled&&r.stats.contacts==5,"V1 sixth contact still fails closed");
}
void v1SampleCap(){++cases;
 Rig r;TestLease lease;lease.end=far();
 check(!r.contact(lease,17000,false)&&r.stats.failed&&r.stats.samples==16384,"V1 per-session sample cap unchanged");
}
void sustainedContacts(){++cases;
 Rig r;TestLease lease;lease.end=far();lease.policy={S,4,1000};check(lease.policy.enabled(),"policy enabled");
 for(unsigned i=0;i<40;++i){
  check(r.contact(lease,3,true),"sustained contact within window quota");
  if(i%4==3)r.now+=S; // next fixed window
 }
 check(r.stats.contacts==40&&!r.stats.failed,"sustained writing far beyond the V1 five-contact cap");
}
void windowQuota(){++cases;
 Rig r;TestLease lease;lease.end=far();lease.policy={S,4,1000};
 for(unsigned i=0;i<4;++i)check(r.contact(lease,3,true),"four contacts in one window");
 check(!r.contact(lease,3,true)&&r.stats.failed&&r.stats.cancelled&&r.stats.windowContacts==4,"fifth contact in the same window refused");
 Rig q;TestLease samples;samples.end=far();samples.policy={S,10,50};check(samples.policy.enabled(),"valid sample-quota policy");
 check(!q.contact(samples,60,true)&&q.stats.failed&&q.stats.windowSamples==50,"window sample quota refused");
}
void fixedMemory(){++cases;
 Rig r;TestLease lease;lease.end=far();lease.policy={S,50,20000};
 for(unsigned i=0;i<60;++i){check(r.contact(lease,998,true),"long sustained contact");if(i%10==9)r.now+=S;}
 check(r.stats.samples==60000&&r.stats.samples>16384&&!r.stats.failed,"samples beyond the V1 per-session cap");
 check(r.retired>0&&r.engine.points().size()<16384,"Engine memory stays fixed by retirement");
}
void retireContinuity(){++cases;
 Rig r;TestLease lease;lease.end=far();lease.policy={S,50,20000};
 check(r.contact(lease,9000,true),"fill past half capacity");check(r.retired==1&&r.engine.points().empty(),"retired after full submission");
 // An older receipt than the last retired point is still refused.
 input_event down[]={ev(EV_KEY,BTN_TOOL_PEN,1),ev(EV_ABS,ABS_X,300),ev(EV_ABS,ABS_Y,300),ev(EV_KEY,BTN_TOUCH,1),ev(EV_SYN,SYN_REPORT,0)};
 check(!consumeEvents(down,r.assembler,r.engine,r.scheduler,r.stats,S/2,&lease),"out-of-order input refused after retirement");
 check(r.engine.failed()&&r.stats.failed,"refusal is terminal");
}
void liveDeadline(){++cases;
 Rig r;TestLease lease;lease.policy={S,4,1000};lease.end=far();
 check(r.contact(lease,1,true),"contact before the live deadline");
 lease.end=monotonic(); // the live grant has expired
 check(!r.contact(lease,1,true)&&r.stats.failed&&r.stats.cancelled,"expired grant refuses the next sample");
 Rig q;TestLease revoked;revoked.policy={S,4,1000};revoked.end=far();check(q.contact(revoked,1,true),"admitted");
 revoked.live=false;check(!q.contact(revoked,1,true),"revoked lease refuses immediately");
}
void policyValidity(){++cases;
 check(!SustainedPolicy{}.enabled(),"V1 default has no policy");check(!SustainedPolicy{S,0,10}.enabled()&&!SustainedPolicy{0,1,10}.enabled()&&!SustainedPolicy{S,10,5}.enabled(),"incomplete or inconsistent policy disabled");
 TestLease v1;check(!v1.sustained().enabled(),"RecoveryLease default is V1");
}
}
int main(){
 try{wakeGate();v1ContactCap();v1SampleCap();sustainedContacts();windowQuota();fixedMemory();retireContinuity();liveDeadline();policyValidity();
  std::printf("SUSTAINED_SESSION_PASS cases=%u checks=%u v1_limits=UNCHANGED sustained=V2_ONLY physical=NOT_RUN\n",cases,checks);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"SUSTAINED_SESSION_FAIL %s checks=%u\n",e.what(),checks);return 1;}
}
