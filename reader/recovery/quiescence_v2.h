// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "protocol_v2.h"
namespace recovery::v2::quiescence {
// Application durability evidence, distinct from closure, witness and
// restoration. Witness/restoration prove process absence and stock return;
// only a bound report whose durable() holds may claim accepted work is saved.
inline constexpr std::size_t Domains=5;
struct Report {
 std::array<char,33> session{},invocation{};Nonce ticket{};
 std::uint64_t generation{},clockGeneration{},lastGrantSequence{};
 std::array<WorkerHealth,Domains> domains{};std::array<std::uint64_t,Domains> faults{};
 std::uint32_t unsaved{},semanticPending{};
 bool inputClosed{},admissionClosed{},helpersReconciled{},saveFailed{},shellFailed{},semanticFailed{};
 bool operator==(const Report&)const=default;
};
// Every accepted prefix completed with no hole or fault, helpers reconciled,
// input and admission closed, and no save/shell/semantic failure or unsaved work.
bool durable(const Report&)noexcept;
// Bound to the exact session identity, protocol generation and the owner's
// own closure record. Clock epoch is checked separately from generation.
bool bound(const Report&,const SessionIdentity&,std::uint64_t generation,std::uint64_t clockGeneration,const ClosureProof&)noexcept;
// Diagnostic only: names the first binding field that fails ("" when bound).
const char* mismatch(const Report&,const SessionIdentity&,std::uint64_t generation,std::uint64_t clockGeneration,const ClosureProof&)noexcept;
// Fixed-length little-endian framing with its own magic; never a Message,
// bootstrap Packet or V1 Binding.
bool encode(const Report&,Wire&)noexcept;
std::optional<Report> decode(std::span<const std::uint8_t>)noexcept;
}
