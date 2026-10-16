// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include "progress_v2.h"
#include <time.h>
namespace reader::lifecycle {
// The foreground owns the ledger and outlives all attached workers. Accept and
// snapshot stay on that thread; workers can only complete their captured token.
// An empty binding preserves the retained V1 path. V2 must bind every domain.
struct Tracking {
 recovery::v2::progress::WorkLedger* ledger{};
 // Clock epoch from the paired observation; independent of protocol generation.
 std::uint64_t clockGeneration{};
 recovery::v2::WorkerKind kind{recovery::v2::WorkerKind::DurablePrefix};
 bool supports(recovery::v2::WorkerKind expected)const noexcept{return !ledger?clockGeneration==0:clockGeneration&&kind==expected;}
 bool accept(recovery::v2::progress::WorkToken& token)const noexcept {
  token={};if(!ledger)return clockGeneration==0;
  if(kind!=recovery::v2::WorkerKind::DurablePrefix&&kind!=recovery::v2::WorkerKind::CancelableHelperPrefix)return false;
  if(ledger->snapshot().fault!=recovery::v2::progress::Fault::None)return false;
  timespec m{},b{};recovery::v2::ClockPair now{};
  if(clockGeneration&&!clock_gettime(CLOCK_MONOTONIC,&m)&&!clock_gettime(CLOCK_BOOTTIME,&b)&&m.tv_sec>=0&&b.tv_sec>=0)
   now={std::uint64_t(m.tv_sec)*1000000000+std::uint64_t(m.tv_nsec),std::uint64_t(b.tv_sec)*1000000000+std::uint64_t(b.tv_nsec),clockGeneration};
  auto accepted=ledger->accept(now);token=accepted.token;
  return accepted.status==recovery::v2::progress::Acceptance::Accepted;
 }
 bool complete(recovery::v2::progress::WorkToken& token,bool durable)const noexcept {
  if(!token)return true;auto captured=token;token={};
  return ledger&&ledger->complete(captured,durable?(kind==recovery::v2::WorkerKind::CancelableHelperPrefix?recovery::v2::progress::Outcome::HelperReapedReconciled:recovery::v2::progress::Outcome::DurableCommitted):recovery::v2::progress::Outcome::Failed);
 }
};
}
