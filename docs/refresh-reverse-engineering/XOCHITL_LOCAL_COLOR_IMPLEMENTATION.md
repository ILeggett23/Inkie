# Xochitl local color cleanup and Inkbound implementation

Status: implementation and bounded SDK checks complete; 126 release checks and eight full-gate stages passed. The original sanitizer stage failed on a panel-animation assertion; its result is preserved. Three isolated reruns and the complete unchanged sanitizer script subsequently passed with identical source and sanitized binaries. This is a recorded-retry result, not a clean full-gate pass. The user subsequently approved installation; it completed reversibly at15:00 CDT on2026-10-14. Actual private-helper execution and optical qualification have not occurred.

## What the evidence establishes

**CONFIRMED, exact stock ELF:** the ordinary renderer passes flags 0 at `0x9c2ce4` to the region wrapper at `0x9bbee0`. That wrapper clips damage. Its complete-update bit selects the full image instead. The Acep2 implementation at `0x9d8400` partitions damage using the mode map and processes quality rectangles individually. The quality loop constructs stock internal pixel mode 16 at `0x9d8b3c`, prepares each rectangle at `0x9d8b94`, and schedules maintenance at `0x9d8bcc`. Control selection also depends on temperature. These offsets apply only to stock SHA256 `9749880daa2f10844e77b560ec0ecddd1634d43eb328af637c7026edf3ef120e`.

**CONFIRMED, limited device observations:** during three red strokes, the sampled stock queue contained local quality classes `2/16` and `5/16` alongside live updates. The operator reported that red appeared after a local update, without a whole-screen flash. Three palette cycles and a separate 25-cycle palette run also produced no reported whole-screen flash. These are lower queue observations, not interception of every public argument. See [stock trace](STOCK_REFRESH_TRACE.md) and `evidence/20261014/stock-red-ink.jsonl`.

**CONFIRMED, exact standalone library:** Inkbound's previous `VendorDisplay::clean` passed the complete-update bit, even for a small rectangle. The plugin wrapper expands that request to the full image. Public UI mode 3 with flags 0 retains regional geometry. Public content classes are Mono 0, Grayscale 1 and Color 3 for SHA256 `3800e9f3d01f40fa3f5cc96ca49bcd982471ce5b033320f8788707520c8ddd08`.

**Do not translate stock lower mode numbers directly into plugin arguments.** Xochitl embeds its backend; Inkbound loads a different standalone module. Stock quality class 16 is not a usable public color constant for that module: the plugin's lower converter 16 is luma-only. Its public UI path selects its own quality conversion. The implementation uses the verified public interface and intact RGB pixels.

**UNKNOWN:** exact live stock EPContentMap values, original Qt damage, universal padding/batching thresholds, and a general ghosting counter. Queue sampling and ELF analysis do not establish those facts. No new thresholds were invented.

The regional loop now has a repeatable, read-only verifier: `tools/verify_stock_local_color.py`. It first enforces the existing whole-file/wrapper/vtable checks, then checks eight regional-quality instructions and four call targets. [Recorded result](evidence/20261014/stock-regional-quality-static.json). This verifier never loads or executes the vendor ELF.

## Implemented behavior

The active isolated candidate is `work/refresh-fix21a/reader`, based on the exact installed 21a source. Newer Reader feature trees remain separate.

| Operation | Candidate request | Extent |
|---|---|---|
| Live black ink | Mono 0, Pen 0, flags 0 | Existing live damage/bands |
| Settled UI or erase cleanup | UI 3, flags 0; composed pixels select Mono 0, Grayscale 1 or Color 3 | Existing damaged region |
| Overlay removal over color | Recompose underlying pixels, then classify and request regional quality | Existing retirement region |
| Explicit initial/page/scene transition | Existing complete request | Existing full-page decision |

`VendorDisplay::qualityContent` reads the composed source over a conservative controller/converter footprint: 4-by-2 alignment and 8-pixel conversion chunks, clipped to visible dimensions. It does not modify pixels, change submitted geometry, inspect stride padding, or choose a fullscreen fallback for color. Any unequal RGB channels select Color 3; neutral intermediate values select Grayscale 1.

`UIUpdate` and `Cleanup` now select regional quality. UI quality debt waits for active contact and durable saving to settle. Fast feedback during contact remains available. An off-contact tool save presents once after saving. Failed quality debt remains pending; presentation acknowledgment waits for successful coverage.

