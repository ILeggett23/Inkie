// SPDX-License-Identifier: AGPL-3.0-only
#include "dispatch_v2.h"
#include "contract.h"
#include "e2_provisional_v2.h"
#include "session_power_v2.h"
#include <systemd/sd-bus.h>
#include <systemd/sd-daemon.h>
#include <systemd/sd-id128.h>
#include <systemd/sd-journal.h>
#include <systemd/sd-messages.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <csignal>
#include <vector>
#include <cstdio>
#include <sys/syscall.h>
#include <stdexcept>
#include <time.h>
#include <unistd.h>
namespace recovery::v2::dispatch {
namespace {
constexpr Ns Ms=1000000;
void need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
struct Fd {int fd{-1};explicit Fd(int f=-1):fd(f){}~Fd(){if(fd>=0)::close(fd);}Fd(const Fd&)=delete;Fd& operator=(const Fd&)=delete;int release(){int f=fd;fd=-1;return f;}};
bool hex32(const std::string& s){return s.size()==32&&recovery::hex32(s.c_str());}
std::string directory(const std::string& session){return std::string(Base)+"/"+session;}
std::string socketPath(const std::string& session){return directory(session)+"/"+RuntimeSocket;}
// The shipping Listener created this directory: root-owned, 0700, not a link.
bool privateDirectory(const std::string& session){struct stat st{};return !lstat(directory(session).c_str(),&st)&&S_ISDIR(st.st_mode)&&st.st_uid==0&&(st.st_mode&0777)==0700;}
bool address(const std::string& session,sockaddr_un& a){a={};a.sun_family=AF_UNIX;auto p=socketPath(session);if(p.size()>=sizeof a.sun_path)return false;std::memcpy(a.sun_path,p.c_str(),p.size()+1);return true;}
std::uint32_t peerPid(int fd,bool rootOnly=true){ucred c{};socklen_t n=sizeof c;if(getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&c,&n)||n!=sizeof c||c.pid<2||(rootOnly&&c.uid!=0))return 0;return std::uint32_t(c.pid);}
bool passCredentials(int fd){const int yes=1;return !setsockopt(fd,SOL_SOCKET,SO_PASSCRED,&yes,sizeof yes);}
int connectRuntime(const std::string& session){
 if(!hex32(session)||!privateDirectory(session))return -1;
 Fd fd{socket(AF_UNIX,SOCK_SEQPACKET|SOCK_CLOEXEC,0)};sockaddr_un a{};
 if(fd.fd<0||!address(session,a)||!passCredentials(fd.fd)||::connect(fd.fd,reinterpret_cast<sockaddr*>(&a),sizeof a)||!peerPid(fd.fd))return -1;
 return fd.release();
}
class RuntimeListener {
 Fd fd_;bool bound_{};std::string session_;
public:
 explicit RuntimeListener(const std::string& session):session_(session){
  need(privateDirectory(session),"runtime private directory");
  fd_.fd=socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0);sockaddr_un a{};
  need(fd_.fd>=0&&address(session,a)&&passCredentials(fd_.fd)&&!bind(fd_.fd,reinterpret_cast<sockaddr*>(&a),sizeof a),"runtime socket bind");bound_=true;
  need(!chmod(socketPath(session).c_str(),0600)&&!listen(fd_.fd,4),"runtime socket listen");
 }
 ~RuntimeListener(){if(bound_)unlink(socketPath(session_).c_str());}
 // One nonblocking accept; the caller authenticates the kernel peer.
 int accept(std::uint32_t& pid){pid=0;int c=accept4(fd_.fd,nullptr,nullptr,SOCK_CLOEXEC);if(c<0)return -1;if(!passCredentials(c)||!(pid=peerPid(c))){::close(c);pid=0;return -1;}return c;}
};
bootstrap::Packet receive(control::Channel& channel,Ns budget,std::uint64_t epoch){
 const auto until=sample(epoch).monotonic+budget;Wire wire{};
 for(;;){auto read=channel.receive(wire);if(read==control::Read::Packet){auto p=bootstrap::decode(wire);need(p.has_value(),"bootstrap decode");return *p;}
  need(read==control::Read::Empty&&sample(epoch).monotonic<until,"bootstrap receive bound");usleep(1000);}
}
void send(control::Channel& channel,const bootstrap::Packet& p){Wire wire{};need(bootstrap::encode(p,wire)&&channel.send(wire),"bootstrap send");}
bool same(const runtime::Profile& a,const runtime::Profile& b){auto x=a;x.selection=b.selection;return x==b;}
struct Entropy {Nonce value{};bool fill(){return getrandom(value.data(),value.size(),0)==ssize_t(value.size());}};
}
ClockPair sample(std::uint64_t epoch)noexcept{
 timespec m{},b{};if(!epoch||clock_gettime(CLOCK_MONOTONIC,&m)||clock_gettime(CLOCK_BOOTTIME,&b)||m.tv_sec<0||b.tv_sec<0)return {};
 return {Ns(m.tv_sec)*1000000000+Ns(m.tv_nsec),Ns(b.tv_sec)*1000000000+Ns(b.tv_nsec),epoch};
}
bool consistent(const OwnerPlan& p)noexcept{
 try{
  return roles::supported(p.roles)&&runtime::valid(p.manager)&&same(p.roles,p.manager)&&
   (p.manager.selection==runtime::Selection::Qualified||p.manager.selection==runtime::Selection::Synthetic)&&
   runtime::allowsStock(p.manager,p.session,p.stock)&&hex32(p.session)&&p.generation&&p.launcherEpoch&&p.launcherPid>1&&
   !p.reader.empty()&&!p.fuse.empty()&&!p.witness.empty();
 }catch(...){return false;}
}
OwnerResult runOwner(const OwnerPlan& plan,PowerSource& power)noexcept{
 OwnerResult r;
 try{
  r.stage="plan";e2::mark("owner:plan");if(!consistent(plan))return r;
  // 0. Stock start-limit headroom, read-only, before any reservation or reference.
  const auto headroom=stockStartHeadroom(plan.stock);if(!headroom.ok){r.stage="stock-start-limit:"+headroom.reason;return r;}
  const auto& t=plan.manager.timing;
  // 1. Shared legacy exclusion before any manager reference or mutation.
  r.stage="reservation";e2::mark("owner:reservation");launcher::Reservation reservation(plan.roles,plan.session);if(!reservation.held())return r;r.reserved=true;
  r.stage="manager";e2::mark("owner:manager");runtime::Manager manager(plan.manager,plan.session,plan.stock);manager.retainOwnerAndStock();
  const std::uint64_t epoch=manager.connection();need(epoch,"manager connection epoch");
  // 2. Typed one-use launcher COMMIT on the shipping endpoint.
  r.stage="launcher";e2::mark("owner:launcher");roles::Listener control(plan.roles,plan.session);need(control.valid(),"launcher listener");
  std::unique_ptr<roles::Socket> endpoint;const roles::Peer expected{plan.launcherPid,1};
  for(const auto until=sample(epoch).monotonic+t.startupBudget;!endpoint&&sample(epoch).monotonic<until;){
   auto a=control.poll({&expected,1});if(a.status==roles::Accept::Accepted)endpoint=std::move(a.socket);else{need(a.status==roles::Accept::Empty,"launcher accept");usleep(1000);}}
  // A frontend that never connects leaves no endpoint; the gate then cannot commit.
  const auto le=plan.launcherEpoch;launcher::OwnerCommit gate(reservation,manager,plan.roles,plan.generation,sample(le),std::move(endpoint));
  auto state=gate.poll(sample(le));
  for(const auto until=sample(le).monotonic+t.startupBudget;state==launcher::OwnerState::Waiting&&sample(le).monotonic<until;){usleep(1000);state=gate.poll(sample(le));}
  r.committed=gate.committed();gate.seal();
  if(!r.committed){
   r.stage="abort";e2::mark("owner:abort");r.outcome=Outcome::Aborted;
   // No reader was created, so no accepted application work can be pending.
   power.applicationQuiesced(true);
   if(auto proof=launcher::verifyCompletion(manager,reservation,gate))r.released=reservation.release(manager,std::move(*proof));
   if(!r.released)r.outcome=Outcome::Unresolved;
   return r;
  }
  // 3. After COMMIT the original owner holds restoration responsibility.
  r.stage="takeover";e2::mark("owner:takeover");r.outcome=Outcome::Unresolved;
  // Failures after COMMIT but before the stock app is stopped are recovered below without restoration.
  try{
  RuntimeListener runtimeSocket(plan.session);
  auto witness=plan.witness;witness.push_back(plan.session);witness.push_back(std::to_string(plan.generation));witness.push_back(std::to_string(epoch));
  manager.create(runtime::Unit::Fuse,plan.fuse);manager.create(runtime::Unit::Reader,plan.reader,witness);
  const auto identity=manager.bind();const auto fuse=manager.fuseBinding();const auto start=sample(epoch);
  int child=-1,fuseFd=-1;
  for(const auto until=start.monotonic+t.startupBudget;(child<0||fuseFd<0)&&sample(epoch).monotonic<until;){
   std::uint32_t pid{};int fd=runtimeSocket.accept(pid);if(fd<0){usleep(1000);continue;}
   if(pid==identity.mainPid&&child<0)child=fd;else if(pid==fuse.mainPid&&fuseFd<0)fuseFd=fd;else ::close(fd);
  }
  Fd childHeld{child},fuseHeld{fuseFd};need(child>=0&&fuseFd>=0,"original reader and fuse endpoints");
  bootstrap::Packet offer;offer.kind=bootstrap::Kind::Offer;offer.identity=identity;need(text(offer.profile,plan.manager.qualification),"offer profile");offer.generation=plan.generation;offer.started=start;
  {
   control::Channel childBoot(dup(child),1,0,identity.mainPid),fuseBoot(dup(fuseFd),1,0,fuse.mainPid);
   send(childBoot,offer);send(fuseBoot,offer);
   auto ack=offer;ack.kind=bootstrap::Kind::Acknowledged;
   need(receive(childBoot,t.startupBudget,epoch)==ack&&receive(fuseBoot,t.startupBudget,epoch)==ack,"exact bootstrap acknowledgements");
   // The stock invocation checked for headroom must be the one being stopped.
   need(manager.state(runtime::Unit::Stock).invocation==headroom.invocation,"stock invocation changed since start-limit check");
   manager.stopStock();r.takeover=true;
   for(const auto until=sample(epoch).monotonic+plan.manager.stopBudget+t.stopGrace+t.killGrace;;){auto s=manager.state(runtime::Unit::Stock);if(s.active=="inactive"&&!s.main&&!s.control&&!s.job)break;need(sample(epoch).monotonic<until,"stock stopped");usleep(1000);}
   auto permit=offer;permit.kind=bootstrap::Kind::Permit;send(childBoot,permit);
  }
  // 4. D2 user-ended session: no grants, health checks or ceilings. It ends only when the reader exits
  // (its own power-button handling, or a crash) or, if the reader is frozen, after the owner's own
  // power-button watch gives it drainLead to save and then stops it.
  r.stage="session";e2::mark("owner:session");
  sessionpower::EndSignals keys(plan.powerKey,{},~Ns(0)/4,sample(epoch).monotonic);need(keys.valid(),"owner power key");
  auto takeReport=[&]{std::uint32_t pid{};int fd=runtimeSocket.accept(pid);if(fd<0)return;
   if(pid!=identity.mainPid||r.reportReceived){::close(fd);return;}
   control::Channel channel(fd,3,0,identity.mainPid);Wire wire{};
   for(const auto until=sample(epoch).monotonic+500*1000000ull;sample(epoch).monotonic<until;){auto read=channel.receive(wire);
    if(read==control::Read::Packet){if(auto report=quiescence::decode(wire)){r.reportReceived=true;r.reportDurable=quiescence::durable(*report);}break;}
    if(read!=control::Read::Empty)break;usleep(1000);}};
  // A slow or failed manager (PID1) reply never ends the session: it is retried. Run 8 ended when PID1
  // stalled past the 100 ms call budget while the USB cable was unplugged. The reader's own process
  // handle observes its exit and can stop it on a power press even while PID1 is slow.
  unsigned busRetries=0;auto busRetry=[&]{if(++busRetries==1||busRetries%100==0)e2::mark("owner:bus-retry",busRetries);};
#ifdef SYS_pidfd_open
  const int readerFd=int(syscall(SYS_pidfd_open,pid_t(identity.mainPid),0));
#else
  const int readerFd=-1;
#endif
  auto readerGone=[&]{if(readerFd<0)return false;pollfd f{readerFd,POLLIN,0};return poll(&f,1,0)>0&&(f.revents&(POLLIN|POLLHUP|POLLERR));};
  auto signalReader=[&](int sig){
#ifdef SYS_pidfd_send_signal
   if(readerFd>=0)syscall(SYS_pidfd_send_signal,readerFd,sig,nullptr,0);
#else
   (void)sig;
#endif
  };
  Ns pressedAt=0,stopAt=0,goneAt=0;bool stopIssued=false,killed=false;
  // D2 sleep: a press while the reader is asleep (its wake lock released) or just after a resume may be the wake
  // itself. It still starts the drainLead backstop, which is cancelled only if the reader then takes its lock again
  // (alive and awake); a frozen reader never does, so it is still stopped.
  sessionpower::OwnerPresses presses(sessionpower::suspendOffset(nullptr));
  for(;;){
   const Ns now=sample(epoch).monotonic;
   try{takeReport();}catch(...){busRetry();}
   bool terminal=false;try{terminal=recovery::terminal(manager.state(runtime::Unit::Reader));}catch(...){busRetry();}
   if(terminal)break;
   // Exited but PID1 cannot confirm it yet: keep asking; restoration below has its own bounded retries.
   if(readerGone()){if(!goneAt){goneAt=now;e2::mark("owner:reader-exited");}else if(now-goneAt>=t.startupBudget)break;}
   if(!stopIssued){const auto press=pressedAt?sessionpower::EndSignals::Press::None:keys.takePress();
    if(press==sessionpower::EndSignals::Press::Lost&&!pressedAt){pressedAt=now;e2::mark("owner:power-key-lost");}
    else switch(presses.step(now,sessionpower::suspendOffset(nullptr),press==sessionpower::EndSignals::Press::Pressed,sessionpower::wakeLockActive("/sys/power",sessionpower::ReaderWakeLock))){
     case sessionpower::OwnerPresses::Event::Pressed:pressedAt=now;e2::mark("owner:power-pressed");break;
     case sessionpower::OwnerPresses::Event::PressedAsleep:pressedAt=now;e2::mark("owner:power-pressed-asleep");break;
     case sessionpower::OwnerPresses::Event::WakeConfirmed:pressedAt=0;e2::mark("owner:wake-press");break;
     default:break;}}
   if(pressedAt&&!stopIssued&&now-pressedAt>=t.drainLead){try{manager.stopReader();}catch(...){busRetry();signalReader(SIGTERM);}stopIssued=true;stopAt=now;e2::mark("owner:reader-stopped");}
   if(stopIssued&&!killed&&now-stopAt>=t.stopGrace+t.killGrace){signalReader(SIGKILL);killed=true;e2::mark("owner:reader-killed");}
   usleep(20000);
  }
  if(readerFd>=0)::close(readerFd);
  // 5. Single restoration of the stock app once the bound reader is terminal, then end the backstop.
  r.stage="restoration";e2::mark("owner:restoration");try{takeReport();}catch(...){busRetry();}
  const Ns restoredAt=sample(epoch).monotonic;
  for(const auto until=sample(epoch).monotonic+t.startupBudget;!manager.restoreUserEnded(identity);){need(sample(epoch).monotonic<until,"user-ended restoration");usleep(2000);}
  for(const auto until=sample(epoch).monotonic+plan.manager.startBudget;;){bool up=false;std::string restoredInvocation;try{auto s=manager.state(runtime::Unit::Stock);up=s.active=="active"&&s.sub=="running"&&s.main>1&&!s.control&&!s.job;restoredInvocation=s.invocation;}catch(...){busRetry();}
   if(up){if(!recordStockStart(restoredAt/1000,headroom.nrestarts,restoredInvocation))e2::mark("owner:stock-ledger-unwritten");break;}need(sample(epoch).monotonic<until,"restored stock readiness");usleep(1000);}
  r.restored=true;try{manager.stopFuse();}catch(...){busRetry();} // a fuse left running only re-checks stock (already up)
  for(const auto until=sample(epoch).monotonic+1000*1000000ull;!r.reportReceived&&sample(epoch).monotonic<until;){try{takeReport();}catch(...){busRetry();}usleep(5000);}
  power.applicationQuiesced(r.reportReceived&&r.reportDurable);
  // 6. Proof-gated reservation release from the same original Manager.
  r.stage="release";e2::mark("owner:release");
  for(const auto until=sample(epoch).monotonic+t.stopGrace+t.killGrace+plan.manager.fuse.leaseLifetime+plan.manager.fuse.dispatchMargin+plan.manager.startBudget;sample(epoch).monotonic<until;){bool done=false;try{done=recovery::terminal(manager.state(runtime::Unit::Fuse));}catch(...){busRetry();}if(done)break;usleep(1000);}
  if(auto proof=launcher::verifyCompletion(manager,reservation,gate))r.released=reservation.release(manager,std::move(*proof));
  r.outcome=r.released?Outcome::Restored:Outcome::Unresolved;r.stage="complete";e2::mark("owner:complete");
  return r;
  }catch(const std::exception& e){
   if(r.takeover)throw;
   // Stock was never stopped: stop what this session created, prove both roles terminated and
   // release the reservation through the same completion proof (stock-untouched branch).
   r.stage+=std::string(":")+e.what()+":abort-before-takeover";e2::mark("owner:abort-before-takeover");
   try{manager.stopReader();}catch(...){}
   try{manager.stopFuse();}catch(...){} // D2: the fuse backstop no longer expires on its own
   for(const auto until=sample(epoch).monotonic+t.stopGrace+t.killGrace+plan.manager.fuse.bootstrapLifetime+plan.manager.fuse.dispatchMargin+plan.manager.startBudget;
       !(recovery::terminal(manager.state(runtime::Unit::Reader))&&recovery::terminal(manager.state(runtime::Unit::Fuse)))&&sample(epoch).monotonic<until;)usleep(1000);
   power.applicationQuiesced(true); // no permit was sent, so no application work was accepted
   if(auto proof=launcher::verifyCompletion(manager,reservation,gate))r.released=reservation.release(manager,std::move(*proof));
   r.outcome=r.released?Outcome::Aborted:Outcome::Unresolved;return r;
  }
 }catch(const std::exception& e){r.stage+=std::string(":")+e.what();if(r.committed)r.outcome=Outcome::Unresolved;return r;}
 catch(...){r.stage+=":unknown";if(r.committed)r.outcome=Outcome::Unresolved;return r;}
}
Launch launch(const runtime::Profile& roles,const std::string& session,std::uint32_t ownerPid,const std::string& ownerInvocation,std::uint64_t generation,std::uint64_t launcherEpoch)noexcept{
 try{
  if(!roles::supported(roles)||!hex32(session)||!hex32(ownerInvocation)||ownerPid<2||!generation||!launcherEpoch)return Launch::Refused;
  std::uint64_t epoch=launcherEpoch;
  // The frontend started the owner unit; wait (bounded) for its listener before the single connect attempt.
  {struct stat st{};const auto path=directory(session)+"/control.sock";for(const auto until=sample(epoch).monotonic+roles.timing.startupBudget;lstat(path.c_str(),&st)||!S_ISSOCK(st.st_mode);){if(sample(epoch).monotonic>=until)return Launch::Failed;usleep(1000);}}
  roles::Connector connector(roles,session,{ownerPid,1});auto endpoint=connector.connect();if(!endpoint)return Launch::Failed;
  launcher::Client client(roles,session,ownerInvocation,generation,sample(epoch),std::move(endpoint));
  auto clock=[](void* c)noexcept{return sample(*static_cast<const std::uint64_t*>(c));};
  for(const auto until=sample(epoch).monotonic+roles.timing.startupBudget;sample(epoch).monotonic<until;){auto s=client.pollObserved(clock,&epoch);if(s==launcher::ClientState::Ready)break;if(s==launcher::ClientState::Failed)return Launch::Failed;usleep(1000);}
  if(client.state()!=launcher::ClientState::Ready||!client.commit(sample(epoch)))return Launch::Failed;
  // After COMMIT, ACK loss cannot revoke the owner's responsibility; report what was observed.
  for(const auto until=sample(epoch).monotonic+roles.timing.startupBudget;sample(epoch).monotonic<until;){auto s=client.pollObserved(clock,&epoch);if(s==launcher::ClientState::Complete)return Launch::Committed;if(s==launcher::ClientState::Failed)break;usleep(1000);}
  return Launch::Committed;
 }catch(...){return Launch::Failed;}
}
std::optional<Permit> readerBootstrap(const runtime::Profile& p,const std::string& session)noexcept{
 try{
  const char* invocation=getenv("INVOCATION_ID");if(!runtime::valid(p)||!invocation||!recovery::hex32(invocation))return {};
  Fd fd{connectRuntime(session)};if(fd.fd<0)return {};const auto owner=peerPid(fd.fd);
  control::Channel channel(dup(fd.fd),1,0,owner);need(channel.valid(),"reader bootstrap channel");
  // The offer carries the owner's epoch; bound the wait by a local monotonic budget first.
  Wire wire{};bootstrap::Packet offer;timespec m0{};clock_gettime(CLOCK_MONOTONIC,&m0);const Ns begin=Ns(m0.tv_sec)*1000000000+Ns(m0.tv_nsec);
  for(;;){auto read=channel.receive(wire);if(read==control::Read::Packet){auto d=bootstrap::decode(wire);need(d.has_value(),"offer decode");offer=*d;break;}
   timespec m{};clock_gettime(CLOCK_MONOTONIC,&m);need(read==control::Read::Empty&&Ns(m.tv_sec)*1000000000+Ns(m.tv_nsec)-begin<p.timing.startupBudget,"offer bound");usleep(1000);}
  const auto epoch=offer.started.generation;
  need(offer.kind==bootstrap::Kind::Offer&&std::string(offer.profile.data())==p.qualification&&std::string(offer.identity.session.data())==session&&offer.identity.ownerPid==owner,"selected offer");
  need(offer.identity.mainPid==std::uint32_t(getpid())&&std::string(offer.identity.invocation.data())==invocation,"actual reader identity");
  auto ack=offer;ack.kind=bootstrap::Kind::Acknowledged;send(channel,ack);
  bootstrap::Child gate;need(gate.offer(offer,channel.peer(),offer.identity,p.qualification,sample(epoch),p.timing),"reader offer accepted");
  auto permit=receive(channel,p.timing.startupBudget+p.stopBudget+p.timing.stopGrace+p.timing.killGrace,epoch);
  need(gate.permit(permit,channel.peer(),sample(epoch),p.timing),"reader original permit");
  return Permit{offer,fd.release()};
 }catch(...){return {};}
}
bool sendReport(const std::string& session,const quiescence::Report& report)noexcept{
 Wire wire{};if(!quiescence::encode(report,wire))return false;
 Fd fd{connectRuntime(session)};if(fd.fd<0)return false;
 return ::send(fd.fd,wire.data(),wire.size(),MSG_NOSIGNAL)==ssize_t(wire.size());
}
// D2 backstop: start the session's stock unit once if it is inactive or failed (owner lost).
static void startStockIfDown(const runtime::Profile& p,const std::string& session)noexcept{
 sd_bus* bus{};if(sd_bus_open_system(&bus)<0)return;
 const std::string unit=p.selection==runtime::Selection::Synthetic?"inkbound-stock-v2-test-"+session+".service":"xochitl.service";
 sd_bus_message* reply{};sd_bus_error error=SD_BUS_ERROR_NULL;const char* path{};std::string state;
 if(sd_bus_call_method(bus,"org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","LoadUnit",&error,&reply,"s",unit.c_str())>=0&&sd_bus_message_read(reply,"o",&path)>0&&path){
  char* active{};sd_bus_error pe=SD_BUS_ERROR_NULL;if(sd_bus_get_property_string(bus,"org.freedesktop.systemd1",path,"org.freedesktop.systemd1.Unit","ActiveState",&pe,&active)>=0&&active)state=active;free(active);sd_bus_error_free(&pe);}
 sd_bus_message_unref(reply);reply=nullptr;sd_bus_error_free(&error);
 if(state=="inactive"||state=="failed"){sd_bus_call_method(bus,"org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","StartUnit",&error,&reply,"ss",unit.c_str(),"replace");e2::mark("fuse:stock-started");}
 sd_bus_message_unref(reply);sd_bus_error_free(&error);sd_bus_flush_close_unref(bus);
}
int runFuse(const runtime::Profile& p,const std::string& session,FuseEvidenceSource& source)noexcept{
 try{
  const char* invocation=getenv("INVOCATION_ID");if(!roles::supported(p)||!invocation||!recovery::hex32(invocation))return 78;
  roles::FuseReady ready(p);Fd fd{connectRuntime(session)};if(fd.fd<0||!ready.announce(roles::notifyReady,nullptr))return 78;
  const auto owner=peerPid(fd.fd);control::Channel channel(dup(fd.fd),1,0,owner);need(channel.valid(),"fuse bootstrap channel");
  Wire wire{};bootstrap::Packet offer;timespec m0{};clock_gettime(CLOCK_MONOTONIC,&m0);const Ns begin=Ns(m0.tv_sec)*1000000000+Ns(m0.tv_nsec);
  for(;;){auto read=channel.receive(wire);if(read==control::Read::Packet){auto d=bootstrap::decode(wire);need(d.has_value(),"offer decode");offer=*d;break;}
   timespec m{};clock_gettime(CLOCK_MONOTONIC,&m);need(read==control::Read::Empty&&Ns(m.tv_sec)*1000000000+Ns(m.tv_nsec)-begin<p.fuse.bootstrapLifetime,"fuse offer bound");usleep(1000);}
  need(offer.kind==bootstrap::Kind::Offer&&std::string(offer.profile.data())==p.qualification&&std::string(offer.identity.session.data())==session&&offer.identity.ownerPid==owner,"fuse selected offer");
  auto ack=offer;ack.kind=bootstrap::Kind::Acknowledged;send(channel,ack);
  // D2 (user-ended sessions): backstop only, never a timer on the reader. If the owner disappears
  // (socket hang-up or process gone) the stock app is started once if it is down, then the fuse exits.
  (void)source;Fd keep{fd.release()};
  for(;;){
   pollfd hup{keep.fd,POLLIN|POLLHUP,0};const int ready=poll(&hup,1,200);
   const bool socketGone=ready>0&&(hup.revents&(POLLHUP|POLLERR|POLLNVAL));
   const bool processGone=kill(pid_t(owner),0)!=0&&errno==ESRCH;
   if(socketGone||processGone){e2::mark("fuse:owner-lost");startStockIfDown(p,session);return 0;}
  }
 }catch(...){return 78;}
}
int runWitness(const runtime::Profile& p,const std::string& session,std::uint64_t generation,std::uint64_t epoch)noexcept{
 try{
  e2::mark("witness:start");
  const char* invocation=getenv("INVOCATION_ID");if(!roles::supported(p)||!invocation||!recovery::hex32(invocation)||!generation||!epoch)return 78;
  // D2: there is no witness exchange; the owner restores once the reader is terminal.
  return 0;
  Fd fd{connectRuntime(session)};if(fd.fd<0)return 78;const auto owner=peerPid(fd.fd);
  auto socket=roles::Socket::adopt(p,fd.release(),{owner,1});if(!socket||!socket->valid())return 78;
  roles::WitnessContext context;if(!text(context.session,session)||!text(context.invocation,invocation))return 78;
  context.generation=generation;context.localStarted=sample(epoch);if(!context.localStarted.monotonic)return 78;
  roles::WitnessActor actor(p,context,std::move(socket));
  for(;;){auto step=actor.poll(sample(epoch));if(step==roles::WitnessStep::Complete)return 0;if(step==roles::WitnessStep::Failed)return 78;usleep(1000);}
 }catch(...){return 78;}
}
std::optional<Started> startOwner(const runtime::Profile& p,const std::string& session,const std::vector<std::string>& argv)noexcept{
 sd_bus* bus{};sd_bus_message* request{};sd_bus_message* reply{};sd_bus_error error=SD_BUS_ERROR_NULL;
 struct Cleanup {sd_bus*& b;sd_bus_message*& q;sd_bus_message*& r;sd_bus_error& e;~Cleanup(){sd_bus_message_unref(r);sd_bus_message_unref(q);sd_bus_error_free(&e);sd_bus_flush_close_unref(b);}} cleanup{bus,request,reply,error};
 try{
  SessionIdentity names;need(runtime::valid(p)&&hex32(session)&&text(names.session,session)&&setUnitNames(names),"owner unit names");
  need(geteuid()==0&&!argv.empty()&&argv[0].starts_with('/')&&argv.size()<=24,"owner command");
  const std::string owner=names.owner.data(),fuseName="inkbound-fuse-v2-"+session+".service";
  const auto u=runtime::requested(p,runtime::Role::Owner,owner,fuseName);need(!u.type.empty(),"owner requested profile");
  auto check=[](int rc,const char* what){if(rc<0)throw std::runtime_error(what);};
  check(sd_bus_open_system(&bus),"system bus");
  check(sd_bus_message_new_method_call(bus,&request,"org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","StartTransientUnit"),"owner transient");
  check(sd_bus_message_append(request,"ss",owner.c_str(),"fail"),"owner name");
  auto open=[&](char t,const char* sig){check(sd_bus_message_open_container(request,t,sig),"container");};auto close=[&]{check(sd_bus_message_close_container(request),"container close");};
  open('a',"(sv)");
  auto property=[&](const char* k,const char* t,auto append){open('r',"sv");check(sd_bus_message_append(request,"s",k),k);open('v',t);append();close();close();};
  auto str=[&](const char* k,const std::string& v){property(k,"s",[&]{check(sd_bus_message_append(request,"s",v.c_str()),k);});};
  auto num=[&](const char* k,std::uint64_t v){property(k,"t",[&]{check(sd_bus_message_append(request,"t",v),k);});};
  auto boolean=[&](const char* k,bool v){property(k,"b",[&]{check(sd_bus_message_append(request,"b",int(v)),k);});};
  auto integer=[&](const char* k,int v){property(k,"i",[&]{check(sd_bus_message_append(request,"i",v),k);});};
  auto array=[&](const char* k,const std::vector<std::string>& values){property(k,"as",[&]{open('a',"s");for(const auto& v:values)check(sd_bus_message_append(request,"s",v.c_str()),k);close();});};
  // Same mapping as Manager::create, except the owner writes its own session
  // directory and shared reservation, so no ReadOnlyPaths on /run/inkbound-owner-v2.
  str("Type",u.type);str("NotifyAccess",u.notifyAccess);str("Restart",u.restart);str("KillMode",u.killMode);str("CollectMode","inactive");
  num("RuntimeMaxUSec",u.runtimeUsec);num("TimeoutStartUSec",u.startUsec);num("TimeoutStopUSec",u.stopUsec);num("TimeoutAbortUSec",u.abortUsec);
  num("CapabilityBoundingSet",u.capabilities);num("LimitCORE",0);num("LimitCORESoft",0);
  boolean("SendSIGKILL",u.sendSigkill);boolean("NoNewPrivileges",u.noNewPrivileges);boolean("Delegate",u.delegate);boolean("ProtectControlGroups",true);
  integer("KillSignal",u.killSignal);integer("FinalKillSignal",u.finalKillSignal);
  array("Environment",{"INKBOUND_RECOVERY_V2_SESSION="+session});
  property("ExecStart","a(sasb)",[&]{open('a',"(sasb)");open('r',"sasb");check(sd_bus_message_append(request,"s",argv[0].c_str()),"exec");open('a',"s");for(const auto& a:argv)check(sd_bus_message_append(request,"s",a.c_str()),"exec");close();check(sd_bus_message_append(request,"b",0),"exec");close();close();});
  close();open('a',"(sa(sv))");close();
  check(sd_bus_call(bus,request,p.callBudget/1000,&error,&reply),"StartTransientUnit");
  // Read the started unit's original MainPID and invocation, bounded by the start budget.
  std::string path;{sd_bus_message* m{};sd_bus_error e=SD_BUS_ERROR_NULL;const char* o{};
   int rc=sd_bus_call_method(bus,"org.freedesktop.systemd1","/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","GetUnit",&e,&m,"s",owner.c_str());
   if(rc>=0&&sd_bus_message_read(m,"o",&o)>0&&o)path=o;sd_bus_message_unref(m);sd_bus_error_free(&e);}
  need(!path.empty(),"owner unit path");
  timespec m0{};clock_gettime(CLOCK_MONOTONIC,&m0);const Ns begin=Ns(m0.tv_sec)*1000000000+Ns(m0.tv_nsec);
  for(;;){
   std::uint32_t pid{};std::string invocation;
   {sd_bus_error e=SD_BUS_ERROR_NULL;if(sd_bus_get_property_trivial(bus,"org.freedesktop.systemd1",path.c_str(),"org.freedesktop.systemd1.Service","MainPID",&e,'u',&pid)<0)pid=0;sd_bus_error_free(&e);}
   {sd_bus_message* m{};sd_bus_error e=SD_BUS_ERROR_NULL;if(sd_bus_get_property(bus,"org.freedesktop.systemd1",path.c_str(),"org.freedesktop.systemd1.Unit","InvocationID",&e,&m,"ay")>=0){const void* bytes{};std::size_t count{};if(sd_bus_message_read_array(m,'y',&bytes,&count)>0&&count==16)for(std::size_t n=0;n<count;++n){auto b=static_cast<const unsigned char*>(bytes)[n];invocation+="0123456789abcdef"[b>>4];invocation+="0123456789abcdef"[b&15];}}sd_bus_message_unref(m);sd_bus_error_free(&e);}
   if(pid>1&&hex32(invocation)&&invocation!=std::string(32,'0'))return Started{pid,invocation};
   timespec m{};clock_gettime(CLOCK_MONOTONIC,&m);need(Ns(m.tv_sec)*1000000000+Ns(m.tv_nsec)-begin<p.startBudget,"owner start bound");usleep(1000);
  }
 }catch(...){return {};}
}
EventsPower::EventsPower(power::Config config)noexcept{
 try{events_=std::make_unique<power::Events>(std::move(config));started_=events_->start();}catch(...){events_.reset();started_=false;}
}
bool EventsPower::verified()noexcept{try{return started_&&events_&&events_->snapshot().admissionReady();}catch(...){return false;}}
void EventsPower::applicationQuiesced(bool boundDurable)noexcept{
 try{if(!boundDurable||!started_||!events_)return;auto s=events_->snapshot();if(s.state==power::State::Preparing)(void)events_->acknowledgeQuiesced(s.generation);}catch(...){}
}
std::vector<OwnStockStart> readStockStarts(const char* path)noexcept{
 std::vector<OwnStockStart> out;
 try{
  int fd=::open(path,O_RDONLY|O_NOFOLLOW|O_CLOEXEC);if(fd<0)return out;struct stat st{};
  if(fstat(fd,&st)||!S_ISREG(st.st_mode)||st.st_uid!=geteuid()||(st.st_mode&0077)||st.st_size>4096){::close(fd);return out;}
  char buffer[4097]{};ssize_t n=::read(fd,buffer,4096);::close(fd);if(n<0)return out;
  for(const char* line=buffer;*line;){const char* end=std::strchr(line,'\n');if(!end)return {};unsigned long long usec{};unsigned restarts{};char id[33]{};
   if(std::sscanf(line,"%llu %u %32s",&usec,&restarts,id)!=3||!hex32(id))return {};out.push_back({usec,restarts,id});line=end+1;}
 }catch(...){return {};}
 return out;
}
bool ownStartsAllow(const std::vector<OwnStockStart>& own,const std::string& invocation,std::uint32_t nrestarts,std::uint64_t nowUsec,std::uint64_t interval,unsigned burst,unsigned* recentOut)noexcept{
 if(own.empty()||!hex32(invocation))return false;unsigned recent=0;for(const auto& o:own)if(o.usec<=nowUsec&&nowUsec-o.usec<interval)++recent;if(recentOut)*recentOut=recent;
 return own.back().invocation==invocation&&own.back().nrestarts==nrestarts&&recent>0&&recent+1<=(burst>1?burst-1:0);
}
bool journalRecord(const std::vector<std::string>& fields,std::uint64_t usec,const std::string& unit,JournalStart& out)noexcept{
 try{
  std::string message,owner,pid;JournalStart r;r.usec=usec;
  for(const auto& f:fields){const auto eq=f.find('=');if(eq==std::string::npos)continue;const auto k=f.substr(0,eq),v=f.substr(eq+1);
   if(k=="MESSAGE_ID")message=v;else if(k=="UNIT")owner=v;else if(k=="_PID")pid=v;else if(k=="JOB_ID")r.job=v;else if(k=="INVOCATION_ID")r.invocation=v;}
  if(pid!="1"||owner!=unit)return false;
  if(message==SD_MESSAGE_UNIT_STARTED_STR)r.started=true;else if(message!=SD_MESSAGE_UNIT_FAILED_STR)return false;
  out=r;return true;
 }catch(...){return false;}
}
bool journalStartsAllow(const std::vector<JournalStart>& starts,const std::string& invocation,std::uint64_t nowUsec,std::uint64_t interval,unsigned burst,unsigned* recentOut)noexcept{
 try{
  if(!hex32(invocation))return false;std::vector<std::string> jobs;unsigned recent=0;bool seen=false;
  for(const auto& s:starts){if(s.usec<=nowUsec&&nowUsec-s.usec>=interval)continue; /* a record stamped after now still counts */
   if(s.started&&s.invocation==invocation)seen=true;
   if(!s.job.empty()){bool dup=false;for(const auto& j:jobs)dup=dup||j==s.job;if(dup)continue;jobs.push_back(s.job);}
   ++recent;}
  if(recentOut)*recentOut=recent;
  return seen&&recent>0&&recent+1<=(burst>1?burst-1:0);
 }catch(...){return false;}
}
bool readJournalStarts(const std::string& unit,std::uint64_t nowUsec,std::uint64_t interval,std::vector<JournalStart>& out,const char* directory,const char* bootId)noexcept{
 sd_journal* j{};
 struct Close{sd_journal*& j;~Close(){if(j)sd_journal_close(j);}} closer{j};
 try{
  out.clear();sd_id128_t boot{};char bootText[SD_ID128_STRING_MAX]{};
  if((bootId?sd_id128_from_string(bootId,&boot):sd_id128_get_boot(&boot))<0)return false;
  if((directory?sd_journal_open_directory(&j,directory,0):sd_journal_open(&j,SD_JOURNAL_LOCAL_ONLY|SD_JOURNAL_SYSTEM))<0)return false;sd_id128_to_string(boot,bootText);
  const std::string matches[]={std::string("_BOOT_ID=")+bootText,"_PID=1","UNIT="+unit,std::string("MESSAGE_ID=")+SD_MESSAGE_UNIT_STARTED_STR,std::string("MESSAGE_ID=")+SD_MESSAGE_UNIT_FAILED_STR};
  for(const auto& m:matches)if(sd_journal_add_match(j,m.c_str(),0)<0)return false;
  if(sd_journal_seek_tail(j)<0)return false;
  for(unsigned n=0;;++n){
   const int step=sd_journal_previous(j);if(step<0)return false;if(step==0)return true;if(n>=256)return false;
   std::uint64_t usec{};sd_id128_t entryBoot{};if(sd_journal_get_monotonic_usec(j,&usec,&entryBoot)<0||!sd_id128_equal(entryBoot,boot))return false;
   if(usec<=nowUsec&&nowUsec-usec>=interval)return true; /* older than the window; entries are in time order */
   std::vector<std::string> fields;
   for(const char* k:{"MESSAGE_ID","UNIT","_PID","JOB_ID","JOB_TYPE","INVOCATION_ID"}){const void* d{};std::size_t l{};if(sd_journal_get_data(j,k,&d,&l)>=0)fields.emplace_back(static_cast<const char*>(d),l);}
   JournalStart r;if(journalRecord(fields,usec,unit,r))out.push_back(r);
  }
 }catch(...){out.clear();return false;}
}
bool recordStockStart(std::uint64_t usec,std::uint32_t nrestarts,const std::string& invocation,const char* path)noexcept{
 try{
  if(!hex32(invocation))return false;auto entries=readStockStarts(path);entries.push_back({usec,nrestarts,invocation});
  if(entries.size()>16)entries.erase(entries.begin(),entries.end()-16);
  std::string text;for(const auto& e:entries)text+=std::to_string(e.usec)+" "+std::to_string(e.nrestarts)+" "+e.invocation+"\n";
  const std::string temp=std::string(path)+".new";::unlink(temp.c_str());
  int fd=::open(temp.c_str(),O_WRONLY|O_CREAT|O_EXCL|O_NOFOLLOW|O_CLOEXEC,0600);if(fd<0)return false;
  const bool written=::write(fd,text.data(),text.size())==ssize_t(text.size());::close(fd);
  if(!written||::rename(temp.c_str(),path)){::unlink(temp.c_str());return false;}
  return true;
 }catch(...){return false;}
}
StockHeadroom stockStartHeadroom(const std::string& stock)noexcept{
 StockHeadroom h;sd_bus* bus{};
 struct Close{sd_bus*& b;~Close(){sd_bus_flush_close_unref(b);}} closer{bus};
 try{
  auto fail=[&](const char* why){h.ok=false;h.reason=why;return h;};
  if(stock.empty()||sd_bus_open_system(&bus)<0)return fail("bus");
  const char* M="org.freedesktop.systemd1";std::string path;
  {sd_bus_message* m{};sd_bus_error e=SD_BUS_ERROR_NULL;const char* o{};
   int rc=sd_bus_call_method(bus,M,"/org/freedesktop/systemd1","org.freedesktop.systemd1.Manager","GetUnit",&e,&m,"s",stock.c_str());
   if(rc>=0&&sd_bus_message_read(m,"o",&o)>0&&o)path=o;sd_bus_message_unref(m);sd_bus_error_free(&e);}
  if(path.empty())return fail("unit");
  auto t=[&](const char* k,std::uint64_t& v){sd_bus_error e=SD_BUS_ERROR_NULL;int rc=sd_bus_get_property_trivial(bus,M,path.c_str(),"org.freedesktop.systemd1.Unit",k,&e,'t',&v);sd_bus_error_free(&e);return rc>=0;};
  std::uint64_t interval{},enter{};std::uint32_t burst{};char* active{};
  {sd_bus_error e=SD_BUS_ERROR_NULL;if(sd_bus_get_property_trivial(bus,M,path.c_str(),"org.freedesktop.systemd1.Unit","StartLimitBurst",&e,'u',&burst)<0){sd_bus_error_free(&e);return fail("burst");}sd_bus_error_free(&e);}
  {sd_bus_error e=SD_BUS_ERROR_NULL;if(sd_bus_get_property_string(bus,M,path.c_str(),"org.freedesktop.systemd1.Unit","ActiveState",&e,&active)<0||!active){sd_bus_error_free(&e);return fail("state");}sd_bus_error_free(&e);}
  const std::string state(active);free(active);
  if(!t("StartLimitIntervalUSec",interval)||!t("ActiveEnterTimestampMonotonic",enter))return fail("timestamps");
  {sd_bus_error e=SD_BUS_ERROR_NULL;if(sd_bus_get_property_trivial(bus,M,path.c_str(),"org.freedesktop.systemd1.Service","NRestarts",&e,'u',&h.nrestarts)<0){sd_bus_error_free(&e);return fail("restarts");}sd_bus_error_free(&e);}
  {sd_bus_message* m{};sd_bus_error e=SD_BUS_ERROR_NULL;
   if(sd_bus_get_property(bus,M,path.c_str(),"org.freedesktop.systemd1.Unit","InvocationID",&e,&m,"ay")>=0){const void* bytes{};std::size_t count{};if(sd_bus_message_read_array(m,'y',&bytes,&count)>0&&count==16)for(std::size_t n=0;n<count;++n){auto b=static_cast<const unsigned char*>(bytes)[n];h.invocation+="0123456789abcdef"[b>>4];h.invocation+="0123456789abcdef"[b&15];}}
   sd_bus_message_unref(m);sd_bus_error_free(&e);}
  timespec now{};if(clock_gettime(CLOCK_MONOTONIC,&now))return fail("clock");
  const std::uint64_t nowUsec=std::uint64_t(now.tv_sec)*1000000+std::uint64_t(now.tv_nsec)/1000;
  h.intervalUsec=interval;h.burst=burst;h.activeForUsec=enter&&nowUsec>enter?nowUsec-enter:0;
  if(state!="active"||!hex32(h.invocation)||!enter)return fail("stock-not-active");
  if(!burst)return fail("burst-zero");
  // StartLimitIntervalUSec==0 disables start limiting; otherwise require a full quiet window.
  if(interval&&h.activeForUsec<interval){
   // D2: inside the window only when every start in it is an Inkbound restoration (the running invocation is the latest one
   // recorded and no automatic restart happened since) and this session's own restoration still leaves one start spare.
   if(!ownStartsAllow(readStockStarts(),h.invocation,h.nrestarts,nowUsec,interval,burst,&h.recentOwnStarts)){
    /* Starts made by others (XOVI's restart after the power-button triple press) are counted from PID 1's journal records. */
    std::vector<JournalStart> starts;unsigned recent=0;
    if(!readJournalStarts(stock,nowUsec,interval,starts)){e2::mark("owner:stock-journal-unread");return fail("window");}
    const bool allowed=journalStartsAllow(starts,h.invocation,nowUsec,interval,burst,&recent);e2::mark("owner:stock-journal-starts",recent);
    if(!allowed)return fail("window");h.journalStarts=recent;}
  }
  h.ok=true;h.reason="ok";return h;
 }catch(...){h.ok=false;h.reason="exception";return h;}
}
}
