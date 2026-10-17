# Inkie — notes for Claude Code

Private archive and active source of the Inkbound reader for the reMarkable Paper Pro (C++20/Qt6, direct display, no
DOM/QML). The tablet runs the build from `fix/ui-input-refresh` (PR #1). `main` is the archived 21b baseline.

## Layout
- `reader/src/` — reader runtime. `reader/src/ib/` is the pure-C++ presentation kit: tokens (`theme.h`), zones
  (`layout.h`), text and type roles (`text.h`, `type.h`), components and screen painters. Device pixels, 1620×2160, 229 ppi.
- `reader/tests/` — host suites; `reader/tools/` — build, packaging and preview tools (`ib_preview.cpp`).
- `reader/assets/fonts/` + `manifest.json`, `reader/notices/` — bundled fonts and licences.
- `reader/recovery/`, `reader/third_party/`, `reader/reused/` — protected (see below).
- `companion/` — Companion service (Cloudflare). `docs/` — design and refresh records. `provenance/`, `backup/` — archive.

## Build and test
- No local builder: the Lima VM was retired on 2026-10-16. Do not recreate it unless asked.
- CI is the validation: "Reader UI and refresh regressions" (`.github/workflows/reader-ui-tests.yml`, Qt 6.8.2 + pinned
  MuPDF 1.28.3, release and ASan/UBSan, plus Companion contracts) and "Verified Ferrari tablet build"
  (`tablet-sdk-build.yml`, ARM SDK build, target regressions, package checks). Both run on PRs into `fix/ui-input-refresh`.
- Full CMake configure needs `-DBUILD_VENDOR_RUNTIME=ON` with Qt 6.8.2 and MuPDF; see the workflows for exact flags.
- The kit alone builds without Qt: compile `reader/src/ib/*.cpp` + `reader/src/core.cpp` (`-Isrc -Irecovery -isystem
  third_party`, link libsystemd) to run `ib-layout-tests <reader>`, `status-screen-tests <reader>` and
  `ib-preview <reader> <out>` (PNG previews; `--targets` lists every target for before/after diffs).
- Queue-only test modes may run locally. Never weaken timing or settled-state checks to make a test pass.

## Rules for UI work
- Sizes from the role table, rects from `ib/layout.h` zones, faces from `ib/text.h`/`ib/type.h`; no per-screen values.
- Cursive (`Face::Script`) only through `ib/type.h` roles; it must switch off with the Plain-text preference.
- Refresh: regional damage only (`docs/UI-REFRESH-REFACTOR.md`). Opening/closing a panel damages old∪new bounds; no new
  full-update flags, waveform constants or timers; complete updates only for real scene transitions with a reason.

## Protected code — do not modify (propose changes in the PR description instead)
- Recovery owner, lifecycle/sleep/power profile, vendor refresh authority.
- `reader/recovery/`, `reader/third_party/`, `reader/reused/`.
- Core Marker/session/touch input path and styled ink (`marker.*`, `session.*`, `touch.*`, `styled_ink.*`).
- Product storage formats, including the persisted ink order (`theme::Ink` indices).
- `companion/`.

## Authorization boundaries — each needs fresh, explicit approval for that step
- Accessing or installing to the tablet.
- Deploying or changing the Companion.
- Paid AI/provider calls.
- Pushing to `main` or `fix/ui-input-refresh`, merging anything, or touching the backup release
  (`inkbound-builder-backup-2026-10-15`).
- Work happens on a feature branch with a draft PR against `fix/ui-input-refresh` (or `main` once PR #1 merges).
