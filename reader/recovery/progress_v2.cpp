// SPDX-License-Identifier: AGPL-3.0-only
#include "progress_v2.h"
#ifdef PROGRESS_V2_TEST_INTERLEAVING
// Test-only build pauses at the publication boundary; absent from the library.
extern void progressV2PublicationCheckpoint() noexcept;
#endif
namespace recovery::v2::progress {
namespace {
std::atomic<std::uint64_t> threads{},ledgers{};
std::uint64_t issue(std::atomic<std::uint64_t>& issuer)noexcept {
 auto old=issuer.load();for(unsigned i=0;i<3;++i){if(old==UINT64_MAX)return 0;if(issuer.compare_exchange_strong(old,old+1))return old+1;}return 0;
}
std::uint64_t threadIdentity()noexcept {thread_local const auto id=issue(threads);return id;}
void latch(std::atomic<std::uint64_t>& dst,Fault f)noexcept {std::uint64_t none=0;dst.compare_exchange_strong(none,static_cast<std::uint64_t>(f));}
using Words=std::array<std::uint64_t,9+6*MaxWorkers>;
Words flatten(const Health& h)noexcept {
 Words a{};std::size_t i=0;auto clock=[&](ClockPair c){a[i++]=c.monotonic;a[i++]=c.boottime;a[i++]=c.generation;};
 clock(h.captured);clock(h.mainCompleted);a[i++]=h.mainSequence;a[i++]=h.workerCount;a[i++]=h.saveError|(std::uint64_t(h.shutdown)<<1)|(std::uint64_t(h.sleep)<<2);
 for(const auto& w:h.workers){a[i++]=w.accepted;a[i++]=w.completed;a[i++]=w.backlog;clock(w.oldest);}return a;
}
Health expand(const Words& a)noexcept {
 Health h;std::size_t i=0;auto clock=[&](){ClockPair c{a[i],a[i+1],a[i+2]};i+=3;return c;};
 h.captured=clock();h.mainCompleted=clock();h.mainSequence=a[i++];h.workerCount=static_cast<std::uint32_t>(a[i++]);auto flags=a[i++];h.saveError=flags&1;h.shutdown=flags&2;h.sleep=flags&4;
 for(auto& w:h.workers){w.accepted=a[i++];w.completed=a[i++];w.backlog=static_cast<std::uint32_t>(a[i++]);w.oldest=clock();}return h;
}
}
void WorkLedger::fail(Fault f)noexcept{latch(fault_,f);}
bool WorkLedger::owner()noexcept{if(owner_&&threadIdentity()==owner_)return true;fail(Fault::ForeignProducer);return false;}
WorkLedger::WorkLedger(WorkerKind kind,std::uint32_t cap,std::uint64_t ceiling)noexcept:kind_(kind),capacity_(cap),limit_(ceiling),owner_(threadIdentity()),id_(issue(ledgers)) {
 if(!owner_||!id_||!cap||cap>32||!ceiling||ceiling>HardLimit||(kind!=WorkerKind::DurablePrefix&&kind!=WorkerKind::CancelableHelperPrefix))fail(Fault::Configuration);
}
Accepted WorkLedger::accept(ClockPair now)noexcept {
 if(!owner()||fault()!=Fault::None)return {};
 if(!now.generation||(accepted_&&(now.generation!=last_.generation||now.monotonic<last_.monotonic||now.boottime<last_.boottime))){fail(Fault::Clock);return {};}
 if(accepted_==limit_){fail(Fault::Overflow);return {};}
 if(accepted_-completed_==capacity_)return {Acceptance::Full,{}};
 auto seq=accepted_+1;auto index=static_cast<std::uint32_t>((seq-1)%capacity_);auto& slot=slots_[index];
 if(slot.tag.load()!=0){fail(Fault::Completion);return {};}
 slot.time=now;slot.tag.store((seq<<2)|1);accepted_=seq;last_=now;
 WorkToken token;token.ledger_=id_;token.sequence_=seq;token.slot_=index;return {Acceptance::Accepted,token};
}
bool WorkLedger::complete(WorkToken token,Outcome outcome)noexcept {
 if(token.ledger_!=id_||!token.sequence_||token.sequence_>limit_||token.slot_>=capacity_||token.slot_>=32){fail(Fault::Completion);return false;}
 auto expected=(token.sequence_<<2)|1;auto& tag=slots_[token.slot_].tag;
 if(tag.load()!=expected){fail(Fault::Completion);return false;}
 const bool valid=outcome==Outcome::Failed||(kind_==WorkerKind::DurablePrefix&&outcome==Outcome::DurableCommitted)||(kind_==WorkerKind::CancelableHelperPrefix&&outcome==Outcome::HelperReapedReconciled);
 if(!valid){fail(Fault::Outcome);return false;}
 if(!tag.compare_exchange_strong(expected,(token.sequence_<<2)|(outcome==Outcome::Failed?3:2))){fail(Fault::Completion);return false;}
 if(outcome==Outcome::Failed)fail(Fault::WorkFailed);return true;
}
Snapshot WorkLedger::snapshot()noexcept {
 if(!owner())return {{},fault()};
 if(!capacity_||capacity_>32)return {{},fault()};
 for(unsigned n=0;n<capacity_&&completed_<accepted_;++n){auto seq=completed_+1;auto& slot=slots_[(seq-1)%capacity_];if(slot.tag.load()!=((seq<<2)|2))break;slot.tag.store(0);slot.time={};completed_=seq;}
 WorkerHealth h{accepted_,completed_,static_cast<std::uint32_t>(accepted_-completed_),{}};
 if(h.backlog)h.oldest=slots_[completed_%capacity_].time;return {h,fault()};
}
void MainProgressChannel::fail(Fault f)noexcept{latch(fault_,f);}
std::uint64_t currentThread()noexcept{return threadIdentity();}
MainProgressChannel::MainProgressChannel(std::uint64_t ceiling)noexcept:owner_(threadIdentity()),limit_(ceiling){producer_.store(owner_);if(!owner_||!ceiling||ceiling>UINT64_MAX/2)fail(Fault::Configuration);}
bool MainProgressChannel::delegate(std::uint64_t thread)noexcept{
 if(!owner_||threadIdentity()!=owner_||producer_.load(std::memory_order_acquire)!=owner_||!thread||thread==owner_){fail(Fault::ForeignProducer);return false;}
 producer_.store(thread,std::memory_order_release);return fault()==Fault::None;
}
bool MainProgressChannel::reclaim()noexcept{
 if(!owner_||threadIdentity()!=owner_){fail(Fault::ForeignProducer);return false;}
 producer_.store(owner_,std::memory_order_release);return fault()==Fault::None;
}
bool MainProgressChannel::publish(const Health& h)noexcept {
 if(!owner_||threadIdentity()!=producer_.load(std::memory_order_acquire)){fail(Fault::ForeignProducer);return false;}
 if(fault()!=Fault::None)return false;auto s=stamp_.load();
 if(s&1){fail(Fault::Coherence);return false;}if(s/2==limit_){fail(Fault::Overflow);return false;}
 if(!stamp_.compare_exchange_strong(s,s+1)){fail(Fault::Coherence);return false;}
#ifdef PROGRESS_V2_TEST_INTERLEAVING
 progressV2PublicationCheckpoint();
#endif
 auto a=flatten(h);for(std::size_t i=0;i<a.size();++i)words_[i].store(a[i]);
 std::atomic_thread_fence(std::memory_order_seq_cst);stamp_.store(s+2);return fault()==Fault::None;
}
ReadResult MainProgressChannel::read()noexcept {
 for(unsigned attempt=1;attempt<=3;++attempt){
  auto f=fault();if(f!=Fault::None)return {ReadStatus::Closed,{},f,attempt,0};
  auto s=stamp_.load();if(!s){f=fault();return {f==Fault::None?ReadStatus::Empty:ReadStatus::Closed,{},f,attempt,0};}
  if(s&1)continue;Words a{};for(std::size_t i=0;i<a.size();++i)a[i]=words_[i].load();
  std::atomic_thread_fence(std::memory_order_seq_cst);if(stamp_.load()!=s)continue;
  f=fault();if(f!=Fault::None)return {ReadStatus::Closed,{},f,attempt,0};return {ReadStatus::Ready,expand(a),Fault::None,attempt,s/2};
 }
 // Torn reads against a live publisher are transient contention, not
 // incoherence: report no fresh snapshot for this poll. A publisher frozen
 // mid-copy stays unreadable, so the challenge deadline still closes the session.
 return {ReadStatus::Empty,{},Fault::None,3,0};
}
}
