// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "dispatch_v2.h"
namespace recovery::v2::dispatch {
// Independent fuse evidence from the fuse's OWN manager connection: its own
// RefUnit references on the owner/reader/fuse units, exact invocation and PID
// continuity against the bootstrap identity, typed unit profiles equal to
// runtime::requested(), unchanged manager/login1 bus owners, and login1 not
// preparing for sleep. routeQualified must come from the pinned profile's
// explicit verified route; it is never inferred. Any failure is permanent.
class ManagerFuseEvidence final:public FuseEvidenceSource {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 ManagerFuseEvidence(runtime::Profile,std::string session,bool routeQualified)noexcept;
 ~ManagerFuseEvidence()override;
 bool evidence(const fuse::Binding&,const SessionIdentity&,fuse::Evidence&)noexcept override;
};
}
