// SPDX-License-Identifier: AGPL-3.0-only
#pragma once
#include <atomic>
#include <string>
#include <vector>
namespace reader {
// D2 page turns, as the stock reader's long-lived renderer: one persistent "reader-experience1-prepare --serve"
// child keeps the open document between pages, so only the first page pays for hashing and whole-book layout.
// Each request is the exact argument list of a one-shot page helper (--page / --view-page / --epub-view-page ...)
// and produces the same prepared.bin transport. If the server cannot run a request (spawn failure, death, timeout,
// stop), it is killed and reaped before this returns, so callers may reconcile transport files exactly as after a
// one-shot helper; a request the server refused falls back to a one-shot helper. Thread-safe (one request at a time).
// Returns true when the helper reported success.
bool runPagePreparation(const std::string& helper,const std::vector<std::string>& args,const std::atomic<bool>& stop);
// Ends the server (EOF, then SIGKILL after a short grace) and reaps it. The preparer calls this before it reports
// itself stopped, so no helper outlives preparation.
void closePageServer()noexcept;
// Test/diagnostic counters.
struct PageServerStats {unsigned spawned{},served{},fallbacks{},failures{};};
PageServerStats pageServerStats()noexcept;
}
