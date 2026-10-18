# Codex handoff: Inkie beta stabilization and v2 completion

This is for the next agent picking up `claude/zealous-bohr-qi7lnt` (draft PR ILeggett23/Inkie#2, into `fix/ui-input-refresh`).

- Read `CLAUDE.md` first. It sets the protected paths and the approval boundaries.
- The full audit, measurements and device checklist are in `docs/BETA-STABILIZATION-REPORT.md`.

## What this branch is

Starting from the shipping reader (`fix/ui-input-refresh`), the branch carries:

1. **Layout architecture and the cursive role.** Zones in `src/ib/layout.h`; type roles in `src/ib/type.h`; Caveat for
   UI words ≥ 30 px. See `docs/LAYOUT-ARCHITECTURE.md`.
2. **Inkie rename and the v2 "stationery" redesign** for the Gallery 3 panel: six clean pops, colour as identity, ink as
   state, dotted ink rules, no grey. See `docs/INKIE-DESIGN.md`. Internal names stay `inkbound`.
3. **Toolbar icons.** Face-on pen tips (`penGlyph`, cached) and outline usability icons (`src/ib/icons.cpp`).
4. **The beta pass, in five batches:**

| Batch | Commits | What changed |
|---|---|---|
| A: stability | `d89477d` | `writable()` respects the lock and the reader's contact gate (S1 Critical, S2). `retireFailedTom` keeps the current state (S3). Failed opens stop retrying (S4). `navigateTo` reports refusal (S5). Wi-Fi status read under its mutex (S6). `librarySheetOpen()` defers library repaints (S7). Refused Companion taps can be repeated (S8). Close during a page turn returns to the library (S9). Per-book state is cleared on close (S10). `ux::friendly` messages (S11). Typed text survives a refused send (S12). Tom history clamped (S13). |
| B: performance | `d3a5e85`, `bc4dca6`, `5c3cb68` | Release policy read once (P1). Settings tick compares flags first (P2). Styled results repaint the chrome only (P3). Retained diff frame for the Wi-Fi and view sheets (P4). `text::fit` bisects (P6). Tool taps damage only changed rects, diffed against the frame held at `railAction` entry (`chromeBeforeHeld_`) (P7). Pen cache keyed on the disabled ink (P8). Scanline `fillPath` and per-segment `stroke`, pixel-identical. New `tools/ib_perf.cpp`. |
| C: UI/UX | `2d5e914`, `f72483a` | Wi-Fi bars, rows and copy. Companion copy and `Literal`. Note markers ≥96. Notice card. `HintFont` placeholders. Every target ≥96. One close glyph. Question confirmations. Copy fixes. Writing guides as dotted ink. |
| D: tests and CI | `41221ee`, `132152e`, `6b33315` | View and route tests that were local-only now run in CI. New `ib-surface-tests` and `user-messages-tests`. Wi-Fi paint checks. Committed target baseline with a CI diff. Three existing tests corrected (below). |
| E: docs | this commit | Report, this handoff, design-doc updates. |

### Three existing tests changed

These are contract/fixture corrections; timing and settled-state checks stay unchanged:
- **`ux_behavior_tests` cleanup-retry scenario.** It ignores at most one request lying wholly inside `ib::status::ReaderClock`. The reader clock repaints when the minute turns, and this failed once on the tablet build at 01:47:00. The cleanup counts stay exact.
- **`library_route_tests` high-contrast check.** It checks `theme::Disabled`, the only token High contrast changes under Inkie. It used to check `InkSecondary`.

- **`gesture_route_tests` Selection-tool stylus drag.** A drag selects entire touched paragraphs/blocks by the shipping user contract. The expected full text, raw glyph quads and UTF-16 bounds come from parser block metadata. Highlighter/handle exact-word cases and no-stored-ink assertions remain exact. The supplied `890eaa63` host run failed this stale endpoint-word expectation even though its ARM run passed; require all current-head gates green before installation.

### Post-handoff CI follow-up

The whole-block test correction (`4d67f963`) passes release, but ASan caught the existing asynchronous Notes-deletion assertion. A busy empty snapshot can replace the populated View before the settled empty snapshot arrives; the old edge predicate then misses the removal. `NotePresence` retains presentation ownership across that interleaving, resetting on a new binding or non-Note paint. Deterministic snapshot cases and the existing real delete/Undo/Redo regression cover it. Initial creation, command/save guards and protected Runtime/storage code are preserved. Require fresh host and ARM checks on the new head; do not install an older green artifact to skip a red gate.

### Device report follow-up (rendering and selection)

From an on-device report. Details are in report §8, "Device report follow-up".
- **`ux_display.h`:**
  - only pen-mode work is banded to a quarter page (quality UI updates had been banded too, so one screen change
    finished in stages);
  - a Regional scene is one region;
  - quality updates keep the richest class owed or shown (per-60 px-cell record `owed_`, the existing
    `cleanRemoval()`), replacing the single `eraseContent_`.
- **`ux_shell.*`:**
  - library repaints, sheet diffs and the document menu with its slot are each one region (`Shell::joined`);
  - the removal cleanup flag (`cleanupNext_`) holds until the removal's damage arrives;
  - the collapsed handle reopens the toolbar in Select mode;
  - Marker taps on a selection strip or definition card are taps without the Selection tool (`PenSelect::probe`);
  - refused Notes, Highlight and Tom opens wait for saving to settle (`pendingSelectionAction_`);
  - `ui:selection-action` journals each command.
- `vendor.*` is unchanged; no new waveform, flag or timer.

## Where things live

| Concern | Place |
|---|---|
| Tokens, contrast | `reader/src/ib/theme.h` (`setHighContrast` changes `Disabled` only) |
| Zones and rects | `reader/src/ib/layout.h`, `reader_screen.h`, `status_screen.h` |
| Text, fit/wrap/quiet | `reader/src/ib/text.*` (`fit` bisects code-point ends) |
| Components (`hitArea`, `divider`, `ruledPaper`, `HintFont`, buttons, sheets) | `reader/src/ib/components.*` |
| Icons, pen tips and their cache | `reader/src/ib/icons.*` (`PenKey` includes `disabledInk`) |
| Shell state machine, damage, admission | `reader/src/ux_shell.cpp/.h` (`writable()`, `inkAdmission()`, `librarySheetOpen()`, `railAction()`, `toolsPaint()`) |
| User-facing messages | `reader/src/user_messages.h` |
| Qt-side views | `reader/src/wifi_view.cpp`, `companion_view.*`, `library_view.cpp`, `document_controls.cpp`, `note_indicators.cpp` |
| Previews, targets, timing | `reader/tools/ib_preview.cpp` (`--targets`), `reader/tools/ib_perf.cpp` |
| Kit tests | `reader/tests/ib_layout_tests.cpp`, `reader/tests/ib_surface_tests.cpp`, `reader/tests/fixtures/ib-targets.txt` |

## How to build and test

- **There is no local device builder.** The Lima VM was retired; do not recreate it. CI is the validation:
  - "Reader UI and refresh regressions": host release, ASan/UBSan, Companion contracts;
  - "Verified Ferrari tablet build": ARM SDK build, target regressions, the `verified-tablet-candidate` artifact.
- **The kit alone builds without Qt:** compile `reader/src/ib/*.cpp` + `reader/src/core.cpp` (`-std=c++20 -Isrc -Irecovery -isystem third_party`, link libsystemd and zlib). Then run:
  - `ib-layout-tests <reader>`
  - `ib-surface-tests <reader>`
  - `status-screen-tests <reader>`
  - `ib-preview <reader> <out> [--targets]`
  - `ib-perf <reader> [n]`
- **Light Qt tests** (wifi, companion-view, tools-view, library-manage, user-messages) also build against a system Qt 6. Anything that links `ux-lib` needs Qt 6.8.2 and MuPDF 1.28.3; see the workflow for the exact flags.
- **Changing a layout on purpose:** regenerate the baseline with `ib-preview reader <dir> --targets && cp <dir>/targets.txt reader/tests/fixtures/ib-targets.txt`, and say why in the commit.
- **Never** weaken timing or settled-state checks.

## Open decisions and proposals

These are protected or policy areas, so they are proposed only, not changed. Details are in report §9.

- **Refresh policy (`ux_display.h`):**
  - possible repeated aligned-footprint colour classification (not single-pixel); measure the call paths before changing it;
  - banding in `Display::live` (not the settled-cleanup loop);
  - union fallbacks in some queues, versus least-cost merging in `accumulateDamage`;
  - live-ink damage unioned with stale damage;
  - the lock screen's full refresh on a wrong passcode.
- **Power and memory:**
  - the 2 ms session loop and 1–2 ms worker polls;
  - the 45.8 MB styled-ink raster per stroke.
- **Reader and storage:**
  - `reader.cpp` `errors_` is never cleared;
  - the passcode fails open on corruption;
  - the failure count is held only in memory.
- **Pen colours:** the proposed clean values in INKIE-DESIGN §8. The persisted ink order cannot change without a storage decision.

## Not done (deliberately)

- **P5:** caching the passcode and import-error reads per library tap. The invalidation risk outweighs about 0.2 ms.
- **`text::wrap`** incremental measurement and a glyph cache. Not a hotspot (0.18 ms).
- **Dead handlers:** `SnapHighlights` action, reading-light menu, `MenuLayers`/`MenuLibrary`, `wifiSelected_`. Removing them needs a no-behaviour-change PR.
- **Device validation.** Nothing here was run on a Paper Pro. Use the checklist in report §10.

## Next steps

1. Get approval to install `verified-tablet-candidate` (latest green tablet run on the PR head). Then run the report §10 checklist.
2. Take the refresh proposals to the refresh-policy owner, with device E2T captures.
3. Mark PR #2 ready for review once CI and the device checklist pass. Merging needs explicit approval.
