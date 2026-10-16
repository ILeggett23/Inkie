// SPDX-License-Identifier: AGPL-3.0-only
// Session power (kernel autosleep is enabled on the target): timed wake lock held
// only while admitted, power key / cover / idle ending the session, device lookup
// by name. Uses a private simulated /sys/power, sysfs and FIFO input devices.
#include "session_power_v2.h"
#include <linux/input.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
using namespace recovery::v2::sessionpower;
namespace fs=std::filesystem;
namespace {
unsigned checks{},cases{};
void check(bool b,const char* what){++checks;if(!b)throw std::runtime_error(what);}
constexpr Ns Ms=1000000,S=1000*Ms;
std::string slurp(const fs::path& p){std::ifstream in(p);std::stringstream s;s<<in.rdbuf();return s.str();}
struct Dir {fs::path path;Dir(){char t[]="/tmp/session-power.XXXXXX";auto p=mkdtemp(t);if(!p)throw std::runtime_error("temp");path=p;}~Dir(){std::error_code e;fs::remove_all(path,e);}};
// Simulated kernel attribute files; the kernel appends semantics are not needed here,
// only that exactly one bounded record is written per call.
void attributes(const fs::path& d){fs::create_directories(d);for(auto f:{"wake_lock","wake_unlock"}){std::ofstream(d/f).close();}}
struct Device {fs::path path;int writer{-1};explicit Device(fs::path p):path(std::move(p)){if(mkfifo(path.c_str(),0600))throw std::runtime_error("fifo");writer=open(path.c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC);if(writer<0)throw std::runtime_error("fifo open");}
 ~Device(){if(writer>=0)close(writer);}
 void event(unsigned short type,unsigned short code,int value){input_event e{};e.type=type;e.code=code;e.value=value;if(write(writer,&e,sizeof e)!=ssize_t(sizeof e))throw std::runtime_error("event");}
 void lose(){close(writer);writer=-1;}};
void wakeLock(){++cases;
 Dir d;attributes(d.path);
 {WakeLock lock(d.path,"inkbound-reader");check(!lock.held(),"not held initially");
  check(lock.hold(5*S)&&lock.held(),"hold writes");check(slurp(d.path/"wake_lock")=="inkbound-reader 5000000000","exact name and timeout");
  check(lock.release()&&!lock.held()&&slurp(d.path/"wake_unlock")=="inkbound-reader","release writes name");}
 {std::ofstream(d.path/"wake_unlock",std::ios::trunc).close();{WakeLock lock(d.path,"inkbound-reader");check(lock.hold(S),"held");}check(slurp(d.path/"wake_unlock")=="inkbound-reader","destructor releases");}
 {WakeLock bad(d.path,"Bad Name");check(bad.failed()&&!bad.hold(S),"unsafe name refused");}
 {WakeLock zero(d.path,"inkbound-reader");check(!zero.hold(0),"zero timeout refused (no untimed lock)");}
 {Dir e;WakeLock missing(e.path,"inkbound-reader");check(!missing.hold(S)&&missing.failed(),"missing attribute fails, never created");check(!fs::exists(e.path/"wake_lock"),"attribute not created");}
 {Dir e;fs::create_directories(e.path);fs::create_symlink(d.path/"wake_lock",e.path/"wake_lock");WakeLock link(e.path,"inkbound-reader");check(!link.hold(S),"symlinked attribute refused");}
}
void lookup(){++cases;
 Dir d;auto cls=d.path/"sys/class/input";
 auto add=[&](const char* ev,const char* name){fs::create_directories(cls/ev/"device");std::ofstream(cls/ev/"device/name")<<name<<"\n";};
 add("event0","30370000.snvs:snvs-powerkey");add("event1","Hall effect sensors");add("event2","Elan marker input");add("input0","not an event node");
 check(findInput(d.path/"sys",d.path/"dev","30370000.snvs:snvs-powerkey")==d.path/"dev/event0","power key by name");
 check(findInput(d.path/"sys",d.path/"dev","Hall effect sensors")==d.path/"dev/event1","cover by name");
 check(findInput(d.path/"sys",d.path/"dev","missing").empty(),"missing name refused");
 add("event7","Hall effect sensors");check(findInput(d.path/"sys",d.path/"dev","Hall effect sensors").empty(),"ambiguous name refused");
 check(findInput(d.path/"sys",d.path/"dev","").empty(),"empty name refused");
}
void signals(){++cases;
 Dir d;
 {Device key(d.path/"key");EndSignals s(key.path,{},60*S,S);check(s.valid()&&!s.endRequested(2*S),"quiet key");
  key.event(EV_KEY,KEY_VOLUMEUP,1);key.event(EV_KEY,KEY_POWER,0);check(!s.endRequested(3*S),"other keys and release ignored");
  key.event(EV_KEY,KEY_POWER,1);check(s.endRequested(4*S),"power key press ends the session");check(s.endRequested(5*S),"sticky");}
 {Device key(d.path/"key2"),cover(d.path/"cover");EndSignals s(key.path,cover.path,60*S,S);check(s.valid()&&!s.endRequested(2*S),"quiet");
  cover.event(EV_SW,SW_LID,0);check(!s.endRequested(3*S),"cover open ignored");cover.event(EV_SW,SW_LID,1);check(s.endRequested(4*S),"cover close ends the session");}
 {Device key(d.path/"key3");EndSignals s(key.path,{},10*S,S);s.activity(5*S);check(!s.endRequested(14*S),"activity defers idle");check(s.endRequested(15*S),"idle timeout ends the session");}
 {Device key(d.path/"key4");EndSignals s(key.path,{},60*S,S);key.lose();check(s.endRequested(2*S),"lost power-key device ends the session");}
 {EndSignals s(d.path/"absent",{},60*S,S);check(!s.valid()&&s.endRequested(2*S),"missing power key is never a silent session");}
 {Device key(d.path/"key5");EndSignals s(key.path,d.path/"absent-cover",60*S,S);check(!s.valid(),"configured cover must open");}
}
void session(){++cases;
 Dir d;attributes(d.path/"power");Device key(d.path/"key");
 SessionPower p(d.path/"power","inkbound-reader",4*S,key.path,{},S,nullptr);check(p.valid(),"valid");
 check(p.tick(S,false)&&!p.lock().held(),"no lock before admission");
 check(p.tick(2*S,true)&&p.lock().held(),"lock taken when admitted");
 std::ofstream(d.path/"power/wake_lock",std::ios::trunc).close();check(p.tick(2*S+S/2,true)&&slurp(d.path/"power/wake_lock").empty(),"no renewal before timeout/4");
 check(p.tick(3*S+1,true)&&slurp(d.path/"power/wake_lock")=="inkbound-reader 4000000000","renewed after timeout/4");
 check(p.tick(4*S,false)&&!p.lock().held()&&slurp(d.path/"power/wake_unlock")=="inkbound-reader","loss of admission releases");
 Dir e;Device k2(e.path/"key");SessionPower broken(e.path/"no-power","inkbound-reader",4*S,k2.path,{},S,nullptr);check(!broken.tick(S,true),"unwritable lock reports failure (session ends)");
}
}
// D2 sleep: cover close / idle -> SleepPending -> sleep() releases the lock; only power or cover-open wake; a press
// within WakeGrace of a detected resume is the wake itself; a press while awake still ends the session.
Ns offsetValue=0;Ns offset(void* c)noexcept{return *static_cast<Ns*>(c);}
void sleepCycle(){++cases;
 Dir d;attributes(d.path/"power");Device key(d.path/"key"),cover(d.path/"cover");
 auto unlocks=[&]{return slurp(d.path/"power/wake_unlock");};auto clear=[&]{std::ofstream(d.path/"power/wake_unlock",std::ios::trunc).close();std::ofstream(d.path/"power/wake_lock",std::ios::trunc).close();};
 offsetValue=0;SessionPower p(d.path/"power","inkbound-reader",4*S,key.path,cover.path,S,offset,&offsetValue);check(p.valid()&&p.coverAvailable(),"valid with cover");
 check(p.tick(S,true)&&p.lock().held(),"held when admitted");
 cover.event(EV_SW,SW_LID,1);check(p.tick(2*S,true)&&p.state()==PowerState::Awake,"cover ignored until configured");
 p.configure({true,0},2*S);check(p.state()==PowerState::SleepPending,"remembered close asks for sleep when configured");cover.event(EV_SW,SW_LID,0);check(p.tick(2*S+Ms,true)&&p.state()==PowerState::Awake,"cover open while awake: nothing");
 cover.event(EV_SW,SW_LID,1);check(p.tick(3*S,true)&&p.state()==PowerState::SleepPending&&p.reason()==SleepReason::Cover&&p.lock().held(),"cover close: pending, lock kept for the quiesce");
 clear();check(p.sleep(3*S)&&p.state()==PowerState::Sleeping&&!p.lock().held()&&unlocks()=="inkbound-reader","sleep releases the lock");
 check(!p.sleep(3*S),"sleep only from pending");
 check(p.tick(4*S,true)&&!p.lock().held()&&slurp(d.path/"power/wake_lock").empty(),"asleep: never renewed");
 p.activity(5*S);check(p.tick(5*S,true)&&p.state()==PowerState::Sleeping,"pen/touch never wake");
 offsetValue=600*S;cover.event(EV_SW,SW_LID,0);check(p.tick(6*S,true)&&p.state()==PowerState::Awake&&p.lock().held()&&p.takeWake()&&!p.takeWake()&&p.takeResume(),"cover open wakes, lock retaken, resume seen");
 check(!p.endRequested(6*S),"waking is not an end");
 cover.event(EV_SW,SW_LID,1);check(p.tick(7*S,true)&&p.state()==PowerState::SleepPending,"close again");
 key.event(EV_KEY,KEY_POWER,1);check(p.tick(7*S+Ms,true)&&p.state()==PowerState::Awake&&!p.endRequested(7*S+Ms),"power while pending cancels the sleep, no end");
 cover.event(EV_SW,SW_LID,1);p.tick(8*S,true);check(p.sleep(8*S),"asleep again");
 offsetValue=3600*S;key.event(EV_KEY,KEY_POWER,1);check(p.tick(9*S,true)&&p.state()==PowerState::Awake&&p.lock().held()&&!p.endRequested(9*S),"power press while asleep wakes, never ends");
 key.event(EV_KEY,KEY_POWER,1);check(!p.endRequested(9*S+S/2)&&p.swallowedPresses()==1,"second press inside the wake grace is swallowed");
 key.event(EV_KEY,KEY_POWER,1);check(p.endRequested(9*S+3*S),"a press while awake after the grace ends the session");
 check(p.wakes()==4&&p.sleeps()==2,"counters: remembered pending close reopened, cover open, power while pending, power while asleep");
 }

