#pragma once
#include <cstdint>
namespace recovery::limits {
inline constexpr std::uint64_t Second=1000000000ULL;
#if defined(OWNER_VM_TEST) && defined(OWNER_VM_SHORT_DEADLINE)
// Explicit inert-VM accelerated matrix only; never enabled in target artifacts.
inline constexpr std::uint64_t Payload=10*Second,Child=12*Second,Owner=30*Second,ProofCap=25*Second;
#else
inline constexpr std::uint64_t Payload=300*Second,Child=302*Second,Owner=320*Second,ProofCap=315*Second;
#endif
inline constexpr bool validDeadline(std::uint64_t now,std::uint64_t end){return now&&end>now&&end-now<=Payload;}
}
