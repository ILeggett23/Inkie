// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "protocol_v2.h"
#include <array>
#include <span>
namespace recovery::v2::fuse {
// All values are externally qualified nanoseconds, never production defaults.
// dispatchMargin includes notification delivery, PID1 dispatch and dependency
// SIGKILL propagation. No stop/abort extension or restoration operation exists.
struct Policy {
 Ns bootstrapLifetime{},leaseLifetime{},dispatchMargin{};
 bool operator==(const Policy&)const=default;
};
struct Binding {
 std::array<char,96> unit{};std::array<char,33> invocation{};
 std::uint32_t mainPid{};std::uint64_t originalReference{};
 bool operator==(const Binding&)const=default;
};
// Supplied ONLY by the original manager connection's typed/property checks.
// Notification acceptance alone cannot establish these platform facts.
struct Evidence {
 Binding fuse{};SessionIdentity session{};
 bool originalOwnerReference{},originalReaderReference{},originalFuseReference{};
 bool originalManager{},verifiedProfile{},completePowerBarrier{};
};
enum class Fault {None,Configuration,Identity,Peer,Authority,Clock,StartupExpired,
 LeaseExpired,GrantExpired,Malformed,Sequence,Progress,Notification,Closure,Disconnected};
using Notify=int(*)(void*,std::uint64_t extensionUsec) noexcept;
using ClockSource=ClockPair(*)(void*)noexcept;
bool validPolicy(const Policy&,const Timing&)noexcept;
bool setUnitName(Binding&,const SessionIdentity&)noexcept;
// Thread-confined termination-only actor. Never grants reader admission, reads
// worker health, starts stock, proves emptiness, or reacquires references.
class Lease final {
 const SessionIdentity identity_;const Binding binding_;const Timing timing_;
 const Policy policy_;const std::uint64_t generation_;const VerifiedPeer owner_;
 const std::uint32_t uid_;Notify notify_;void* context_;
 ClockPair last_{},startupUntil_{},outstandingUntil_{},leaseUntil_{},drainAt_{},closedAt_{},killBy_{};
 Nonce nonce_{};std::uint64_t sequence_{},mainSequence_{},challengeSequence_{};
 bool closed_{},active_{};Fault fault_{Fault::None};
 void shut(Fault,ClockPair)noexcept;
 bool observe(ClockPair,const Evidence&)noexcept;
public:
 Lease(SessionIdentity,Binding,std::uint64_t generation,Timing,Policy,VerifiedPeer originalOwner,
       std::uint32_t expectedUid,ClockPair start,Notify,void*)noexcept;
 Lease(const Lease&)=delete;Lease& operator=(const Lease&)=delete;
 // Accept exactly one renewal per authenticated OwnerMachine-issued Grant.
 // The owner must forward the identical packet it issued after fresh real-loop
 // health. No health/observer packet is a renewal and no catch-up is allowed.
 bool accept(std::span<const std::uint8_t>,VerifiedPeer,ClockPair,const Evidence&)noexcept;
 bool tick(ClockPair,const Evidence&)noexcept; // Never sends a notification.
 void disconnect(ClockPair)noexcept;
 bool closed()const noexcept{return closed_;}Fault fault()const noexcept{return fault_;}
 std::uint64_t lastSequence()const noexcept{return sequence_;}
 const ClockPair& outstandingUntil()const noexcept{return outstandingUntil_;}
 const ClockPair& closedAt()const noexcept{return closedAt_;}
 const ClockPair& killBy()const noexcept{return killBy_;}
};
// Owns an already-created original SOCK_SEQPACKET fd. SO_PEERCRED, socket type
// and nonblocking mode are verified before use. Each packet also needs kernel
// SCM_CREDENTIALS from the original owner PID; inherited-fd writers are denied. No path connect/reconnect API.
class OwnerChannel final {
 int fd_{-1};VerifiedPeer peer_{};
 bool receiveAt(Lease&,ClockPair,const Evidence&,ClockSource,void*)noexcept;
public:
 OwnerChannel(int ownedFd,std::uint64_t originalConnection,std::uint32_t uid,std::uint32_t pid)noexcept;
 ~OwnerChannel();OwnerChannel(const OwnerChannel&)=delete;OwnerChannel& operator=(const OwnerChannel&)=delete;
 bool valid()const noexcept{return fd_>=0&&peer_.verified;}
 const VerifiedPeer& peer()const noexcept{return peer_;}
 // At most one packet per call; EAGAIN only ticks the lease, EOF/malformed data
 // seals it. The surrounding control loop owns its fixed scheduling bound.
 bool receive(Lease&,ClockPair,const Evidence&)noexcept;
 bool receiveObserved(Lease&,const Evidence&,ClockSource,void*)noexcept;
};
// Called only by the fuse's main process after its actual NotifyAccess=main
// profile was verified. Uses the existing process's systemd notify socket.
int notifyRuntime(void*,std::uint64_t extensionUsec)noexcept;
}
