// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "bootstrap_v2.h"
#include "manager_runtime_v2.h"
namespace recovery::v2::runtime {
// Lives only inside the original restoration owner. Bootstrap must finish on
// these exact sockets before ownership transfers here. Power evidence is an
// explicit observation from the selected backend, never inferred from activity.
class Owner final {
 Manager& manager_;const SessionIdentity identity_;const Profile profile_;
 const std::uint64_t generation_;const ClockPair started_;
 control::OwnerControl control_;
 std::unique_ptr<control::Channel> witness_;
 witness::Receipt receipt_{};witness::Authentication authentication_{};
 bool receiptReady_{},witnessAttempted_{},terminationRequested_{};
public:
 Owner(Manager&,SessionIdentity,Profile,std::uint64_t generation,ClockPair started,
       int childFd,std::uint64_t childConnection,int fuseFd,std::uint64_t fuseConnection,
       std::uint32_t fusePid);
 bool poll(ClockPair,bool verifiedPower,const Nonce* entropy=nullptr)noexcept;
 bool admitted(ClockPair,bool verifiedPower)noexcept;
 void close(ClockPair)noexcept;
 bool closed()const noexcept{return control_.closed();}
 // The owner accepts the witness socket; peer/control-PID authentication and
 // before/after invocation reads occur here, on its original manager reference.
 bool beginWitness(int ownedFd,ClockPair)noexcept;
 bool pollWitness(ClockPair)noexcept;
 bool restore(ClockPair)noexcept;
 const std::optional<ClosureProof>& closure()const noexcept{return control_.closure();}
};
}
