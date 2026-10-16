// SPDX-License-Identifier: AGPL-3.0-only
#include "launcher_v2.h"
#include "manager_history_v2.h"
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/random.h>
#include <sys/syscall.h>
#include <poll.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
namespace recovery::v2::launcher {namespace {
constexpr std::uint32_t Magic=0x324c4249;
struct Fd {int fd{-1};~Fd(){if(fd>=0)::close(fd);}int release(){int n=fd;fd=-1;return n;}};
bool privateDirectory(int fd,struct stat& st){return fd>=0&&!fstat(fd,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==0&&!(st.st_mode&0077);}
bool delta(ClockPair a,ClockPair b,Ns skew){if(!a.monotonic||!a.generation||a.generation!=b.generation||b.monotonic<a.monotonic||b.boottime<a.boottime)return false;auto m=b.monotonic-a.monotonic,n=b.boottime-a.boottime;return (m>n?m-n:n-m)<=skew;}
bool reached(ClockPair now,ClockPair end){return now.monotonic>=end.monotonic||now.boottime>=end.boottime;}
bool plus(ClockPair start,Ns n,ClockPair& out){constexpr auto max=std::numeric_limits<Ns>::max();if(!start.monotonic||start.boottime<start.monotonic||!start.generation||n>max-start.monotonic||n>max-start.boottime)return false;out={start.monotonic+n,start.boottime+n,start.generation};return true;}
ClockPair current(std::uint64_t generation){timespec m{},b{};if(clock_gettime(CLOCK_MONOTONIC,&m)||clock_gettime(CLOCK_BOOTTIME,&b))return {};return {Ns(m.tv_sec)*1000000000+Ns(m.tv_nsec),Ns(b.tv_sec)*1000000000+Ns(b.tv_nsec),generation};}
template<std::size_t N>bool hexField(const std::array<char,N>& a){if(N!=33||a.back())return false;for(std::size_t n=0;n<32;++n)if(!((a[n]>='0'&&a[n]<='9')||(a[n]>='a'&&a[n]<='f')))return false;return true;}
bool shape(const Frame& f){return f.kind>=Kind::Ready&&f.kind<=Kind::Acknowledged&&hexField(f.session)&&hexField(f.ownerInvocation)&&f.ownerPid>1&&f.launcherPid>1&&f.ownerPid!=f.launcherPid&&f.generation&&f.generation<std::numeric_limits<std::uint64_t>::max()&&std::any_of(f.ticket.begin(),f.ticket.end(),[](auto b){return b!=0;})&&f.issued.monotonic&&f.issued.boottime>=f.issued.monotonic&&f.issued.generation&&f.until.generation==f.issued.generation&&f.until.monotonic>f.issued.monotonic&&f.until.boottime>f.issued.boottime;}
bool healthyStock(const recovery::State& s){return s.load=="loaded"&&s.active=="active"&&s.sub=="running"&&s.main>1&&!s.control&&!s.job;}
int processHandle(pid_t pid){
#ifdef SYS_pidfd_open
 return int(syscall(SYS_pidfd_open,pid,0));
#else
 (void)pid;errno=ENOSYS;return -1;
#endif
}
bool processAlive(int fd){pollfd p{fd,POLLIN,0};return fd>=0&&poll(&p,1,0)==0;}
bool socketAlive(int fd){pollfd p{fd,POLLIN|POLLRDHUP,0};int n=poll(&p,1,0);return n>=0&&!(p.revents&(POLLRDHUP|POLLHUP|POLLERR|POLLNVAL));}
struct Writer {Wire& data;std::size_t at{};void n(std::uint64_t v,unsigned count){for(unsigned n=0;n<count;++n)data[at++]=std::uint8_t(v>>(n*8));}template<class T>void a(const T& x){for(auto v:x)data[at++]=std::uint8_t(v);}void clock(ClockPair c){n(c.monotonic,8);n(c.boottime,8);n(c.generation,8);}};
struct Reader {std::span<const std::uint8_t> data;std::size_t at{};std::uint64_t n(unsigned count){std::uint64_t v{};for(unsigned i=0;i<count;++i)v|=std::uint64_t(data[at++])<<(i*8);return v;}template<class T>void a(T& x){for(auto& v:x)v=static_cast<typename T::value_type>(data[at++]);}ClockPair clock(){return {n(8),n(8),n(8)};}};
// inode 0 = the unit failed before its group was ever observed (Manager::create); the proof below needs only
// the cgroup filesystem device and the absence of the expected group path.
bool groupAbsent(const runtime::UnitHistory& h){if(!h.device||h.mount!=recovery::managerMount()||h.initial.group!="/system.slice/"+h.initial.id)return false;Fd root{open("/",O_RDONLY|O_DIRECTORY|O_CLOEXEC)};if(root.fd<0)return false;const auto parent=h.mount+"/system.slice";std::size_t at=1;while(at<parent.size()){auto end=parent.find('/',at);if(end==std::string::npos)end=parent.size();auto piece=parent.substr(at,end-at);if(piece.empty()||piece=="."||piece=="..")return false;int next=openat(root.fd,piece.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(next<0)return false;close(root.fd);root.fd=next;at=end+1;}struct stat parentStat{},st{};if(fstat(root.fd,&parentStat)||parentStat.st_dev!=h.device)return false;errno=0;return fstatat(root.fd,h.initial.id.c_str(),&st,AT_SYMLINK_NOFOLLOW)<0&&errno==ENOENT;}
bool terminated(runtime::Manager& manager,runtime::Unit u,const runtime::UnitHistory& h){if(!h.creationAttempted)return !h.originalReference&&h.initial==recovery::State{}&&!h.device&&!h.inode;if(!h.originalReference||h.initial.id!=manager.name(u)||h.initial.invocation.empty())return false;auto a=manager.state(u),b=manager.state(u);return a==b&&recovery::terminal(a)&&a.boot==h.initial.boot&&a.id==h.initial.id&&a.invocation==h.initial.invocation&&groupAbsent(h);}
}
Reservation::Reservation(runtime::Profile profile,std::string session)noexcept:profile_(std::move(profile)){
 try{if(!roles::supported(profile_)||geteuid()!=0||!recovery::hex32(session)||getpid()<2)return;if(!text(identity_.session,session))return;identity_.ownerPid=getpid();if(getrandom(identity_.ticket.data(),identity_.ticket.size(),0)!=ssize_t(identity_.ticket.size())||std::all_of(identity_.ticket.begin(),identity_.ticket.end(),[](auto n){return n==0;}))return;
  if(mkdir(SharedBase,0700)&&errno!=EEXIST)return;base_=open(SharedBase,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);struct stat base{},active{};if(!privateDirectory(base_,base))return;identity_.baseDevice=base.st_dev;identity_.baseInode=base.st_ino;if(mkdirat(base_,"active",0700))return;acquired_=true;active_=openat(base_,"active",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(!privateDirectory(active_,active))return;identity_.device=active.st_dev;identity_.inode=active.st_ino;
 }catch(...){return;}
}
Reservation::~Reservation(){if(active_>=0)close(active_);if(base_>=0)close(base_);}
bool Reservation::held()const noexcept{if(!acquired_||released_||identity_.ownerPid!=unsigned(getpid())||geteuid()!=0||!identity_.device||!identity_.inode)return false;Fd named{open(SharedBase,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};struct stat base{},active{},entry{};return privateDirectory(named.fd,base)&&base.st_dev==identity_.baseDevice&&base.st_ino==identity_.baseInode&&privateDirectory(active_,active)&&active.st_dev==identity_.device&&active.st_ino==identity_.inode&&!fstatat(base_,"active",&entry,AT_SYMLINK_NOFOLLOW)&&S_ISDIR(entry.st_mode)&&entry.st_uid==0&&!(entry.st_mode&0077)&&entry.st_dev==identity_.device&&entry.st_ino==identity_.inode;}
bool encode(const Frame& f,Wire& out)noexcept{out.fill(0);if(!shape(f))return false;Writer w{out};w.n(Magic,4);w.n(2,2);w.n(unsigned(f.kind),2);w.n(WireBytes,4);w.n(0,4);w.a(f.session);w.a(f.ownerInvocation);w.a(f.ticket);w.n(f.ownerPid,4);w.n(f.launcherPid,4);w.n(f.generation,8);w.clock(f.issued);w.clock(f.until);return true;}
std::optional<Frame> decode(std::span<const std::uint8_t> wire)noexcept{if(wire.size()!=WireBytes)return {};Reader r{wire};if(r.n(4)!=Magic||r.n(2)!=2)return {};Frame f;f.kind=Kind(r.n(2));if(r.n(4)!=WireBytes||r.n(4))return {};r.a(f.session);r.a(f.ownerInvocation);r.a(f.ticket);f.ownerPid=r.n(4);f.launcherPid=r.n(4);f.generation=r.n(8);f.issued=r.clock();f.until=r.clock();while(r.at<wire.size())if(r.n(1))return {};return shape(f)?std::optional<Frame>(f):std::nullopt;}
OwnerCommit::OwnerCommit(Reservation& r,runtime::Manager& manager,runtime::Profile profile,std::uint64_t generation,ClockPair start,std::unique_ptr<roles::Socket> endpoint)noexcept:reservation_(r),manager_(manager),profile_(std::move(profile)),last_(start){
 try{if(!roles::supported(profile_)||profile_!=r.profile()||!r.held()||r.commitBound_)return;r.commitBound_=true;connection_=manager.connection();if(!endpoint||!endpoint->valid()||endpoint->peer().uid||endpoint->peer().pid==unsigned(getpid())||!manager.alive())return;auto history=manager.history();auto owner=manager.state(runtime::Unit::Owner),stock=manager.state(runtime::Unit::Stock);if(!history.owner.originalReference||!history.stock.originalReference||history.stockStopAttempted||history.stockStartAttempted||owner.main!=unsigned(getpid())||owner.active!="active"||owner.invocation!=history.owner.initial.invocation||stock.invocation!=history.stock.initial.invocation||!healthyStock(stock))return;
  auto peer=endpoint->peer();Fd fd{endpoint->release()};originalSocket_=fcntl(fd.fd,F_DUPFD_CLOEXEC,3);launcher_=std::make_unique<control::Channel>(fd.fd,peer.connection,peer.uid,peer.pid);fd.release();launcherPidfd_=processHandle(peer.pid);if(originalSocket_<0||!launcher_->valid()||!liveLauncher())return;ready_.session=r.identity().session;ready_.ticket=r.identity().ticket;ready_.ownerPid=getpid();ready_.launcherPid=peer.pid;ready_.generation=generation;text(ready_.ownerInvocation,owner.invocation);ready_.issued=start;if(!plus(start,profile_.startBudget,ready_.until)||!shape(ready_))return;state_=OwnerState::Waiting;
 }catch(...){state_=OwnerState::Rejected;}
}
OwnerCommit::~OwnerCommit(){disconnect();}
void OwnerCommit::disconnect()noexcept{if(launcher_)launcher_->seal();if(originalSocket_>=0){close(originalSocket_);originalSocket_=-1;}if(launcherPidfd_>=0){close(launcherPidfd_);launcherPidfd_=-1;}}
bool OwnerCommit::liveLauncher()noexcept{return launcher_&&launcher_->valid()&&processAlive(launcherPidfd_)&&socketAlive(originalSocket_);}
void OwnerCommit::seal()noexcept{sealed_=true;if(state_!=OwnerState::Committed)state_=OwnerState::Rejected;disconnect();}
OwnerState OwnerCommit::poll(ClockPair supplied)noexcept{
 if(state_!=OwnerState::Waiting||sealed_)return state_;
 try{if(!delta(last_,supplied,profile_.timing.clockSkew)||!reservation_.held()||!manager_.alive()||manager_.connection()!=connection_||!liveLauncher()){seal();return state_;}auto history=manager_.history();auto stock=manager_.state(runtime::Unit::Stock);if(history.stockStopAttempted||history.stockStartAttempted||stock.invocation!=history.stock.initial.invocation||!healthyStock(stock)){seal();return state_;}auto now=current(supplied.generation);if(!delta(last_,now,profile_.timing.clockSkew)||reached(now,ready_.until)){seal();return state_;}last_=now;
  Wire wire;if(!sent_){if(!encode(ready_,wire)||!launcher_->send(wire)){seal();return state_;}sent_=true;}
  auto read=launcher_->receive(wire);if(read==control::Read::Empty)return state_;if(read!=control::Read::Packet){seal();return state_;}auto commit=decode(wire);auto expected=ready_;expected.kind=Kind::Commit;if(!commit||*commit!=expected){seal();return state_;}
  // This final observation is the COMMIT linearization point. Original pidfd
  // remains readable forever after death, even if descendants retain the fd.
  if(!manager_.alive()||!reservation_.held()){seal();return state_;}now=current(supplied.generation);if(!delta(last_,now,profile_.timing.clockSkew)||reached(now,ready_.until)||!liveLauncher()){seal();return state_;}
  state_=OwnerState::Committed;auto ack=ready_;ack.kind=Kind::Acknowledged;if(encode(ack,wire))launcher_->send(wire);disconnect();return state_;
 }catch(...){seal();return state_;}
}
Client::Client(runtime::Profile profile,std::string session,std::string invocation,std::uint64_t generation,ClockPair start,std::unique_ptr<roles::Socket> endpoint)noexcept:profile_(std::move(profile)),generation_(generation),last_(start){
 try{if(!roles::supported(profile_)||!text(session_,session)||!hexField(session_)||!text(ownerInvocation_,invocation)||!hexField(ownerInvocation_)||!generation||generation==std::numeric_limits<std::uint64_t>::max()||!endpoint||!endpoint->valid()||endpoint->peer().uid||endpoint->peer().pid==unsigned(getpid())||!plus(start,profile_.startBudget,bootstrapDeadline_))return;auto peer=endpoint->peer();Fd fd{endpoint->release()};owner_=std::make_unique<control::Channel>(fd.fd,peer.connection,peer.uid,peer.pid);fd.release();if(owner_->valid())state_=ClientState::Waiting;
 }catch(...){state_=ClientState::Failed;}
}
ClientState Client::fail()noexcept{state_=ClientState::Failed;if(owner_)owner_->seal();return state_;}
ClientState Client::poll(ClockPair now)noexcept{return pollAt(now,nullptr,nullptr);}
ClientState Client::pollObserved(control::ClockSource source,void* context)noexcept{if(!source)return fail();return pollAt(source(context),source,context);}
ClientState Client::pollAt(ClockPair now,control::ClockSource source,void* context)noexcept{if(state_==ClientState::Failed||state_==ClientState::Complete)return state_;if(!delta(last_,now,profile_.timing.clockSkew)||reached(now,bootstrapDeadline_))return fail();last_=now;if(state_!=ClientState::Waiting&&reached(now,ready_.until))return fail();Wire raw;auto read=owner_->receive(raw);if(source){now=source(context);if(!delta(last_,now,profile_.timing.clockSkew)||reached(now,bootstrapDeadline_)||(state_!=ClientState::Waiting&&reached(now,ready_.until)))return fail();last_=now;}if(read==control::Read::Empty)return state_;if(read!=control::Read::Packet)return fail();auto frame=decode(raw);if(!frame)return fail();if(state_==ClientState::Waiting){ClockPair until;if(frame->kind!=Kind::Ready||frame->session!=session_||frame->ownerInvocation!=ownerInvocation_||frame->ownerPid!=owner_->peer().pid||frame->launcherPid!=unsigned(getpid())||frame->generation!=generation_||!plus(frame->issued,profile_.startBudget,until)||until!=frame->until||!delta(frame->issued,now,profile_.timing.clockSkew)||reached(now,frame->until))return fail();ready_=*frame;state_=ClientState::Ready;return state_;}if(state_!=ClientState::AwaitingAcknowledgement)return fail();auto expected=ready_;expected.kind=Kind::Acknowledged;if(*frame!=expected)return fail();state_=ClientState::Complete;owner_->seal();return state_;}
bool Client::commit(ClockPair now)noexcept{if(state_!=ClientState::Ready)return false;if(!delta(last_,now,profile_.timing.clockSkew)||reached(now,bootstrapDeadline_)||reached(now,ready_.until)){fail();return false;}last_=now;auto commit=ready_;commit.kind=Kind::Commit;Wire wire;if(!encode(commit,wire)||!owner_->send(wire)){fail();return false;}state_=ClientState::AwaitingAcknowledgement;return true;}
Completion::Completion(ReservationIdentity reservation,std::uint64_t connection)noexcept:reservation_(reservation),connection_(connection){}
Completion::Completion(Completion&& other)noexcept:reservation_(other.reservation_),connection_(other.connection_),spent_(other.spent_){other.spent_=true;}
std::optional<Completion> verifyCompletion(runtime::Manager& manager,const Reservation& reservation,const OwnerCommit& commit)noexcept{
 try{if(!reservation.held()||!commit.sealed()||commit.reservationIdentity()!=reservation.identity()||!manager.alive()||commit.connection()!=manager.connection())return {};auto history=manager.history();if(!history.continuous||history.connection!=manager.connection()||!history.owner.originalReference||!history.stock.originalReference)return {};const auto session=std::string(reservation.identity().session.data());if(manager.name(runtime::Unit::Owner)!="inkbound-owner-v2-"+session+".service"||manager.name(runtime::Unit::Reader)!="inkbound-direct-v2-"+session+".service"||manager.name(runtime::Unit::Fuse)!="inkbound-fuse-v2-"+session+".service")return {};
  auto owner=manager.state(runtime::Unit::Owner);if(owner.main!=reservation.identity().ownerPid||owner.invocation!=history.owner.initial.invocation||owner.boot!=history.owner.initial.boot||owner.active!="active")return {};if(!terminated(manager,runtime::Unit::Reader,history.reader)||!terminated(manager,runtime::Unit::Fuse,history.fuse))return {};auto a=manager.state(runtime::Unit::Stock),b=manager.state(runtime::Unit::Stock);if(a!=b||!healthyStock(a)||a.boot!=history.stock.initial.boot||a.id!=history.stock.initial.id)return {};
  if(history.stockStopAttempted){if(!commit.committed()||!history.stockStartAttempted||!history.stockStartAccepted||!manager.restorationRequested()||a.invocation==history.stock.initial.invocation)return {};}
  else if(history.stockStartAttempted||history.stockStartAccepted||a.invocation!=history.stock.initial.invocation)return {};
  if(!manager.alive()||!reservation.held())return {};return Completion(reservation.identity(),manager.connection());
 }catch(...){return {};}
}
bool Reservation::release(runtime::Manager& manager,Completion&& proof)noexcept{if(proof.spent_)return false;proof.spent_=true;try{if(proof.reservation_!=identity_||proof.connection_!=manager.connection()||!manager.alive()||!held())return false;if(unlinkat(base_,"active",AT_REMOVEDIR))return false;released_=true;return true;}catch(...){return false;}}
}
