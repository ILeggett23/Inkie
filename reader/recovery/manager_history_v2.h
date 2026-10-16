// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "contract.h"
namespace recovery::v2::runtime {
// Returned by the existing Manager from its own private mutation/reference
// history. Completion verification reads this directly, never from a packet.
struct UnitHistory {
 bool creationAttempted{},originalReference{};
 recovery::State initial{};std::string mount;
 std::uint64_t device{},inode{};
};
struct History {
 std::uint64_t connection{};bool continuous{},stockStopAttempted{},stockStartAttempted{},stockStartAccepted{};
 UnitHistory owner,stock,reader,fuse;
};
}
