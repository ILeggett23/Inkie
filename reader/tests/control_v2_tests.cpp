// SPDX-License-Identifier: AGPL-3.0-only
#include "control_v2.h"
#include "lease_fuse_v2.h"
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <csignal>
#include <string>
#include <cstdlib>
#include <stdexcept>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <memory>
extern "C" void __lsan_do_leak_check() __attribute__((weak));
using namespace recovery::v2;
namespace {
unsigned checks{},cases{},endpointFailures{};
void check(bool b,const char* what){++checks;if(!b)throw std::runtime_error(what);}
void bytes(int fd,void* data,std::size_t n,bool send){auto p=static_cast<char*>(data);while(n){auto k=send?write(fd,p,n):read(fd,p,n);if(k<=0)throw std::runtime_error("fixture pipe");p+=k;n-=std::size_t(k);}}
ClockPair later(ClockPair p,Ns n){p.monotonic+=n;p.boottime+=n;return p;}
Timing timing(){auto t=syntheticTiming();t.grantLifetime=200;t.drainLead=60;t.stopGrace=20;t.killGrace=20;t.proofWindow=50;t.sleepAllowance=100;
 for(auto p:{&t.healthCadence,&t.challengeLifetime,&t.healthMaxAge,&t.grantLifetime,&t.drainLead,&t.startupBudget,&t.stopGrace,&t.killGrace,&t.proofWindow,&t.sleepAllowance,&t.clockSkew})*p*=1000;
 for(auto& p:t.workerPolicies)p.maxPendingAge*=1000;return t;}
SessionIdentity identity(pid_t child){SessionIdentity i;text(i.session,"0123456789abcdef0123456789abcdef");text(i.boot,"01234567-89ab-cdef-0123-456789abcdef");text(i.invocation,"11111111111111111111111111111111");text(i.ownerInvocation,"22222222222222222222222222222222");text(i.mount,"/sys/fs/cgroup/unified");i.ownerPid=getpid();i.mainPid=child;i.device=21;i.inode=4097;i.ticket.fill(0x7b);setUnitNames(i);return i;}
Nonce nonce(unsigned n){Nonce x{};x[0]=n;x[31]=0xa5;return x;}
struct Setup {SessionIdentity id{};fuse::Binding bound{};};
enum class Op {Publish,Accept,Complete,Failed,Shutdown,Poll,Admitted,Duplicate,RefuseWrites,Exit};
struct Command {Op op{Op::Poll};ClockPair now{1000000,9000000,11};Authority auth{true,true,true,true};};
struct Reply {bool ok{},closed{};unsigned fault{},reason{},notifications{};std::uint64_t sequence{},completed{},publication{};};
// Actual foreground producer thread; observer cannot publish via this channel.
struct Foreground {
 std::mutex mutex;std::condition_variable condition;std::function<void()> work;bool stopping{},done{};
 std::unique_ptr<progress::MainProgressChannel> channel;std::unique_ptr<progress::WorkLedger> ledger;progress::WorkToken token;Health health{};std::thread thread;
 Foreground():thread([this]{channel=std::make_unique<progress::MainProgressChannel>();ledger=std::make_unique<progress::WorkLedger>(WorkerKind::DurablePrefix,4);{std::lock_guard lock(mutex);done=true;}condition.notify_all();for(;;){std::function<void()> task;{std::unique_lock lock(mutex);condition.wait(lock,[&]{return stopping||bool(work);});if(stopping)return;task=std::move(work);}task();{std::lock_guard lock(mutex);done=true;}condition.notify_all();}}){std::unique_lock lock(mutex);condition.wait(lock,[&]{return done;});}
 void run(std::function<void()> task){std::unique_lock lock(mutex);done=false;work=std::move(task);condition.notify_all();condition.wait(lock,[&]{return done;});}
 ~Foreground(){{std::lock_guard lock(mutex);stopping=true;}condition.notify_all();thread.join();}
};
void childLoop(int control,int fd,Setup setup){Foreground foreground;Command start;
 control::ChildControl child(setup.id,7,timing(),start.now,fd,77,getuid(),*foreground.channel);char ready='r';bytes(control,&ready,1,true);
 for(;;){Command c;bytes(control,&c,sizeof(c),false);Reply r;
  if(c.op==Op::Exit)return;
  if(c.op==Op::Accept)foreground.run([&]{auto a=foreground.ledger->accept(c.now);foreground.token=a.token;r.ok=a.status==progress::Acceptance::Accepted;});
  if(c.op==Op::Complete||c.op==Op::Failed){std::thread worker([&]{r.ok=foreground.ledger->complete(foreground.token,c.op==Op::Failed?progress::Outcome::Failed:progress::Outcome::DurableCommitted);});worker.join();}
  if(c.op==Op::Publish||c.op==Op::Shutdown)foreground.run([&]{auto w=foreground.ledger->snapshot();auto& h=foreground.health;h.captured=h.mainCompleted=c.now;++h.mainSequence;h.workerCount=timing().workers;h.workers[0]=w.health;h.saveError=w.fault!=progress::Fault::None;h.shutdown=c.op==Op::Shutdown;r.ok=foreground.channel->publish(h);r.completed=w.health.completed;});
  if(c.op==Op::Poll)r.ok=child.poll(c.now,c.auth);
  if(c.op==Op::RefuseWrites)r.ok=shutdown(fd,SHUT_RD)==0;
  if(c.op==Op::Admitted)r.ok=child.admitted(c.now,c.auth);
  if(c.op==Op::Duplicate){Message m;m.kind=Kind::Health;m.identity=setup.id;m.generation=7;m.sequence=1;m.responseNonce=nonce(1);m.health=foreground.health;Wire raw;check(encode(m,raw),"duplicate encodes");r.ok=send(fd,raw.data(),raw.size(),MSG_NOSIGNAL)==ssize_t(raw.size());}
  r.closed=child.closed();r.fault=unsigned(child.fault());r.reason=unsigned(child.reason());r.sequence=child.grantSequence();r.publication=foreground.channel->read().publication;bytes(control,&r,sizeof(r),true);
 }
}
void fuseLoop(int control,int fd,Setup setup){struct Notifications {unsigned count{};static int notify(void* p,std::uint64_t)noexcept{++static_cast<Notifications*>(p)->count;return 1;}} notifications;
 fuse::OwnerChannel channel(fd,88,getuid(),setup.id.ownerPid);Command start;fuse::Evidence e{setup.bound,setup.id,true,true,true,true,true,true};
 fuse::Lease lease(setup.id,setup.bound,7,timing(),{25000,35000,5000},channel.peer(),getuid(),start.now,&Notifications::notify,&notifications);char ready='r';bytes(control,&ready,1,true);
 for(;;){Command c;bytes(control,&c,sizeof(c),false);if(c.op==Op::Exit)return;Reply r;r.ok=c.op==Op::RefuseWrites?shutdown(fd,SHUT_RD)==0:channel.receive(lease,c.now,e);r.closed=lease.closed();r.sequence=lease.lastSequence();r.notifications=notifications.count;bytes(control,&r,sizeof(r),true);}
}
struct Endpoint {
 pid_t pid{};int command{-1},fd{-1};std::string path;
 Endpoint(const std::string& pathValue,bool fuse):path(pathValue){int pair[2];check(!socketpair(AF_UNIX,SOCK_STREAM,0,pair),"fixture commands");pid=fork();check(pid>=0,"fork endpoint");
  if(!pid){close(pair[0]);try{int listener=socket(AF_UNIX,SOCK_SEQPACKET,0);sockaddr_un a{};a.sun_family=AF_UNIX;std::memcpy(a.sun_path,path.c_str(),path.size()+1);if(bind(listener,reinterpret_cast<sockaddr*>(&a),sizeof(a))||listen(listener,1))_exit(91);char ready='l';bytes(pair[1],&ready,1,true);int peer=accept(listener,nullptr,nullptr);close(listener);Setup setup;bytes(pair[1],&setup,sizeof(setup),false);if(fuse)fuseLoop(pair[1],peer,setup);else childLoop(pair[1],peer,setup);close(pair[1]);if(__lsan_do_leak_check)__lsan_do_leak_check();_exit(0);}catch(...){_exit(92);}}
  close(pair[1]);command=pair[0];char ready;bytes(command,&ready,1,false);fd=socket(AF_UNIX,SOCK_SEQPACKET,0);sockaddr_un a{};a.sun_family=AF_UNIX;std::memcpy(a.sun_path,path.c_str(),path.size()+1);check(!connect(fd,reinterpret_cast<sockaddr*>(&a),sizeof(a)),"connect original endpoint");
 }
 void setup(Setup s){bytes(command,&s,sizeof(s),true);char ready;bytes(command,&ready,1,false);}
 Reply call(Op op,ClockPair now,Authority auth={true,true,true,true}){Command c{op,now,auth};bytes(command,&c,sizeof(c),true);Reply r;bytes(command,&r,sizeof(r),false);return r;}
 int release(){int result=fd;fd=-1;return result;}
 ~Endpoint(){if(command>=0){Command c;c.op=Op::Exit;try{bytes(command,&c,sizeof(c),true);}catch(...){}close(command);}if(fd>=0)close(fd);int status{};if(pid>0){if(waitpid(pid,&status,0)!=pid||!WIFEXITED(status)||WEXITSTATUS(status)!=0)++endpointFailures;}unlink(path.c_str());}
};
struct Rig {
 std::string dir;std::unique_ptr<Endpoint> child,fuse;std::unique_ptr<control::OwnerControl> owner;ClockPair now{1000000,9000000,11};Authority auth{true,true,true,true};
 Rig(){char temp[]="/tmp/control-v2.XXXXXX";auto p=mkdtemp(temp);check(p,"private fixture");dir=p;child=std::make_unique<Endpoint>(dir+"/child",false);fuse=std::make_unique<Endpoint>(dir+"/fuse",true);Setup s;s.id=identity(child->pid);s.bound.mainPid=fuse->pid;s.bound.originalReference=71;text(s.bound.invocation,"33333333333333333333333333333333");fuse::setUnitName(s.bound,s.id);child->setup(s);fuse->setup(s);owner=std::make_unique<control::OwnerControl>(s.id,7,timing(),now,child->release(),77,getuid(),fuse->release(),88,getuid(),fuse->pid);check(!owner->closed(),"production owner adapter initialized");}
 ~Rig(){owner.reset();child.reset();fuse.reset();rmdir(dir.c_str());}
 void challenge(unsigned n){auto entropy=nonce(n);check(owner->poll(now,auth,&entropy),"owner sends challenge");check(child->call(Op::Poll,now).ok,"child handles challenge or waits");}
 void publish(){check(child->call(Op::Publish,now).ok,"foreground publication");}
 void grant(){check(child->call(Op::Poll,now).ok,"observer responds from foreground");check(owner->poll(now,auth),"owner validates actual health and forwards");auto f=fuse->call(Op::Poll,now);check(f.ok&&f.sequence==owner->grantSequence(),"actual fuse accepts identical issued grant");auto c=child->call(Op::Poll,now);check(c.ok&&c.sequence==owner->grantSequence(),"child accepts same grant");check(owner->admitted(now,auth)&&child->call(Op::Admitted,now).ok,"both admitted");}
 void first(){challenge(1);publish();grant();check(owner->grantSequence()==1,"first exact grant");}
};
template<class F>void test(const char* name,F body){body();++cases;std::printf("PASS %s\n",name);}
}
int main(){signal(SIGPIPE,SIG_IGN);try{
 test("fresh_publication_waits_and_sustained_grants",[]{Rig r;r.first();for(unsigned n=2;n<=8;++n){r.now=later(r.now,10000);r.challenge(n);check(r.owner->grantSequence()==n-1,"old foreground cannot renew");r.publish();r.grant();}check(r.fuse->call(Op::Poll,r.now).notifications==8,"exactly one notify per real main progress");});
 test("worker_and_observer_continue_without_foreground_renewal",[]{Rig r;check(r.child->call(Op::Accept,r.now).ok,"accepted durable work");r.first();r.now=later(r.now,10000);r.challenge(2);check(r.child->call(Op::Complete,r.now).ok,"real worker completes while main stopped");for(unsigned n=0;n<3;++n){check(r.child->call(Op::Poll,r.now).ok,"observer continues");check(r.owner->poll(r.now,r.auth),"owner continues");check(r.fuse->call(Op::Poll,r.now).notifications==1,"background did not renew");}r.now=later(r.now,20000);check(!r.child->call(Op::Admitted,r.now).ok,"deferred challenge deadline closes admission");check(!r.owner->poll(r.now,r.auth),"owner challenge deadline closes");r.now=later(r.now,5000);check(r.fuse->call(Op::Poll,r.now).closed,"fuse closed without foreground progress");});
 test("failed_save_is_sticky_and_terminal",[]{Rig r;check(r.child->call(Op::Accept,r.now).ok,"work accepted");r.first();check(r.child->call(Op::Failed,r.now).ok,"worker records failed durability");r.now=later(r.now,1000);r.publish();auto c=r.child->call(Op::Poll,r.now);check(!c.ok&&c.closed&&c.fault==unsigned(control::Fault::Progress),"published failed save closes observer");check(!r.owner->poll(r.now,r.auth),"EOF closes owner");check(r.fuse->call(Op::Poll,r.now).notifications==1,"failed save cannot renew");});
 test("shutdown_closes_without_waiting_for_new_challenge",[]{Rig r;r.first();r.now=later(r.now,1000);check(r.child->call(Op::Shutdown,r.now).ok,"foreground quiescence published");check(r.child->call(Op::Poll,r.now).closed,"stop flag closes child");check(!r.owner->poll(r.now,r.auth),"stop reaches owner");});
 test("reference_loss_and_final_clock_loss_close",[]{for(unsigned variant=0;variant<4;++variant){Rig r;r.first();auto bad=r.now;auto a=r.auth;if(variant==0)a.reference=false;if(variant==1)bad.boottime+=10000;if(variant==2)--bad.monotonic;if(variant==3)++bad.generation;check(!r.owner->admitted(bad,a)&&r.owner->closed(),"owner final authority/clock check seals");check(r.owner->closure().has_value(),"closure retained");if(variant==0)check(!r.owner->closure()->originalReference,"lost original ref cannot prove restoration");else check(!r.owner->closure()->clockReliable,"bad final clock cannot prove clean closure");check(r.fuse->call(Op::Poll,r.now).notifications==1,"no post-failure renewal");}});
 test("child_reference_and_clock_loss_close",[]{for(unsigned variant=0;variant<2;++variant){Rig r;r.first();auto bad=r.now;auto a=r.auth;if(!variant)a.reference=false;else bad.boottime+=10000;check(r.child->call(Op::Poll,bad,a).closed,"child final authority/clock failure");check(!r.owner->poll(r.now,r.auth),"peer failure reaches owner");}});
 test("fuse_or_child_send_failure_is_terminal_without_retry",[]{for(unsigned variant=0;variant<2;++variant){Rig r;r.challenge(1);r.publish();check(r.child->call(Op::Poll,r.now).ok,"actual health queued");check((variant?r.child:r.fuse)->call(Op::RefuseWrites,r.now).ok,"peer refuses further reads");check(!r.owner->poll(r.now,r.auth)&&r.owner->closed()&&r.owner->fault()==control::Fault::Send,"grant send failure seals owner");auto entropy=nonce(2);check(!r.owner->poll(r.now,r.auth,&entropy),"no partial-grant retry");check(r.child->call(Op::Poll,r.now).closed,"reader cannot continue after send failure");if(variant){auto f=r.fuse->call(Op::Poll,r.now);check(f.notifications==1,"fuse-first grant was sent once");check(r.fuse->call(Op::Poll,r.now).closed,"partial delivery followed by terminal closure");}else check(r.fuse->call(Op::Poll,r.now).notifications==0,"failed fuse send never notifies");}});
 test("duplicate_health_seals_without_extra_grant",[]{Rig r;r.first();check(r.child->call(Op::Duplicate,r.now).ok,"real original peer sends duplicate");check(!r.owner->poll(r.now,r.auth)&&r.owner->closed(),"owner rejects replay");check(r.owner->grantSequence()==1&&r.fuse->call(Op::Poll,r.now).notifications==1,"no replay renewal");});
 test("normal_owner_close_is_terminal_and_nonrestoring",[]{Rig r;r.first();r.owner->close(r.now);check(r.owner->closed()&&r.owner->reason()==Reason::Requested,"explicit owner close");check(r.child->call(Op::Poll,r.now).closed&&r.fuse->call(Op::Poll,r.now).closed,"both actual peers close");auto n=nonce(2);check(!r.owner->poll(r.now,r.auth,&n),"no reopening or regrant");});
 test("kernel_channel_rejects_inherited_writer_truncation_rights_eof",[]{for(unsigned variant=0;variant<4;++variant){int fds[2];check(!socketpair(AF_UNIX,SOCK_SEQPACKET,0,fds),"socketpair channel boundary");control::Channel receiver(fds[1],99,getuid(),getpid());check(receiver.valid(),"original kernel peer");Wire wire{};
  if(variant==0){auto p=fork();check(p>=0,"foreign sender");if(!p){send(fds[0],wire.data(),wire.size(),0);_exit(0);}int s;waitpid(p,&s,0);}
  if(variant==1){std::array<char,WireBytes+1> large{};send(fds[0],large.data(),large.size(),0);}
  if(variant==2){int p[2];check(!pipe(p),"rights fixture");iovec io{wire.data(),wire.size()};msghdr m{};m.msg_iov=&io;m.msg_iovlen=1;alignas(cmsghdr)std::array<char,CMSG_SPACE(sizeof(int))> buffer{};m.msg_control=buffer.data();m.msg_controllen=buffer.size();auto c=CMSG_FIRSTHDR(&m);c->cmsg_level=SOL_SOCKET;c->cmsg_type=SCM_RIGHTS;c->cmsg_len=CMSG_LEN(sizeof(int));std::memcpy(CMSG_DATA(c),p,sizeof(int));sendmsg(fds[0],&m,0);close(p[0]);close(p[1]);}
  if(variant==3){close(fds[0]);fds[0]=-1;}check(receiver.receive(wire)==control::Read::Failed&&!receiver.valid(),"kernel rejection seals transport");if(fds[0]>=0)close(fds[0]);}});
 check(endpointFailures==0,"all endpoint subprocesses and their leak checks exited zero");
 std::printf("CONTROL_V2_PASS cases=%u checks=%u policy=SYNTHETIC manager=NOT_INVOKED production_wiring=CALLER_REQUIRED\n",cases,checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"CONTROL_V2_FAIL %s\n",e.what());return 1;}}
