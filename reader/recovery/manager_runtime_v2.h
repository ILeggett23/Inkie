// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "profile_v2.h"
#include "witness_v2.h"
#include "manager_history_v2.h"
#include <memory>
namespace recovery::v2::runtime {
enum class Unit { Owner, Stock, Reader, Fuse };
// Owned by the one restoration authority. No reconnection, adoption of an
// existing reader/fuse, wildcard stop, or destructor restoration is available.
class Manager final {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 Manager(Profile,std::string session,std::string stock);
 ~Manager();Manager(const Manager&)=delete;Manager& operator=(const Manager&)=delete;
 bool alive()noexcept;void disconnect()noexcept;
 const std::string& name(Unit)const;
 std::uint64_t connection()const noexcept;
 History history()const; // actual private mutation/reference history, read-only
 void retainOwnerAndStock();
 void create(Unit,const std::vector<std::string>& executable,const std::vector<std::string>& witness={});
 recovery::State state(Unit);
 UnitProfile profile(Unit);
 bool verify();
 SessionIdentity bind();
 fuse::Binding fuseBinding();
 Authority authority(const SessionIdentity&,bool externallyVerifiedPower);
 void stopStock(); // only after exact live binding and typed profile proof
 void stopReader();
 void stopFuse(); // D2: after restoration
 // Two reads on this original connection plus exact authenticated receipt,
 // absence and proof budget. One start request total, with no retry.
 bool restore(const SessionIdentity&,const ClosureProof&,const witness::Receipt&,
              const witness::Authentication&,ClockPair);
 bool restorationRequested()const noexcept;
 bool restoreUserEnded(const SessionIdentity&)noexcept; // D2: after the bound reader is terminal
};
}
