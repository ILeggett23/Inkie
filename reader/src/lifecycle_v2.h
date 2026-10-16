// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "work_tracking.h"
#include "session.h"
#include "control_v2.h"
#include "profile_v2.h"
#include "quiescence_v2.h"
#include <algorithm>
#include <atomic>
#include <memory>
#include <thread>
namespace reader::lifecycle {
// Fixed trusted domain mapping; the qualified profile's worker policies must
// match it exactly. Health never selects or reorders domains.
enum class Domain:std::uint32_t {Canonical=0,Notes=1,Tom=2,Preparation=3,Resume=4};
inline constexpr std::uint32_t DomainCount=recovery::v2::quiescence::Domains;
recovery::v2::WorkerKind kind(Domain)noexcept;
bool matches(const recovery::v2::Timing&)noexcept;
// Foreground-owned ledgers. Accept and snapshot stay on the foreground thread;
// workers only complete captured tokens through the Shell bindings.
class Domains final {
 std::array<std::unique_ptr<recovery::v2::progress::WorkLedger>,DomainCount> ledgers_{};
 std::uint64_t clockGeneration_{};bool valid_{};
public:
 Domains(const recovery::v2::Timing&,std::uint64_t clockGeneration);
 Domains(const Domains&)=delete;Domains& operator=(const Domains&)=delete;
 bool valid()const noexcept{return valid_;}
 Tracking tracking(Domain)const noexcept;
 recovery::v2::progress::Snapshot snapshot(Domain)noexcept;
 std::uint64_t clockGeneration()const noexcept{return clockGeneration_;}
};
recovery::v2::ClockPair sample(std::uint64_t clockGeneration)noexcept;
// The only producer of the main progress channel. Called from the real session
// loop's Display::pump() and the pre-session admission wait; no allocation,
// locking, socket or file work. A stalled loop therefore stops publishing.
class Foreground final {
 Domains& domains_;recovery::v2::progress::MainProgressChannel& channel_;
 recovery::v2::Health health_{};std::atomic<bool> shutdown_{},sleep_{};bool failed_{};
public:
 Foreground(Domains&,recovery::v2::progress::MainProgressChannel&)noexcept;
 bool publish(bool saveError)noexcept;
 // Display start-up window only (see ux::Display): scoped producer handoff on the channel.
 bool delegate(std::uint64_t thread)noexcept{return channel_.delegate(thread);}
 // Delegated thread only: re-publish the last worker snapshot taken by the main thread with a fresh
 // main-progress stamp. Never touches the work ledgers (they accept only their owner thread).
 bool heartbeat()noexcept;
 // Diagnostics (E2 logging): the last published health, read on the main thread only.
 const recovery::v2::Health& lastHealth()const noexcept{return health_;}
 bool reclaim()noexcept{return channel_.reclaim();}
 void requestShutdown()noexcept{shutdown_.store(true,std::memory_order_release);}
 void requestSleep()noexcept{sleep_.store(true,std::memory_order_release);}
 bool failed()const noexcept{return failed_;}
 const recovery::v2::Health& last()const noexcept{return health_;}
};
// Fixed at construction: never renewed, and within both the unchanged V1
// payload limit and the reader unit ceiling less its drain/stop/kill budget.
recovery::v2::Ns sessionDeadline(recovery::v2::Ns now,const recovery::v2::runtime::Profile&)noexcept;
// D1 sustained sessions: the reader unit ceiling less its drain/stop/kill budget
// (not the V1 payload cap); the live grant expiry bounds it further.
recovery::v2::Ns sustainedDeadline(recovery::v2::Ns now,const recovery::v2::runtime::Profile&)noexcept;
// Presents V2 short grants to the unchanged session loop. valid() is true only
// while the owner's latest grant admits; loss, closure or stop is terminal.
// Precondition: the bootstrap Channel already enabled SO_PASSCRED on ownerFd
// before the owner could send; otherwise the first challenge fails closed.
// D2 (user-ended sessions): valid from the stock-stopped permit until the user's power-button press;
// no grants, no deadline (one year ahead, re-read each loop) and input quotas far above any pen
// (1 s window, 1000 contacts, 1,000,000 samples) so nothing but the button ends writing.
class UserLease final:public spike::RecoveryLease {
 std::atomic<bool> stop_{};
public:
 bool valid()const noexcept override{return !stop_.load(std::memory_order_acquire);}
 spike::Ns deadline()const noexcept override{return spike::monotonic()+spike::Ns(365)*24*3600*1000000000ull;}
 bool stopRequested()const noexcept override{return stop_.load(std::memory_order_acquire);}
 spike::SustainedPolicy sustained()const noexcept override{return {1000000000ull,1000,1000000};}
 void requestStop()noexcept{stop_.store(true,std::memory_order_release);}
};
class Lease final:public spike::RecoveryLease {
 std::atomic<bool> admitted_{},closed_{},stop_{},power_{},quit_{};
 std::atomic<std::uint64_t> grantSequence_{},grantExpiry_{};std::atomic<unsigned> fault_{},reason_{};const spike::SustainedPolicy policy_;
 const recovery::v2::Ns deadline_;const std::uint64_t clockGeneration_;
 std::unique_ptr<recovery::v2::control::ChildControl> child_;std::thread observer_;
 void observe()noexcept;
public:
 Lease(const recovery::v2::SessionIdentity&,std::uint64_t generation,const recovery::v2::Timing&,recovery::v2::ClockPair start,
       int ownerFd,std::uint64_t connection,std::uint32_t ownerUid,recovery::v2::progress::MainProgressChannel&,
       recovery::v2::Ns deadline,bool verifiedPower,spike::SustainedPolicy policy={});
 ~Lease()override;Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
 bool valid()const noexcept override{return admitted_.load(std::memory_order_acquire)&&!closed_.load(std::memory_order_acquire)&&!stop_.load(std::memory_order_acquire);}
 // Sustained: the current grant's expiry, never later than the fixed ceiling;
 // no grant means no deadline (fails closed). Otherwise the fixed V1-bounded deadline.
 recovery::v2::Ns deadline()const noexcept override{if(!policy_.enabled())return deadline_;const auto g=grantExpiry_.load(std::memory_order_acquire);return g?std::min(deadline_,g):0;}
 spike::SustainedPolicy sustained()const noexcept override{return policy_;}
 bool stopRequested()const noexcept override{return closed_.load(std::memory_order_acquire)||stop_.load(std::memory_order_acquire);}
 void requestStop()noexcept{stop_.store(true,std::memory_order_release);}
 // Power authority can only be withdrawn; it is never restored in-session.
 void powerLost()noexcept{power_.store(false,std::memory_order_release);}
 bool closed()const noexcept{return closed_.load(std::memory_order_acquire);}
 std::uint64_t grantSequence()const noexcept{return grantSequence_.load(std::memory_order_acquire);}
 // Diagnostics only: last observed control fault and protocol reason.
 unsigned fault()const noexcept{return fault_.load(std::memory_order_acquire);}
 unsigned reason()const noexcept{return reason_.load(std::memory_order_acquire);}
};
// Publishes until the first grant admits or the budget/closure ends the wait.
bool awaitAdmission(Lease&,Foreground&,recovery::v2::Ns budget,bool saveError)noexcept;
struct ApplicationState {bool inputClosed{},admissionClosed{},saveFailed{},shellFailed{},semanticFailed{};std::uint32_t unsaved{},semanticPending{};};
// Foreground only, after the session loop has returned and workers finished.
recovery::v2::quiescence::Report finalReport(Domains&,const recovery::v2::SessionIdentity&,std::uint64_t generation,
                                             std::uint64_t lastGrantSequence,const ApplicationState&)noexcept;
}
