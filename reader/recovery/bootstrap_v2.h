// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "control_v2.h"
#include "witness_v2.h"
namespace recovery::v2::bootstrap {
// A distinct codec on the already authenticated Channel, never V1 packets and
// never an application protocol challenge masquerading as a bootstrap permit.
enum class Kind:std::uint16_t { Hello=1,Offer,Acknowledged,Permit,WitnessOffer,Receipt,WitnessAck };
struct Packet {
 Kind kind{Kind::Hello};SessionIdentity identity{};
 std::array<char,129> profile{};std::uint64_t generation{};ClockPair started{};
 ClosureProof closure{};std::uint32_t witnessPid{};bool drained{};
 bool operator==(const Packet&)const=default;
};
bool encode(const Packet&,Wire&)noexcept;
std::optional<Packet> decode(std::span<const std::uint8_t>)noexcept;
// The child receives its binding while inert, acknowledges exactly that offer,
// and consumes one permit only after stock has actually stopped. No grants or
// input admission come from this handshake; ChildControl must grant separately.
class Child final {
 Packet offer_{};VerifiedPeer owner_{};bool offered_{},permitted_{},sealed_{};
public:
 bool offer(const Packet&,VerifiedPeer owner,const SessionIdentity& locallyVerified,
            std::string_view selectedProfile,ClockPair now,const Timing&)noexcept;
 std::optional<Packet> acknowledgement()const noexcept;
 bool permit(const Packet&,VerifiedPeer owner,ClockPair now,const Timing&)noexcept;
 bool permitted()const noexcept{return permitted_&&!sealed_;}
 void close()noexcept{sealed_=true;permitted_=false;}
};
}
