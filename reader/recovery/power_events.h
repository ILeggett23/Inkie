// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <sys/types.h>
namespace recovery::power {
enum class Scope {Unverified,PrivateFixture,VerifiedPlatform};
struct RouteEvidence {
 Scope scope{Scope::Unverified};
 bool allRelevantSleepRoutes{},finitePreSleepBarrier{};
 std::string evidenceId;
 // External evidence, never inferred from login1 availability or clock samples.
 std::uint64_t delayBudgetUsec{},maximumSignalLagUsec{};
};
struct ClockReading {std::uint64_t monotonicUsec{},boottimeUsec{};};
using FixtureClock=ClockReading(*)(void*) noexcept;
struct Config {
 RouteEvidence route;
 // Only PrivateFixture may supply an address or clock. VerifiedPlatform uses
 // /run/dbus/system_bus_socket directly, not an environment override.
 std::string privateBusAddress;
 uid_t expectedServiceUid{};
 std::uint64_t startupTimeoutUsec{500000};
 FixtureClock fixtureClock{};void* fixtureClockContext{};
};
enum class State {Unsupported,Starting,FixtureReady,Ready,Preparing,Closed};
enum class Reason {None,UnverifiedRoute,InvalidConfiguration,BusUnavailable,ServiceUnavailable,UnauthenticatedService,ProtocolError,InhibitorDenied,InhibitorLost,InitiallyPreparing,StartupRace,StartupTimeout,OwnerChanged,ConnectionLost,SleepPreparing,DelayExpired,Quiesced,Resumed,ClockDiscontinuity,ExternalInvalidation,Stopped,ProcessingCapacity};
struct Snapshot {
 State state{State::Unsupported};Reason reason{Reason::UnverifiedRoute};
 std::uint64_t generation{1},deadlineBoottimeUsec{},providerDelayUsec{};
 unsigned acquisitions{},duplicateSignals{},foreignSignals{};
 bool inhibitorHeld{},platformVerified{};
 std::string uniqueOwner;
 bool admissionReady()const noexcept{return state==State::Ready&&platformVerified&&inhibitorHeld;}
};
// Independent, control-thread API. No owner, display, input, service-manager or
// sleep action is invoked. An operational bus is not platform sleep coverage.
// Snapshot uses a short mutex and MUST NOT be called from a per-sample hot path.
// One start per object/generation. Sleep/resume or any loss seals it permanently.
class Events final {
 struct Impl;std::unique_ptr<Impl> impl_;
public:
 explicit Events(Config={});~Events();Events(const Events&)=delete;Events& operator=(const Events&)=delete;
 bool start();Snapshot snapshot()const;
 // Acknowledge only after accepted work/helpers/owner have actually quiesced.
 // Old-generation, early, or expired acknowledgements cannot release authority.
 bool acknowledgeQuiesced(std::uint64_t closureGeneration)noexcept;
 void invalidate()noexcept;
 // Signals stop, releases/reaps the private worker, and returns false on timeout.
 // Destruction requires this bounded cleanup to succeed (fail-closed exit 78).
 bool finish(unsigned milliseconds=100)noexcept;
};
}
