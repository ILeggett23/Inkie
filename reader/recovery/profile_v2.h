// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "lease_fuse_v2.h"
#include <string>
#include <vector>
namespace recovery::v2::runtime {
enum class Selection { Unsupported, Synthetic, Qualified };
// An explicit external selection, not discovered or inferred from login1 or
// passing component tests. There is intentionally no production default.
struct Profile {
 Selection selection{Selection::Unsupported};
 std::string qualification,managerVersion,powerRoutes;
 Timing timing{};fuse::Policy fuse{};
 Ns readerCeiling{},ownerCeiling{},callBudget{},startBudget{},stopBudget{};
 bool operator==(const Profile&)const=default;
};
bool valid(const Profile&)noexcept;
bool allowsStock(const Profile&,const std::string& session,const std::string& stock)noexcept;
enum class Role { Owner, Reader, Fuse };
struct UnitProfile {
 std::string type,notifyAccess,restart,killMode;
 std::uint64_t runtimeUsec{},startUsec{},stopUsec{},abortUsec{},capabilities{};
 std::int32_t killSignal{},finalKillSignal{};
 bool sendSigkill{},noNewPrivileges{},delegate{};
 std::vector<std::string> bindsTo,after;
};
UnitProfile requested(const Profile&,Role,const std::string& owner,const std::string& fuse);
bool matches(const UnitProfile& requested,const UnitProfile& observed)noexcept;
}
