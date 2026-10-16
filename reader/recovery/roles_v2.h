// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "bootstrap_v2.h"
#include "profile_v2.h"
#include <memory>
#include <span>
namespace recovery::v2::roles {
// These shipping adapters accept only an explicitly externally-qualified
// profile. They do not select a profile from environment, names or inventory.
bool supported(const runtime::Profile&)noexcept;
struct Peer {std::uint32_t pid{};std::uint64_t connection{};};
class Socket final {
 int fd_{-1};VerifiedPeer peer_{};
 friend class Listener;friend class Connector;
 Socket(int,VerifiedPeer)noexcept;
public:
 ~Socket();Socket(const Socket&)=delete;Socket& operator=(const Socket&)=delete;
 // Adopts one existing socket after kernel authentication, never connects or
 // reacquires a disconnected endpoint. Owned fd closes on any refusal.
 static std::unique_ptr<Socket> adopt(const runtime::Profile&,int ownedFd,Peer)noexcept;
 const VerifiedPeer& peer()const noexcept{return peer_;}
 bool valid()const noexcept{return fd_>=0&&peer_.verified;}
 int release()noexcept;
};
enum class Accept {Empty,Accepted,Rejected,Failed};
struct Accepted {Accept status{Accept::Failed};std::unique_ptr<Socket> socket;};
class Listener final {
 int fd_{-1},root_{-1},directory_{-1};std::string session_;runtime::Profile profile_;
 bool created_{},bound_{};std::uint64_t socketDevice_{},socketInode_{};
 std::array<Peer,3> consumed_{};std::size_t consumedCount_{};
public:
 // Fixed /run/inkbound-owner-v2/<session>/control.sock. No existing session,
 // socket, symlink, non-root directory or permissive directory is adopted.
 Listener(runtime::Profile,std::string session)noexcept;
 ~Listener();Listener(const Listener&)=delete;Listener& operator=(const Listener&)=delete;
 bool valid()const noexcept{return fd_>=0;}
 // Exactly one nonblocking accept. Expected PIDs are from the caller's retained
 // actual manager state; no packet field contributes identity evidence.
 // Accepted PID/connection pairs are permanently consumed (maximum three).
 Accepted poll(std::span<const Peer> expected)noexcept;
};
class Connector final {
 runtime::Profile profile_;std::string session_;Peer owner_;bool attempted_{};
public:
 Connector(runtime::Profile,std::string session,Peer originalOwner)noexcept;
 // One attempt only. Kernel SO_PEERCRED must match the original managed owner.
 std::unique_ptr<Socket> connect()noexcept;
};
using Ready=int(*)(void*)noexcept;
int notifyReady(void*)noexcept;
// READY only ends finite inert bootstrap startup ordering; it grants no input
// and cannot extend RuntimeMax. Caller must keep the original finite fuse
// unit alive while waiting for its fully bound owner offer.
class FuseReady final {
 bool supported_{},attempted_{};
public:
 explicit FuseReady(const runtime::Profile&)noexcept;
 bool announce(Ready,void*)noexcept;
};
enum class FuseStep {Running,Holding,Exit};
class FuseActor final {
 std::unique_ptr<fuse::OwnerChannel> owner_;
 std::unique_ptr<fuse::Lease> lease_;
 ClockPair last_{},cutoff_{};Ns skew_{};bool holding_{},exit_{};
 FuseStep hold(ClockPair)noexcept;
 FuseStep pollAt(ClockPair,const fuse::Evidence&,fuse::ClockSource,void*)noexcept;
public:
 FuseActor(const runtime::Profile&,SessionIdentity,fuse::Binding,std::uint64_t generation,
           ClockPair started,std::unique_ptr<Socket>,fuse::Notify,void*)noexcept;
 // Once closed, accepts no more packets or notifications and retains only the
 // already-issued finite cutoff. EOF/normal closure does not exit immediately.
 // Exit on invalid clocks or the original cutoff triggers the existing BindsTo.
 FuseStep poll(ClockPair,const fuse::Evidence&)noexcept;
 FuseStep pollObserved(const fuse::Evidence&,fuse::ClockSource,void*)noexcept;
 bool valid()const noexcept{return lease_&&!exit_;}
 fuse::Fault fault()const noexcept;
 ClockPair cutoff()const noexcept{return cutoff_;}
};
struct WitnessContext {
 std::array<char,33> session{},invocation{};
 std::uint64_t generation{};
 // Local witness-process timeout origin, NOT the original session start.
 ClockPair localStarted{};
};
enum class WitnessStep {Waiting,Complete,Failed};
class WitnessActor final {
 runtime::Profile profile_;WitnessContext context_;
 std::unique_ptr<control::Channel> owner_;
 bootstrap::Packet receipt_{};ClockPair last_{},deadline_{};
 bool offered_{},finished_{},failed_{};
 WitnessStep fail()noexcept;
public:
 WitnessActor(runtime::Profile,WitnessContext,std::unique_ptr<Socket>)noexcept;
 // Calls actual bounded membership() itself. Sends a receipt only for the
 // actual current process, then stays alive for the exact original owner's ACK.
 // No callback can bypass membership; there is no restoration operation here.
 WitnessStep poll(ClockPair)noexcept;
};
}
