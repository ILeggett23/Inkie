# Companion implementation checkpoint

## 2026-09-13

Created a physically separate companion root. Only companion frontend/backend, generic contract, synthetic fixtures/tests, operator documentation and deployment configuration are in scope. No reader code, device runtime, backup, personal book, reference photograph, handwriting or credential was copied. GitHub creation/push and live service deployment are handled separately after staging and history audit.

Implemented Python/SQLite schema migration, private durable upload queue/chunks, separate bounded validation/conversion worker, authenticated browser and device APIs, pairing/approval/revocation, stable IDs, range downloads, durable ACK, tombstones and explicit intentional-import propagation. Implemented responsive frontend with actual upload/status/device controls, private settings, Tom configuration state, handwriting canvas/interpretation fallback, streamed text UI, history/cancel/retry and local draft preservation. Added Pages-only static entry with safe navigation to configured service origin.

Consulted current official OpenAI Responses streaming/image/model documentation before adapter implementation. No API key was available and no provider call was made. Simulated SSE is explicitly labeled in tests. User selected GitHub Pages for static entry, then asked about their existing Cloudflare account; Cloudflare source is developed in its own subdirectory. No paid resource was activated by this work.

### Executed checks

| Command / check | Observed result |
| --- | --- |
| `python -m unittest -v tests.test_integration` using bundled runtime initially | 22 tests passed, 11.629 seconds |
| Public OSV query for initial bundled pypdf 6.10.0 + Pillow 12.3.0 | Failed dependency gate: pypdf advisories found; Pillow clear |
| Install pypdf 6.18.1 in companion-only virtual environment | Success; shared runtime left unchanged |
| `python scripts/check_dependencies.py` with final pins | Pass: no OSV advisories returned for pypdf 6.18.1 or Pillow 12.3.0 |
| `.venv/bin/python -m unittest -v tests.test_integration` after dependency + intentional-import migration | 22 tests passed, 11.406 seconds |
| Complete suite after UTF-16 XML/entity hardening and Tom transcript privacy/storage limits | 22 tests passed, 11.667 seconds |
| `python -m unittest -v tests.test_boundary` | 7 synthetic Git regressions passed, 0.725 seconds: empty index/history, symlink ancestors, historical symlinks/gitlinks, old secret-like blob and binaries |
| `node --check web/app.js` | Pass |
| Real Chromium `node tests/browser.cjs` | Pass at 390, 768 and 1280 pixels: sign-in, four sections, no page overflow, saved draft, actual upload/conversion, sign-out; no page errors |
| Visual inspection of phone screenshot | Legible controls/queue and original quiet-paper design; screenshots stay ignored/local |
| Actual five-format fixture service | PDF, EPUB, DOCX→EPUB, TXT→EPUB, MD→EPUB all reached waiting for device through actual workers |

The 22-test suite includes archive/XML/PDF action/encryption rejection, conversion with images/tables/formatting, all-format actual HTTP range transfer/checksum/ACK replay, upload interruption/replay/offset/checksum failure, device pairing expiry/replay/revocation, account/object isolation and CSRF/origin/download ticket checks, disk-full fsync failure without false acknowledgement, cancel/expiry/original-file preservation, restart recovery, unknown migration refusal, Tom unconfigured behavior, all context scopes/privacy/limits, and simulated provider streaming/disconnect/idempotency/history/cancel.

### Failures resolved without weakening assertions

- System Python 3.9 attempted an unwritable OS bytecode cache and lacks required Python APIs. Verification uses Python 3.12 with project-local bytecode cache.
- Sandbox denied loopback listening. An authorized loopback-only test-server escalation was used; no remote device operation was involved.
- Playwright's default expected browser revision was not installed. Tests were rerun with the existing Chromium executable path explicitly configured; all responsive checks then passed.
- Initial dependency audit found actual pypdf advisories. Upgraded isolated dependency pin, reran audit and complete tests.
- Boundary scanner initially rejected legitimate Cloudflare `.jsonc`/nested `.gitignore` source metadata. Added those exact source file kinds, retaining runtime/binary/credential and reader-content exclusions.
- Independent audit found empty tracked sets and historical symlink/gitlink modes were insufficiently checked. Both now fail closed, with seven temporary-Git regression tests; current-file symlink ancestors are also rejected. Added only the exact Cloudflare converter adapter file to the Docker build allowlist.

