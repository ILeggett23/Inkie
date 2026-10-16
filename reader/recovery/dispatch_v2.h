// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "launcher_v2.h"
#include "owner_runtime_v2.h"
#include "quiescence_v2.h"
#include "power_events.h"
#include <memory>
#include <optional>
#include <string>
#include <vector>
namespace recovery::v2::dispatch {
// Production composition of the qualified V2 pieces, mirroring the inert
// takeover-release sequence: shared legacy reservation before any manager
// mutation, typed one-use launcher COMMIT on the shipping control.sock, then
// reader/fuse/witness bootstrap on runtime.sock in the same private directory,
// verified by kernel peer credentials against the original Manager's records.
inline constexpr const char* RuntimeSocket="runtime.sock";
// Explicit, externally established facts. Production sources return false
// until the platform route and manager gates are qualified; nothing here is
// inferred from bus availability, clocks or passing component tests.
struct PowerSource {
 virtual bool verified()noexcept=0;
 // Called once by the owner after closure. A pending sleep preparation may be
 // acknowledged only when the reader's final report was bound AND durable (or no
 // reader was ever created); witness/restoration alone never qualifies.
 virtual void applicationQuiesced(bool boundDurable)noexcept{(void)boundDurable;}
 virtual ~PowerSource()=default;
};
// Attests the given original fuse binding and session, or refuses.
struct FuseEvidenceSource {virtual bool evidence(const fuse::Binding&,const SessionIdentity&,fuse::Evidence&)noexcept=0;virtual ~FuseEvidenceSource()=default;};
struct OwnerPlan {
 runtime::Profile roles;   // Qualified: reservation, launcher and listener adapters.
 runtime::Profile manager; // Identical in production; Synthetic only with its test stock unit.
 std::string session,stock;
 std::string powerKey; // D2: /dev/input/eventN of the power key, watched by the owner itself (non-grabbing)
 std::vector<std::string> reader,fuse,witness;
 std::uint32_t launcherPid{};std::uint64_t generation{};
 // Paired-clock epoch of the launcher exchange, chosen by the frontend.
 // Independent of protocol generation; no equality is imposed.
 std::uint64_t launcherEpoch{};
};
enum class Outcome {Refused,Aborted,Restored,Unresolved};
struct OwnerResult {
 Outcome outcome{Outcome::Refused};Reason closure{Reason::None};
 bool reserved{},committed{},takeover{},witnessed{},restored{},released{};
 // Diagnostic only: first failing report-binding field and the two grant sequences.
 std::string reportDetail;
 // Application durability is separate from restoration: only a received,
 // bound and durable quiescence report sets reportDurable.
 bool reportReceived{},reportBound{},reportDurable{};
 std::string stage;
};
bool consistent(const OwnerPlan&)noexcept;
// Stock start-limit guard. The target's xochitl has StartLimitBurst=4 per 600 s and
// OnFailure=emergency.target, and every restoration is a start. A session is allowed
// only if the current stock invocation has been continuously active for at least the
// unit's own live StartLimitIntervalUSec, so V2 adds at most one start per window.
// D2 exception: when the running stock invocation is itself one that an Inkbound restoration started (recorded below),
// no automatic restart happened since (NRestarts unchanged) and every recorded start within the window plus this
// session's restoration still leaves one start of the burst spare, a session may begin inside the window.
struct StockHeadroom {bool ok{};std::string invocation,reason;std::uint64_t intervalUsec{},activeForUsec{};unsigned burst{},recentOwnStarts{},journalStarts{};std::uint32_t nrestarts{};};
StockHeadroom stockStartHeadroom(const std::string& stock)noexcept;
// Root-only tmpfs record of stock starts made by restorations (cleared on reboot): "<monotonic_usec> <nrestarts> <invocation>".
inline constexpr const char* StockStartLedger="/run/inkbound-stock-starts";
struct OwnStockStart {std::uint64_t usec{};std::uint32_t nrestarts{};std::string invocation;};
// Any doubt about the record (missing, other owner, accessible to others, malformed, too large) yields no entries.
std::vector<OwnStockStart> readStockStarts(const char* path=StockStartLedger)noexcept;
bool recordStockStart(std::uint64_t monotonicUsec,std::uint32_t nrestarts,const std::string& invocation,const char* path=StockStartLedger)noexcept;
// The in-window rule: the running invocation is the latest recorded own start, NRestarts is unchanged since, and the
// recorded starts within the interval plus one more stay below the burst (one start kept spare).
bool ownStartsAllow(const std::vector<OwnStockStart>&,const std::string& invocation,std::uint32_t nrestarts,std::uint64_t nowUsec,std::uint64_t intervalUsec,unsigned burst,unsigned* recent=nullptr)noexcept;
// Journal rule (any starter, e.g. XOVI's `systemctl restart`): PID 1's "Started"/"Failed to start" records for the stock
// unit in this boot, one per job whatever its JOB_TYPE (start, restart or absent; a record without JOB_ID counts alone).
// A session may begin inside the window when the running invocation's own "Started" record is present (the journal saw
// it) and the recorded starts within the interval plus this session's restoration still leave one start spare.
struct JournalStart {std::uint64_t usec{};std::string job,invocation;bool started{};};
// One record from its "KEY=VALUE" fields (MESSAGE_ID, UNIT, _PID, JOB_ID, JOB_TYPE, INVOCATION_ID); false if not a stock start.
bool journalRecord(const std::vector<std::string>& fields,std::uint64_t monotonicUsec,const std::string& unit,JournalStart& out)noexcept;
bool journalStartsAllow(const std::vector<JournalStart>&,const std::string& invocation,std::uint64_t nowUsec,std::uint64_t intervalUsec,unsigned burst,unsigned* recent=nullptr)noexcept;
// This boot's records newer than nowUsec-intervalUsec (newest first); false (refuse) on any journal error or overflow.
// directory/boot replace the system journal and the current boot (host probes of a copied device journal only).
bool readJournalStarts(const std::string& unit,std::uint64_t nowUsec,std::uint64_t intervalUsec,std::vector<JournalStart>& out,const char* directory=nullptr,const char* boot=nullptr)noexcept;
OwnerResult runOwner(const OwnerPlan&,PowerSource&)noexcept;
// Frontend: one explicit COMMIT after an authentic bounded READY.
enum class Launch {Refused,Committed,Failed};
Launch launch(const runtime::Profile& roles,const std::string& session,std::uint32_t ownerPid,const std::string& ownerInvocation,std::uint64_t generation,std::uint64_t launcherEpoch)noexcept;
// Reader side: returns the permitted offer and the original socket, whose
// SO_PASSCRED is already enabled for ChildControl. No input or grant results.
struct Permit {bootstrap::Packet offer{};int fd{-1};};
std::optional<Permit> readerBootstrap(const runtime::Profile&,const std::string& session)noexcept;
// One authenticated report connection from the reader's original PID.
bool sendReport(const std::string& session,const quiescence::Report&)noexcept;
// Fuse unit main: bounded READY, bootstrap, then the shipping FuseActor.
int runFuse(const runtime::Profile&,const std::string& session,FuseEvidenceSource&)noexcept;
// ExecStopPost witness: the shipping WitnessActor over runtime.sock.
// The owner appends session, protocol generation and its runtime clock epoch.
int runWitness(const runtime::Profile&,const std::string& session,std::uint64_t generation,std::uint64_t epoch)noexcept;
ClockPair sample(std::uint64_t epoch)noexcept;
// Frontend only: asks the service manager to start the owner's transient unit
// with exactly runtime::requested(profile, Role::Owner) properties, which the
// owner's Manager::verify() then checks. Returns the original PID/invocation.
struct Started {std::uint32_t pid{};std::string invocation;};
std::optional<Started> startOwner(const runtime::Profile& manager,const std::string& session,const std::vector<std::string>& argv)noexcept;
// No platform sleep route or independent fuse evidence source is qualified.
// These production sources therefore always refuse; V2 cannot admit input.
struct UnqualifiedPower final:PowerSource {bool verified()noexcept override{return false;}};
// logind delay-inhibitor backend. Admits only when the explicitly verified
// platform route is Ready with the inhibitor held; any loss is permanent.
class EventsPower final:public PowerSource {
 std::unique_ptr<power::Events> events_;bool started_{};
public:
 explicit EventsPower(power::Config)noexcept;
 bool verified()noexcept override;
 void applicationQuiesced(bool boundDurable)noexcept override;
};
struct UnqualifiedFuseEvidence final:FuseEvidenceSource {bool evidence(const fuse::Binding&,const SessionIdentity&,fuse::Evidence&)noexcept override{return false;}};
}
