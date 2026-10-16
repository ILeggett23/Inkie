// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "protocol_v2.h"
#include "contract.h"
namespace recovery::v2::witness {
// A typed V2 receipt, not a V1 Packet/Binding cast. Transport must authenticate
// the exact managed ExecStopPost ControlPID before accepting this receipt.
struct Receipt {ClosureProof closure{};std::uint32_t witnessPid{};bool drained{};};
struct Authentication {
 std::uint32_t peerUid{},peerPid{},controlBefore{},controlAfter{};
 std::uint64_t originalConnection{};
 bool kernelCredentialsVerified{},sameOriginalConnection{};
 std::string invocationBefore,invocationAfter;
};
bool authenticated(const SessionIdentity&,const Receipt&,const Authentication&)noexcept;
// Reuses only the original bounded no-symlink subtree scanner, never V1 names,
// deadline interpretation or receipt validation. No manager mutation occurs.
bool membership(const SessionIdentity&,pid_t self)noexcept;
bool absent(const SessionIdentity&)noexcept;
// The owner supplies two actual retained terminal reads and original reference
// continuity. A closure scheduling proof alone never establishes process drain.
bool complete(const SessionIdentity&,const Timing&,const ClosureProof& ownerClosure,
              const Receipt&,const Authentication&,const recovery::State& before,
              const recovery::State& after,ClockPair observed,
              bool referenceContinuous,bool admissionClosed,bool groupAbsent)noexcept;
}
