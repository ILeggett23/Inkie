// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "profile_v2.h"
#include "power_events.h"
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <sys/types.h>
namespace recovery::v2::runtime {
using Digest=std::array<std::uint8_t,32>;
Digest sha256(std::span<const std::uint8_t>)noexcept;
enum class LoadFailure {None,Unpinned,Open,NotRegular,Owner,Mode,Links,Size,Read,Digest,Format,Selection,Inferred,Invalid};
// route is the explicit externally verified platform sleep-route evidence; it is
// never inferred and exists only together with a qualified profile.
// D1 sustained-writing quotas (fixed windows), measured and qualified with the profile.
struct SustainedQuota {std::uint64_t windowNs{};std::uint32_t contacts{},samples{};bool operator==(const SustainedQuota&)const=default;};
// Reader-side session power: input device NAMES (numbering is not stable) and idle timeout.
struct SessionPowerConfig {std::string powerKey,cover;std::uint64_t idleNs{};bool operator==(const SessionPowerConfig&)const=default;};
struct Loaded {std::optional<Profile> profile;std::optional<power::RouteEvidence> route;std::optional<SustainedQuota> sustained;std::optional<SessionPowerConfig> sessionPower;LoadFailure failure{LoadFailure::Unpinned};};
// Explicit external selection only. An all-zero pin, a missing file or any
// other refusal yields no profile; there is no default and nothing is inferred
// from inventory, login1 or passing tests. Production passes expectedOwner 0.
Loaded loadQualified(const char* path,const Digest& pinned,uid_t expectedOwner)noexcept;
// Exact line-ordered text form. Accepts only Selection::Qualified, rejects any
// synthetic qualification or power route, and requires runtime::valid().
std::optional<Profile> parseQualified(std::string_view)noexcept;
// The same text's mandatory trailing power-route section (VerifiedPlatform only).
std::optional<power::RouteEvidence> parseRoute(std::string_view)noexcept;
// The mandatory trailing sustained-quota section.
std::optional<SustainedQuota> parseSustained(std::string_view)noexcept;
std::optional<SessionPowerConfig> parseSessionPower(std::string_view)noexcept;
}