int lidQuery(int,void* context)noexcept{return *static_cast<int*>(context);}
void coverStateRecovery(){++cases;
 {Dir d;attributes(d.path/"power");Device key(d.path/"key"),cover(d.path/"cover");SessionPower p(d.path/"power","inkbound-reader",4*S,key.path,cover.path,S,nullptr);check(p.tick(S,true),"early-cover hold");cover.event(EV_SW,SW_LID,1);check(p.tick(2*S,true)&&p.state()==PowerState::Awake&&p.lidClosed(),"disabled cover close is retained");p.configure({true,0},2*S);check(p.tick(3*S,true)&&p.state()==PowerState::SleepPending,"close before configuration is not lost");check(p.sleep(3*S)&&!p.lock().held(),"early-close sleep releases lock");}
 {Dir d;attributes(d.path/"power");Device key(d.path/"key"),cover(d.path/"cover");int lid=1;Ns off=0;SessionPower p(d.path/"power","inkbound-reader",4*S,key.path,cover.path,S,offset,&off,lidQuery,&lid);check(p.lidKnown()&&p.lidClosed(),"initial closed native snapshot");p.configure({true,0},S);check(p.tick(S,true)&&p.state()==PowerState::SleepPending&&p.lock().held(),"initial closed cover quiesces");check(p.sleep(S),"initial-closed sleep");off=600*S;check(p.tick(2*S,true)&&p.state()==PowerState::Sleeping&&!p.lock().held()&&p.takeResume(),"closed resume never renews reader lock");lid=0;off=1200*S;check(p.tick(3*S,true)&&p.state()==PowerState::Awake&&p.lock().held()&&p.takeWake(),"verified open snapshot wakes without an edge");}
 {Dir d;attributes(d.path/"power");Device key(d.path/"key"),cover(d.path/"cover");int lid=0;SessionPower p(d.path/"power","inkbound-reader",4*S,key.path,cover.path,S,nullptr,nullptr,lidQuery,&lid);p.configure({true,0},S);check(p.tick(S,true)&&p.state()==PowerState::Awake,"open baseline");cover.event(EV_SYN,SYN_DROPPED,0);cover.event(EV_SW,SW_LID,0);lid=1;cover.event(EV_SYN,SYN_REPORT,0);check(p.tick(2*S,true)&&p.state()==PowerState::SleepPending&&p.lidDrops()==1,"dropped close recovered from snapshot");check(p.sleep(2*S),"recovered close sleeps");lid=-1;cover.event(EV_SYN,SYN_DROPPED,0);cover.event(EV_SW,SW_LID,0);cover.event(EV_SYN,SYN_REPORT,0);check(p.tick(3*S,true)&&p.state()==PowerState::Sleeping&&!p.lock().held()&&p.lidQueryFailures()==1&&!p.endRequested(3*S),"failed query never fabricates open or session end");lid=0;cover.event(EV_SYN,SYN_DROPPED,0);cover.event(EV_SW,SW_LID,1);cover.event(EV_SYN,SYN_REPORT,0);check(p.tick(4*S,true)&&p.state()==PowerState::Awake&&p.lock().held(),"dropped open ignores stale closed packet");cover.event(EV_SW,SW_PEN_INSERTED,1);check(p.tick(5*S,true)&&p.state()==PowerState::Awake,"other Hall sensor switch is not the lid");}
}
void backgroundResumeKeepsIdleSleep(){++cases;Dir d;attributes(d.path/"power");Device key(d.path/"key"),cover(d.path/"cover");int lid=0;Ns off=0;SessionPower p(d.path/"power","inkbound-reader",4*S,key.path,cover.path,S,offset,&off,lidQuery,&lid);p.configure({true,S},S);check(p.tick(3*S,true)&&p.state()==PowerState::SleepPending,"idle requests sleep with open cover");check(p.sleep(3*S)&&!p.lock().held(),"idle release");off=600*S;check(p.tick(4*S,true)&&p.state()==PowerState::Sleeping&&!p.lock().held()&&!p.takeWake(),"background resume and unchanged open lid do not wake idle reader");cover.event(EV_SW,SW_LID,1);check(p.tick(5*S,true)&&p.state()==PowerState::Sleeping,"closing already sleeping lid keeps sleep");lid=0;off=1200*S;check(p.tick(6*S,true)&&p.state()==PowerState::Awake&&p.lock().held(),"actual closed-to-open state wakes after idle sleep");}
void idleSleep(){++cases;
 Dir d;attributes(d.path/"power");Device key(d.path/"key");offsetValue=0;
 SessionPower p(d.path/"power","inkbound-reader",4*S,key.path,{},S,offset,&offsetValue);check(!p.coverAvailable(),"no cover device");
 p.configure({true,60*S},S);check(p.tick(S,true)&&p.state()==PowerState::Awake,"cover setting without a device is ignored");
 p.activity(30*S);check(p.tick(89*S,true)&&p.state()==PowerState::Awake,"activity defers idle");
 check(p.tick(91*S,true)&&p.state()==PowerState::SleepPending&&p.reason()==SleepReason::Idle,"idle timeout asks for sleep");
 p.activity(92*S);check(p.tick(92*S,true)&&p.state()==PowerState::Awake,"pen/touch cancels a pending idle sleep");
 p.configure({false,0},93*S);check(p.tick(1000*S,true)&&p.state()==PowerState::Awake,"idle never");
 p.configure({false,60*S},1000*S);check(p.tick(1061*S,true)&&p.state()==PowerState::SleepPending,"re-enabled idle");
 p.configure({false,0},1061*S);check(p.state()==PowerState::Awake,"disabling idle cancels its pending sleep");
 check(p.tick(1062*S,false)&&!p.lock().held(),"loss of admission releases");
}
void wakeLockList(){++cases;
 check(listedWakeLock("inkbound-reader","inkbound-reader"),"single");check(listedWakeLock("a inkbound-reader b\n","inkbound-reader"),"among others");
 check(!listedWakeLock("inkbound-reader-old other","inkbound-reader"),"prefix is not a match");check(!listedWakeLock("","inkbound-reader"),"empty");check(!listedWakeLock("x","")," empty name");
}
void ownerPress(){++cases;
 Dir d;Device key(d.path/"key");EndSignals s(key.path,{},~Ns(0)/4,S);
 check(s.takePress()==EndSignals::Press::None,"quiet");key.event(EV_KEY,KEY_POWER,0);key.event(EV_KEY,KEY_VOLUMEUP,1);check(s.takePress()==EndSignals::Press::None,"release and other keys");
 key.event(EV_KEY,KEY_POWER,1);check(s.takePress()==EndSignals::Press::Pressed&&s.takePress()==EndSignals::Press::None,"press then not sticky");
 key.lose();check(s.takePress()==EndSignals::Press::Lost,"lost device");
}
void ownerBackstop(){++cases;using E=OwnerPresses::Event;
 {OwnerPresses o(0);check(o.step(S,0,false,true)==E::None,"quiet");check(o.step(2*S,0,true,true)==E::Pressed&&o.pressedAt()==2*S,"awake press is an end");check(o.step(3*S,0,true,true)==E::None&&o.pressedAt()==2*S,"end stays pending (backstop runs)");}
 {OwnerPresses o(0);check(o.step(2*S,0,true,false)==E::PressedAsleep,"press with the lock released may be a wake");check(o.step(2*S+20*Ms,0,false,false)==E::None,"still pending");
  check(o.step(2*S+40*Ms,0,false,true)==E::WakeConfirmed&&!o.pressedAt(),"reader takes its lock again: wake confirmed, backstop cancelled");check(o.step(3*S,0,true,true)==E::Pressed,"a later awake press ends");}
 {OwnerPresses o(0);check(o.step(2*S,0,true,false)==E::PressedAsleep,"asleep press");for(Ns t=2*S;t<8*S;t+=20*Ms)check(o.step(t,0,false,false)==E::None,"frozen reader never relocks");check(o.pressedAt()==2*S,"backstop still pending for a frozen reader");}
 {OwnerPresses o(100*Ms);check(o.step(5*S,3600*S,false,true)==E::None&&o.resumed(5*S),"resume detected");check(o.step(5*S+S/2,3600*S,true,true)==E::PressedAsleep,"press just after a resume may be the wake");check(o.step(5*S+S/2+20*Ms,3600*S,false,true)==E::WakeConfirmed,"lock held: confirmed");
  check(o.step(8*S,3600*S,true,true)==E::Pressed&&!o.resumed(8*S),"after the grace an awake press ends");}
 {OwnerPresses o(0);check(o.step(S,10*Ms,false,true)==E::None&&!o.resumed(S),"clock jitter below the threshold is not a resume");}
}
// The display rail: shortened for each sleep, restored on wake and when the session ends; a missing attribute never
// blocks the sleep itself.
void displayRail(){++cases;
 Dir d;attributes(d.path/"power");Device key(d.path/"key"),cover(d.path/"cover");std::ofstream(d.path/"vpdd_length")<<"30000\n";
 {DisplayRail rail(d.path/"vpdd_length");offsetValue=0;
  {SessionPower p(d.path/"power","inkbound-reader",4*S,key.path,cover.path,S,offset,&offsetValue);p.setDisplayRail(&rail);p.configure({true,0},S);check(p.tick(S,true),"held");
   cover.event(EV_SW,SW_LID,1);p.tick(2*S,true);check(p.sleep(2*S)&&slurp(d.path/"vpdd_length")=="1000"&&rail.shortened()&&p.railShortenings()==1,"sleep shortens the rail");
   offsetValue=60*S;cover.event(EV_SW,SW_LID,0);check(p.tick(3*S,true)&&p.state()==PowerState::Awake&&slurp(d.path/"vpdd_length")=="30000"&&!rail.shortened(),"wake restores the saved value");
   cover.event(EV_SW,SW_LID,1);p.tick(4*S,true);check(p.sleep(4*S)&&slurp(d.path/"vpdd_length")=="1000","second sleep shortens again");}
  check(slurp(d.path/"vpdd_length")=="30000"&&!rail.shortened(),"ending the session while asleep restores the rail");}
 {DisplayRail missing(d.path/"absent");Device k2(d.path/"key2"),c2(d.path/"cover2");SessionPower p(d.path/"power","inkbound-reader",4*S,k2.path,c2.path,S,offset,&offsetValue);p.setDisplayRail(&missing);p.configure({true,0},S);p.tick(S,true);
  c2.event(EV_SW,SW_LID,1);p.tick(2*S,true);check(p.sleep(2*S)&&p.state()==PowerState::Sleeping&&!p.lock().held()&&p.railFailures()==1&&!fs::exists(d.path/"absent"),"a missing rail attribute never blocks sleep and is never created");}
 {std::ofstream(d.path/"bad",std::ios::trunc)<<"thirty\n";DisplayRail bad(d.path/"bad");check(!bad.shorten(1000)&&slurp(d.path/"bad")=="thirty\n","a non-numeric value is left untouched");}
}
int penQuery(int,unsigned code,void* context)noexcept{return code==SW_PEN_INSERTED?*static_cast<int*>(context):-1;}
void optionalWake(){++cases;Dir d;attributes(d.path/"power");Device key(d.path/"key"),cover(d.path/"cover");int lid=0,pen=1;SessionPower p(d.path/"power","inkbound-reader",4*S,key.path,cover.path,S,nullptr,nullptr,lidQuery,&lid,penQuery,&pen,d.path/"sys");p.configure({false,S},S);check(p.tick(3*S,true)&&p.sleep(3*S),"optional baseline idle sleep");cover.event(EV_SW,SW_PEN_INSERTED,0);check(p.tick(4*S,true)&&p.state()==PowerState::Awake&&p.lock().held()&&p.wakeCause()==WakeCause::PenUndocked&&!p.endRequested(4*S),"verified dock-to-undock wakes same session");p.configure({false,S},5*S);check(p.tick(7*S,true)&&p.sleep(7*S),"second idle sleep");pen=0;cover.event(EV_SYN,SYN_DROPPED,0);cover.event(EV_SW,SW_PEN_INSERTED,1);cover.event(EV_SYN,SYN_REPORT,0);check(p.tick(8*S,true)&&p.state()==PowerState::Sleeping,"resync does not invent dock or detach");check(p.inputWake(WakeCause::PenTip,9*S)&&p.lock().held()&&p.takeWake()&&!p.endRequested(9*S),"tip request wakes with lock held");p.configure({false,S},10*S);check(p.tick(12*S,true)&&p.sleep(12*S),"third sleep");cover.event(EV_SW,SW_LID,1);p.tick(13*S,true);check(!p.inputWake(WakeCause::DoubleTap,14*S)&&p.state()==PowerState::Sleeping&&!p.lock().held(),"known closed cover suppresses optional wake");check(!p.inputWake(WakeCause::PenTip,14*S)&&!p.endRequested(14*S),"closed cover tip never ends clean session");}
int main(){
 try{optionalWake();wakeLock();lookup();signals();session();sleepCycle();coverStateRecovery();backgroundResumeKeepsIdleSleep();idleSleep();wakeLockList();ownerPress();ownerBackstop();displayRail();
  std::printf("SESSION_POWER_PASS cases=%u checks=%u sysfs=SIMULATED device_power=NOT_TOUCHED physical=NOT_RUN\n",cases,checks);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"SESSION_POWER_FAIL %s checks=%u\n",e.what(),checks);return 1;}
}