### Remaining separate gates

Final root-level integration recheck after Cloudflare code is complete; staged/tracked/history audit before push; GitHub private-repository creation/Pages eligibility and actual Pages deployment; Cloudflare account/resource/converter/network qualification; server-side OpenAI API credential, allowed-model access and authorized capped real call; Docker image test where applicable. Actual receiver cross-tests are owned outside this repository; any reader-specific evidence stays local and is not copied here. Physical device behavior is not established by this worklog.

## 2026-09-19 Cloudflare continuation

Recovered the original eight grouped Cloudflare checks and reran them successfully (3.488 seconds) with local workerd, D1, R2, queue delivery, and the real Python HTTP converter. A sandboxed Node invocation aborted before tests; enabling the authorized loopback-only execution resolved the environment restriction. No public service/provider request occurred.

Added focused regressions before changing implementation. All three failed against the prior Worker: a late chunk remained after cancellation, concurrent conflicting Tom identities surfaced an unhandled database conflict, and a 257-turn conversation was silently truncated to 256. Also reproduced that canceling an already-deleted relay job overwrote its tombstone-related status. Fixed these without weakening the checks:

- Remove rejected, unreferenced upload objects and restore terminal cleanup intent after in-flight writes.
- Resolve concurrent Tom identities with exact owner/content replay checks and remove losing R2 payloads.
- Add migration 0002 for durable deleted-conversation markers, prevent late turns into deleted conversations, stream paginated history, and resume interrupted payload deletion through the scheduled sweep.
- Preserve imported/deleted document states when relay cancellation races those transitions.

Final `npm test` from `cloudflare/`: **11 grouped tests passed, 10.622 seconds**. Includes the 257-turn cleanup interruption/recovery test and preserved document tombstone behavior. Final production Worker `wrangler deploy --dry-run --env production` also passed. This was bundle/config verification with placeholder production identity and disabled conversion/Tom, not deployment.

Added the missing Cloudflare setup/migration/retention guide, a clean-checkout-safe test harness, and a dedicated CI job for the Cloudflare suite and Worker dry-run. Added a self-contained synthetic converter image smoke helper; its actual image execution is a separate gate. Public CI execution, Cloudflare deployment/network behavior, and real provider behavior remain unverified by these local checks.

### Local converter image verification — 2026-09-19

The actual converter Docker image built in a local ARM64 Linux VM and passed all ten synthetic smoke cases. The process ran as UID10001 with networking set to none, a read-only root filesystem, all capabilities dropped, no-new-privileges,1GiB memory,1CPU,32processes and a192MiB temporary filesystem. The exact test container was stopped afterward. Image identity and local runtime evidence remain outside this repository. Cloud-target Linux/AMD64 image qualification is separately included in CI; local ARM64 results do not establish the deployed architecture or Cloudflare runtime. No private content, reader implementation or real provider credentials entered the build context.

## 2026-09-19 live Cloudflare delivery and provider integration

Deployed dedicated interface/API and private converter to the existing Cloudflare account; no unrelated resources or subscriptions changed. Fixed two real streaming boundary defects using FixedLengthStream, with local reproductions that now cross actual workerd HTTP and service response streams. Final cloud delivery verification used exactly ten synthetic conversions, passed all five formats/security/pairing/range/checksum/ACK/tombstone checks, and separately validated/fsynced a real received EPUB before ACK. The real scheduled sweep cleaned a deliberately expired synthetic fixture. Live Chromium passed at three responsive widths. Synthetic accounts/data were removed and eleven known R2 keys confirmed absent; real-owner sign-in was separately verified, with credentials confined to the requested private local file.

Added Workers AI alongside the optional OpenAI adapter, authenticated provider/model discovery, actual-provider consent disclosure, durable provider identity and abort propagation. Thirteen local Worker groups and 29 reference/boundary tests passed. Two targeted normalization/cancellation tests subsequently passed. Exactly two capped live AI requests were made: one strict normalization failure (fixed locally, not re-probed), then real vision/SSE completion with `H I` versus expected `HI`. The literal-match failure remains recorded. Tom is enabled for its correctable-interpretation workflow with a128-token output cap; no general recognition or physical-device qualification is claimed. Full details are in cloudflare/DEPLOYMENT-STATUS.md.
