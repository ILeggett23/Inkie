// SPDX-License-Identifier: AGPL-3.0-only
#include "control_v2.h"
#include <sys/socket.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
namespace recovery::v2::control {
namespace {
bool reached(ClockPair now,ClockPair end)noexcept{return now.monotonic>=end.monotonic||now.boottime>=end.boottime;}
bool atLeast(ClockPair now,ClockPair start)noexcept{return now.generation==start.generation&&now.monotonic>=start.monotonic&&now.boottime>=start.boottime;}
}
Channel::Channel(int fd,std::uint64_t connection,std::uint32_t uid,std::uint32_t pid)noexcept:fd_(fd){
 if(fd_<0)return;struct ucred credentials{};socklen_t size=sizeof(credentials);int type{};socklen_t typeSize=sizeof(type);int yes=1;
 int flags=fcntl(fd_,F_GETFL),fdflags=fcntl(fd_,F_GETFD);
 if(!connection||pid<2||getsockopt(fd_,SOL_SOCKET,SO_TYPE,&type,&typeSize)||type!=SOCK_SEQPACKET||
    getsockopt(fd_,SOL_SOCKET,SO_PEERCRED,&credentials,&size)||size!=sizeof(credentials)||credentials.pid<=0||
    std::uint32_t(credentials.pid)!=pid||std::uint32_t(credentials.uid)!=uid||flags<0||fdflags<0||
    setsockopt(fd_,SOL_SOCKET,SO_PASSCRED,&yes,sizeof(yes))||fcntl(fd_,F_SETFL,flags|O_NONBLOCK)||fcntl(fd_,F_SETFD,fdflags|FD_CLOEXEC)) {seal();return;}
 peer_={uid,pid,connection,true};
}
Channel::~Channel(){seal();}
void Channel::seal()noexcept{if(fd_>=0){close(fd_);fd_=-1;}peer_.verified=false;}
Read Channel::receive(Wire& raw)noexcept{
 if(!valid())return Read::Failed;iovec io{raw.data(),raw.size()};
 alignas(cmsghdr) std::array<char,CMSG_SPACE(sizeof(struct ucred))+CMSG_SPACE(sizeof(int)*16)> ancillary{};
 msghdr m{};m.msg_iov=&io;m.msg_iovlen=1;m.msg_control=ancillary.data();m.msg_controllen=ancillary.size();
 auto n=recvmsg(fd_,&m,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);
 if(n<0&&(errno==EAGAIN||errno==EWOULDBLOCK||errno==EINTR))return Read::Empty;
 unsigned count{};bool foreign{};
 for(auto c=CMSG_FIRSTHDR(&m);n>=0&&c;c=CMSG_NXTHDR(&m,c)){
  if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_RIGHTS){
   if(c->cmsg_len>=CMSG_LEN(0)){auto length=(c->cmsg_len-CMSG_LEN(0))/sizeof(int);auto fds=reinterpret_cast<int*>(CMSG_DATA(c));for(std::size_t k=0;k<length;++k)close(fds[k]);}foreign=true;
  }else if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_CREDENTIALS&&c->cmsg_len==CMSG_LEN(sizeof(struct ucred))){
   struct ucred sender{};std::memcpy(&sender,CMSG_DATA(c),sizeof(sender));++count;
   if(sender.pid<=0||std::uint32_t(sender.pid)!=peer_.pid||std::uint32_t(sender.uid)!=peer_.uid)foreign=true;
  }else foreign=true;
 }
 if(n!=ssize_t(raw.size())||(m.msg_flags&(MSG_TRUNC|MSG_CTRUNC))||count!=1||foreign){seal();return Read::Failed;}
 return Read::Packet;
}
bool Channel::send(const Wire& raw)noexcept{
 if(!valid())return false;
 // EAGAIN/EINTR are terminal: no retry may reorder a partially issued grant.
 if(::send(fd_,raw.data(),raw.size(),MSG_DONTWAIT|MSG_NOSIGNAL)!=ssize_t(raw.size())){seal();return false;}return true;
}
OwnerControl::OwnerControl(SessionIdentity id,std::uint64_t gen,Timing t,ClockPair start,
 int childFd,std::uint64_t childConnection,std::uint32_t childUid,
 int fuseFd,std::uint64_t fuseConnection,std::uint32_t fuseUid,std::uint32_t fusePid)noexcept:
 child_(childFd,childConnection,childUid,id.mainPid),fuse_(fuseFd,fuseConnection,fuseUid,fusePid),
 machine_(id,gen,t,child_.peer(),childUid,start),timing_(t){
 if(id.ownerPid!=std::uint32_t(getpid())||!child_.valid()||!fuse_.valid()||childConnection==fuseConnection||fusePid==id.mainPid||fusePid==id.ownerPid||machine_.closed())seal(start,Fault::Channel);
}
void OwnerControl::seal(ClockPair now,Fault fault)noexcept{
 if(sealed_)return;if(fault_==Fault::None)fault_=fault;
 if(!machine_.closed())machine_.disconnect(now);
 // Best effort one-shot closure only. Failed grant delivery is never retried.
 if(auto packet=machine_.closurePacket()){(void)fuse_.send(*packet);(void)child_.send(*packet);}
 fuse_.seal();child_.seal();pending_=false;sealed_=true;
}
bool OwnerControl::poll(ClockPair now,Authority authority,const Nonce* entropy)noexcept{return pollAt(now,authority,entropy,nullptr,nullptr);}
bool OwnerControl::pollObserved(Authority authority,const Nonce* entropy,ClockSource source,void* context)noexcept{if(!source){seal({},Fault::Protocol);return false;}return pollAt(source(context),authority,entropy,source,context);}
bool OwnerControl::pollAt(ClockPair now,Authority authority,const Nonce* entropy,ClockSource source,void* context)noexcept{
 if(sealed_)return false;(void)machine_.admitted(now,authority);
 if(machine_.closed()){seal(now,Fault::Protocol);return false;}
 Wire raw{};if(fuse_.receive(raw)!=Read::Empty){seal(now,Fault::Transport);return false;}
 auto received=child_.receive(raw);if(source){now=source(context);(void)machine_.admitted(now,authority);if(machine_.closed()){seal(now,Fault::Protocol);return false;}}if(received==Read::Failed){seal(now,Fault::Transport);return false;}
 if(received==Read::Packet){
  auto grant=machine_.acceptHealth(raw,child_.peer(),now,authority);
  if(!grant){seal(now,Fault::Protocol);return false;}
  pending_=false;
  // Identical OwnerMachine packet, fuse first. Partial delivery closes the
  // session, with no second grant issuance or retry of either failed send.
  if(!fuse_.send(*grant)||!child_.send(*grant)){seal(now,Fault::Send);return false;}
 }
 bool due=!issued_||(atLeast(now,issuedAt_)&&now.monotonic-issuedAt_.monotonic>=timing_.healthCadence&&now.boottime-issuedAt_.boottime>=timing_.healthCadence);
 if(!pending_&&due&&entropy){
  auto challenge=machine_.challenge(now,authority,*entropy);if(!challenge){seal(now,Fault::Protocol);return false;}
  if(!child_.send(*challenge)){seal(now,Fault::Send);return false;}pending_=issued_=true;issuedAt_=now;
 }
 return true;
}
bool OwnerControl::admitted(ClockPair now,Authority authority)noexcept{if(sealed_)return false;bool yes=machine_.admitted(now,authority);if(machine_.closed())seal(now,Fault::Protocol);return yes&&!sealed_;}
void OwnerControl::close(ClockPair now)noexcept{if(sealed_)return;machine_.close(now);seal(now,Fault::None);}
ChildControl::ChildControl(SessionIdentity id,std::uint64_t gen,Timing t,ClockPair start,
 int ownerFd,std::uint64_t connection,std::uint32_t uid,progress::MainProgressChannel& progress)noexcept:
 owner_(ownerFd,connection,uid,id.ownerPid),machine_(id,gen,t,owner_.peer(),uid,start),progress_(progress),identity_(id),generation_(gen),timing_(t){
 if(id.mainPid!=std::uint32_t(getpid())||!owner_.valid()||machine_.closed())seal(start,Fault::Channel);
}
void ChildControl::seal(ClockPair now,Fault fault)noexcept{if(sealed_)return;if(fault_==Fault::None)fault_=fault;if(!machine_.closed())machine_.disconnect(now);owner_.seal();waiting_=awaitingGrant_=false;sealed_=true;}
bool ChildControl::poll(ClockPair now,Authority authority)noexcept{return pollAt(now,authority,nullptr,nullptr);}
bool ChildControl::pollObserved(Authority authority,ClockSource source,void* context)noexcept{if(!source){seal({},Fault::Protocol);return false;}return pollAt(source(context),authority,source,context);}
bool ChildControl::pollAt(ClockPair now,Authority authority,ClockSource source,void* context)noexcept{
 if(sealed_)return false;(void)machine_.admitted(now,authority);
 if(machine_.closed()){seal(now,Fault::Protocol);return false;}
 Wire raw{};auto result=owner_.receive(raw);if(source){now=source(context);(void)machine_.admitted(now,authority);if(machine_.closed()){seal(now,Fault::Protocol);return false;}}
 if(result==Read::Failed){seal(now,Fault::Transport);return false;}
 if(result==Read::Packet){
  auto m=decode(raw);if(!m){seal(now,Fault::Protocol);return false;}
  if(m->kind==Kind::Closure){(void)machine_.acceptClosure(raw,owner_.peer(),now);seal(now,Fault::Protocol);return false;}
  if(m->kind==Kind::Grant){
   if(waiting_||!awaitingGrant_||!machine_.acceptGrant(raw,owner_.peer(),now,authority)){seal(now,Fault::Protocol);return false;}awaitingGrant_=false;
  }else if(m->kind==Kind::Challenge){
   const auto& c=m->challenge;
   if(waiting_||awaitingGrant_||m->identity!=identity_||m->generation!=generation_||challengeSequence_==UINT64_MAX||m->sequence!=challengeSequence_+1||machine_.lastGrantSequence()==UINT64_MAX||c.nextGrant!=machine_.lastGrantSequence()+1||c.nonce==lastNonce_||!atLeast(now,c.issued)||!atLeast(c.replyBy,c.issued)||c.replyBy.monotonic-c.issued.monotonic!=timing_.challengeLifetime||c.replyBy.boottime-c.issued.boottime!=timing_.challengeLifetime||reached(now,c.replyBy)){
    seal(now,Fault::Protocol);return false;
   }
   pendingWire_=raw;pendingChallenge_=c;waiting_=true;challengeSequence_=m->sequence;lastNonce_=c.nonce;
  }else{seal(now,Fault::Protocol);return false;}
 }
 auto snapshot=progress_.read();if(source){now=source(context);(void)machine_.admitted(now,authority);if(machine_.closed()){seal(now,Fault::Protocol);return false;}}
 if(snapshot.status==progress::ReadStatus::Closed){seal(now,Fault::Progress);return false;}
 if(snapshot.status==progress::ReadStatus::Ready&&(snapshot.health.saveError||snapshot.health.shutdown||snapshot.health.sleep)){
  // Failure/stop is terminal even when no challenge is pending. Never wait for
  // freshness before obeying an already-published shutdown or failed save.
  seal(now,Fault::Progress);return false;
 }
 if(waiting_){
  if(reached(now,pendingChallenge_.replyBy)){seal(now,Fault::Deadline);return false;}
  if(snapshot.status==progress::ReadStatus::Ready&&snapshot.health.mainSequence>respondedMain_&&atLeast(snapshot.health.captured,pendingChallenge_.issued)){
   auto health=machine_.respond(pendingWire_,owner_.peer(),now,authority,{true,snapshot.health});
   if(!health){seal(now,Fault::Protocol);return false;}if(!owner_.send(*health)){seal(now,Fault::Send);return false;}
   respondedMain_=snapshot.health.mainSequence;waiting_=false;awaitingGrant_=true;
  }
 }
 return true;
}
bool ChildControl::admitted(ClockPair now,Authority authority)noexcept{if(sealed_)return false;if(waiting_&&reached(now,pendingChallenge_.replyBy)){seal(now,Fault::Deadline);return false;}bool yes=machine_.admitted(now,authority);if(machine_.closed())seal(now,Fault::Protocol);return yes&&!sealed_;}
void ChildControl::close(ClockPair now)noexcept{if(sealed_)return;machine_.close(now);seal(now,Fault::None);}
}
