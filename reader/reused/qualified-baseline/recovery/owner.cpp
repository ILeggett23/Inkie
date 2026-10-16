// SPDX-License-Identifier: AGPL-3.0-only
#include "bus.h"
#include "session_limits.h"
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/prctl.h>
#include <sys/random.h>
#include <sys/wait.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <signal.h>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <filesystem>
using namespace recovery;
static volatile sig_atomic_t quit=0;
static void signalQuit(int){quit=1;}
static std::string dir;
static void event(const std::string&s){std::printf("OWNER %s\n",s.c_str());std::fflush(stdout);if(!dir.empty())writeFile(dir+"/events.log",s+"\n");}
static std::string stock(const std::string&s){
#ifdef OWNER_VM_TEST
 return "inkbound-stock3-"+s+".service";
#else
 (void)s;return "xochitl.service";
#endif
}
static std::vector<Property> policy(Ns runtime){return {{"Type","s","exec"},{"Restart","s","no"},{"KillMode","s","control-group"},{"RuntimeMaxUSec","t","",runtime/1000},{"TimeoutStopUSec","t","",1000000},{"SendSIGKILL","b","",1},{"CollectMode","s","inactive-or-failed"}};}
static void trusted(const std::string&p){struct stat s{};if(lstat(p.c_str(),&s)||!S_ISREG(s.st_mode)||s.st_uid!=0||(s.st_mode&0022)||!(s.st_mode&0100))throw std::runtime_error("untrusted executable");}
static bool stockReady(const State&s){
#ifdef OWNER_VM_TEST
 return s.active=="active"&&(s.sub=="exited"||s.sub=="running")&&s.control==0&&s.job==0;
#else
 return s.active=="active"&&s.sub=="running"&&s.main>1&&s.control==0&&s.job==0;
#endif
}
static bool waitStockStopped(Bus&bus,const std::string&name,Ns end,int launcher){while(now()<end&&!quit){auto s=bus.state(name);if(s.active=="inactive"&&s.main==0&&s.control==0&&s.job==0)return true;if(launcher>=0&&!connected(launcher))return false;usleep(10000);}return false;}
static Packet hello(Kind k,const std::string&s){Packet p;p.kind=k;copy(p.binding.session,sizeof p.binding.session,s);return p;}
static int acceptHello(int listener,Packet&p,int ms){pollfd f{listener,POLLIN,0};if(poll(&f,1,ms)<=0)return -1;int fd=accept4(listener,nullptr,nullptr,SOCK_CLOEXEC);if(fd<0)return -1;if(peer(fd)<2||!receivePacket(fd,p,200)){close(fd);return -1;}return fd;}
static void cleanup(const std::string&s){unlink(socketPath(s).c_str());unlink((dir+"/events.log").c_str());if(rmdir(dir.c_str()))throw std::runtime_error("session cleanup");if(rmdir((std::string(Base)+"/active").c_str()))throw std::runtime_error("reservation cleanup");}
static int witness(const std::string&s){
 int fd=connectOwner(s,2500);if(fd<0)return 78;Packet h=hello(Kind::Witness,s);const char*inv=getenv("INVOCATION_ID");if(!inv||!hex32(inv)){close(fd);return 78;}copy(h.binding.invocation,sizeof h.binding.invocation,inv);
 Packet p;bool ok=sendPacket(fd,h)&&receivePacket(fd,p,2500)&&p.kind==Kind::Binding&&p.closed==1&&p.binding.session==s&&std::string(p.binding.invocation)==inv&&p.binding.ownerPid==unsigned(peer(fd))&&membershipWitness(p.binding,getpid());
 if(ok){p.kind=Kind::Receipt;p.drained=1;ok=sendPacket(fd,p);Packet ack;ok=ok&&receivePacket(fd,ack,500)&&ack.kind==Kind::Ack;}
 close(fd);return ok?0:78;
}
static int own(const std::string&s,const std::string&testCase){
 (void)testCase;
 dir=std::string(Base)+"/"+s;Bus bus;std::string self=executable(),child=childUnit(s),owner=ownerUnit(s),target=stock(s);trusted(self);
 auto ownState=bus.state(owner);if(ownState.main!=unsigned(getpid())||ownState.active!="active")throw std::runtime_error("owner not PID1 managed");
 int listener=listenOwner(s),launch=-1,childfd=-1,pendingWitness=-1;Packet pendingPacket;bool stopped=false,reference=false,closed=false,restoring=false,authenticated=false;Packet receipt;Binding bound{};Ns start=now();
 auto closeAdmission=[&]{if(!closed){closed=true;event("ADMISSION_CLOSED");if(childfd>=0){auto p=hello(Kind::Stop,s);sendPacket(childfd,p);}}};
 auto manual=[&](const std::string&why){closeAdmission();event("MANUAL_RECOVERY_REQUIRED reason="+why);if(childfd>=0)close(childfd);if(launch>=0)close(launch);close(listener);return 78;};
 try {
 event("CREATED session="+s+" owner_pid="+std::to_string(getpid()));
 Packet lp;while(now()<start+3000000000ULL&&!quit){launch=acceptHello(listener,lp,50);if(launch>=0)break;}
 if(launch<0||lp.kind!=Kind::Launcher||!hex32(lp.binding.session)||lp.binding.session!=s)return manual("launcher_missing");
 if(!stockReady(bus.state(target))){event("BASELINE_INACTIVE_NO_START");close(launch);close(listener);cleanup(s);return 78;}
 bus.call("RefUnit",target);
 // Bootstrap cannot enter display code until this owner binds its real PID and
 // sends the exact one-use permit. PID1 limits and BindsTo are independent of us.
 auto cp=policy(limits::Child);cp.push_back({"BindsTo","as",owner});cp.push_back({"After","as",owner});cp.push_back({"Environment","as","INKBOUND_RECOVERY_SESSION="+s});cp.push_back({"Delegate","b","",0});cp.push_back({"NoNewPrivileges","b","",1});cp.push_back({"ProtectControlGroups","b","",1});cp.push_back({"CapabilityBoundingSet","t","",0});cp.push_back({"ReadOnlyPaths","as",Base});
 // Vendor initialization requires FIFO99/98. Keep all capabilities absent;
 // bound unprivileged RT priority and continuous RT CPU, not session lifetime.
 cp.push_back({"LimitRTPRIO","t","",99});
 cp.push_back({"LimitRTPRIOSoft","t","",99});
 cp.push_back({"LimitRTTIME","t","",100000});
 cp.push_back({"LimitRTTIMESoft","t","",100000});
 std::vector<std::string> command;
#ifdef OWNER_VM_TEST
 command={std::filesystem::path(self).parent_path().string()+"/recovery-client-vm",testCase};
#else
 std::string payload=std::filesystem::path(self).parent_path().string()+"/inkbound-direct-handwriting-spike3";trusted(payload);command={payload,"--direct"};
#endif
 bus.transient(child,cp,command,{self,"--witness",s});bus.call("RefUnit",child);reference=true;
 State live;for(unsigned i=0;i<150;++i){live=bus.state(child);if(live.main&&hex32(live.invocation)&&!live.group.empty())break;if(!connected(launch))throw std::runtime_error("launcher_lost_prepermit");usleep(10000);}
 copy(bound.session,sizeof bound.session,s);copy(bound.boot,sizeof bound.boot,boot());copy(bound.unit,sizeof bound.unit,child);copy(bound.owner,sizeof bound.owner,owner);copy(bound.invocation,sizeof bound.invocation,live.invocation);copy(bound.ownerInvocation,sizeof bound.ownerInvocation,ownState.invocation);copy(bound.group,sizeof bound.group,live.group);copy(bound.mount,sizeof bound.mount,managerMount());bound.ownerPid=getpid();bound.mainPid=live.main;bound.deadline=now()+limits::Payload;
 if(getrandom(bound.ticket.data(),bound.ticket.size(),0)!=ssize_t(bound.ticket.size()))throw std::runtime_error("ticket entropy");
 struct stat st{};auto group=std::string(bound.mount)+bound.group;if(lstat(group.c_str(),&st)||!S_ISDIR(st.st_mode))throw std::runtime_error("live group");bound.device=st.st_dev;bound.inode=st.st_ino;
 if(!validBinding(bound)||readSmall("/proc/"+std::to_string(live.main)+"/cgroup").find("0::"+live.group+"\n")==std::string::npos)throw std::runtime_error("live binding");
 event("REFERENCE_BOUND unit="+child+" invocation="+live.invocation+" deadline_ns="+std::to_string(bound.deadline));
 Packet ch;for(unsigned i=0;i<100&&childfd<0;++i){int fd=acceptHello(listener,ch,20);if(fd>=0){if(ch.kind==Kind::Child&&ch.binding.session==s&&std::string(ch.binding.invocation)==live.invocation&&peer(fd)==pid_t(live.main))childfd=fd;else close(fd);}if(!connected(launch))throw std::runtime_error("launcher_lost_prepermit");}
 if(childfd<0)throw std::runtime_error("bootstrap missing");
 event("RECOVERY_READY");if(!sendPacket(launch,hello(Kind::Ready,s)))throw std::runtime_error("launcher ready lost");
 Packet commit;if(!receivePacket(launch,commit,1500)||commit.kind!=Kind::Commit||commit.binding.session!=s||!connected(launch))throw std::runtime_error("launcher_lost_prepermit");
 // Linearize the launcher's one-use admission COMMIT while it is alive. Its
 // frontend may now exit before StopUnit kills the AppLoad/Xochitl cgroup.
 // Runtime entry remains gated separately: no child permit is sent until stock
 // is actually stopped. Launcher/controller loss AFTER COMMIT cannot lose RefUnit.
 event("PAYLOAD_PERMITTED admission_committed=1 runtime_release_pending=1");
 sendPacket(launch,hello(Kind::Granted,s));close(launch);launch=-1;
 stopped=true;event("STOCK_STOP_REQUESTED");bus.call("StopUnit",target,true);
 if(!waitStockStopped(bus,target,now()+2000000000ULL,-1))throw std::runtime_error("stock_stop");
 if(now()>=bound.deadline)throw std::runtime_error("deadline_before_runtime_release");
 Packet permit;permit.kind=Kind::Permit;permit.binding=bound;
 if(!sendPacket(childfd,permit))throw std::runtime_error("permit delivery");
 event("PAYLOAD_RUNNING runtime_permit_released=1");
 while(now()<bound.deadline&&!quit){
  if(!bus.alive())return manual("reference_lost");
  auto state=bus.state(child);if(state.invocation!=bound.invocation)return manual("invocation_changed");
  if(state.main==0||state.active!="active"||!connected(childfd))break;
  int fd=acceptHello(listener,ch,20);
  if(fd>=0){
   if(std::string(ch.binding.session)!=s){close(fd);continue;}
#ifdef OWNER_VM_TEST
   if(ch.kind==Kind::DropReference){bus.disconnect();close(fd);return manual("actual_reference_connection_closed");}
#endif
   if(ch.kind==Kind::Witness){pendingWitness=fd;pendingPacket=ch;break;}
   if(ch.kind==Kind::Recover){event("RECOVERY_REQUEST_RECEIVED");close(fd);break;}close(fd);
  }
 }
 closeAdmission();event("TERMINATING");bus.call("StopUnit",child,true);
 } catch(const std::exception&ex){
  event("RECOVERY_TRIGGER "+std::string(ex.what()));closeAdmission();
  if(!reference){if(stopped)return manual("missing_reference_after_stop");close(listener);if(launch>=0)close(launch);cleanup(s);return 78;}
  try{bus.call("StopUnit",child,true);}catch(...){return manual("termination_request_reference_lost");}
 }
 // Owner remains alive after the payload budget only for bounded proof/restoration.
 Ns recoveryEnd=std::min<Ns>(now()+5000000000ULL,start+limits::ProofCap);
 while(now()<recoveryEnd&&!authenticated){
  if(!reference||!bus.alive())return manual("reference_lost");
  Packet p;int fd;
  if(pendingWitness>=0){fd=pendingWitness;p=pendingPacket;pendingWitness=-1;}else fd=acceptHello(listener,p,30);if(fd<0)continue;
  auto state=bus.state(child);
  if(p.kind!=Kind::Witness||p.binding.session!=s||std::string(p.binding.invocation)!=bound.invocation||state.invocation!=bound.invocation||state.control!=unsigned(peer(fd))){close(fd);continue;}
  Packet offer;offer.kind=Kind::Binding;offer.closed=1;offer.binding=bound;
  if(sendPacket(fd,offer)&&receivePacket(fd,receipt,1200)&&receipt.kind==Kind::Receipt&&receipt.closed==1&&receipt.drained==1&&same(receipt.binding,bound)){
   auto after=bus.state(child);authenticated=after.invocation==bound.invocation&&after.control==unsigned(peer(fd));
   if(authenticated){event("WITNESS_AUTHENTICATED control_pid="+std::to_string(after.control));sendPacket(fd,hello(Kind::Ack,s));}
  }
  close(fd);
 }
 if(!authenticated)return manual("no_authenticated_witness");
 bool proven=false;State final;
 while(now()<recoveryEnd){
  auto a=bus.state(child);
  if(complete(bound,receipt,a,reference&&bus.alive(),closed,absentGroup(bound),authenticated)){
   auto b=bus.state(child);if(a==b&&complete(bound,receipt,b,bus.alive(),closed,absentGroup(bound),authenticated)){proven=true;final=b;break;}
  }
  usleep(10000);
 }
 if(!proven||now()>=recoveryEnd)return manual("terminal_proof_unresolved");
 event("DRAIN_PROVEN reference_continuous=1 child_main=0 child_control=0 child_job=0 group_released=1");
 if(stopped){
  restoring=true;event("RESTORING stock_start_requests=1");
  try{bus.call("StartUnit",target,true);}catch(...){return manual("stock_start_request_failed_no_retry");}
  bool ready=false;Ns until=now()+2000000000ULL;
  while(now()<until){if(stockReady(bus.state(target))){ready=true;break;}usleep(10000);}
  if(!ready)return manual("stock_readiness_failed_no_retry");
  event("RESTORED manager_ready=1 visual_health=UNTESTED");
 }
 (void)restoring;bus.call("UnrefUnit",child);reference=false;bus.call("UnrefUnit",target);if(childfd>=0)close(childfd);if(launch>=0)close(launch);close(listener);event("COMPLETE");cleanup(s);return 0;
}
static int launch(const std::string&testCase){
 std::string self=executable();trusted(self);std::filesystem::create_directories(Base);struct stat st{};if(lstat(Base,&st)||!S_ISDIR(st.st_mode)||st.st_uid!=0||(st.st_mode&0022))throw std::runtime_error("state root");chmod(Base,0700);
 if(mkdir((std::string(Base)+"/active").c_str(),0700))throw std::runtime_error("existing/manual reservation");
 std::string s=nonce();
#ifdef OWNER_VM_TEST
 const char*chosen=getenv("OWNER_TEST_SESSION");if(chosen){if(!hex32(chosen))throw std::runtime_error("test session");s=chosen;}
#endif
 dir=std::string(Base)+"/"+s;if(mkdir(dir.c_str(),0700))throw std::runtime_error("session collision");Bus bus;auto p=policy(limits::Owner);
 std::vector<std::string> command={self,"--owner",s};
#ifdef OWNER_VM_TEST
 command.push_back(testCase);
#else
 (void)testCase;
#endif
 bus.transient(ownerUnit(s),p,command);int fd=connectOwner(s,2500);if(fd<0)throw std::runtime_error("owner connection");Packet response;
 if(!sendPacket(fd,hello(Kind::Launcher,s))||!receivePacket(fd,response,3000)||response.kind!=Kind::Ready){close(fd);return 78;}
 std::printf("LAUNCH_READY session=%s launcher_pid=%d\n",s.c_str(),getpid());std::fflush(stdout);
#ifdef OWNER_VM_TEST
 if(testCase=="before-permit")for(;;)pause();
#endif
 if(!sendPacket(fd,hello(Kind::Commit,s))||!receivePacket(fd,response,3000)||response.kind!=Kind::Granted){close(fd);return 78;}
 std::printf("HANDOFF_COMPLETE session=%s\n",s.c_str());std::fflush(stdout);close(fd);
#ifdef OWNER_VM_TEST
 if(testCase=="controller-kill"||testCase=="after-permit")for(;;)pause();
#endif
 return 0;
}
#ifdef OWNER_VM_TEST
static int inert(const std::string&s,const std::string&kind){int fd=connectOwner(s,2500);if(fd<0)return 78;Packet p=hello(Kind::Child,s);const char*inv=getenv("INVOCATION_ID");if(!inv||!hex32(inv))return 78;copy(p.binding.invocation,sizeof p.binding.invocation,inv);Packet permit;if(!sendPacket(fd,p)||!receivePacket(fd,permit,5000)||permit.kind!=Kind::Permit||!validBinding(permit.binding)||permit.binding.session!=s||std::string(permit.binding.invocation)!=inv||permit.binding.mainPid!=unsigned(getpid()))return 78;
 std::puts("INERT_PAYLOAD_ENTERED");std::fflush(stdout);
 if(kind=="crash")raise(SIGKILL);
 if(kind=="descendant"){pid_t c=fork();if(c<0)return 1;if(c==0){signal(SIGTERM,SIG_IGN);for(;;)pause();}usleep(100000);return 0;}
 if(kind=="normal"||kind=="duplicate"||kind=="restore-failure"){usleep(100000);return 0;}
 if(kind=="term-responsive"){while(!quit)usleep(10000);return 0;}
 signal(SIGTERM,SIG_IGN);for(;;)pause();
}
#endif
int main(int argc,char**argv){umask(0077);prctl(PR_SET_DUMPABLE,0);signal(SIGTERM,signalQuit);signal(SIGINT,signalQuit);
 try{if(getuid()!=0||argc<2)return 78;std::string role=argv[1];
#ifdef OWNER_VM_TEST
 if((role=="--dummy"||role=="--dummy-loop")&&argc==3){auto d=std::string(argv[2]);writeFile(d+"/starts","start\n");if(access((d+"/fail").c_str(),F_OK)==0)return 1;if(role=="--dummy-loop")while(!quit)usleep(10000);return 0;}
 if(role=="--inert"&&argc==4&&hex32(argv[2]))return inert(argv[2],argv[3]);
#endif
 if(role=="--launch"){
  // The helper must not outlive an AppLoad/frontend death before its COMMIT.
  if(prctl(PR_SET_PDEATHSIG,SIGKILL)||getppid()==1)return 78;
#ifdef OWNER_VM_TEST
  if(argc!=3)return 78;return launch(argv[2]);
#else
  if(argc!=2)return 78;return launch("");
#endif
 }
 if(argc==3&&hex32(argv[2])&&role=="--request-stop") {int fd=connectOwner(argv[2],300);if(fd<0)return 78;bool ok=sendPacket(fd,hello(Kind::Recover,argv[2]));close(fd);return ok?0:78;}
#ifdef OWNER_VM_TEST
 if(argc==3&&hex32(argv[2])&&(role=="--test-drop-reference"||role=="--test-forge")){int fd=connectOwner(argv[2],300);if(fd<0)return 78;Packet p=hello(role=="--test-drop-reference"?Kind::DropReference:Kind::Child,argv[2]);copy(p.binding.invocation,sizeof p.binding.invocation,std::string(32,'f'));bool ok=sendPacket(fd,p);Packet response;bool granted=receivePacket(fd,response,500)&&response.kind==Kind::Permit;close(fd);return ok&&!granted?0:78;}
#endif
 if(argc<3||!hex32(argv[2]))return 78;
 if(role=="--witness"&&argc==3)return witness(argv[2]);
 if(role=="--owner"){
#ifdef OWNER_VM_TEST
  if(argc!=4)return 78;return own(argv[2],argv[3]);
#else
  if(argc!=3)return 78;return own(argv[2],"");
#endif
 }
 return 78;
 }catch(const std::exception&e){std::fprintf(stderr,"MANUAL_RECOVERY_REQUIRED %s\n",e.what());return 78;}
}
