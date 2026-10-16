// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "protocol_v2.h"
#include "progress_v2.h"
namespace recovery::v2::control {
enum class Fault {None,Channel,Transport,Protocol,Progress,Deadline,Send};
enum class Read {Empty,Packet,Failed};
using ClockSource=ClockPair(*)(void*)noexcept;
// Owns only the original connected socket. No connect/reconnect operation.
// Construction must happen before the sender starts writing (SO_PASSCRED).
class Channel final {
 int fd_{-1};VerifiedPeer peer_{};
public:
 Channel(int ownedFd,std::uint64_t connection,std::uint32_t uid,std::uint32_t pid)noexcept;
 ~Channel();Channel(const Channel&)=delete;Channel& operator=(const Channel&)=delete;
 bool valid()const noexcept{return fd_>=0&&peer_.verified;}
 const VerifiedPeer& peer()const noexcept{return peer_;}
 Read receive(Wire&)noexcept;
 bool send(const Wire&)noexcept;
 void seal()noexcept;
};
// Both adapters are confined to their control thread. Caller supplies paired
// clocks, externally verified authority and real nonce entropy. No defaults,
// manager calls, restoration, input, worker mutation or foreground publication.
class OwnerControl final {
 Channel child_,fuse_;OwnerMachine machine_;Timing timing_;
 bool pending_{},issued_{},sealed_{};ClockPair issuedAt_{};Fault fault_{Fault::None};
 void seal(ClockPair,Fault)noexcept;
 bool pollAt(ClockPair,Authority,const Nonce*,ClockSource,void*)noexcept;
public:
 OwnerControl(SessionIdentity,std::uint64_t generation,Timing,ClockPair start,
              int childFd,std::uint64_t childConnection,std::uint32_t childUid,
              int fuseFd,std::uint64_t fuseConnection,std::uint32_t fuseUid,std::uint32_t fusePid)noexcept;
 // At most one child packet and one fuse peer-status packet per call. Pass
 // entropy when a new challenge may be due. Missing entropy never renews.
 bool poll(ClockPair,Authority,const Nonce* entropy=nullptr)noexcept;
 // Production polling samples after receive, retaining exact freshness checks.
 bool pollObserved(Authority,const Nonce*,ClockSource,void*)noexcept;
 bool admitted(ClockPair,Authority)noexcept;
 void close(ClockPair)noexcept;
 bool closed()const noexcept{return sealed_||machine_.closed();}
 Fault fault()const noexcept{return fault_;}
 Reason reason()const noexcept{return machine_.reason();}
 const Grant& grant()const noexcept{return machine_.lastGrant();}
 std::uint64_t grantSequence()const noexcept{return machine_.lastGrantSequence();}
 const std::optional<ClosureProof>& closure()const noexcept{return machine_.closure();}
};
class ChildControl final {
 Channel owner_;ChildMachine machine_;progress::MainProgressChannel& progress_;
 const SessionIdentity identity_;const std::uint64_t generation_;const Timing timing_;
 Wire pendingWire_{};Challenge pendingChallenge_{};Nonce lastNonce_{};
 std::uint64_t challengeSequence_{},respondedMain_{};bool waiting_{},awaitingGrant_{},sealed_{};
 Fault fault_{Fault::None};
 void seal(ClockPair,Fault)noexcept;
 bool pollAt(ClockPair,Authority,ClockSource,void*)noexcept;
public:
 ChildControl(SessionIdentity,std::uint64_t generation,Timing,ClockPair start,
              int ownerFd,std::uint64_t ownerConnection,std::uint32_t ownerUid,
              progress::MainProgressChannel&)noexcept;
 // Reads, never writes, the foreground channel. An old snapshot waits for a
 // new publication within the received challenge's original fixed deadline.
 bool poll(ClockPair,Authority)noexcept;
 // Production polling samples after receive AND the foreground snapshot.
 bool pollObserved(Authority,ClockSource,void*)noexcept;
 bool admitted(ClockPair,Authority)noexcept;
 void close(ClockPair)noexcept;
 bool closed()const noexcept{return sealed_||machine_.closed();}
 Fault fault()const noexcept{return fault_;}
 Reason reason()const noexcept{return machine_.reason();}
 const Grant& grant()const noexcept{return machine_.lastGrant();}
 std::uint64_t grantSequence()const noexcept{return machine_.lastGrantSequence();}
};
}
