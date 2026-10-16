// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "roles_v2.h"
#include "manager_runtime_v2.h"
namespace recovery::v2::launcher {
inline constexpr const char* SharedBase="/run/inkbound-owner3";
struct ReservationIdentity {
 std::array<char,33> session{};Nonce ticket{};std::uint32_t ownerPid{};
 std::uint64_t baseDevice{},baseInode{},device{},inode{};
 bool operator==(const ReservationIdentity&)const=default;
};
class Completion;
class OwnerCommit;
class Reservation final {
 int base_{-1},active_{-1};ReservationIdentity identity_{};bool acquired_{},released_{},commitBound_{};
 runtime::Profile profile_;
 friend class OwnerCommit;
public:
 // The actual managed owner creates the exact legacy exclusion before READY
 // or child/fuse/stock mutation. Existing state is never adopted or deleted.
 Reservation(runtime::Profile,std::string session)noexcept;
 ~Reservation();Reservation(const Reservation&)=delete;Reservation& operator=(const Reservation&)=delete;
 bool held()const noexcept;const ReservationIdentity& identity()const noexcept{return identity_;}
 const runtime::Profile& profile()const noexcept{return profile_;}
 // Explicit, proof-only, captured-inode removal. Destruction only closes FDs.
 bool release(runtime::Manager&,Completion&&)noexcept;
};
enum class Kind:std::uint16_t {Ready=1,Commit,Acknowledged};
struct Frame {
 Kind kind{Kind::Ready};std::array<char,33> session{},ownerInvocation{};
 Nonce ticket{};std::uint32_t ownerPid{},launcherPid{};
 // Protocol generation is independent of ClockPair::generation. Both clocks
 // share one epoch, which receivers check against their own original clock.
 std::uint64_t generation{};
 ClockPair issued{},until{};bool operator==(const Frame&)const=default;
};
bool encode(const Frame&,Wire&)noexcept;
std::optional<Frame> decode(std::span<const std::uint8_t>)noexcept;
enum class OwnerState {Waiting,Committed,Rejected};
class OwnerCommit final {
 Reservation& reservation_;runtime::Manager& manager_;runtime::Profile profile_;
 std::unique_ptr<control::Channel> launcher_;
 int originalSocket_{-1},launcherPidfd_{-1};Frame ready_{};ClockPair last_{};
 std::uint64_t connection_{};OwnerState state_{OwnerState::Rejected};bool sent_{},sealed_{};
 bool liveLauncher()noexcept;void disconnect()noexcept;
public:
 OwnerCommit(Reservation&,runtime::Manager&,runtime::Profile,std::uint64_t generation,
             ClockPair start,std::unique_ptr<roles::Socket> originalLauncher)noexcept;
 ~OwnerCommit();OwnerCommit(const OwnerCommit&)=delete;OwnerCommit& operator=(const OwnerCommit&)=delete;
 // Receives at most one frame. COMMIT linearizes at a final kernel liveness
 // observation of the original pidfd/socket; no queued postmortem COMMIT.
 // After that point frontend loss cannot revoke restoration responsibility.
 OwnerState poll(ClockPair)noexcept;
 bool committed()const noexcept{return state_==OwnerState::Committed;}
 bool sealed()const noexcept{return sealed_;}
 void seal()noexcept;
 const ReservationIdentity& reservationIdentity()const noexcept{return reservation_.identity();}
 std::uint64_t connection()const noexcept{return connection_;}
};
enum class ClientState {Waiting,Ready,AwaitingAcknowledgement,Complete,Failed};
class Client final {
 runtime::Profile profile_;std::array<char,33> session_{},ownerInvocation_{};
 std::uint64_t generation_{};std::unique_ptr<control::Channel> owner_;
 Frame ready_{};ClockPair last_{},bootstrapDeadline_{};ClientState state_{ClientState::Failed};
 ClientState fail()noexcept;
 ClientState pollAt(ClockPair,control::ClockSource,void*)noexcept;
public:
 Client(runtime::Profile,std::string session,std::string ownerInvocation,std::uint64_t generation,
        ClockPair start,std::unique_ptr<roles::Socket> originalOwner)noexcept;
 ClientState poll(ClockPair)noexcept;
 ClientState pollObserved(control::ClockSource,void*)noexcept;
 bool commit(ClockPair)noexcept; // explicit one-use action, never in destructor
 ClientState state()const noexcept{return state_;}
};
class Completion final {
 ReservationIdentity reservation_{};std::uint64_t connection_{};bool spent_{};
 Completion(ReservationIdentity,std::uint64_t)noexcept;
 friend std::optional<Completion> verifyCompletion(runtime::Manager&,const Reservation&,const OwnerCommit&)noexcept;
 friend class Reservation;
public:
 Completion()=delete;Completion(const Completion&)=delete;Completion& operator=(const Completion&)=delete;
 Completion(Completion&&)noexcept;Completion& operator=(Completion&&)=delete;
};
// Reads the existing Manager's actual private history and repeated retained
// states. No caller-supplied no-mutation boolean can mint this proof.
std::optional<Completion> verifyCompletion(runtime::Manager&,const Reservation&,const OwnerCommit&)noexcept;
}
