// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
namespace recovery::v2 {
using Ns=std::uint64_t;
inline constexpr std::string_view Base="/run/inkbound-owner-v2";
inline constexpr std::size_t MaxWorkers=8,WireBytes=1280;
using Nonce=std::array<std::uint8_t,32>;
using Wire=std::array<std::uint8_t,WireBytes>;
struct SessionIdentity {
 std::array<char,33> session{};std::array<char,37> boot{};
 std::array<char,96> unit{},owner{};std::array<char,33> invocation{},ownerInvocation{};
 std::array<char,256> group{};std::array<char,128> mount{};
 std::uint64_t device{},inode{};std::uint32_t ownerPid{},mainPid{};Nonce ticket{};
 bool operator==(const SessionIdentity&)const=default;
};
template<std::size_t N>bool text(std::array<char,N>& out,std::string_view value)noexcept{out.fill(0);if(value.empty()||value.size()>=N)return false;for(char c:value)if(c<32||c>126)return false;for(std::size_t n=0;n<value.size();++n)out[n]=value[n];return true;}
bool setUnitNames(SessionIdentity&)noexcept;
bool validIdentity(const SessionIdentity&)noexcept;
struct ClockPair {Ns monotonic{},boottime{};std::uint64_t generation{};bool operator==(const ClockPair&)const=default;};
enum class WorkerKind:std::uint8_t {None=0,DurablePrefix=1,CancelableHelperPrefix=2};
// Trusted session policy, indexed by a fixed domain mapping. Never sent or
// selected by Health. maxPendingAge includes queueing and terminal reporting.
struct WorkerPolicy {WorkerKind kind{WorkerKind::None};std::uint32_t maxBacklog{};Ns maxPendingAge{};bool operator==(const WorkerPolicy&)const=default;};
struct Timing {
 Ns healthCadence{},challengeLifetime{},healthMaxAge{},grantLifetime{},drainLead{};
 Ns startupBudget{},stopGrace{},killGrace{},proofWindow{},sleepAllowance{},clockSkew{};
 std::uint32_t workers{};std::array<WorkerPolicy,MaxWorkers> workerPolicies{};
 bool operator==(const Timing&)const=default;
};
bool validTiming(const Timing&)noexcept;
// Deliberately microscopic virtual-time values. Never a production selection.
Timing syntheticTiming()noexcept;
// Only future transport code may create this evidence, using SO_PEERCRED and
// the pinned original SOCK_SEQPACKET socket. These fields are NEVER wire claims.
struct VerifiedPeer {std::uint32_t uid{},pid{};std::uint64_t connection{};bool verified{};};
struct Authority {bool reference{},identity{},manager{},power{};};
// accepted is the acceptance high-water mark; completed is the COMPLETED
// PREFIX, never an arbitrary count of out-of-order finishes. backlog is the
// entire unretired suffix, including newer completed entries behind a hole.
struct WorkerHealth {std::uint64_t accepted{},completed{};std::uint32_t backlog{};ClockPair oldest{};bool operator==(const WorkerHealth&)const=default;};
struct Health {
 ClockPair captured{},mainCompleted{};std::uint64_t mainSequence{};
 std::uint32_t workerCount{};std::array<WorkerHealth,MaxWorkers> workers{};
 bool saveError{},shutdown{},sleep{};
 bool operator==(const Health&)const=default;
};
struct HealthInput {bool coherent{};Health value{};};
struct Challenge {Nonce nonce{};ClockPair issued{},replyBy{};std::uint64_t nextGrant{};bool operator==(const Challenge&)const=default;};
struct Grant {std::uint64_t challengeSequence{},mainSequence{};Nonce nonce{};ClockPair issued{},drainAt{},expires{};bool operator==(const Grant&)const=default;};
enum class Reason:std::uint16_t {None,InvalidIdentity,InvalidTiming,InvalidGeneration,InvalidPeer,ReferenceLost,IdentityLost,ManagerLost,PowerLost,Malformed,WrongIdentity,WrongGeneration,WrongSequence,WrongNonce,FutureOrStale,ChallengePending,ChallengeExpired,StartupExpired,GrantExpired,DrainDeadline,SnapshotUnavailable,NoMainProgress,SaveError,Shutdown,Sleep,WorkerInvalid,WorkerAge,CounterOverflow,ClockDiscontinuity,Disconnected,Requested,PeerClosed};
struct ClosureProof {
 SessionIdentity identity{};std::uint64_t generation{},closureGeneration{},lastGrantSequence{};
 ClockPair closedAt{},proofUntil{};Reason reason{Reason::None};bool originalReference{},clockReliable{};
 bool operator==(const ClosureProof&)const=default;
};
enum class Kind:std::uint16_t {Challenge=1,Health=2,Grant=3,Closure=4};
struct Message {
 Kind kind{Kind::Challenge};SessionIdentity identity{};std::uint64_t generation{},sequence{};
 Challenge challenge{};Nonce responseNonce{};Health health{};Grant grant{};ClosureProof closure{};
};
// Exact fixed-length V2 framing, explicit little-endian fields, zero padding.
// This codec neither includes nor accepts a V1 Binding/Packet.
bool encode(const Message&,Wire&)noexcept;
std::optional<Message> decode(std::span<const std::uint8_t>)noexcept;
// Scheduling/binding check ONLY. Never process-emptiness or stock-start proof.
bool proofWithinBudget(const ClosureProof&,const SessionIdentity&,std::uint64_t generation,std::uint64_t lastGrant,const ClockPair&,const Timing&)noexcept;
class OwnerMachine final {
 const SessionIdentity identity_;const Timing timing_;const std::uint64_t generation_;
 const VerifiedPeer peer_;const std::uint32_t expectedUid_;
 ClockPair lastClock_{},start_{};bool closed_{},active_{},pending_{},referenceContinuous_{true},initialized_{};
 Reason reason_{Reason::None};std::uint64_t challengeSequence_{},grantSequence_{};
 Challenge challenge_{};Grant grant_{};Health lastHealth_{};bool hasHealth_{};
 std::optional<ClosureProof> proof_;
 bool observe(ClockPair,Authority)noexcept;void shut(Reason,ClockPair,bool clockReliable=true)noexcept;
public:
 OwnerMachine(SessionIdentity,std::uint64_t generation,Timing,VerifiedPeer child,std::uint32_t expectedUid,ClockPair start)noexcept;
 OwnerMachine(const OwnerMachine&)=delete;OwnerMachine& operator=(const OwnerMachine&)=delete;
 std::optional<Wire> challenge(ClockPair,Authority,const Nonce&)noexcept;
 std::optional<Wire> acceptHealth(std::span<const std::uint8_t>,VerifiedPeer,ClockPair,Authority)noexcept;
 bool admitted(ClockPair,Authority)noexcept;
 void disconnect(ClockPair)noexcept;void close(ClockPair)noexcept;
 bool closed()const noexcept{return closed_;}Reason reason()const noexcept{return reason_;}
 std::uint64_t lastGrantSequence()const noexcept{return grantSequence_;}
 const SessionIdentity& identity()const noexcept{return identity_;}
 const Grant& lastGrant()const noexcept{return grant_;}
 const std::optional<ClosureProof>& closure()const noexcept{return proof_;}
 std::optional<Wire> closurePacket()const noexcept;
};
class ChildMachine final {
 const SessionIdentity identity_;const Timing timing_;const std::uint64_t generation_;
 const VerifiedPeer peer_;const std::uint32_t expectedUid_;
 ClockPair lastClock_{},start_{};bool closed_{},active_{},pending_{},hasHealth_{};
 Reason reason_{Reason::None};std::uint64_t challengeSequence_{},grantSequence_{};
 Challenge challenge_{};Grant grant_{};Health lastHealth_{};
 bool observe(ClockPair,Authority)noexcept;void shut(Reason)noexcept;
public:
 ChildMachine(SessionIdentity,std::uint64_t generation,Timing,VerifiedPeer owner,std::uint32_t expectedUid,ClockPair start)noexcept;
 ChildMachine(const ChildMachine&)=delete;ChildMachine& operator=(const ChildMachine&)=delete;
 std::optional<Wire> respond(std::span<const std::uint8_t>,VerifiedPeer,ClockPair,Authority,HealthInput)noexcept;
 bool acceptGrant(std::span<const std::uint8_t>,VerifiedPeer,ClockPair,Authority)noexcept;
 bool acceptClosure(std::span<const std::uint8_t>,VerifiedPeer,ClockPair)noexcept;
 bool admitted(ClockPair,Authority)noexcept;
 void disconnect(ClockPair)noexcept;void close(ClockPair)noexcept;
 bool closed()const noexcept{return closed_;}Reason reason()const noexcept{return reason_;}
 const Grant& lastGrant()const noexcept{return grant_;}
 std::uint64_t lastGrantSequence()const noexcept{return grantSequence_;}
};
}
