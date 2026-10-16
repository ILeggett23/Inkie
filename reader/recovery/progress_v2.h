// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "protocol_v2.h"
#include <atomic>
#include <limits>
namespace recovery::v2::progress {
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
enum class Fault:std::uint64_t {None,Configuration,ForeignProducer,Clock,Overflow,Completion,Outcome,WorkFailed,Coherence};
enum class Outcome {DurableCommitted,HelperReapedReconciled,Failed};
class WorkToken {
 std::uint64_t ledger_{},sequence_{};std::uint32_t slot_{};
 friend class WorkLedger;
public:
 explicit operator bool()const noexcept{return ledger_&&sequence_;}
 std::uint64_t sequence()const noexcept{return sequence_;}
};
enum class Acceptance {Accepted,Full,Closed};
struct Accepted {Acceptance status{Acceptance::Closed};WorkToken token{};};
struct Snapshot {WorkerHealth health{};Fault fault{};};
class WorkLedger final {
 static constexpr std::uint64_t HardLimit=std::numeric_limits<std::uint64_t>::max()>>2;
 struct Slot {std::atomic<std::uint64_t> tag{};ClockPair time{};};
 const WorkerKind kind_;const std::uint32_t capacity_;const std::uint64_t limit_,owner_,id_;
 std::array<Slot,32> slots_{};std::atomic<std::uint64_t> fault_{};
 std::uint64_t accepted_{},completed_{};ClockPair last_{};
 void fail(Fault)noexcept;bool owner()noexcept;
public:
 explicit WorkLedger(WorkerKind,std::uint32_t capacity,std::uint64_t sequenceCeiling=HardLimit)noexcept;
 WorkLedger(const WorkLedger&)=delete;WorkLedger& operator=(const WorkLedger&)=delete;
 Accepted accept(ClockPair)noexcept;
 bool complete(WorkToken,Outcome)noexcept;
 Snapshot snapshot()noexcept; // Also retires the completed prefix; producer only.
 Fault fault()const noexcept{return static_cast<Fault>(fault_.load());}
};
enum class ReadStatus {Empty,Ready,Closed};
struct ReadResult {ReadStatus status{ReadStatus::Closed};Health health{};Fault fault{};unsigned attempts{};std::uint64_t publication{};};
// Process-unique identity of the calling thread (for MainProgressChannel::delegate).
std::uint64_t currentThread()noexcept;
class MainProgressChannel final {
 const std::uint64_t owner_,limit_;
 std::atomic<std::uint64_t> stamp_{},fault_{},producer_{};
 std::array<std::atomic<std::uint64_t>,9+6*MaxWorkers> words_{};
 void fail(Fault)noexcept;
public:
 explicit MainProgressChannel(std::uint64_t publicationCeiling=std::numeric_limits<std::uint64_t>::max()/2)noexcept;
 MainProgressChannel(const MainProgressChannel&)=delete;MainProgressChannel& operator=(const MainProgressChannel&)=delete;
 bool publish(const Health&)noexcept;
 // Scoped producer handoff (V2 display start-up window only): the owning thread may delegate
 // publication to exactly one other thread and must reclaim it after that thread is joined. While
 // delegated the owner cannot publish; any other thread still faults (ForeignProducer).
 bool delegate(std::uint64_t thread)noexcept;
 bool reclaim()noexcept;
 ReadResult read()noexcept;
 Fault fault()const noexcept{return static_cast<Fault>(fault_.load());}
};
}
