// SPDX-License-Identifier: AGPL-3.0-only
#include "roles_v2.h"
#include <systemd/sd-daemon.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <limits>
namespace recovery::v2::roles {namespace {
constexpr const char* RootName="inkbound-owner-v2",*SocketName="control.sock",*PendingName="control.sock.pending";
struct Fd {int fd{-1};~Fd(){if(fd>=0)::close(fd);}int release(){int out=fd;fd=-1;return out;}};
bool session(const std::string& s){return recovery::hex32(s);}
bool privateDirectory(int fd){struct stat st{};return fd>=0&&!fstat(fd,&st)&&S_ISDIR(st.st_mode)&&st.st_uid==0&&!(st.st_mode&0077);}
int base(bool create){Fd run{open("/run",O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};if(run.fd<0)return -1;struct stat st{};if(fstat(run.fd,&st)||st.st_uid||!S_ISDIR(st.st_mode)||(st.st_mode&0022))return -1;if(create&&mkdirat(run.fd,RootName,0700)&&errno!=EEXIST)return -1;Fd root{openat(run.fd,RootName,O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};return privateDirectory(root.fd)?root.release():-1;}
bool address(int directory,sockaddr_un& out,const char* name=SocketName){out={};out.sun_family=AF_UNIX;const auto p="/proc/self/fd/"+std::to_string(directory)+"/"+name;if(p.size()>=sizeof out.sun_path)return false;std::memcpy(out.sun_path,p.c_str(),p.size()+1);return true;}
bool socketStat(int directory,struct stat& st){return !fstatat(directory,SocketName,&st,AT_SYMLINK_NOFOLLOW)&&S_ISSOCK(st.st_mode)&&st.st_uid==0&&!(st.st_mode&0077);}
bool stepClock(ClockPair before,ClockPair now,Ns skew){if(!before.generation||now.generation!=before.generation||now.monotonic<before.monotonic||now.boottime<before.boottime)return false;Ns m=now.monotonic-before.monotonic,b=now.boottime-before.boottime;return (m>b?m-b:b-m)<=skew;}
bool reached(ClockPair now,ClockPair end){return now.monotonic>=end.monotonic||now.boottime>=end.boottime;}
bool plus(ClockPair start,Ns amount,ClockPair& result){constexpr auto max=std::numeric_limits<Ns>::max();if(!start.monotonic||start.boottime<start.monotonic||!start.generation||amount>max-start.monotonic||amount>max-start.boottime)return false;result={start.monotonic+amount,start.boottime+amount,start.generation};return true;}
template<std::size_t N>bool terminated(const std::array<char,N>& value){for(std::size_t i=0;i<N;++i)if(!value[i]){for(std::size_t j=i;j<N;++j)if(value[j])return false;return i!=0;}return false;}
}
bool supported(const runtime::Profile& profile)noexcept{return profile.selection==runtime::Selection::Qualified&&runtime::valid(profile);}
Socket::Socket(int fd,VerifiedPeer peer)noexcept:fd_(fd),peer_(peer){}
Socket::~Socket(){if(fd_>=0)::close(fd_);}
int Socket::release()noexcept{int out=fd_;fd_=-1;peer_.verified=false;return out;}
std::unique_ptr<Socket> Socket::adopt(const runtime::Profile& profile,int fd,Peer expected)noexcept{
 Fd owned{fd};try{if(!supported(profile)||geteuid()!=0||fd<0||expected.pid<2||!expected.connection)return {};ucred peer{};socklen_t length=sizeof peer;int type{};socklen_t size=sizeof type;const int yes=1;int flags=fcntl(fd,F_GETFL),fdflags=fcntl(fd,F_GETFD);
  if(getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&size)||type!=SOCK_SEQPACKET||getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&peer,&length)||length!=sizeof peer||peer.uid!=0||peer.pid<2||std::uint32_t(peer.pid)!=expected.pid||flags<0||fdflags<0||setsockopt(fd,SOL_SOCKET,SO_PASSCRED,&yes,sizeof yes)||fcntl(fd,F_SETFL,flags|O_NONBLOCK)||fcntl(fd,F_SETFD,fdflags|FD_CLOEXEC))return {};
  std::unique_ptr<Socket> result(new Socket(owned.release(),{0,expected.pid,expected.connection,true}));return result;
 }catch(...){return {};}
}
Listener::Listener(runtime::Profile profile,std::string value)noexcept:session_(std::move(value)),profile_(std::move(profile)){
 try{if(!supported(profile_)||geteuid()!=0||!session(session_))return;root_=base(true);if(root_<0)return;if(mkdirat(root_,session_.c_str(),0700))return;created_=true;directory_=openat(root_,session_.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC);if(!privateDirectory(directory_))return;Fd sock{socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0)};sockaddr_un at{};const int yes=1;// Bound and listening under a pending name first, then atomically published: a frontend that
  // sees control.sock can always connect (no bind-before-listen ECONNREFUSED window).
  if(sock.fd<0||!address(directory_,at,PendingName)||setsockopt(sock.fd,SOL_SOCKET,SO_PASSCRED,&yes,sizeof yes)||bind(sock.fd,reinterpret_cast<sockaddr*>(&at),sizeof at))return;bound_=true;struct stat st{};if(fstatat(directory_,PendingName,&st,AT_SYMLINK_NOFOLLOW)||!S_ISSOCK(st.st_mode)||st.st_uid)return;socketDevice_=st.st_dev;socketInode_=st.st_ino;if(fchmodat(directory_,PendingName,0600,0))return;if(listen(sock.fd,4))return;if(renameat2(directory_,PendingName,directory_,SocketName,RENAME_NOREPLACE)||!socketStat(directory_,st)||st.st_dev!=socketDevice_||st.st_ino!=socketInode_)return;fd_=sock.release();
 }catch(...){return;}
}
Listener::~Listener(){
 if(fd_>=0)::close(fd_);
 if(bound_&&directory_>=0)for(const char* name:{SocketName,PendingName}){struct stat st{};if(!fstatat(directory_,name,&st,AT_SYMLINK_NOFOLLOW)&&S_ISSOCK(st.st_mode)&&st.st_uid==0&&st.st_dev==socketDevice_&&st.st_ino==socketInode_)unlinkat(directory_,name,0);}
 if(created_&&root_>=0&&directory_>=0){struct stat held{},named{};if(!fstat(directory_,&held)&&!fstatat(root_,session_.c_str(),&named,AT_SYMLINK_NOFOLLOW)&&S_ISDIR(named.st_mode)&&held.st_dev==named.st_dev&&held.st_ino==named.st_ino)unlinkat(root_,session_.c_str(),AT_REMOVEDIR);}
 if(directory_>=0)::close(directory_);if(root_>=0)::close(root_);
}
Accepted Listener::poll(std::span<const Peer> expected)noexcept{
 if(!valid()||expected.empty()||expected.size()>3)return {};
 for(std::size_t i=0;i<expected.size();++i){if(expected[i].pid<2||!expected[i].connection)return {};for(std::size_t j=0;j<i;++j)if(expected[i].pid==expected[j].pid||expected[i].connection==expected[j].connection)return {};}
 Fd client{accept4(fd_,nullptr,nullptr,SOCK_NONBLOCK|SOCK_CLOEXEC)};if(client.fd<0)return {errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR?Accept::Empty:Accept::Failed,{}};
 ucred credentials{};socklen_t length=sizeof credentials;if(getsockopt(client.fd,SOL_SOCKET,SO_PEERCRED,&credentials,&length)||length!=sizeof credentials||credentials.uid!=0||credentials.pid<2)return {Accept::Rejected,{}};
 for(const auto& peer:expected)if(peer.pid==std::uint32_t(credentials.pid)){for(std::size_t n=0;n<consumedCount_;++n)if(consumed_[n].pid==peer.pid||consumed_[n].connection==peer.connection)return {Accept::Rejected,{}};if(consumedCount_==consumed_.size())return {Accept::Rejected,{}};consumed_[consumedCount_++]=peer;auto result=Socket::adopt(profile_,client.release(),peer);return {result?Accept::Accepted:Accept::Failed,std::move(result)};}
 return {Accept::Rejected,{}};
}
Connector::Connector(runtime::Profile profile,std::string value,Peer owner)noexcept:profile_(std::move(profile)),session_(std::move(value)),owner_(owner){}
std::unique_ptr<Socket> Connector::connect()noexcept{
 if(attempted_)return {};attempted_=true;try{if(!supported(profile_)||geteuid()!=0||!session(session_)||owner_.pid<2||!owner_.connection)return {};Fd root{base(false)};if(root.fd<0)return {};Fd directory{openat(root.fd,session_.c_str(),O_RDONLY|O_DIRECTORY|O_NOFOLLOW|O_CLOEXEC)};if(!privateDirectory(directory.fd))return {};struct stat before{},after{};if(!socketStat(directory.fd,before))return {};Fd fd{socket(AF_UNIX,SOCK_SEQPACKET|SOCK_NONBLOCK|SOCK_CLOEXEC,0)};sockaddr_un at{};int yes=1;if(fd.fd<0||!address(directory.fd,at)||setsockopt(fd.fd,SOL_SOCKET,SO_PASSCRED,&yes,sizeof yes)||::connect(fd.fd,reinterpret_cast<sockaddr*>(&at),sizeof at)||!socketStat(directory.fd,after)||before.st_dev!=after.st_dev||before.st_ino!=after.st_ino)return {};return Socket::adopt(profile_,fd.release(),owner_);
 }catch(...){return {};}
}
int notifyReady(void*)noexcept{return sd_notify(0,"READY=1");}
FuseReady::FuseReady(const runtime::Profile& profile)noexcept:supported_(supported(profile)){}
bool FuseReady::announce(Ready ready,void* context)noexcept{if(attempted_)return false;attempted_=true;return supported_&&ready&&ready(context)>0;}
FuseActor::FuseActor(const runtime::Profile& profile,SessionIdentity identity,fuse::Binding binding,std::uint64_t generation,ClockPair started,std::unique_ptr<Socket> endpoint,fuse::Notify notify,void* context)noexcept:last_(started),skew_(profile.timing.clockSkew){
 try{if(!supported(profile)||!endpoint||!endpoint->valid()||endpoint->peer().uid!=0||endpoint->peer().pid!=identity.ownerPid||binding.mainPid!=std::uint32_t(getpid())||!notify){exit_=true;return;}const auto peer=endpoint->peer();owner_=std::make_unique<fuse::OwnerChannel>(endpoint->release(),peer.connection,peer.uid,peer.pid);if(!owner_->valid()){exit_=true;return;}lease_=std::make_unique<fuse::Lease>(identity,binding,generation,profile.timing,profile.fuse,owner_->peer(),0,started,notify,context);if(lease_->closed()){exit_=true;owner_.reset();return;}cutoff_=lease_->outstandingUntil();
 }catch(...){exit_=true;owner_.reset();}
}
FuseStep FuseActor::hold(ClockPair now)noexcept{if(exit_)return FuseStep::Exit;if(!stepClock(last_,now,skew_)||reached(now,cutoff_)){exit_=true;owner_.reset();return FuseStep::Exit;}last_=now;return FuseStep::Holding;}
FuseStep FuseActor::poll(ClockPair now,const fuse::Evidence& evidence)noexcept{return pollAt(now,evidence,nullptr,nullptr);}
FuseStep FuseActor::pollObserved(const fuse::Evidence& evidence,fuse::ClockSource source,void* context)noexcept{if(!source){exit_=true;owner_.reset();return FuseStep::Exit;}return pollAt(source(context),evidence,source,context);}
FuseStep FuseActor::pollAt(ClockPair now,const fuse::Evidence& evidence,fuse::ClockSource source,void* context)noexcept{
 if(exit_||!lease_)return FuseStep::Exit;if(holding_)return hold(now);
 bool received=source?owner_->receiveObserved(*lease_,evidence,source,context):owner_->receive(*lease_,now,evidence);if(source)now=source(context);if(!received||lease_->closed()){cutoff_=lease_->outstandingUntil();holding_=true;owner_.reset();return hold(now);}last_=now;cutoff_=lease_->outstandingUntil();return FuseStep::Running;
}
fuse::Fault FuseActor::fault()const noexcept{return lease_?lease_->fault():fuse::Fault::Configuration;}
WitnessActor::WitnessActor(runtime::Profile profile,WitnessContext context,std::unique_ptr<Socket> endpoint)noexcept:profile_(std::move(profile)),context_(context),last_(context.localStarted){
 try{if(!supported(profile_)||!terminated(context.session)||!terminated(context.invocation)||!recovery::hex32(context.session.data())||!recovery::hex32(context.invocation.data())||!context.generation||context.generation==std::numeric_limits<std::uint64_t>::max()||!endpoint||!endpoint->valid()||endpoint->peer().uid!=0||!plus(context.localStarted,profile_.stopBudget,deadline_)){failed_=true;return;}auto peer=endpoint->peer();owner_=std::make_unique<control::Channel>(endpoint->release(),peer.connection,peer.uid,peer.pid);if(!owner_->valid())failed_=true;
 }catch(...){failed_=true;owner_.reset();}
}
WitnessStep WitnessActor::fail()noexcept{failed_=true;if(owner_)owner_->seal();return WitnessStep::Failed;}
WitnessStep WitnessActor::poll(ClockPair now)noexcept{
 if(failed_)return WitnessStep::Failed;if(finished_)return WitnessStep::Complete;if(!owner_||!stepClock(last_,now,profile_.timing.clockSkew)||reached(now,deadline_))return fail();last_=now;Wire wire;auto read=owner_->receive(wire);if(read==control::Read::Empty)return WitnessStep::Waiting;if(read!=control::Read::Packet)return fail();auto packet=bootstrap::decode(wire);if(!packet)return fail();
 if(!offered_){if(packet->kind!=bootstrap::Kind::WitnessOffer||packet->identity.ownerPid!=owner_->peer().pid||packet->identity.session!=context_.session||packet->identity.invocation!=context_.invocation||packet->generation!=context_.generation||packet->profile.data()!=profile_.qualification||!proofWithinBudget(packet->closure,packet->identity,context_.generation,packet->closure.lastGrantSequence,now,profile_.timing)||!witness::membership(packet->identity,getpid()))return fail();
  receipt_=*packet;receipt_.kind=bootstrap::Kind::Receipt;receipt_.witnessPid=getpid();receipt_.drained=true;Wire response;if(!bootstrap::encode(receipt_,response)||!owner_->send(response))return fail();offered_=true;return WitnessStep::Waiting;
 }
 auto expected=receipt_;expected.kind=bootstrap::Kind::WitnessAck;if(*packet!=expected||!proofWithinBudget(receipt_.closure,receipt_.identity,context_.generation,receipt_.closure.lastGrantSequence,now,profile_.timing))return fail();finished_=true;owner_->seal();return WitnessStep::Complete;
}
}
