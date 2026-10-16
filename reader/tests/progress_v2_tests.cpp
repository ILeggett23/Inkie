#include "progress_v2.h"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <thread>
#include <vector>
using namespace recovery::v2;using namespace recovery::v2::progress;
std::atomic<bool> tracking{};std::atomic<unsigned long> allocations{};
void* operator new(std::size_t n){if(tracking.load())++allocations;if(auto p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void operator delete(void* p)noexcept{std::free(p);}void operator delete(void* p,std::size_t)noexcept{std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}void operator delete[](void* p)noexcept{std::free(p);}void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
unsigned checks{};
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::abort();}}while(false)
ClockPair at(std::uint64_t n){return {n,n,1};}
Health sample(std::uint64_t n){Health h;h.captured=at(n);h.mainCompleted=at(n);h.mainSequence=n;h.workerCount=8;h.saveError=n&1;h.shutdown=n&2;h.sleep=n&4;for(auto& w:h.workers)w={n,n/2,static_cast<std::uint32_t>(n-n/2),at(n)};return h;}
void ledger(){
 WorkLedger l(WorkerKind::DurablePrefix,3);auto a=l.accept(at(1)),b=l.accept(at(2)),c=l.accept(at(3));CHECK(a.status==Acceptance::Accepted&&c.token.sequence()==3);
 std::thread t([&]{l.complete(c.token,Outcome::DurableCommitted);l.complete(b.token,Outcome::DurableCommitted);});t.join();
 auto s=l.snapshot();CHECK(s.health.accepted==3&&s.health.completed==0&&s.health.backlog==3&&s.health.oldest==at(1));CHECK(l.accept(at(4)).status==Acceptance::Full);CHECK(l.fault()==Fault::None);
 CHECK(l.complete(a.token,Outcome::DurableCommitted));s=l.snapshot();CHECK(s.health.completed==3&&s.health.oldest==ClockPair{});
 auto d=l.accept(at(4));CHECK(d.token.sequence()==4);std::atomic<bool> go{};std::thread late([&]{while(!go.load()){}l.complete(a.token,Outcome::DurableCommitted);});std::thread valid([&]{while(!go.load()){}l.complete(d.token,Outcome::DurableCommitted);});go=true;late.join();valid.join();s=l.snapshot();CHECK(s.health.completed==4&&s.fault==Fault::Completion);CHECK(l.accept(at(5)).status==Acceptance::Closed);
 WorkLedger dup(WorkerKind::DurablePrefix,1);auto token=dup.accept(at(1)).token;CHECK(dup.complete(token,Outcome::DurableCommitted));CHECK(!dup.complete(token,Outcome::DurableCommitted));CHECK(dup.snapshot().fault==Fault::Completion);
 WorkLedger cross(WorkerKind::DurablePrefix,1);CHECK(!cross.complete(token,Outcome::DurableCommitted));CHECK(cross.fault()==Fault::Completion);
 WorkLedger bad(WorkerKind::DurablePrefix,1);CHECK(!bad.complete({},Outcome::DurableCommitted));CHECK(bad.fault()==Fault::Completion);
 for(auto cap:{0u,33u}){WorkLedger invalid(WorkerKind::DurablePrefix,cap);CHECK(invalid.accept(at(1)).status==Acceptance::Closed);CHECK(invalid.snapshot().fault==Fault::Configuration);}
 WorkLedger invalidKind(WorkerKind::None,1);CHECK(invalidKind.fault()==Fault::Configuration);
 WorkLedger invalidCeiling(WorkerKind::DurablePrefix,1,UINT64_MAX);CHECK(invalidCeiling.fault()==Fault::Configuration);
 WorkLedger backwards(WorkerKind::DurablePrefix,2);backwards.accept(at(5));CHECK(backwards.accept(at(4)).status==Acceptance::Closed&&backwards.fault()==Fault::Clock);
 WorkLedger zeroEpoch(WorkerKind::DurablePrefix,1);CHECK(zeroEpoch.accept({1,1,0}).status==Acceptance::Closed);
 WorkLedger foreignSnapshot(WorkerKind::DurablePrefix,1);foreignSnapshot.accept(at(1));std::thread badSnapshot([&]{foreignSnapshot.snapshot();});badSnapshot.join();CHECK(foreignSnapshot.fault()==Fault::ForeignProducer&&foreignSnapshot.snapshot().health.accepted==1);
 WorkLedger staleBefore(WorkerKind::DurablePrefix,1);auto old=staleBefore.accept(at(1)).token;CHECK(staleBefore.complete(old,Outcome::DurableCommitted));staleBefore.snapshot();auto fresh=staleBefore.accept(at(2)).token;CHECK(!staleBefore.complete(old,Outcome::DurableCommitted));CHECK(staleBefore.snapshot().health.completed==1);CHECK(staleBefore.complete(fresh,Outcome::DurableCommitted));CHECK(staleBefore.snapshot().health.completed==2&&staleBefore.fault()==Fault::Completion);
 WorkLedger wrap(WorkerKind::DurablePrefix,1,2);for(unsigned i=1;i<=2;++i){auto tok=wrap.accept(at(i)).token;CHECK(wrap.complete(tok,Outcome::DurableCommitted));CHECK(wrap.snapshot().health.completed==i);}CHECK(wrap.accept(at(3)).status==Acceptance::Closed);CHECK(wrap.fault()==Fault::Overflow);
 WorkLedger fail(WorkerKind::DurablePrefix,2);auto f=fail.accept(at(1));auto newer=fail.accept(at(2));CHECK(fail.complete(newer.token,Outcome::DurableCommitted));CHECK(fail.complete(f.token,Outcome::Failed));s=fail.snapshot();CHECK(s.fault==Fault::WorkFailed&&s.health.completed==0&&s.health.backlog==2);
 WorkLedger helper(WorkerKind::CancelableHelperPrefix,1);auto h=helper.accept(at(1));CHECK(!helper.complete(h.token,Outcome::DurableCommitted));CHECK(helper.fault()==Fault::Outcome&&helper.snapshot().health.completed==0);
 WorkLedger epoch(WorkerKind::DurablePrefix,2);epoch.accept(at(1));CHECK(epoch.accept({2,2,2}).status==Acceptance::Closed);CHECK(epoch.fault()==Fault::Clock);
 WorkLedger foreign(WorkerKind::DurablePrefix,1);std::thread wrong([&]{foreign.accept(at(1));foreign.snapshot();});wrong.join();CHECK(foreign.snapshot().health.accepted==0&&foreign.fault()==Fault::ForeignProducer);
}
void channel(){
 MainProgressChannel c;CHECK(c.read().status==ReadStatus::Empty);CHECK(c.publish(sample(1)));auto r=c.read();CHECK(r.status==ReadStatus::Ready&&r.health==sample(1)&&r.publication==1);
 std::atomic<unsigned> wrong{};std::thread observer([&]{for(unsigned i=0;i<10000;++i){auto x=c.read();if(x.status!=ReadStatus::Ready||x.health.mainSequence!=1||x.publication!=1)++wrong;}});observer.join();CHECK(!wrong.load());CHECK(c.read().health.mainSequence==1);
 std::thread foreign([&]{c.publish(sample(2));});foreign.join();CHECK(c.read().status==ReadStatus::Closed&&c.fault()==Fault::ForeignProducer);CHECK(!c.publish(sample(3)));
 MainProgressChannel limit(1);CHECK(limit.publish(sample(1)));CHECK(!limit.publish(sample(2)));CHECK(limit.fault()==Fault::Overflow);
 MainProgressChannel invalid(0);CHECK(invalid.read().status==ReadStatus::Closed);
 // Scoped producer handoff (display start-up window): owner delegates to one named thread, reclaims after join.
 {MainProgressChannel d;CHECK(d.publish(sample(1)));std::atomic<std::uint64_t> id{0};std::atomic<bool> may{false},did{false};
  std::thread helper([&]{id=currentThread();while(!may)std::this_thread::yield();did=d.publish(sample(2));});
  while(!id)std::this_thread::yield();CHECK(d.delegate(id));may=true;helper.join();CHECK(did&&d.read().health.mainSequence==2&&d.fault()==Fault::None);
  CHECK(d.reclaim()&&d.publish(sample(3))&&d.read().health.mainSequence==3);}
 {MainProgressChannel d;std::atomic<std::uint64_t> id{0};std::thread h([&]{id=currentThread();});h.join();CHECK(d.delegate(id));CHECK(!d.publish(sample(1))&&d.fault()==Fault::ForeignProducer);} // owner cannot publish while delegated
 {MainProgressChannel d;std::atomic<std::uint64_t> id{0};std::thread h([&]{id=currentThread();});h.join();CHECK(d.delegate(id));std::thread third([&]{d.publish(sample(1));});third.join();CHECK(d.fault()==Fault::ForeignProducer);} // a third thread still faults
 {MainProgressChannel d;std::atomic<bool> r{true};std::thread h([&]{r=d.delegate(currentThread()+1000);});h.join();CHECK(!r&&d.fault()==Fault::ForeignProducer);} // only the owner may delegate
 {MainProgressChannel d;CHECK(!d.delegate(currentThread())&&d.fault()==Fault::ForeignProducer);} // never to itself
 {MainProgressChannel d;std::atomic<std::uint64_t> a{0},b{0};std::thread h1([&]{a=currentThread();}),h2([&]{b=currentThread();});h1.join();h2.join();CHECK(d.delegate(a));CHECK(!d.delegate(b)&&d.fault()==Fault::ForeignProducer);} // no re-delegation while delegated
 // A reader may close while the publisher is copying. The publisher's final
 // status must observe closure and no later call may reopen the channel.
 MainProgressChannel concurrent;std::atomic<bool> start{},done{};std::atomic<unsigned> corrupt{},ready{},closed{};
 std::thread reader([&]{while(!start.load()){}while(!done.load()){auto x=concurrent.read();if(x.attempts>3)++corrupt;if(x.status==ReadStatus::Ready){++ready;if(x.health!=sample(x.health.mainSequence))++corrupt;}if(x.status==ReadStatus::Closed)++closed;}});
 start=true;for(unsigned i=1;i<=100000;++i)if(!concurrent.publish(sample(i)))break;done=true;reader.join();CHECK(!corrupt.load());auto end=concurrent.read();CHECK(end.status==ReadStatus::Ready||end.status==ReadStatus::Closed);if(end.status==ReadStatus::Closed){CHECK(!concurrent.publish(sample(100001)));CHECK(concurrent.read().status==ReadStatus::Closed);}std::printf("contention ready=%u closed=%u final=%d\n",ready.load(),closed.load(),int(end.status));
 // Regression: benign reader/publisher contention must never close a healthy
 // channel, and a torn read is Empty, never a corrupt Ready.
 MainProgressChannel sustained;std::atomic<bool> go{},stop{};std::atomic<unsigned> bad{},seen{},empty{};
 std::thread poller([&]{while(!go.load()){}while(!stop.load()){auto x=sustained.read();if(x.attempts>3||x.status==ReadStatus::Closed)++bad;if(x.status==ReadStatus::Ready){++seen;if(x.health!=sample(x.health.mainSequence))++bad;}if(x.status==ReadStatus::Empty)++empty;}});
 go=true;unsigned published=0;for(unsigned i=1;i<=200000;++i){if(!sustained.publish(sample(i)))break;published=i;}stop=true;poller.join();
 CHECK(published==200000);CHECK(!bad.load());CHECK(sustained.fault()==Fault::None);CHECK(sustained.read().status==ReadStatus::Ready&&sustained.read().health==sample(200000));
 std::printf("sustained contention ready=%u empty=%u closed=0\n",seen.load(),empty.load());
}
void zeroAlloc(){
 WorkLedger l(WorkerKind::CancelableHelperPrefix,1);MainProgressChannel c;std::atomic<unsigned> submitted{},finished{},errors{};WorkToken token;
 std::thread worker([&]{for(unsigned i=1;i<=1000;++i){while(submitted.load()!=i){}if(!l.complete(token,Outcome::HelperReapedReconciled))++errors;finished=i;}});
 tracking=true;
 for(unsigned i=1;i<=1000;++i){token=l.accept(at(i)).token;submitted=i;while(finished.load()!=i){}auto s=l.snapshot();auto h=sample(i);h.workers[0]=s.health;bool ok=c.publish(h);auto r=c.read();if(!ok||r.status!=ReadStatus::Ready||r.health.workers[0].completed!=i)++errors;}
 tracking=false;worker.join();CHECK(!errors.load());CHECK(allocations.load()==0);CHECK(l.snapshot().health.completed==1000);
}

int main(){ledger();channel();zeroAlloc();std::printf("PASS progress-v2 checks=%u allocations=%lu runtime_wired=false\n",checks,allocations.load());}
