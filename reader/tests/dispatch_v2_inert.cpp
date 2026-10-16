// SPDX-License-Identifier: AGPL-3.0-only
// Inert PID1 harness for the production V2 dispatcher. Every role calls the
// shipping dispatch/lifecycle functions; only the profile source, the stock
// unit and the power/fuse evidence sources are synthetic and test-only.
#include <systemd/sd-bus.h>
#include "dispatch_v2.h"
#include "fuse_evidence_v2.h"
#include "lifecycle_v2.h"
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/input.h>
#include <fcntl.h>
#include <cstring>
#include <csignal>
#include <dirent.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
using namespace recovery::v2;
namespace lc=reader::lifecycle;
namespace {
std::string root,session,scenario;
constexpr Ns Ms=1000000,S=1000*Ms;
constexpr std::uint64_t Generation=7,LauncherEpoch=11;
void need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
void mark(const std::string& key,const std::string& text="yes"){std::ofstream out(root+"/"+key,std::ios::app);out<<text<<"\n";out.flush();need(bool(out),"evidence marker");}
// Test-only: the synthetic profile names the live manager's exact Version so the
// same fixtures run unchanged on systemd 249 and 255 VMs (production pins it).
static std::string liveManagerVersion(){sd_bus* b{};char* v{};sd_bus_error e=SD_BUS_ERROR_NULL;std::string out;
 if(sd_bus_open_system(&b)>=0&&sd_bus_get_property_string(b,"org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","Version",&e,&v)>=0&&v)out=v;
 free(v);sd_bus_error_free(&e);sd_bus_flush_close_unref(b);if(out.empty())throw std::runtime_error("live manager version");return out;}
runtime::Profile synthetic(){
 runtime::Profile p;p.selection=runtime::Selection::Synthetic;p.qualification="synthetic-dispatch-v2";p.managerVersion=liveManagerVersion();p.powerRoutes="synthetic-awake-no-suspend";
 auto& t=p.timing;t.healthCadence=200*Ms;t.challengeLifetime=500*Ms;t.healthMaxAge=750*Ms;t.grantLifetime=5*S;t.drainLead=2*S;t.startupBudget=5*S;
 t.stopGrace=800*Ms;t.killGrace=800*Ms;t.proofWindow=3*S;t.sleepAllowance=8*S;t.clockSkew=10*Ms;t.workers=lc::DomainCount;
 for(std::uint32_t n=0;n<lc::DomainCount;++n)t.workerPolicies[n]={lc::kind(lc::Domain(n)),8,2*S};
 p.fuse={S,S,250*Ms};p.readerCeiling=20*S;p.ownerCeiling=30*S;p.callBudget=100*Ms;p.startBudget=2*S;p.stopBudget=500*Ms;
 need(runtime::valid(p),"synthetic dispatch profile");return p;
}
runtime::Profile qualifiedCopy(){auto p=synthetic();p.selection=runtime::Selection::Qualified;return p;} // test-injected only
// Test-only: in the sleep scenario a synthetic preparation withdraws power once
// accepted work completed; acknowledgement outcome is recorded as evidence.
struct SyntheticPower final:dispatch::PowerSource {
 bool verified()noexcept override{return scenario!="sleep"||!std::filesystem::exists(root+"/reader-work-done");}
 void applicationQuiesced(bool durable)noexcept override{try{mark("power-ack",durable?"1":"0");}catch(...){}}
};
std::vector<std::string> command(const char* role){return {root+"/helper",role,root,session,scenario};}
int runPlan(std::uint32_t launcherPid,pid_t frontend);
int owner(){
 const char* invocation=getenv("INVOCATION_ID");need(invocation&&recovery::hex32(invocation),"owner invocation");
 auto frontend=fork();need(frontend>=0,"frontend spawn");
 if(!frontend){for(int fd=3;fd<1024;++fd)close(fd);auto self=root+"/helper",pid=std::to_string(getppid());
  execl(self.c_str(),self.c_str(),"frontend",root.c_str(),session.c_str(),scenario.c_str(),pid.c_str(),invocation,nullptr);_exit(92);}
 return runPlan(std::uint32_t(frontend),frontend);
}
// Production launch shape: an independent original frontend starts the owner
// unit through dispatch::startOwner, then performs the one-use COMMIT.
int starter(){
 const auto self=std::to_string(getpid());
 auto started=dispatch::startOwner(synthetic(),session,{root+"/helper","owner-started",root,session,scenario,self});
 need(bool(started),"production owner unit start");mark("owner-started",std::to_string(started->pid));
 auto r=dispatch::launch(qualifiedCopy(),session,started->pid,started->invocation,Generation,LauncherEpoch);
 mark("frontend",r==dispatch::Launch::Committed?"committed":r==dispatch::Launch::Failed?"failed":"refused");return 0;
}
int runPlan(std::uint32_t launcherPid,pid_t frontend){
 dispatch::OwnerPlan plan;plan.roles=qualifiedCopy();plan.manager=synthetic();plan.session=session;plan.stock="inkbound-stock-v2-test-"+session+".service";
 plan.reader=command("reader");plan.fuse=command("fuse");plan.witness=command("witness");plan.generation=Generation;plan.launcherEpoch=LauncherEpoch;
 // D2: the owner's power-key device is a private FIFO here (power-frozen writes a KEY_POWER press into it).
 {const auto key=root+"/powerkey";if(access(key.c_str(),F_OK))need(!mkfifo(key.c_str(),0600),"power key fifo");static const int writerEnd=::open(key.c_str(),O_RDWR|O_NONBLOCK|O_CLOEXEC);need(writerEnd>=0,"power key writer end");plan.powerKey=key;}
 // reader-exec-fail: a root-owned regular file that cannot be executed, so the reader unit fails at exec
 // before any main process is observed; the owner must still abort before takeover and release.
 if(scenario=="reader-exec-fail"){const auto bad=root+"/badexec";{std::ofstream(bad)<<"not an executable\n";}need(!chmod(bad.c_str(),0755),"badexec mode");plan.reader={bad,"x"};}
 plan.launcherPid=launcherPid;
 SyntheticPower power;auto r=dispatch::runOwner(plan,power);
 if(frontend>0){int status{};waitpid(frontend,&status,0);}
 const char* outcome=r.outcome==dispatch::Outcome::Restored?"RESTORED":r.outcome==dispatch::Outcome::Aborted?"ABORTED":r.outcome==dispatch::Outcome::Unresolved?"UNRESOLVED":"REFUSED";
 if(!r.reportDetail.empty())mark("report-detail",r.reportDetail);
 mark("result",std::string(outcome)+" committed="+std::to_string(r.committed)+" takeover="+std::to_string(r.takeover)+" witnessed="+std::to_string(r.witnessed)+" restored="+std::to_string(r.restored)+
  " released="+std::to_string(r.released)+" report="+std::to_string(r.reportReceived)+" bound="+std::to_string(r.reportBound)+" durable="+std::to_string(r.reportDurable)+" closure="+std::to_string(unsigned(r.closure))+" stage="+r.stage);
 return 0;
}
int frontend(pid_t ownerPid,const std::string& invocation){
 if(scenario=="launcher-death"){mark("frontend","exit-before-commit");return 77;}
 auto r=dispatch::launch(qualifiedCopy(),session,std::uint32_t(ownerPid),invocation,Generation,LauncherEpoch);
 mark("frontend",r==dispatch::Launch::Committed?"committed":r==dispatch::Launch::Failed?"failed":"refused");return 0;
}
int readerRole(){
 // reader-fail: the reader process exits at once (e.g. an unloadable binary on a changed OS);
 // the owner must abort before takeover, never stop stock, and release its reservation.
 if(scenario=="reader-fail"){mark("reader-exited-early");return 1;}
 // D2 (user-ended sessions): bootstrap exactly like the production reader, then no grants at all.
 auto p=synthetic();auto permit=dispatch::readerBootstrap(p,session);need(bool(permit),"reader bootstrap");
 const auto& offer=permit->offer;lc::Domains domains(p.timing,offer.started.generation);need(domains.valid(),"reader domains");
 mark("reader-entered");
 auto canonical=domains.tracking(lc::Domain::Canonical),helper=domains.tracking(lc::Domain::Preparation);
 // power-frozen: the reader hangs; a child plays the user pressing the power button on the
 // owner's key device. The owner must give the reader drainLead, stop it and restore stock.
 if(scenario=="power-frozen"){if(fork()==0){usleep(800000);int k=::open((root+"/powerkey").c_str(),O_WRONLY|O_CLOEXEC);input_event e{};e.type=EV_KEY;e.code=KEY_POWER;e.value=1;if(k>=0){(void)!write(k,&e,sizeof e);close(k);}_exit(0);}mark("reader-frozen");for(;;)pause();}
 if(scenario=="reader-crash"){mark("reader-crashing");_exit(3);}
 progress::WorkToken a,b;need(canonical.accept(a)&&helper.accept(b),"accepted work");
 const bool failed=scenario=="failed-save";usleep(250000);canonical.complete(a,!failed);helper.complete(b,true);
 std::thread noise([&]{if(scenario!="bus-noise")return;std::vector<sd_bus*> clients;for(int n=0;n<150;++n){sd_bus* bus{};if(sd_bus_open_system(&bus)>=0)clients.push_back(bus);}try{mark("bus-noise-clients",std::to_string(clients.size()));}catch(...){}for(auto* bus:clients)sd_bus_flush_close_unref(bus);});
 // bus-stall (VM only): the matrix script (unconfined root) pauses the system bus daemon three times for
 // 300 ms while this session runs, past the 100 ms call budget, as PID1 stalled when the tablet's USB
 // cable was unplugged (run 8). The reader stays in the session long enough for all three.
 std::thread stall([&]{if(scenario=="bus-stall")usleep(1500000);});
 usleep(700000);noise.join();stall.join();mark("reader-closed","user");
 auto report=lc::finalReport(domains,offer.identity,offer.generation,0,{true,true,false,false,false,0,0});
 mark("report-durable",std::to_string(quiescence::durable(report)));need(dispatch::sendReport(session,report),"report delivered");mark("reader-exit");
 return 0;
}
}
int main(int argc,char** argv){
 try{
  need(argc>=5,"role root session scenario");std::string role=argv[1];root=argv[2];session=argv[3];scenario=argv[4];
  need(recovery::hex32(session.c_str())&&(root.starts_with("/var/lib/inkbound-runtime-v2-test-")||root.starts_with("/home/root/inkbound-e2/A/rt-"))&&geteuid()==0,"inert dispatch scope");
  if(role=="owner")return owner();
  if(role=="owner-started"&&argc==6)return runPlan(std::uint32_t(std::stoul(argv[5])),0);
  if(role=="starter")return starter();
  if(role=="frontend"&&argc==7)return frontend(pid_t(std::stoul(argv[5])),argv[6]);
  if(role=="reader")return readerRole();
  // Real independent evidence from the fuse's own manager connection; only the
  // route qualification flag is test-injected (no platform route exists in the VM).
  if(role=="fuse"){dispatch::ManagerFuseEvidence e(synthetic(),session,true);return dispatch::runFuse(qualifiedCopy(),session,e);}
  if(role=="witness"&&argc==8){mark("witness-start");int r=dispatch::runWitness(qualifiedCopy(),argv[5],std::stoull(argv[6]),std::stoull(argv[7]));mark("witness-exit",std::to_string(r));return r;}
  if(role=="stock"){mark("stock-start");for(;;)pause();}
  return 78;
 }catch(const std::exception& e){std::fprintf(stderr,"DISPATCH_INERT_FAIL %s\n",e.what());try{mark("failure",e.what());}catch(...){}return 78;}
}
