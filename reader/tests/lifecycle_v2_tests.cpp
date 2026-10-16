// SPDX-License-Identifier: AGPL-3.0-only
#include "lifecycle_v2.h"
#include "profile_loader_v2.h"
#include "qualified_pin_v2.h"
#include "dispatch_v2.h"
#include "bootstrap_v2.h"
#include "launcher_v2.h"
#include "session_limits.h"
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
using namespace recovery::v2;
namespace lc=reader::lifecycle;
namespace {
unsigned checks{},cases{};
void check(bool b,const char* what){++checks;if(!b)throw std::runtime_error(what);}
constexpr Ns Ms=1000000,S=1000*Ms;
constexpr std::uint64_t Epoch=11,Generation=7;
Timing timing(){
 Timing t;t.healthCadence=20*Ms;t.challengeLifetime=50*Ms;t.healthMaxAge=75*Ms;t.grantLifetime=400*Ms;t.drainLead=150*Ms;
 t.startupBudget=500*Ms;t.stopGrace=40*Ms;t.killGrace=40*Ms;t.proofWindow=300*Ms;t.sleepAllowance=800*Ms;t.clockSkew=5*Ms;
 t.workers=lc::DomainCount;
 for(std::uint32_t n=0;n<lc::DomainCount;++n)t.workerPolicies[n]={lc::kind(lc::Domain(n)),8,2*S};
 return t;
}
runtime::Profile profile(){
 runtime::Profile p;p.selection=runtime::Selection::Qualified;p.qualification="lab-qualification-0001";p.managerVersion="249.11";
 p.powerRoutes="measured-route-evidence-0001";p.timing=timing();p.fuse={70*Ms,72*Ms,5*Ms};
 p.readerCeiling=20*S;p.ownerCeiling=30*S;p.callBudget=20*Ms;p.startBudget=400*Ms;p.stopBudget=30*Ms;
 return p;
}
std::string profileText(const runtime::Profile& p){
 const auto& t=p.timing;std::string s="format=inkbound-lifecycle-profile-1\nselection=qualified\n";
 s+="qualification="+p.qualification+"\nmanagerVersion="+p.managerVersion+"\npowerRoutes="+p.powerRoutes+"\n";
 auto n=[&](const char* k,std::uint64_t v){s+=std::string(k)+"="+std::to_string(v)+"\n";};
 n("healthCadence",t.healthCadence);n("challengeLifetime",t.challengeLifetime);n("healthMaxAge",t.healthMaxAge);n("grantLifetime",t.grantLifetime);
 n("drainLead",t.drainLead);n("startupBudget",t.startupBudget);n("stopGrace",t.stopGrace);n("killGrace",t.killGrace);n("proofWindow",t.proofWindow);
 n("sleepAllowance",t.sleepAllowance);n("clockSkew",t.clockSkew);n("workers",t.workers);
 for(std::uint32_t w=0;w<t.workers;++w){const auto& q=t.workerPolicies[w];s+="worker"+std::to_string(w)+"="+(q.kind==WorkerKind::DurablePrefix?"durable-prefix":"cancelable-helper-prefix")+","+std::to_string(q.maxBacklog)+","+std::to_string(q.maxPendingAge)+"\n";}
 n("fuseBootstrapLifetime",p.fuse.bootstrapLifetime);n("fuseLeaseLifetime",p.fuse.leaseLifetime);n("fuseDispatchMargin",p.fuse.dispatchMargin);
 n("readerCeiling",p.readerCeiling);n("ownerCeiling",p.ownerCeiling);n("callBudget",p.callBudget);n("startBudget",p.startBudget);n("stopBudget",p.stopBudget);
 s+="powerScope=verified-platform\npowerEvidence=measured-route-evidence-0001\npowerAllRelevantSleepRoutes=1\npowerFinitePreSleepBarrier=1\n";
 n("powerDelayBudgetUsec",2000000);n("powerMaximumSignalLagUsec",100000);
 n("sustainedWindowUsec",1000000);n("sustainedContactsPerWindow",20);n("sustainedSamplesPerWindow",4000);
 s+="sessionPowerKeyName=30370000.snvs:snvs-powerkey\nsessionCoverName=Hall effect sensors\n";n("sessionIdleUsec",600000000);
 return s;
}
std::string replace(std::string s,const std::string& from,const std::string& to){auto at=s.find(from);if(at==std::string::npos)throw std::runtime_error("fixture replace");return s.replace(at,from.size(),to);}
std::string hex(const runtime::Digest& d){static const char* x="0123456789abcdef";std::string s;for(auto b:d){s+=x[b>>4];s+=x[b&15];}return s;}
runtime::Digest digestOf(const std::string& s){return runtime::sha256({reinterpret_cast<const std::uint8_t*>(s.data()),s.size()});}
void shaVectors(){++cases;
 check(hex(runtime::sha256({}))=="e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855","sha256 empty");
 check(hex(digestOf("abc"))=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","sha256 abc");
 check(hex(digestOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"))=="248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1","sha256 two-block");
 check(hex(digestOf(std::string(1000000,'a')))=="cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0","sha256 million");
 check(hex(digestOf(std::string(55,'a')))=="9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318","sha256 55-byte padding edge");
 check(hex(digestOf(std::string(64,'a')))=="ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb","sha256 exact block");
}
void parsing(){++cases;
 const auto p=profile();check(runtime::valid(p),"fixture profile valid");const auto good=profileText(p);
 auto parsed=runtime::parseQualified(good);check(parsed&&*parsed==p,"exact qualified profile parses and roundtrips");
 auto route=runtime::parseRoute(good);check(route&&route->scope==recovery::power::Scope::VerifiedPlatform&&route->allRelevantSleepRoutes&&route->finitePreSleepBarrier&&route->delayBudgetUsec==2000000&&route->maximumSignalLagUsec==100000&&route->evidenceId=="measured-route-evidence-0001","explicit verified route parses");
 check(!runtime::parseQualified(good.substr(0,good.find("powerScope="))),"power-route section is mandatory");
 check(!runtime::parseQualified(good.substr(0,good.find("sustainedWindowUsec="))),"sustained quota section is mandatory");
 check(!runtime::parseQualified(good.substr(0,good.find("sessionPowerKeyName="))),"session power section is mandatory");
 {auto sp=runtime::parseSessionPower(good);check(sp&&sp->powerKey=="30370000.snvs:snvs-powerkey"&&sp->cover=="Hall effect sensors"&&sp->idleNs==600000000000ull,"session power names and idle parse");
  auto none=runtime::parseSessionPower(replace(good,"sessionCoverName=Hall effect sensors","sessionCoverName=-"));check(none&&none->cover.empty(),"no cover is explicit");}
 auto quota=runtime::parseSustained(good);check(quota&&quota->windowNs==1000000000ull&&quota->contacts==20&&quota->samples==4000,"explicit sustained quotas parse");
 const std::pair<std::string,std::string> bad[]={
  {"selection=qualified","selection=synthetic"},{"selection=qualified","selection=unsupported"},
  {"qualification=lab-qualification-0001","qualification=synthetic-original-manager-runtime-v2"},
  {"powerRoutes=measured-route-evidence-0001","powerRoutes=synthetic-awake-no-suspend"},
  {"powerRoutes=measured-route-evidence-0001","powerRoutes=inferred-login1"},{"powerRoutes=measured-route-evidence-0001","powerRoutes=DEFAULT"},
  {"format=inkbound-lifecycle-profile-1","format=inkbound-lifecycle-profile-2"},{"drainLead=150000000","drainLead=400000000"},
  {"healthCadence=20000000","healthCadence=020000000"},{"healthCadence=20000000","healthCadence=+20000000"},
  {"clockSkew=5000000","clockSkew=99999999999999999999"},{"workers=5","workers=4"},
  {"worker3=cancelable-helper-prefix","worker3=helper-prefix"},{"worker0=durable-prefix,8,","worker0=durable-prefix,8,,"},
  {"sessionIdleUsec=600000000\n","sessionIdleUsec=600000000\nextra=1\n"},{"sessionIdleUsec=600000000","sessionIdleUsec=9999999"},
  {"sessionPowerKeyName=30370000","sessionPowerKeyName= 30370000"},{"sessionCoverName=Hall effect sensors","sessionCoverName="},
  {"sustainedWindowUsec=1000000","sustainedWindowUsec=0"},{"sustainedWindowUsec=1000000","sustainedWindowUsec=60000001"},
  {"sustainedContactsPerWindow=20","sustainedContactsPerWindow=0"},{"sustainedSamplesPerWindow=4000","sustainedSamplesPerWindow=19"},
  {"sustainedContactsPerWindow=20","sustainedContactsPerWindow=1001"},
  {"powerScope=verified-platform","powerScope=private-fixture"},{"powerScope=verified-platform","powerScope=unverified"},
  {"powerEvidence=measured-route-evidence-0001","powerEvidence=inferred-from-login1"},{"powerAllRelevantSleepRoutes=1","powerAllRelevantSleepRoutes=0"},
  {"powerFinitePreSleepBarrier=1","powerFinitePreSleepBarrier=0"},{"powerMaximumSignalLagUsec=100000","powerMaximumSignalLagUsec=2000000"},
  {"powerDelayBudgetUsec=2000000","powerDelayBudgetUsec=0"},{"readerCeiling=","readerceiling="},
  {"callBudget=20000000","callBudget=60000000"}};
 for(const auto& [from,to]:bad)check(!runtime::parseQualified(replace(good,from,to)),"mutated profile refused");
 check(!runtime::parseQualified(good.substr(0,good.size()-1)),"missing final newline refused");
 check(!runtime::parseQualified(replace(good,"\n","\r\n")),"CRLF refused");
 {auto lines=good;auto a=lines.find("healthCadence="),b=lines.find("challengeLifetime=");auto first=lines.substr(a,b-a);lines.erase(a,b-a);lines.insert(lines.find("healthMaxAge="),first);check(!runtime::parseQualified(lines),"reordered keys refused");}
 {auto dup=replace(good,"managerVersion=249.11\n","managerVersion=249.11\nmanagerVersion=249.11\n");check(!runtime::parseQualified(dup),"duplicate key refused");}
 auto synthetic=p;synthetic.selection=runtime::Selection::Synthetic;check(runtime::valid(synthetic),"synthetic fixture itself valid");
 check(!runtime::parseQualified(std::string(5000,'a')+"\n"),"oversize refused");
}
void loading(){++cases;
 char temp[]="/tmp/lifecycle-v2.XXXXXX";check(mkdtemp(temp),"private temp");const std::string dir=temp;
 const auto good=profileText(profile());const auto pin=digestOf(good);const auto me=getuid();
 auto write=[&](const std::string& name,const std::string& body,mode_t mode){auto path=dir+"/"+name;{std::ofstream o(path,std::ios::binary);o<<body;check(bool(o),"fixture write");}check(!chmod(path.c_str(),mode),"fixture mode");return path;};
 auto path=write("profile",good,0644);
 auto loaded=runtime::loadQualified(path.c_str(),pin,me);check(loaded.profile&&*loaded.profile==profile()&&loaded.route&&loaded.sustained&&loaded.sessionPower&&loaded.failure==runtime::LoadFailure::None,"pinned exact profile, route and quotas load");
 check(runtime::loadQualified(path.c_str(),runtime::Digest{},me).failure==runtime::LoadFailure::Unpinned,"no pin means no profile");
 auto wrong=pin;wrong[0]^=1;auto refused=runtime::loadQualified(path.c_str(),wrong,me);check(!refused.profile&&refused.failure==runtime::LoadFailure::Digest,"digest mismatch refused");
 check(runtime::loadQualified((dir+"/absent").c_str(),pin,me).failure==runtime::LoadFailure::Open,"missing profile refused, no default");
 check(runtime::loadQualified("relative/profile",pin,me).failure==runtime::LoadFailure::Open,"relative path refused");
 check(!symlink(path.c_str(),(dir+"/link").c_str())&&runtime::loadQualified((dir+"/link").c_str(),pin,me).failure==runtime::LoadFailure::Open,"symlink not followed");
 check(runtime::loadQualified(path.c_str(),pin,me+1).failure==runtime::LoadFailure::Owner,"wrong owner refused");
 auto writable=write("writable",good,0664);check(runtime::loadQualified(writable.c_str(),pin,me).failure==runtime::LoadFailure::Mode,"group-writable refused");
 check(!link(path.c_str(),(dir+"/hard").c_str())&&runtime::loadQualified(path.c_str(),pin,me).failure==runtime::LoadFailure::Links,"hard-linked profile refused");
 check(!unlink((dir+"/hard").c_str()),"remove hard link");
 check(runtime::loadQualified(dir.c_str(),pin,me).failure==runtime::LoadFailure::NotRegular,"directory refused");
 auto big=write("big",std::string(5000,'x'),0644);check(runtime::loadQualified(big.c_str(),digestOf(std::string(5000,'x')),me).failure==runtime::LoadFailure::Size,"oversize refused");
 const auto synthetic=replace(good,"selection=qualified","selection=synthetic");auto spath=write("synthetic",synthetic,0644);
 auto s=runtime::loadQualified(spath.c_str(),digestOf(synthetic),me);check(!s.profile&&s.failure==runtime::LoadFailure::Selection,"pinned synthetic still refused");
 const auto routed=replace(good,"powerRoutes=measured-route-evidence-0001","powerRoutes=inferred-login1");auto rpath=write("routed",routed,0644);
 auto r=runtime::loadQualified(rpath.c_str(),digestOf(routed),me);check(!r.profile&&r.failure==runtime::LoadFailure::Inferred,"pinned inferred power route refused");
 // This build carries no pinned production profile, so V2 roles always refuse.
 check(runtime::qualifiedPin()==runtime::Digest{},"default build has no pinned production profile");
 auto pinned=runtime::loadPinned(dir);check(!pinned.profile&&!pinned.route&&pinned.failure==runtime::LoadFailure::Unpinned,"production loader refuses without a build pin");
 {dispatch::EventsPower unverified(recovery::power::Config{});check(!unverified.verified(),"unverified power route never admits");unverified.applicationQuiesced(true);check(!unverified.verified(),"acknowledgement cannot create authority");
  dispatch::UnqualifiedPower none;check(!none.verified(),"unqualified production power refuses");}
 std::filesystem::remove_all(dir);
}
void domains(){++cases;
 auto t=timing();check(lc::matches(t),"fixed five-domain map");
 auto swapped=t;std::swap(swapped.workerPolicies[0],swapped.workerPolicies[3]);check(!lc::matches(swapped),"reordered domains refused");
 auto big=t;big.workerPolicies[1].maxBacklog=33;check(!lc::matches(big),"ledger capacity bound");
 lc::Domains invalid(swapped,Epoch);check(!invalid.valid(),"invalid map");
 progress::WorkToken token;auto binding=invalid.tracking(lc::Domain::Canonical);check(!binding.accept(token)&&!token,"invalid map refuses acceptance, never V1 empty binding");
 lc::Domains zero(t,0);check(!zero.valid(),"clock epoch required");
 lc::Domains d(t,Epoch);check(d.valid(),"valid domains");
 for(std::uint32_t n=0;n<lc::DomainCount;++n){auto b=d.tracking(lc::Domain(n));check(b.ledger&&b.clockGeneration==Epoch&&b.kind==lc::kind(lc::Domain(n))&&b.supports(b.kind),"domain binding kind and epoch");}
 progress::MainProgressChannel channel;lc::Foreground f(d,channel);
 check(f.publish(false)&&f.last().mainSequence==1&&f.last().workerCount==lc::DomainCount&&f.last().captured.generation==Epoch,"first publication");
 auto canonical=d.tracking(lc::Domain::Canonical),helper=d.tracking(lc::Domain::Preparation);
 progress::WorkToken a,b;check(canonical.accept(a)&&helper.accept(b),"foreground accepts");
 check(f.publish(false)&&f.last().workers[0].backlog==1&&f.last().workers[3].backlog==1,"backlog visible");
 std::thread worker([&]{check(canonical.complete(a,true)&&helper.complete(b,true),"worker completions");});worker.join();
 check(f.publish(false)&&!f.last().workers[0].backlog&&f.last().workers[0].completed==1&&f.last().workers[3].completed==1&&!f.last().saveError,"completed prefix retired");
 progress::WorkToken c;check(canonical.accept(c)&&canonical.complete(c,false),"failed write reported");
 check(f.publish(false)&&f.last().saveError,"worker failure is save error");check(f.publish(false)&&f.last().saveError,"save error sticky");
 check(!canonical.accept(c),"faulted domain refuses new work");
 f.requestShutdown();check(f.publish(false)&&f.last().shutdown,"shutdown published");
 f.requestSleep();check(f.publish(false)&&f.last().sleep&&f.last().shutdown,"sleep published and shutdown sticky");
 auto r=channel.read();check(r.status==progress::ReadStatus::Ready&&r.health.mainSequence==f.last().mainSequence,"observer reads latest publication");
}
void deadlines(){++cases;
 auto p=profile();const Ns now=spike::monotonic();check(now,"clock");
 auto end=lc::sessionDeadline(now,p);check(end>now&&recovery::limits::validDeadline(now,end),"session deadline within V1 payload");
 check(end-now<=p.readerCeiling-p.timing.drainLead-p.timing.stopGrace-p.timing.killGrace,"deadline leaves drain/stop/kill budget");
 auto longer=p;longer.readerCeiling=1000*S;longer.ownerCeiling=2000*S;check(runtime::valid(longer)&&lc::sessionDeadline(now,longer)-now==recovery::limits::Payload,"unchanged V1 payload caps longer ceilings");
 check(!lc::sessionDeadline(0,p),"missing clock");auto invalid=p;invalid.selection=runtime::Selection::Unsupported;check(!lc::sessionDeadline(now,invalid),"invalid profile");
}
SessionIdentity identity(pid_t owner,pid_t child){SessionIdentity i;text(i.session,"0123456789abcdef0123456789abcdef");text(i.boot,"01234567-89ab-cdef-0123-456789abcdef");
 text(i.invocation,"11111111111111111111111111111111");text(i.ownerInvocation,"22222222222222222222222222222222");text(i.mount,"/sys/fs/cgroup/unified");
 i.ownerPid=owner;i.mainPid=child;i.device=21;i.inode=4097;i.ticket.fill(0x7b);setUnitNames(i);return i;}
void reports(){++cases;
 auto id=identity(100,200);check(validIdentity(id),"fixture identity");
 quiescence::Report r;r.session=id.session;r.invocation=id.invocation;r.ticket=id.ticket;r.generation=Generation;r.clockGeneration=Epoch;r.lastGrantSequence=4;
 for(auto& d:r.domains){d.accepted=d.completed=3;}r.inputClosed=r.admissionClosed=r.helpersReconciled=true;
 check(quiescence::durable(r),"clean report durable");
 Wire w;check(quiescence::encode(r,w),"report encodes");auto back=quiescence::decode(w);check(back&&*back==r,"report roundtrips");
 for(std::size_t n=0;n<8;++n){auto bad=w;bad[n]^=0x40;check(!quiescence::decode(bad),"report header strict");}
 {auto bad=w;bad[1000]=1;check(!quiescence::decode(bad),"report padding zero");}
 {auto bad=w;bad[398]|=0x80;check(!quiescence::decode(bad),"report unknown flag refused");}
 check(!quiescence::decode(std::span<const std::uint8_t>(w.data(),w.size()-1)),"report truncation refused");
 check(!decode(w)&&!bootstrap::decode(w)&&!launcher::decode(w),"report is not a protocol, bootstrap or launcher frame");
 Message m;m.kind=Kind::Challenge;m.identity=id;m.generation=Generation;m.sequence=1;m.challenge.issued={1,1,Epoch};m.challenge.replyBy={2,2,Epoch};m.challenge.nonce.fill(1);m.challenge.nextGrant=1;
 Wire pw;if(encode(m,pw))check(!quiescence::decode(pw),"protocol frame is not a report");
 auto variant=[&](auto mutate,const char* what){auto x=r;mutate(x);check(!quiescence::durable(x),what);};
 variant([](auto& x){x.domains[0].backlog=1;},"backlog blocks durability");variant([](auto& x){x.domains[2].completed=2;},"prefix hole blocks durability");
 variant([](auto& x){x.faults[1]=1;},"domain fault blocks durability");variant([](auto& x){x.helpersReconciled=false;},"unreconciled helper blocks durability");
 variant([](auto& x){x.inputClosed=false;},"open input blocks durability");variant([](auto& x){x.admissionClosed=false;},"open admission blocks durability");
 variant([](auto& x){x.saveFailed=true;},"save failure blocks durability");variant([](auto& x){x.shellFailed=true;},"shell failure blocks durability");
 variant([](auto& x){x.semanticFailed=true;},"semantic failure blocks durability");variant([](auto& x){x.unsaved=1;},"unsaved blocks durability");
 variant([](auto& x){x.semanticPending=1;},"semantic pending blocks durability");
 ClosureProof closure;closure.identity=id;closure.generation=Generation;closure.lastGrantSequence=4;
 check(quiescence::bound(r,id,Generation,Epoch,closure),"report bound to identity, generation, epoch and closure");
 check(!quiescence::bound(r,id,Generation+1,Epoch,closure),"protocol generation binds");check(!quiescence::bound(r,id,Generation,Epoch+1,closure),"clock epoch binds separately");
 {auto c=closure;c.lastGrantSequence=5;check(quiescence::bound(r,id,Generation,Epoch,c),"one grant in flight at child-sealed closure still binds");}
 {auto c=closure;c.lastGrantSequence=6;check(!quiescence::bound(r,id,Generation,Epoch,c),"two grants behind does not bind");}
 {auto c=closure;c.lastGrantSequence=3;check(!quiescence::bound(r,id,Generation,Epoch,c),"report ahead of the closure proof does not bind");}
 {auto c=closure;c.lastGrantSequence=0;auto x=r;x.lastGrantSequence=0;check(quiescence::bound(x,id,Generation,Epoch,c),"zero grants binds");x.lastGrantSequence=~std::uint64_t{0};check(!quiescence::bound(x,id,Generation,Epoch,c),"no wraparound below zero");}
 {auto other=id;other.ticket[3]^=1;check(!quiescence::bound(r,other,Generation,Epoch,closure),"ticket binds");}
 {auto c=closure;c.identity.mainPid=201;check(!quiescence::bound(r,id,Generation,Epoch,c),"closure identity binds");}
 check(Generation!=Epoch,"fixture keeps protocol generation and clock epoch distinct");
}
// ---- actual lease adapter over a real SOCK_SEQPACKET pair, reader in a forked child ----
enum class Scenario:std::uint8_t {Normal,Stall,Power,FailedSave};
struct Outcome {bool admitted{},validAfter{},closed{},observerAlive{};std::uint64_t grants{};unsigned fault{},reason{},channelFault{},closedFlags{},closedAtMs{};std::array<std::uint64_t,lc::DomainCount> closedFaults{};Wire report{};bool hasReport{};};
Outcome runChild(Scenario scenario,int fd,pid_t owner,SessionIdentity id){
 Outcome out;auto t=timing();auto p=profile();
 lc::Domains d(t,Epoch);progress::MainProgressChannel channel;lc::Foreground f(d,channel);
 const auto start=lc::sample(Epoch);
 lc::Lease lease(id,Generation,t,start,fd,1,getuid(),channel,lc::sessionDeadline(spike::monotonic(),p),true);
 (void)owner;out.admitted=lc::awaitAdmission(lease,f,2*S,false);out.fault=lease.fault();out.reason=lease.reason();if(!out.admitted)return out;
 const Ns admittedAt=spike::monotonic();bool captured=false;
 auto capture=[&]{if(captured||!lease.closed())return;captured=true;const auto& h=f.last();out.closedFlags=unsigned(h.saveError)|unsigned(h.shutdown)<<1|unsigned(h.sleep)<<2;out.closedAtMs=unsigned((spike::monotonic()-admittedAt)/Ms);for(std::uint32_t n=0;n<lc::DomainCount;++n)out.closedFaults[n]=std::uint64_t(d.snapshot(lc::Domain(n)).fault);};
 auto pump=[&](Ns duration,bool publish){const Ns end=spike::monotonic()+duration;while(spike::monotonic()<end&&!lease.closed()){if(publish)f.publish(false);usleep(1000);}capture();};
 auto canonical=d.tracking(lc::Domain::Canonical),helper=d.tracking(lc::Domain::Preparation);
 if(scenario==Scenario::Normal){
  progress::WorkToken a,b;if(!canonical.accept(a)||!helper.accept(b))return out;
  std::thread worker([&]{usleep(100000);canonical.complete(a,true);helper.complete(b,true);});
  pump(300*Ms,true);worker.join();out.observerAlive=lease.valid();
  f.requestShutdown();pump(2*S,true);out.closed=lease.closed();out.validAfter=lease.valid();out.grants=lease.grantSequence();
  auto report=lc::finalReport(d,id,Generation,lease.grantSequence(),{true,true,false,false,false,0,0});
  out.hasReport=quiescence::encode(report,out.report);
 }else if(scenario==Scenario::Stall){
  pump(200*Ms,true);out.observerAlive=lease.valid();
  // Foreground frozen: the observer keeps polling but cannot renew admission.
  usleep(useconds_t((t.grantLifetime+200*Ms)/1000));out.validAfter=lease.valid();
  pump(1*S,false);out.closed=lease.closed();out.grants=lease.grantSequence();
 }else if(scenario==Scenario::Power){
  pump(200*Ms,true);out.observerAlive=lease.valid();lease.powerLost();pump(50*Ms,true);out.validAfter=lease.valid();
  pump(1*S,true);out.closed=lease.closed();
 }else{
  progress::WorkToken a;if(!canonical.accept(a))return out;canonical.complete(a,false);
  pump(1*S,true);out.closed=lease.closed();out.validAfter=lease.valid();
  auto report=lc::finalReport(d,id,Generation,lease.grantSequence(),{true,true,false,false,false,0,0});
  out.hasReport=quiescence::encode(report,out.report);
 }
 out.fault=lease.fault();out.reason=lease.reason();out.channelFault=unsigned(channel.fault());
 return out;
}
struct OwnerResult {bool closed{};Reason reason{};std::optional<ClosureProof> closure;std::uint64_t grants{};unsigned challenges{},replies{},sendFailures{},shortReads{};int ended{};Ns lastGrantAt{},endedAt{};};
// Mirrors OwnerControl::pollAt pacing: admitted() every step, one outstanding
// challenge, a new challenge only when the cadence is due, grants on health.
OwnerResult runOwner(OwnerMachine& machine,int fd,VerifiedPeer child,Ns duration){
 OwnerResult r;const Authority authority{true,true,true,true};const Ns end=spike::monotonic()+duration;
 bool sentClosure=false,pending=false,issued=false;ClockPair issuedAt{};const auto cadence=timing().healthCadence;
 while(spike::monotonic()<end){
  auto now=lc::sample(Epoch);(void)machine.admitted(now,authority);
  Wire raw;auto n=recv(fd,raw.data(),raw.size(),MSG_DONTWAIT);const int receiveError=n<0?errno:0;
  if(n==ssize_t(raw.size())&&!machine.closed()){
   const auto at=lc::sample(Epoch);auto g=machine.acceptHealth(raw,child,at,authority);
   ++r.replies;if(g){pending=false;if(send(fd,g->data(),g->size(),MSG_NOSIGNAL)!=ssize_t(g->size()))++r.sendFailures;r.lastGrantAt=at.monotonic;}
   else{ // OwnerControl seals on any refused reply; so does this fixture.
    if(auto m=decode(raw))std::fprintf(stderr,"OWNER_REFUSED kind=%u seq=%llu main=%llu captured=%llu now=%llu closed=%d reason=%u backlog0=%u backlog3=%u oldest0=%llu\n",unsigned(m->kind),(unsigned long long)m->sequence,(unsigned long long)m->health.mainSequence,(unsigned long long)m->health.captured.monotonic,(unsigned long long)at.monotonic,machine.closed(),unsigned(machine.reason()),m->health.workers[0].backlog,m->health.workers[3].backlog,(unsigned long long)m->health.workers[0].oldest.monotonic);
    else std::fprintf(stderr,"OWNER_REFUSED undecodable\n");
    machine.close(at);
   }
  }
  now=lc::sample(Epoch);
  const bool due=!issued||(now.monotonic-issuedAt.monotonic>=cadence&&now.boottime-issuedAt.boottime>=cadence);
  if(!machine.closed()&&!pending&&due){
   Nonce nonce{};check(getrandom(nonce.data(),nonce.size(),0)==ssize_t(nonce.size()),"owner nonce");
   if(auto w=machine.challenge(now,authority,nonce)){if(send(fd,w->data(),w->size(),MSG_NOSIGNAL)!=ssize_t(w->size()))++r.sendFailures;++r.challenges;pending=issued=true;issuedAt=now;}
  }
  if(machine.closed()&&!sentClosure){sentClosure=true;if(auto c=machine.closurePacket())send(fd,c->data(),c->size(),MSG_NOSIGNAL);}
  if(n>0&&n!=ssize_t(raw.size()))++r.shortReads;
  // As OwnerControl::seal: reader EOF/transport loss is a disconnect closure.
  if(n==0){r.ended=1;machine.disconnect(lc::sample(Epoch));break;}
  if(n<0&&receiveError!=EAGAIN&&receiveError!=EWOULDBLOCK&&receiveError!=EINTR){r.ended=2;machine.disconnect(lc::sample(Epoch));break;}
  usleep(1000);
 }
  r.endedAt=spike::monotonic();
 if(!machine.closed())machine.close(lc::sample(Epoch));
 r.closed=machine.closed();r.reason=machine.reason();r.closure=machine.closure();r.grants=machine.lastGrantSequence();return r;
}
std::pair<Outcome,OwnerResult> scenario(Scenario s){
 int pair[2];check(!socketpair(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0,pair),"socketpair");
 int pipefd[2];check(!pipe2(pipefd,O_CLOEXEC),"result pipe");int ready[2];check(!pipe2(ready,O_CLOEXEC),"ready pipe");
 const pid_t owner=getpid();auto child=fork();check(child>=0,"fork reader");
 if(!child){close(pair[0]);close(pipefd[0]);close(ready[0]);int code=0;Outcome out;
  // As in production, the bootstrap channel enables SO_PASSCRED on this socket before the owner speaks.
  int yes=1;char r='r';if(setsockopt(pair[1],SOL_SOCKET,SO_PASSCRED,&yes,sizeof yes)||write(ready[1],&r,1)!=1)_exit(5);close(ready[1]);
  try{out=runChild(s,pair[1],owner,identity(owner,getpid()));}catch(...){code=3;}
  auto p=reinterpret_cast<const char*>(&out);std::size_t left=sizeof out;while(left){auto k=write(pipefd[1],p,left);if(k<=0){code=4;break;}p+=k;left-=std::size_t(k);}
  _exit(code);}
 close(pair[1]);close(pipefd[1]);close(ready[1]);{char r{};check(read(ready[0],&r,1)==1&&r=='r',"reader credentials ready");close(ready[0]);}
 auto id=identity(owner,child);auto start=lc::sample(Epoch);
 OwnerMachine machine(id,Generation,timing(),VerifiedPeer{getuid(),std::uint32_t(child),1,true},getuid(),start);
 auto owned=runOwner(machine,pair[0],VerifiedPeer{getuid(),std::uint32_t(child),1,true},4*S);
 close(pair[0]);
 Outcome out;auto p=reinterpret_cast<char*>(&out);std::size_t left=sizeof out;while(left){auto k=read(pipefd[0],p,left);check(k>0,"child outcome");p+=k;left-=std::size_t(k);}
 close(pipefd[0]);int status{};check(waitpid(child,&status,0)==child&&WIFEXITED(status)&&!WEXITSTATUS(status),"reader child exit");
 std::fprintf(stderr,"LEASE_DIAGNOSTIC scenario=%u closed_flags=%u closed_at_ms=%u faults=%llu,%llu,%llu,%llu,%llu channel_fault=%u child_fault=%u child_reason=%u owner_closed=%d owner_reason=%u owner_grants=%llu challenges=%u replies=%u send_failures=%u short=%u ended=%d grant_to_end_ms=%llu\n",unsigned(s),out.closedFlags,out.closedAtMs,(unsigned long long)out.closedFaults[0],(unsigned long long)out.closedFaults[1],(unsigned long long)out.closedFaults[2],(unsigned long long)out.closedFaults[3],(unsigned long long)out.closedFaults[4],out.channelFault,out.fault,out.reason,owned.closed,unsigned(owned.reason),(unsigned long long)owned.grants,owned.challenges,owned.replies,owned.sendFailures,owned.shortReads,owned.ended,(unsigned long long)((owned.endedAt-owned.lastGrantAt)/Ms));
 return {out,owned};
}
void leaseNormal(){++cases;
 auto [out,owner]=scenario(Scenario::Normal);
 check(out.admitted,"first grant admits only after real foreground publications");
 check(out.observerAlive,"healthy foreground keeps renewed admission");
 check(out.closed&&!out.validAfter,"shutdown publication closes lease");
 // ChildControl seals on its own published shutdown; the owner records the resulting disconnect.
 check(owner.closed&&owner.reason==Reason::Disconnected&&owner.closure&&owner.ended==1,"owner closes when the reader seals on published shutdown");
 check(out.hasReport,"final report produced");auto report=quiescence::decode(out.report);check(bool(report),"final report decodes");
 check(quiescence::durable(*report),"accepted canonical and helper prefixes durable/reconciled at quiescence");
 check(report->lastGrantSequence==owner.grants&&owner.grants>=2,"reader and owner agree on last grant");
 check(quiescence::bound(*report,owner.closure->identity,Generation,Epoch,*owner.closure),"report bound to owner closure");
}
void leaseStall(){++cases;
 auto [out,owner]=scenario(Scenario::Stall);
 check(out.admitted&&out.observerAlive,"admitted before stall");
 check(!out.validAfter,"frozen foreground cannot be masked by live observer thread");
 check(owner.closed&&owner.reason!=Reason::Shutdown,"owner closes stalled session");
}
void leasePower(){++cases;
 auto [out,owner]=scenario(Scenario::Power);
 check(out.admitted&&out.observerAlive,"admitted before power loss");
 check(!out.validAfter,"power authority loss closes admission immediately");
 check(out.closed,"power loss is terminal for the lease");
}
// D2 stock start-limit: a session may begin inside the window only after Inkbound's own restorations (exact invocation,
// no automatic restart since) while one start of the burst stays spare; anything doubtful keeps the full window.
void stockStartLedger(){++cases;
 const std::string a(32,'a'),b(32,'b'),c(32,'c');const std::uint64_t s=1000000,w=600*s;
 check(!dispatch::ownStartsAllow({},a,0,10*s,w,4),"no record keeps the window");
 check(dispatch::ownStartsAllow({{5*s,0,a}},a,0,10*s,w,4),"one own start allows a second session");
 check(!dispatch::ownStartsAllow({{5*s,0,a}},b,0,10*s,w,4),"a foreign running invocation keeps the window");
 check(!dispatch::ownStartsAllow({{5*s,0,a}},a,1,10*s,w,4),"an automatic restart since keeps the window");
 check(dispatch::ownStartsAllow({{3*s,0,c},{5*s,0,a}},a,0,10*s,w,4),"two own starts allow a third");
 check(!dispatch::ownStartsAllow({{2*s,0,b},{3*s,0,c},{5*s,0,a}},a,0,10*s,w,4),"three own starts in the window leave no spare");
 check(dispatch::ownStartsAllow({{2*s,0,b},{3*s,0,c},{700*s,0,a}},a,0,701*s,w,4),"starts older than the interval no longer count");
 check(!dispatch::ownStartsAllow({{5*s,0,a}},a,0,10*s,w,2),"a burst of two leaves no room");
 char dir[]="/tmp/inkbound-ledger-XXXXXX";check(mkdtemp(dir)!=nullptr,"ledger dir");const std::string path=std::string(dir)+"/starts";
 check(dispatch::readStockStarts(path.c_str()).empty(),"missing record is empty");
 for(unsigned n=0;n<20;++n)check(dispatch::recordStockStart(n*s,0,n%2?a:b,path.c_str()),"record start");
 auto read=dispatch::readStockStarts(path.c_str());check(read.size()==16&&read.back().invocation==a&&read.back().usec==19*s,"record keeps the latest sixteen");
 struct stat st{};check(!stat(path.c_str(),&st)&&(st.st_mode&0777)==0600,"record is private");
 check(!dispatch::recordStockStart(1,0,"not-an-invocation",path.c_str())&&dispatch::readStockStarts(path.c_str()).size()==16,"invalid invocation is refused");
 check(chmod(path.c_str(),0644)==0&&dispatch::readStockStarts(path.c_str()).empty(),"a record readable by others is ignored");
 {std::ofstream bad(path,std::ios::trunc);bad<<"garbage\n";}check(chmod(path.c_str(),0600)==0&&dispatch::readStockStarts(path.c_str()).empty(),"a malformed record is ignored");
 std::filesystem::remove_all(dir);
}
// D2 stock start-limit, journal rule: starts by anyone (XOVI's restart after the power-button triple press) are counted
// from PID 1's records, one per job; the running invocation's own record must be present and one start stays spare.
void stockStartJournal(){++cases;
 const std::string unit="xochitl.service",inv="fffddc5d72e34d5fa97538856bbe3c04",old="fd2755911a4c406b86cec214139d3927",other(32,'e');const std::uint64_t s=1000000,w=600*s;
 const std::string started="MESSAGE_ID=39f53479d3a045ac8e11786248231fbf",failed="MESSAGE_ID=be02cf6855d2428ba40df7e9d022f03d";
 /* The tablet's records of 2026-10-04 04:07:54Z (XOVI start script: systemctl restart xochitl, JOB 5402). */
 std::vector<std::pair<std::uint64_t,std::vector<std::string>>> real{
  {8999610226,{"_PID=1","JOB_TYPE=stop","MESSAGE_ID=de5b426a63be47a7b6ac3eaac82e2f6f","JOB_ID=5402","INVOCATION_ID="+old,"UNIT="+unit}},
  {8999836428,{"_PID=1","MESSAGE_ID=7ad2d189f7e94e70a38c781354912448","INVOCATION_ID="+old,"UNIT="+unit}},
  {8999837363,{"_PID=1","JOB_RESULT=done","MESSAGE_ID=9d1aaa27d60140bd96365438aad20286","JOB_TYPE=restart","JOB_ID=5402","INVOCATION_ID="+old,"UNIT="+unit}},
  {8999862255,{"_PID=1","JOB_TYPE=start","JOB_RESULT=done",started,"JOB_ID=5402","UNIT="+unit,"INVOCATION_ID="+inv}}};
 std::vector<dispatch::JournalStart> starts;for(const auto& [usec,fields]:real){dispatch::JournalStart r;if(dispatch::journalRecord(fields,usec,unit,r))starts.push_back(r);}
 check(starts.size()==1&&starts[0].started&&starts[0].job=="5402"&&starts[0].invocation==inv&&starts[0].usec==8999862255ull,"only the Started record of the restart job is a start");
 const std::uint64_t at=8999862255ull,now=at+156*s;unsigned recent=0;
 check(dispatch::journalStartsAllow(starts,inv,now,w,4,&recent)&&recent==1,"XOVI's single restart leaves room for a session (the 2026-10-04 refusal)");
 check(!dispatch::journalStartsAllow(starts,other,now,w,4),"a running invocation the journal never saw keeps the window");
 check(!dispatch::journalStartsAllow({},inv,now,w,4),"no records keep the window");
 check(!dispatch::journalStartsAllow(starts,inv,now,w,2),"a burst of two leaves no room");
 auto restartTyped=real[3].second;restartTyped[1]="JOB_TYPE=restart";dispatch::JournalStart rt;
 check(dispatch::journalRecord(restartTyped,at,unit,rt)&&rt.started,"a restart-typed Started record counts");
 auto untyped=real[3].second;untyped.erase(untyped.begin()+4);untyped.erase(untyped.begin()+1);dispatch::JournalStart ut;
 check(dispatch::journalRecord(untyped,at,unit,ut)&&ut.job.empty(),"a record without JOB_TYPE or JOB_ID counts");
 dispatch::JournalStart ignored;
 check(!dispatch::journalRecord({"_PID=42",started,"UNIT="+unit},at,unit,ignored),"records not from PID 1 are ignored");
 check(!dispatch::journalRecord({"_PID=1",started,"UNIT=other.service"},at,unit,ignored),"other units are ignored");
 check(!dispatch::journalRecord({"_PID=1","MESSAGE_ID=7d4958e842da4a758f6c1cdc7b36dcc5","UNIT="+unit},at,unit,ignored),"Starting records are not counted twice");
 auto twice=starts;twice.push_back(starts[0]);check(dispatch::journalStartsAllow(twice,inv,now,w,4,&recent)&&recent==1,"one job counts once");
 std::vector<dispatch::JournalStart> noJob{{at-60*s,"",old,true},{at-30*s,"",old,true},{at,"",inv,true}};
 check(!dispatch::journalStartsAllow(noJob,inv,now,w,4,&recent)&&recent==3,"records without JOB_ID count one each; three leave no spare");
 std::vector<dispatch::JournalStart> withFailed{{at-60*s,"5390","",false},{at-30*s,"5396","",false},starts[0]};
 check(!dispatch::journalStartsAllow(withFailed,inv,now,w,4,&recent)&&recent==3,"failed starts count toward the burst");
 std::vector<dispatch::JournalStart> aged{{at-700*s,"5100",old,true},starts[0]};
 check(dispatch::journalStartsAllow(aged,inv,now,w,4,&recent)&&recent==1,"starts older than the interval no longer count");
 std::vector<dispatch::JournalStart> future{{now+5*s,"5500",old,false},starts[0]};
 check(dispatch::journalStartsAllow(future,inv,now,w,4,&recent)&&recent==2,"a record stamped after now still counts");
 std::vector<dispatch::JournalStart> none{starts[0]};dispatch::readJournalStarts("inkbound-no-such-unit.service",now,w,none);check(none.empty(),"reading a unit without records yields none");
}
void leaseFailedSave(){++cases;
 auto [out,owner]=scenario(Scenario::FailedSave);
 check(out.admitted,"admitted before failed save");
 check(out.closed&&!out.validAfter,"failed save closes lease");
 check(owner.closed&&owner.reason==Reason::Disconnected&&owner.ended==1,"reader seals on published save error; owner records disconnect");
 auto report=quiescence::decode(out.report);check(report&&!quiescence::durable(*report)&&report->faults[0],"failed save can never report durability");
}
}
int main(int argc,char** argv){
 /* Manual probe (not part of the suite): count this boot's journal starts of a unit within the last interval seconds. */
 if(argc==4&&std::string(argv[1])=="--journal-probe"){timespec now{};clock_gettime(CLOCK_MONOTONIC,&now);const std::uint64_t n=std::uint64_t(now.tv_sec)*1000000+std::uint64_t(now.tv_nsec)/1000;
  std::vector<dispatch::JournalStart> got;const bool ok=dispatch::readJournalStarts(argv[2],n,std::stoull(argv[3])*1000000,got);
  std::printf("JOURNAL_PROBE ok=%d records=%zu\n",int(ok),got.size());for(const auto& r:got)std::printf(" usec=%llu job=%s started=%d invocation=%s\n",(unsigned long long)r.usec,r.job.c_str(),int(r.started),r.invocation.c_str());return ok?0:1;}
 /* Copied device journal: --journal-dir <unit> <interval_s> <directory> <boot_id> <now_monotonic_usec>. */
 if(argc==7&&std::string(argv[1])=="--journal-dir"){std::vector<dispatch::JournalStart> got;
  const bool ok=dispatch::readJournalStarts(argv[2],std::stoull(argv[6]),std::stoull(argv[3])*1000000,got,argv[4],argv[5]);unsigned recent=0;
  std::printf("JOURNAL_DIR ok=%d records=%zu\n",int(ok),got.size());for(const auto& r:got)std::printf(" usec=%llu job=%s started=%d invocation=%s\n",(unsigned long long)r.usec,r.job.c_str(),int(r.started),r.invocation.c_str());
  const bool allow=!got.empty()&&dispatch::journalStartsAllow(got,got.front().invocation,std::stoull(argv[6]),std::stoull(argv[3])*1000000,4,&recent);std::printf("JOURNAL_DIR allow=%d recent=%u\n",int(allow),recent);return ok?0:1;}
 try{shaVectors();parsing();loading();domains();deadlines();reports();leaseNormal();leaseStall();leasePower();leaseFailedSave();stockStartLedger();stockStartJournal();
  std::printf("LIFECYCLE_V2_READER_PASS cases=%u checks=%u profile=EXPLICIT_PINNED_ONLY production_default=NONE owner_dispatch=NOT_WIRED physical=NOT_RUN\n",cases,checks);return 0;
 }catch(const std::exception& e){std::fprintf(stderr,"LIFECYCLE_V2_READER_FAIL %s checks=%u\n",e.what(),checks);return 1;}
}
