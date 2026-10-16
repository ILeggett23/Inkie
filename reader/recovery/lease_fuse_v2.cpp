// SPDX-License-Identifier: AGPL-3.0-only
#include "lease_fuse_v2.h"
#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/socket.h>
#include <unistd.h>
#include <systemd/sd-daemon.h>
namespace recovery::v2::fuse {
namespace {
constexpr auto Max=std::numeric_limits<Ns>::max();
bool add(Ns a,Ns b,Ns& out)noexcept{if(b>Max-a)return false;out=a+b;return true;}
bool plus(ClockPair a,Ns n,ClockPair& out)noexcept{out.generation=a.generation;return add(a.monotonic,n,out.monotonic)&&add(a.boottime,n,out.boottime);}
bool elapsed(ClockPair from,ClockPair to,Ns skew)noexcept{
 if(!from.generation||to.generation!=from.generation||to.monotonic<from.monotonic||to.boottime<from.boottime)return false;
 auto m=to.monotonic-from.monotonic,b=to.boottime-from.boottime;return (m>b?m-b:b-m)<=skew;
}
bool reached(ClockPair n,ClockPair end)noexcept{return n.monotonic>=end.monotonic||n.boottime>=end.boottime;}
bool window(ClockPair a,ClockPair b,Ns n)noexcept{ClockPair expected;return plus(a,n,expected)&&expected==b;}
bool peerMatches(VerifiedPeer a,VerifiedPeer b,std::uint32_t uid,std::uint32_t pid)noexcept{return a.verified&&b.verified&&a.uid==uid&&b.uid==uid&&a.pid==pid&&b.pid==pid&&a.connection&&a.connection==b.connection;}
bool hexadecimal(std::span<const char> text)noexcept{for(char c:text)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
bool validBinding(const Binding& b,const SessionIdentity& id)noexcept{
 Binding expected=b;if(!setUnitName(expected,id)||expected.unit!=b.unit||!b.mainPid||b.mainPid==id.ownerPid||b.mainPid==id.mainPid||!b.originalReference)return false;
 return b.invocation[32]==0&&hexadecimal(std::span(b.invocation).first(32))&&std::string_view(b.invocation.data())!="00000000000000000000000000000000";
}
bool socketPeer(int fd,VerifiedPeer& p)noexcept{
 struct ucred c{};socklen_t n=sizeof(c);int type{};socklen_t t=sizeof(type);
 if(getsockopt(fd,SOL_SOCKET,SO_TYPE,&type,&t)||type!=SOCK_SEQPACKET||getsockopt(fd,SOL_SOCKET,SO_PEERCRED,&c,&n)||n!=sizeof(c)||c.pid<=0)return false;
 p.uid=std::uint32_t(c.uid);p.pid=std::uint32_t(c.pid);p.verified=true;return true;
}
}
bool setUnitName(Binding& b,const SessionIdentity& id)noexcept{
 if(!validIdentity(id))return false;char name[96]{};int n=std::snprintf(name,sizeof(name),"inkbound-fuse-v2-%s.service",id.session.data());
 return n>0&&std::size_t(n)<sizeof(name)&&text(b.unit,name);
}
bool validPolicy(const Policy& p,const Timing& t)noexcept{
 Ns stop,exchange,bound;if(!validTiming(t)||!p.bootstrapLifetime||!p.leaseLifetime||!p.dispatchMargin||p.bootstrapLifetime%1000||p.leaseLifetime%1000||p.dispatchMargin%1000)return false;
 return add(t.stopGrace,t.killGrace,stop)&&add(t.healthCadence,t.challengeLifetime,exchange)&&exchange<p.leaseLifetime&&
 add(p.leaseLifetime,p.dispatchMargin,bound)&&bound<=stop&&add(p.bootstrapLifetime,p.dispatchMargin,bound)&&bound<=stop;
}
Lease::Lease(SessionIdentity id,Binding binding,std::uint64_t gen,Timing t,Policy p,VerifiedPeer owner,std::uint32_t uid,ClockPair start,Notify notify,void* context)noexcept:
 identity_(id),binding_(binding),timing_(t),policy_(p),generation_(gen),owner_(owner),uid_(uid),notify_(notify),context_(context),last_(start){
 if(!validIdentity(id)||!validBinding(binding,id))fault_=Fault::Identity;
 else if(!validPolicy(p,t)||!gen||gen==Max||!start.generation||!start.monotonic||!start.boottime||!notify)fault_=Fault::Configuration;
 else if(!peerMatches(owner,owner,uid,id.ownerPid))fault_=Fault::Peer;
 else if(!plus(start,p.bootstrapLifetime,startupUntil_)||!plus(start,p.bootstrapLifetime+p.dispatchMargin,outstandingUntil_))fault_=Fault::Clock;
 closed_=fault_!=Fault::None;
}
void Lease::shut(Fault f,ClockPair at)noexcept{
 if(closed_)return;closed_=true;active_=false;fault_=f;closedAt_=at;
 Ns stop;if(!add(timing_.stopGrace,timing_.killGrace,stop)||!plus(at,stop,killBy_))killBy_=at;
}
bool Lease::observe(ClockPair now,const Evidence& e)noexcept{
 if(closed_)return false;
 if(!elapsed(last_,now,timing_.clockSkew)){shut(Fault::Clock,last_);return false;}last_=now;
 if(e.fuse!=binding_||e.session!=identity_){shut(Fault::Identity,now);return false;}
 if(!e.originalOwnerReference||!e.originalReaderReference||!e.originalFuseReference||!e.originalManager||!e.verifiedProfile||!e.completePowerBarrier){shut(Fault::Authority,now);return false;}
 if(!active_&&reached(now,startupUntil_)){shut(Fault::StartupExpired,now);return false;}
 if(active_&&reached(now,drainAt_)){shut(Fault::GrantExpired,now);return false;}
 if(active_&&reached(now,leaseUntil_)){shut(Fault::LeaseExpired,now);return false;}
 return true;
}
bool Lease::accept(std::span<const std::uint8_t> raw,VerifiedPeer sender,ClockPair now,const Evidence& e)noexcept{
 if(!observe(now,e))return false;auto fail=[&](Fault f){shut(f,now);return false;};
 if(!peerMatches(sender,owner_,uid_,identity_.ownerPid))return fail(Fault::Peer);
 auto m=decode(raw);if(!m)return fail(Fault::Malformed);
 if(m->identity!=identity_||m->generation!=generation_)return fail(Fault::Identity);
 if(m->kind==Kind::Closure){bool valid=m->sequence==sequence_&&m->closure.closureGeneration==generation_+1&&elapsed(m->closure.closedAt,now,timing_.clockSkew);shut(valid?Fault::Closure:Fault::Malformed,valid?m->closure.closedAt:now);return valid;}
 if(m->kind!=Kind::Grant)return fail(Fault::Malformed);
 const auto& g=m->grant;
 if(sequence_==Max||m->sequence!=sequence_+1||g.challengeSequence<=challengeSequence_||g.nonce==nonce_)return fail(Fault::Sequence);
 if(g.mainSequence<=mainSequence_)return fail(Fault::Progress);
 if(!window(g.issued,g.expires,timing_.grantLifetime)||!window(g.issued,g.drainAt,timing_.grantLifetime-timing_.drainLead)||
    !elapsed(g.issued,now,timing_.clockSkew)||now.monotonic-g.issued.monotonic>=timing_.challengeLifetime||now.boottime-g.issued.boottime>=timing_.challengeLifetime||reached(now,g.drainAt))return fail(Fault::GrantExpired);
 ClockPair until,cutoff;if(!plus(g.issued,policy_.leaseLifetime,cutoff)||!plus(g.issued,policy_.leaseLifetime+policy_.dispatchMargin,until)||reached(now,cutoff))return fail(Fault::Clock);
 // EXTEND cannot shorten the existing PID1 deadline. Retain its maximum here.
 if(until.monotonic<outstandingUntil_.monotonic||until.boottime<outstandingUntil_.boottime)return fail(Fault::Configuration);
 auto remaining=std::min(cutoff.monotonic-now.monotonic,cutoff.boottime-now.boottime)/1000;
 if(!remaining||notify_(context_,remaining)<=0)return fail(Fault::Notification);
 outstandingUntil_=until;leaseUntil_=cutoff;drainAt_=g.drainAt;sequence_=m->sequence;mainSequence_=g.mainSequence;challengeSequence_=g.challengeSequence;nonce_=g.nonce;active_=true;return true;
}
bool Lease::tick(ClockPair now,const Evidence& e)noexcept{return observe(now,e);}
void Lease::disconnect(ClockPair now)noexcept{shut(Fault::Disconnected,elapsed(last_,now,timing_.clockSkew)?now:last_);}
OwnerChannel::OwnerChannel(int fd,std::uint64_t connection,std::uint32_t uid,std::uint32_t pid)noexcept:fd_(fd){
 peer_.connection=connection;auto flags=fd>=0?fcntl(fd,F_GETFL):-1;int pass=1;
 if(fd<0||!connection||flags<0||!socketPeer(fd,peer_)||peer_.uid!=uid||peer_.pid!=pid||setsockopt(fd,SOL_SOCKET,SO_PASSCRED,&pass,sizeof(pass))||fcntl(fd,F_SETFL,flags|O_NONBLOCK)||fcntl(fd,F_SETFD,FD_CLOEXEC)){if(fd>=0)close(fd);fd_=-1;peer_.verified=false;}
}
OwnerChannel::~OwnerChannel(){if(fd_>=0)close(fd_);}
bool OwnerChannel::receive(Lease& lease,ClockPair now,const Evidence& e)noexcept{return receiveAt(lease,now,e,nullptr,nullptr);}
bool OwnerChannel::receiveObserved(Lease& lease,const Evidence& e,ClockSource source,void* context)noexcept{if(!source){lease.disconnect({});return false;}return receiveAt(lease,source(context),e,source,context);}
bool OwnerChannel::receiveAt(Lease& lease,ClockPair now,const Evidence& e,ClockSource source,void* context)noexcept{
 if(!valid()){lease.disconnect(now);return false;}VerifiedPeer actual{};actual.connection=peer_.connection;
 if(!socketPeer(fd_,actual)||!peerMatches(actual,peer_,peer_.uid,peer_.pid)){lease.disconnect(now);return false;}
 Wire bytes{};struct iovec iov{bytes.data(),bytes.size()};struct msghdr m{};m.msg_iov=&iov;m.msg_iovlen=1;
 alignas(struct cmsghdr) std::array<char,CMSG_SPACE(sizeof(struct ucred))+CMSG_SPACE(8*sizeof(int))> control{};
 m.msg_control=control.data();m.msg_controllen=control.size();
 auto n=recvmsg(fd_,&m,MSG_DONTWAIT|MSG_CMSG_CLOEXEC);const int receiveError=errno;if(source)now=source(context);
 if(n<0&&(receiveError==EAGAIN||receiveError==EWOULDBLOCK))return lease.tick(now,e);
 unsigned credentials{};bool foreign{};
 for(auto c=CMSG_FIRSTHDR(&m);c;c=CMSG_NXTHDR(&m,c)){
  if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_RIGHTS){
   if(c->cmsg_len>=CMSG_LEN(0)){auto count=(c->cmsg_len-CMSG_LEN(0))/sizeof(int);auto fds=reinterpret_cast<int*>(CMSG_DATA(c));for(std::size_t k=0;k<count;++k)close(fds[k]);}foreign=true;
  }else if(c->cmsg_level==SOL_SOCKET&&c->cmsg_type==SCM_CREDENTIALS&&c->cmsg_len==CMSG_LEN(sizeof(struct ucred))){
   struct ucred sender{};std::memcpy(&sender,CMSG_DATA(c),sizeof(sender));++credentials;
   if(sender.pid<=0||std::uint32_t(sender.pid)!=peer_.pid||std::uint32_t(sender.uid)!=peer_.uid)foreign=true;
  }else foreign=true;
 }
 if(n<=0||std::size_t(n)!=bytes.size()||(m.msg_flags&(MSG_TRUNC|MSG_CTRUNC))||credentials!=1||foreign){lease.disconnect(now);return false;}
 return lease.accept(bytes,actual,now,e);
}
int notifyRuntime(void*,std::uint64_t usec)noexcept{
 if(!usec)return -EINVAL;char message[80]{};int n=std::snprintf(message,sizeof(message),"EXTEND_TIMEOUT_USEC=%llu",static_cast<unsigned long long>(usec));
 return n>0&&std::size_t(n)<sizeof(message)?sd_notify(0,message):-EINVAL;
}
}
