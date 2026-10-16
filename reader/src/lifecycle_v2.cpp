// SPDX-License-Identifier: AGPL-3.0-only
#include "lifecycle_v2.h"
#include "session_limits.h"
#include <algorithm>
#include <time.h>
#include <unistd.h>
namespace reader::lifecycle {
using namespace recovery::v2;
WorkerKind kind(Domain d)noexcept{return d==Domain::Preparation?WorkerKind::CancelableHelperPrefix:WorkerKind::DurablePrefix;}
bool matches(const Timing& t)noexcept{
 if(t.workers!=DomainCount)return false;
 for(std::uint32_t n=0;n<DomainCount;++n){const auto& p=t.workerPolicies[n];if(p.kind!=kind(Domain(n))||!p.maxBacklog||p.maxBacklog>32)return false;}
 return true;
}
Domains::Domains(const Timing& t,std::uint64_t clockGeneration):clockGeneration_(clockGeneration){
 if(!clockGeneration||!matches(t))return;
 for(std::uint32_t n=0;n<DomainCount;++n)ledgers_[n]=std::make_unique<progress::WorkLedger>(kind(Domain(n)),t.workerPolicies[n].maxBacklog);
 valid_=std::all_of(ledgers_.begin(),ledgers_.end(),[](const auto& l){return l&&l->fault()==progress::Fault::None;});
}
Tracking Domains::tracking(Domain d)const noexcept{
 // An invalid map yields a binding that refuses every acceptance, never the V1 empty binding.
 if(!valid_)return {nullptr,~std::uint64_t(0),kind(d)};
 return {ledgers_[std::uint32_t(d)].get(),clockGeneration_,kind(d)};
}
progress::Snapshot Domains::snapshot(Domain d)noexcept{
 if(!valid_)return {{},progress::Fault::Configuration};
 return ledgers_[std::uint32_t(d)]->snapshot();
}
ClockPair sample(std::uint64_t clockGeneration)noexcept{
 timespec m{},b{};
 if(!clockGeneration||clock_gettime(CLOCK_MONOTONIC,&m)||clock_gettime(CLOCK_BOOTTIME,&b)||m.tv_sec<0||b.tv_sec<0)return {};
 return {Ns(m.tv_sec)*1000000000+Ns(m.tv_nsec),Ns(b.tv_sec)*1000000000+Ns(b.tv_nsec),clockGeneration};
}
Foreground::Foreground(Domains& d,progress::MainProgressChannel& c)noexcept:domains_(d),channel_(c){health_.workerCount=DomainCount;}
bool Foreground::publish(bool saveError)noexcept{
 if(failed_||!domains_.valid())return false;
 auto now=sample(domains_.clockGeneration());if(!now.monotonic){failed_=true;return false;}
 bool faulted=false;
 for(std::uint32_t n=0;n<DomainCount;++n){auto s=domains_.snapshot(Domain(n));health_.workers[n]=s.health;faulted|=s.fault!=progress::Fault::None;}
 health_.captured=health_.mainCompleted=now;++health_.mainSequence;
 // Sticky: a reported save error or worker fault is never cleared in-session.
 health_.saveError=health_.saveError||saveError||faulted;
 health_.shutdown=health_.shutdown||shutdown_.load(std::memory_order_acquire);
 health_.sleep=health_.sleep||sleep_.load(std::memory_order_acquire);
 if(!channel_.publish(health_)){failed_=true;return false;}
 return true;
}
bool Foreground::heartbeat()noexcept{
 if(failed_)return false;
 auto now=sample(domains_.clockGeneration());if(!now.monotonic){failed_=true;return false;}
 health_.captured=health_.mainCompleted=now;++health_.mainSequence;
 health_.shutdown=health_.shutdown||shutdown_.load(std::memory_order_acquire);
 health_.sleep=health_.sleep||sleep_.load(std::memory_order_acquire);
 if(!channel_.publish(health_)){failed_=true;return false;}
 return true;
}
Ns sessionDeadline(Ns now,const runtime::Profile& p)noexcept{
 const auto& t=p.timing;const Ns reserve=t.drainLead+t.stopGrace+t.killGrace;
 if(!now||!runtime::valid(p)||p.readerCeiling<=reserve)return 0;
 const Ns span=std::min<Ns>(recovery::limits::Payload,p.readerCeiling-reserve);
 return recovery::limits::validDeadline(now,now+span)?now+span:0;
}
Ns sustainedDeadline(Ns now,const runtime::Profile& p)noexcept{
 const auto& t=p.timing;const Ns reserve=t.drainLead+t.stopGrace+t.killGrace;
 if(!now||!runtime::valid(p)||p.readerCeiling<=reserve)return 0;
 return now+(p.readerCeiling-reserve);
}
Lease::Lease(const SessionIdentity& id,std::uint64_t generation,const Timing& timing,ClockPair start,int ownerFd,std::uint64_t connection,
             std::uint32_t ownerUid,progress::MainProgressChannel& channel,Ns deadline,bool verifiedPower,spike::SustainedPolicy policy)
 :power_(verifiedPower),policy_(policy),deadline_(deadline),clockGeneration_(start.generation){
 // The channel takes SO_PASSCRED here, before the owner's first challenge can be read.
 child_=std::make_unique<control::ChildControl>(id,generation,timing,start,ownerFd,connection,ownerUid,channel);
 if(!deadline_||child_->closed()){fault_=unsigned(child_->fault());reason_=unsigned(child_->reason());closed_=true;return;}
 observer_=std::thread([this]{observe();});
}
void Lease::observe()noexcept{
 auto clock=[](void* context)noexcept{return sample(*static_cast<const std::uint64_t*>(context));};
 std::uint64_t generation=clockGeneration_;
 while(!quit_.load(std::memory_order_acquire)){
  const Authority authority{true,true,true,power_.load(std::memory_order_acquire)};
  const bool live=child_->pollObserved(authority,clock,&generation);
  const bool admitted=live&&child_->admitted(sample(generation),authority);
  grantSequence_.store(child_->grantSequence(),std::memory_order_release);
  grantExpiry_.store(admitted?child_->grant().expires.monotonic:0,std::memory_order_release);
  fault_.store(unsigned(child_->fault()),std::memory_order_release);reason_.store(unsigned(child_->reason()),std::memory_order_release);
  admitted_.store(admitted,std::memory_order_release);
  if(!live){grantExpiry_.store(0,std::memory_order_release);closed_.store(true,std::memory_order_release);return;}
  usleep(1000);
 }
 child_->close(sample(generation));closed_.store(true,std::memory_order_release);admitted_.store(false,std::memory_order_release);grantExpiry_.store(0,std::memory_order_release);
}
Lease::~Lease(){
 quit_.store(true,std::memory_order_release);
 if(observer_.joinable())observer_.join();
 else if(child_)child_->close(sample(clockGeneration_));
}
bool awaitAdmission(Lease& lease,Foreground& foreground,Ns budget,bool saveError)noexcept{
 const Ns begin=spike::monotonic();
 while(!lease.closed()&&!lease.stopRequested()){
  if(!foreground.publish(saveError))return false;
  if(lease.valid())return true;
  const Ns now=spike::monotonic();if(!now||!begin||now-begin>=budget)return false;
  usleep(1000);
 }
 return false;
}
quiescence::Report finalReport(Domains& domains,const SessionIdentity& id,std::uint64_t generation,std::uint64_t lastGrantSequence,const ApplicationState& app)noexcept{
 quiescence::Report r;r.session=id.session;r.invocation=id.invocation;r.ticket=id.ticket;
 r.generation=generation;r.clockGeneration=domains.clockGeneration();r.lastGrantSequence=lastGrantSequence;
 for(std::uint32_t n=0;n<DomainCount;++n){auto s=domains.snapshot(Domain(n));r.domains[n]=s.health;r.faults[n]=std::uint64_t(s.fault);}
 const auto& helper=r.domains[std::uint32_t(Domain::Preparation)];
 r.helpersReconciled=domains.valid()&&!r.faults[std::uint32_t(Domain::Preparation)]&&!helper.backlog&&helper.accepted==helper.completed;
 r.inputClosed=app.inputClosed;r.admissionClosed=app.admissionClosed;r.saveFailed=app.saveFailed;r.shellFailed=app.shellFailed;
 r.semanticFailed=app.semanticFailed;r.unsaved=app.unsaved;r.semanticPending=app.semanticPending;
 return r;
}
}