No new refresh scheduler, global waveform substitution or ghosting threshold was added. Existing explicit page decisions and region planning remain. This fixes the demonstrated complete-flag error; it does not promise that every existing application-level full-page decision disappears.

## Maintenance boundary and remaining runtime risk

A preserved historical idle hang was independently traced through the foreground Qt event pump into the vendor maintenance callback and a wait on the global Swtcon queue mutex. The mutex owner and a complete deadlock cycle were not established. Stock also uses a blocking maintenance path; the following adaptation is **our direct-adapter policy**, not a discovered Xochitl algorithm.

The candidate filters only the owned, fingerprinted vendor maintenance QTimer. It preserves the vendor's scheduling, temperature decisions and original single-shot interval. On expiry inside the foreground pump it performs the original temperature/power preparation, tries the queue lock, and either defers to the existing timer interval or calls the pinned TMode request/publication helpers. It does not disable maintenance. A failed admission aborts to the existing external recovery owner. Other timers are unaffected.

Private helper ABI assumptions are guarded by whole-library/Qt fingerprints, module origin, singleton symbol offset, Acep2 vtable entry, helper-entry hashes and timer/thread checks. No proprietary file is modified. **These guards and VM tests do not qualify actual helper execution.** This part requires recoverable device qualification before deployment. The exact contract and offsets are recorded in `work/refresh-fix21a/LOCAL_COLOR_IMPLEMENTATION.md` and `evidence/maintenance-abi.json`.

## Validation and source identity

Bounded candidate manifest: `124d134597588b152a742e7de581e2d1a80992d50a6d4ffd260395135b9723e5`, 839 files. SDK GCC 13.4.0, Qt 6.8.2, AArch64 reMarkable target.

- 61 core cases and 644 refresh checks passed.
- 26 extent/RGB classification checks passed, including alignment, padding, large regional UI and complete-bit expansion modeling.
- 13 maintenance checks passed, including a real native mutex held by another thread.
- 6 real Qt timer checks passed, including busy retry, unrelated timers and timer lifetime.
- 13 production Display boundary cases and the post-integration route suite passed using fake vendor calls.
- Actual runtime compiled; dynamic-export audit passed with the two required constructor hooks and three standard string ABI symbols.

Full-gate manifest: `617c5bfb4540532ab28c2fc1c173a3c04ebbbb775d8677f163c44356eac5c265`. Production bytes match the bounded candidate; release-policy metadata additionally requires the three new suites. Results are in `work/refresh-color-fullgate/VALIDATION-SUMMARY.json`; the original failed gate is preserved in `ORIGINAL-GATE-RESULT.json`. All126 release invocations passed,696 compiled source identities matched, four export audits passed, and the remaining eight stages passed. The first animation assertion has no proven root cause and was not reproduced in three isolated runs or the complete sanitizer retry. No check or threshold was relaxed. Previous Mono-only passes do not qualify this candidate.

Actual vendor/idle/optical status: **NOT RUN**. No further manual test campaign is pending. A separate automated scratch-display qualification should exercise RGB patch add/restore, overlay removal and idle maintenance using the existing recovery owner and private scratch storage; it must not bypass liveness/profile gates or touch notebooks. An optical observation remains necessary to establish displayed color and ghost removal; software return and queue geometry alone cannot establish either.

Review patch: `work/refresh-color-fullgate/refresh-local-color.patch`, SHA256 `6efde9058d251a9c8b18cdc6d95894f93c84f9a88fd5516948846fd3399a0c7f`. Sealed validation evidence archive SHA256 `0a79affed4fab71a3c6ca7844114ea8769a020b9e50ae7c4620d3d23cb02c739`. No installable app was exported to the host.

## Approved installation update

The exact candidate was installed at `/home/root/inkbound-e2/E/app`, manifest29666f818ba4d093b177675728fe99b829731117e7502a649321b391f3b678a4. Previous21a was preserved and independently verified as rollback; all839 reader-data hashes and1161 metadata entries were unchanged. Stock Xochitl remained running with the same PID22907. No Inkbound launch occurred. See `work/install-refresh-color/INSTALL_REPORT.md` for current installation evidence; earlier sealed validation summaries describe pre-install state.
