# Inkie: alpha stabilization and premium-experience audit

Branch `claude/zealous-bohr-qi7lnt`, draft PR ILeggett23/Inkie#2 into `fix/ui-input-refresh`.

- **Audit baseline:** `22b97b7`, the face-on pen tips build, which is the last installable candidate before this pass.
- **Final head:** see [§8 Test runs](#8-test-runs).
- **Scope:** the reader shell, the presentation kit (`reader/src/ib/`) and the Qt-side views.

**Not touched (protected).** These paths were read for the audit and changed nowhere:
- the recovery owner;
- lifecycle, sleep and power;
- the vendor refresh authority;
- `reader/recovery/`, `reader/third_party/` and `reader/reused/`;
- marker, session, touch and styled ink;
- the storage formats;
- `companion/`.

Findings in those areas are written up as proposals in §9.

**How each claim is verified.** Every resolved item carries one of three labels:

- **[auto]**: an automated test exercises it and passes in CI.
- **[inspect]**: verified by reading the code path end to end. No test drives it.
- **[device]**: needs a physical Paper Pro. Nothing here claims a device result. No tablet was accessed.

Compiling is never counted as verification.

## 1. Summary

| | Found | Resolved | Partly resolved | Proposed only (protected/policy) | Deferred |
|---|---|---|---|---|---|
| Stability (S) | 13 + 3 | 12 | 1 (S13) | 3 | 0 |
| Performance (P) | 8 + 6 | 5 | 2 (P2, P6) | 6 | 1 (P5) |
| UI/UX (U) | 9 | 9 | 0 | 0 | 0 |
| Tests (T) | 4 | 4 | 0 | 0 | 0 |

**Beta readiness:** ready for a closed beta on the tablet, once the [device checklist](#10-physical-device-validation-checklist) passes. It is not ready for a wider beta until the refresh-policy proposals in §9 (P-R1 to P-R3) are decided on device.

## 2. Issues found and resolved

Severity: **Critical** = ends the session or loses work; **High** = wrong screen, stuck UI, or a large cost on every interaction; **Medium** = wrong state, misleading UI or cost on some paths; **Low** = cosmetic or dead code. Each entry is also marked **confirmed** (reproduced or traced to a line), **suspected** (reasoned from code, not reproduced) or **improvement**.

### Stability (`reader/src/ux_shell.*` unless noted)

| # | Sev | Kind | Issue | Fix | Verified |
|---|---|---|---|---|---|
| S1 | Critical | confirmed | A Marker touch on the passcode screen while a book is open ended the session. `writable()` and `inkAdmission()` ignored the lock, `reserve()` refused, and the protected session loop treats a refused reserve after admission as fatal (`session.cpp:43`). | `writable()` refuses when `stop_` or `lock_!=None`, so admission and reserve agree. | [auto] `ux_behavior_tests: passcode_lock_over_a_book_closes_ink_admission_before_reserve` |
| S2 | High | suspected | Notes or Tom opened during a page turn admitted ink that `reserve()` then refused at `beginExternalContact()`. | The Notes and Tom branches of `writable()` also require `document_->writable()` (reader `Active`, not exiting), which is the same gate `beginExternalContact()` checks. | [inspect] |
| S3 | High | confirmed | A Tom worker failing after Tom closed ran `retireFailedTom()`, which restored a stale `tomReturn_`. The result was a library painted over an open book, or a stuck library. | When the panel was not shown, `retireFailedTom()` drops it without `tomClosed()` and keeps the current state. | [inspect], plus existing Tom route tests in CI |
| S4 | High | confirmed | A failed open driven by `pendingNativeOutput_` retried forever. | Open failure clears `pendingNativeOutput_` and `pendingOpen_`. | [inspect] |
| S5 | Medium | confirmed | `navigateTo()` returned true for a refused page, so pending note/Tom jumps re-requested it on every tick. | Returns true only when the target is the intent, the submission or the current page. | [inspect], plus `gesture-route-tests` (now in CI) |
| S6 | Medium | confirmed | Data race: `wifiFoundStatus_` was read without `wifiMutex_`. | Copied under the mutex. | [inspect] (ASan/UBSan job, no TSan) |
| S7 | Medium | confirmed | Library repaints (the low-battery notice, the Companion catalogue reload) painted over open library sheets. | One `librarySheetOpen()` predicate. `notice()` and the Companion reload defer while a sheet or the lock is up, and each sheet repaints the library on close. | [inspect], plus `library-route-tests` (now in CI) |
| S8 | Medium | confirmed | Companion Approve/Reject taps were lost while a helper ran. | The refused tap calls `companionView_.refused()`, which restores the prior consumed state so the tap can be repeated (`companion_view.*`). | [auto] `companion-view-tests` (now in CI) |
| S9 | Medium | confirmed | Close during a page turn did nothing (V2) or quit (V1). | Sets `exitAfterNavigation_`. `tick()` returns to the library once navigation settles. | [inspect] |
| S10 | Medium | confirmed | Library › Notes went stale after returning from a book. Note indicators survived the close. Permanent delete didn't save favourites. | On return: `noteIndicators_.clear()` and a `libraryNotes()` refresh. Permanent delete calls `saveFavorites()`. | [inspect] |
| S11 | Medium | confirmed | Raw internal codes reached the screen (`note-layer-hidden-or-locked`, `product-store-already-open`, `SEMANTIC DATA UNAVAILABLE`, `WI-FI ERROR - TRY AGAIN`, and a licence path on the definition card). | New `src/user_messages.h` (`ux::friendly`) maps codes to sentences. Codes stay in logs. Used by the Notes/Tom panels, semantic notices and Wi-Fi. The definition card credits "GCIDE · Webster 1913". | [auto] `user-messages-tests` (new) and `wifi-tests` |
| S12 | Medium | confirmed | Typed text was discarded when `send()` was refused (Notes, Tom). | The keyboard `resume()`s with its text, and the message says to try again. | [inspect] |
| S13 | Low | confirmed | Tom history paging offsets were never clamped. Several handlers are dead. | Offsets clamped (`tom_panel.cpp`). **Dead handlers left in place**: removing them changes nothing visible and touches menu ids. Listed for a cleanup PR. | [inspect] |
| S-UI | High | confirmed (CI) | Found by CI during this pass: a tool tap could leave the toolbar tile stale. A rail action repainted the chrome before `toolsPaint()` took its "before" frame, so the diff found no change. | `railAction()` holds the frame shown when the tap began. A scope guard releases it on every exit. | [auto] `selection-route-tests: selection_activation_paints_selected_toolbar` |

### Performance

Measurements are in §4.

| # | Sev | Kind | Issue | Fix | Verified |
|---|---|---|---|---|---|
| P1 | High | confirmed | `wifiTick` read and parsed `release::policy` on every tick (≈500 Hz idle). A throw there ended the session. | Read once at Shell construction (`releaseNetworking_`). Cheap guards go first. | [inspect] (one file read per run instead of one per tick) |
| P2 | High | confirmed | `viewSnapshot()` read files and parsed JSON on every tick while document settings were open. | The tick compares busy/quiet first and rebuilds only on a change. Taps still rebuild (**partly resolved**). | [inspect] |
| P3 | High | confirmed | Every stroke save, undo or redo re-composed the page (two 14 MB copies, highlight wash, SHA-256, indicators), even when only the status or tools changed. | Styled results repaint the chrome only, unless the page's first paint is pending. Footer damage only. The toolbar is damaged when tools changed, which also fixes a stale Undo/Redo tile. | [auto] existing styled and selection route tests. Cost [inspect] |
| P4 | High | confirmed | Each Wi-Fi or view-sheet tap or keystroke allocated and copied a 14 MB frame. | Diff against the retained `libraryBefore_` buffer. | [inspect] |
| P6 | Medium | confirmed | `text::fit` was O(n²): it measured every prefix. | Bisects code-point ends: 68 → 32 µs on a 240-char title. `wrap` and a glyph cache are **not done** (wrap is 0.18 ms, not a hotspot). | [auto] `ib-surface-tests` (fit contract), plus `ib-perf` |
| P7 | Medium | confirmed | A tool tap damaged the whole chrome (≈0.5 Mpx of quality refresh) for one 112 px tile. | `toolsPaint()` damages only the changed rects (`changedRegions`). | [auto] `selection-route-tests`, `ui-damage-tests` |
| P8 | Low | confirmed | The pen-glyph cache key missed the disabled ink, so high contrast reused normal-contrast rasters. | `PenKey` includes `disabledInk`. | [auto] `ib-surface-tests`. Fails against the old cache (mutation-checked) |
| K1 | improvement | confirmed | Kit paint cost: `fillPath` and `stroke` scanned whole bounding boxes. | Scanline `fillPath` and per-segment-box `stroke`. Pixel-identical on all 116 previews. | [auto] previews diffed byte for byte, plus `ib-perf` |
| P5 | Medium | confirmed | Every library tap re-reads `import-errors.json` and the passcode file. | **Deferred**: caching the passcode state needs invalidation on every writer, and the S1 test writes the file directly. The correctness risk outweighs ≈0.2 ms per tap. | n/a |

### UI/UX: Inkie v2 completed on every surface

| # | Sev | Issue | Fix | Verified |
|---|---|---|---|---|
| U1 | High | Wi-Fi signal bars always looked full, because inactive bars used `Border`, now black. Current and unsupported rows looked identical. | Bars are solid up to the level and hollow above it. The current network is an ink row. Unsupported networks are dotted and show no bars. SSIDs are in `Literal` with `fit`. | [auto] `wifi-tests` paints the view and checks every bar |
| U2 | High | The Companion account was drawn in cursive. Expiry was shown in UTC. Raw byte counts were shown. | Account in `Literal`. "Expires in N minutes". "Receiving a document — N%". "Unpair this tablet?". | [auto] `companion-view-tests` |
| U3 | High | Note markers were 42 px targets with a grey corner. | A kit-drawn sticky marker. The hit rect is `ib::hitArea` (≥96), and the shell's hit test uses the same rect. | [auto] `semantic-crop-tests` (indicators, now built). Size [inspect] |
| U4 | Medium | v1 leftovers: a notice pill, solid separators, a literal black in the status bar, **grey writing guides**. | Notice is a kit card. Dividers are dotted ink. Status uses `InkPrimary`. Guides are 2 px ink dots every 16 px. | [auto] previews and `ib-surface-tests` |
| U5 | Medium | Placeholders looked like values. | `HintFont` (italic sans, Meta) for every placeholder. | [auto] previews |
| U6 | Medium | Targets under 96 px: folders 76, document menu rows 88, zoom badge 76, keyboard clear ≈86, note markers 42. | `hitArea` everywhere. **No kit target under 96 px on any surface.** | [auto] `ib-surface-tests` (all 571 targets) |
| U7 | Medium | Close glyphs in four sizes. Mixed sheet titles. Mixed confirmation wording. | One close glyph (36) at one position rule. Titles at Subhead. "Remove highlight?", "Delete conversation?", "Delete note?". | [auto] previews and the target baseline |
| U8 | Medium | Copy defects: the High contrast note overpromised; Settings described a missing snap toggle; About omitted Caveat and GCIDE; "Snap to Text"; "Recent" vs "Recents"; "Write note". | All rewritten. About credits Caveat and GCIDE. | [auto] previews |
| U9 | Low | No Tom history empty state. The notice title had no `fit`. The Favorites star filled yellow on toggle (colour used as state). UI hints were in the book's italic. | Empty states added. `fit`. The star toggles in ink. Hints are in sans italic. | [auto] previews |

### Tests

| # | Issue | Fix |
|---|---|---|
| T1 | Most Qt view and route tests ran only in `run-local-tests.sh`, which is wired to the retired VM. `semantic_crop_tests` was never built. | CI now builds and runs library-view, library-manage, companion-view, wifi, timezone-view, tools-view, document-controls, tom-inline-panel, semantic-crop and user-messages, plus the library, notes, status, timezone and gesture route tests. |
| T3 | Wiring the local-only tests into CI surfaced two stale or time-dependent checks. (a) `library_route_tests` asserted that High contrast changes `InkSecondary`, which Inkie v2 deliberately keeps ink. (b) `ux_behavior_tests` cleanup-retry counted requests exactly, and once on the tablet build a minute turn (`Shell::pollClock` repainting the reader clock) added a third request, stamped 01:47:00.406. | (a) Checks `Disabled`, the token High contrast changes. (b) Ignores at most one request wholly inside `ib::status::ReaderClock`; the cleanup counts stay exact. Neither is a timing or settled-state check. |
| T4 | The newly enabled Selection-tool stylus drag route still expected endpoint words. | Derive complete touched-block text, glyph quads and UTF-16 bounds from immutable parser metadata, matching the user-selected shipping contract. Exact no-stored-ink and implicit-highlight checks remain. |
| T2 | No global ≥96 px check. No `fit`/`wrap`/`quiet` tests. No target-baseline diff. | `ib-surface-tests` (1,377 checks) and `reader/tests/fixtures/ib-targets.txt`, diffed in CI. A moved control fails the build until the baseline is regenerated on purpose. |

## 3. Files changed (`22b97b7..HEAD`)

| Area | Files |
|---|---|
| Shell stability and perf | `src/ux_shell.cpp`, `src/ux_shell.h` |
| Messages | `src/user_messages.h` (new), `src/notes_panel.cpp`, `src/tom_panel.cpp`, `src/text_input.h`, `src/semantic_paint.h` |
| Qt views | `src/wifi_view.cpp`, `src/companion_view.cpp/.h`, `src/note_indicators.cpp`, `src/library_view.cpp`, `src/document_controls.cpp` |
| Kit | `src/ib/canvas.cpp`, `src/ib/text.cpp`, `src/ib/icons.cpp`, `src/ib/components.cpp/.h`, `src/ib/library_screen.cpp`, `src/ib/notes_screen.cpp`, `src/ib/reader_screen.cpp`, `src/ib/settings_screen.cpp`, `src/ib/sheets.cpp`, `src/ib/status_screen.cpp`, `src/ib/tom_screen.cpp` |
| Tools | `tools/ib_perf.cpp` (new), `tools/run-local-tests.sh` |
| Tests | `tests/ib_surface_tests.cpp` (new), `tests/user_messages_tests.cpp` (new), `tests/fixtures/ib-targets.txt` (new), `tests/ux_behavior_tests.cpp`, `tests/wifi_tests.cpp` |
| Build and CI | `CMakeLists.txt` (`ib-perf`, `ib-surface-tests`, `semantic-crop-tests`, `user-messages-tests`, Wi-Fi test fonts), `.github/workflows/reader-ui-tests.yml` |
| Docs | `docs/BETA-STABILIZATION-REPORT.md`, `docs/CODEX-HANDOFF.md`, `docs/INKIE-DESIGN.md` |

## 4. Performance measurements

### Kit paint cost

- **Tool:** `ib-perf <reader> 30`.
- **What it times:** each preview surface painted by the device painters.
- **Statistic:** median of 30 runs after one warm-up; best of two passes.
- **Build:** host x86-64, GCC 13.3, -O2.
- **Trees:** base `22b97b7` vs this branch.

These are host CPU numbers for comparing the two trees. They are not tablet latencies.

| Surface | Base ms | Now ms | Δ |
|---|---|---|---|
| **All 38 surfaces (total)** | **194.4** | **123.3** | **−37%** |
| sleep-screen | 34.1 | 1.9 | −94% |
| library-notes-hub | 4.5 | 1.8 | −61% |
| library-empty | 3.1 | 1.2 | −60% |
| library-list | 4.5 | 2.2 | −52% |
| library-drawer | 7.8 | 4.1 | −47% |
| tom-handwriting-reply | 9.7 | 5.5 | −44% |
| library-grid | 5.8 | 3.4 | −42% |
| notice | 6.8 | 3.9 | −42% |
| settings (Reading / Accessibility / Tom) | 3.3 / 3.1 / 2.9 | 2.1 / 1.9 / 1.8 | −38% |
| status-panel | 6.2 | 4.2 | −32% |
| reader-chrome | 4.3 | 3.8 | −12% |
| reader-palette | 4.6 | 4.2 | −9% |
| keyboard | 5.2 | 5.5 | +5% (hint face) |
| text::fit, 240 chars | 0.068 | 0.032 | −53% |
| text::wrap, 120 words | 0.19 | 0.18 | −6% |

**Shell costs removed.** Verified by code inspection, not timed: there is no shell harness without the vendor display.

- Idle tick: one release-policy file open, read and JSON parse per tick (≈500/s) → none.
- Stroke save, undo or redo: page re-composition (two 14 MB copies, a 3.5 MB wash allocation, SHA-256 of the page snapshot, indicator decode) → a chrome repaint plus footer damage.
- Tool tap: ≈0.5 Mpx chrome damage → the changed rects (one 112 px tile, typically).
- Wi-Fi or view-sheet tap or keystroke: a 14 MB allocate and copy → a copy into a retained buffer.

**Device timings still to capture:** see §10.

## 5. UI/UX refinements

- **Before/after:** CI's `inkbound-host-previews` artifact holds every surface in default, high-contrast, plain and panel views. The preview page is linked from the PR.
- **What changed:**
  - Wi-Fi, Companion, note markers, notices, placeholders, close buttons, titles and confirmations are consistent with Inkie v2;
  - every kit target is at least 96 px;
  - no grey fill or grey text is left in the kit (the one exception: the `Disabled` token for rare disabled controls, per INKIE-DESIGN §2);
  - writing guides are dotted ink.
- **What did not change:** no layout zone moved. 446 of the shipping base's 500 targets are identical; the rest grew to the 96 px hit area or moved to the shared close position.

## 6. Architecture notes

There are no new systems. Three small mechanisms:

1. **`ux::friendly`** (`src/user_messages.h`) is the single place codes become sentences.
2. **`Shell::librarySheetOpen()`** is the single answer to "does a sheet own the screen". Library repaints and notices defer while it is true.
3. **`chromeBeforeHeld_`** and the retained `libraryBefore_` frame: tool taps and sheets diff against what is on screen and damage only changed rects. No new refresh flags, waveforms or timers.

## 7. Tests and results

| Suite | Where | Result |
|---|---|---|
| `ib-layout-tests` | CI + local | pass, 439 checks |
| `ib-surface-tests` (new) | CI + local | pass, 1,377 checks, 38 surfaces, 571 targets |
| `status-screen-tests` | CI + local | pass |
| Target baseline diff | CI | identical |
| `wifi-tests` (+13 painted checks) | CI + local (Qt 6.4) | pass, 83 checks |
| `user-messages-tests` (new) | CI + local (Qt 6.4) | pass, 35 checks |
| `companion-view-tests`, `tools-view-tests`, `library-manage-tests` | CI + local (Qt 6.4) | pass |
| `library-view`, `timezone-view`, `document-controls`, `tom-inline-panel`, `semantic-crop` | CI only (they need the vendor runtime) | see §8 |
| Route tests: library, notes, status, timezone, gesture | CI only | see §8 |
| Existing suites: sustained-session, ui-damage, eink-refresh, styled-*, tom-*, notes-foundation, semantic, selection-route, ux-behavior and others | CI, release + ASan/UBSan | see §8 |
| Tablet ARM build, target regressions, package checks | CI (`tablet-sdk`) | see §8 |

No timing or settled-state check was weakened.

## 8. Test runs

| Commit | What | Host release | ASan/UBSan | Companion | Tablet |
|---|---|---|---|---|---|
| `d89477d` | Batch A, stability | pass | pass | pass | pass |
| `d3a5e85` | Batch B, performance | **fail** (selection_activation_paints_selected_toolbar) | fail | pass | n/a |
| `5c3cb68` | Chrome-hold fix (historical report) | reported pass | reported pass | reported pass | not recorded here |
| `890eaa63` | Supplied beta handoff head | **fail**: stale exact-word expectation in `gesture-route-tests` | **fail**: same assertion | pass | [pass, run 39088152202](https://github.com/ILeggett23/Inkie/actions/runs/39088152202) |

The supplied head's [host run 39088152189](https://github.com/ILeggett23/Inkie/actions/runs/39088152189) passes the core suites but fails the newly enabled Selection-tool route assertion. The shipping Selection contract expands a drag to entire touched paragraphs/blocks. That test still expected endpoint words. Its expected full text, raw glyph quads and UTF-16 bounds now come independently from the parser's block metadata. Exact-word highlighter/finger-handle tests and no-stored-ink/highlight assertions remain intact; no timeout or settled-state check changed.

Final qualification must be read from [PR #2 checks](https://github.com/ILeggett23/Inkie/pull/2/checks) at the exact current head: host release, ASan/UBSan, Companion and ARM must all pass. An older green tablet run does not qualify a different head or waive a red host gate. Record the selected run, commit, artifact digest and independent package verification before installation. No device checklist item is claimed by CI.

### Post-handoff Notes deletion follow-up

At `4d67f963`, [host run 39098582478](https://github.com/ILeggett23/Inkie/actions/runs/39098582478) passes release, including the corrected whole-block gesture route, and Companion. Its ASan/UBSan job fails `deleted_note_returns_to_hub_without_large_note_panel`; the [ARM run 39098582414](https://github.com/ILeggett23/Inkie/actions/runs/39098582414) passes. These results do not qualify a release together.

The deletion test exposed a presentation race: the old predicate required the immediately previous View to contain the selected note. `Runtime::view()` can first report an empty selection while busy (command/handwriting ownership is still active), then the same empty selection after settling. Assigning the busy snapshot discarded the previous selection, so the later settled snapshot never returned the panel to Hub.

A small `NotePresence` latch now remembers ownership across transient snapshots. It resets on a new binding or leaving Note mode, so initial note creation is not mistaken for deletion. A settled, ready, error-free absent note returns to Hub through the existing path. Runtime storage/commit code is unchanged; its busy overlay continues to guard active commands and pending handwriting. Deterministic reducer cases cover busy-empty then settled-empty, rejected/unready snapshots, repeated emptiness and new creation/binding. The real asynchronous delete/Undo/Redo regression retains its exact Hub, no-writing and target assertions. No test deadline, settle predicate or durability guard changed.

### Device report follow-up: staged transitions, erase residue, Select toolbar and strip taps

On-device report (photo of the library after closing the drawer, plus three described issues). Root causes and fixes,
in `reader/src/ux_display.h` and `reader/src/ux_shell.*`. No vendor refresh authority, waveform, flag or timer changed.

| # | Issue | Root cause | Fix | Verified |
|---|---|---|---|---|
| D1 | A screen change finishes in stages: half the old view stays for about a second (photo: the drawer's Wi-Fi, Trash and Settings stay over the library) | `Display::live()` cut *every* update into quarter-page bands. Only pen-mode updates have that vendor bound (`submitChecked` refuses `ModePen` above a quarter page); quality UI updates do not. The 474 px drawer went out as rows 0–1845 and 1845–2160, two consecutive colour updates. Page turns (Regional scene) went out as up to eight tile regions, and library repaints as separate row bands. | Only pen-mode work is banded. A Regional scene is one region; a library repaint, a sheet's own diff and the document menu with its slot are each one region. | [auto] `library_drawer_presents_as_one_update`, `page_turn_presents_as_one_region`. Optics [device] |
| D2 | Colour residue after a view closes or ink is erased | A quality update took its content class from the new pixels only. The one global `eraseContent_` covered some erase paths, but not deferred UI, undo or a cleanup whose old pixels were already composed, so those went out Mono over colour. | Per 60 px cell, the display records the class still owed by fast updates. Every quality update uses the existing `cleanRemoval()` with the richer of the owed class, the class shown before compose, and the new pixels' class. | [auto] drawer close asserts class 3; existing erase-class tests. Optics [device] |
| D3 | Erased ink leaves shadows until a complete refresh, inconsistently | `expectRemoval()` flagged the next styled result for regional cleanup, but any result cleared the flag, including a status-only one (saving, Undo availability) that can arrive first. The removed strokes beyond the eraser's own path then had no cleanup. | The flag holds until the removal's damage arrives, or until the ink owner goes idle without drawing. | [inspect]: the interleaving is timing-dependent, and existing erase cleanup tests still pass. Optics [device] |
| D4 | With Select on and the toolbar collapsed, the handle does not reopen the toolbar until something is selected | In `finger()`, the Select-mode branch claimed every tap above the footer as a word selection before the collapsed-handle check. | The collapsed handle is checked first (a selection showing is kept). | [auto] `collapsed_handle_reopens_toolbar_in_select_mode` (taps the painted handle, as on the device) |
| D5 | Strip or card commands (Highlight, Note, Tom, Copy) do not respond | (a) Without the Selection tool, a Marker contact on the strip or definition card was ink. (b) Notes, Highlight and Tom were refused silently while ink was saving. Finger and Selection-tool Marker taps reach the commands in every test; no device-only drop was found in the code. | (a) While a selection or definition is up, a Marker contact starting on it is a tap (command after pen-up, no ink); elsewhere it is ordinary ink. (b) A refused open waits and runs once saving settles (same selection). Every command's outcome is journaled as `ui:selection-action` = id×10 + 0 refused / 1 done / 2 waiting. | [auto] `definition_card_accepts_pen_without_selection_tool`; existing `selection_options_accept_pen_*`. Device: journal check 18 below |

### Device report follow-up 2: strip commands, erase ghosting, Tom guides

From the 2026-10-18 tablet session on the `2ae400e` build (journal read with the user's approval). The journal shows
Tom (id 121) running 3 times and Note (120) twice, one Note refused with "Finishing the previous panel". Highlight (122) and
Copy (123) never ran. Each Note opened at once in state, but its first screen update went out only after the next tap
(…023.437 → …024.403, …031.092 → …032.161); the Marker-tapped Tom waited 2.2 s the same way.

| # | Issue | Root cause | Fix | Verified |
|---|---|---|---|---|
| D6 | Strip commands look ignored; a second tap lands on the panel | An opened Note or Tom panel is not settled until its helper binds (1–3 s). `RefreshController::feedback` treats an unsettled scene with no contact as deferred and drops the plan, so nothing was shown until the binding finished, and the user tapped again first. | A done strip command, and every `openNotes`/`openTom`, marks the change `preview`: the panel (or notice, or dismissed strip) is presented by the next boundary. A refused open while the previous panel is finishing now waits (`pendingSelectionAction_`) instead of "try again in a moment". | [auto] `strip_note_presents_before_its_helper_binds`. Optics [device] |
| D7 | Taps just off the strip or card do nothing (finger and Marker) | Only exact hits counted; a tap a few px past the edge dismissed the selection, and a Marker tap had to lift within 25 px. | A tap within `StripSlop` (24 px) of the strip or card reaches the nearest command (`Targets::nearest`), for the finger and the Marker; a Marker tap may travel 40 px. `ui:semantic-tap` journals every strip tap (id, 0 elsewhere) and `ui:semantic-pen-dropped` a Marker tap that did not run. | [auto] `strip_tap_near_edge_reaches_command`; existing `selection_options_accept_pen_*` |
| D8 | Erased colour and black ink leave ghosts; no visible local cleanup | Inkie's erase cleanup waited until the stroke was saved (0.5–2 s). The `1c67668` attempt (a fast dark update, then a quality redraw) left a lasting dark box on the device: fast and quality updates to the same area are not applied in submission order, so it was removed. | As stock does (Xochitl three-dot erase pilot, `docs/refresh-reverse-engineering/STOCK_REFRESH_TRACE.md`: local fast feedback, then one regional quality update over the erased stroke's bounds in the same millisecond the eraser lifts), the eraser's area gets one regional quality update on the first boundary after it lifts (`liftCleanup_`), keeping the removed class. Strokes removed beyond the path keep the existing cleanup after saving. `ui:erase-lift-cleanup` journals each. | [auto] `erase_cleanup_runs_when_the_eraser_lifts`, `erase_cleanup_covers_every_live_erase_submission`. Whether Inkie's public UI-mode request reaches the same lower waveform as stock's 2/16 is unmeasured: compare with the read-only queue sampler. Optics [device] |
| D9 | Dotted guides in Tom's answer | `ruledPaper` was drawn under a native reply. | Guides only where the reader writes to Tom. | [inspect]; device check 21 |

Device checks 19–21 below.

## 9. Remaining risks and proposals (protected or policy code: not changed)

| # | Area | Risk | Proposal |
|---|---|---|---|
| P-R1 | `ux_display.h` / `vendor.cpp` (refresh policy) | `qualityContent` scans the aligned footprint and exits early on colour; it is not a single-pixel classifier. Classification may repeat across retention and submission (now also once before compose, to keep the removed class). Quality updates are no longer banded (D1); pen-mode updates still are. Some overflow paths union debt, while `accumulateDamage` merges the least-cost pair. | Measure the specific repeated work, banding and overflow path on device before proposing a protected change. Sparse sampling is not equivalent to the current colour-preservation guarantee and is not approved here. |
| P-R2 | Live ink | Live-ink damage is unioned with stale damage, so a stroke can trigger a larger refresh. | Keep live-ink damage separate. Needs the vendor-authority owner. |
| P-R3 | Lock screen | A wrong passcode triggers a `FullRefreshReason::Security` flash. | A regional refresh for the error line. The full refresh stays for unlock. |
| P-R4 | Session loop | The 2 ms loop plus 1–2 ms worker polls make ≈6–8 k wakeups/s idle. | Event-driven waits. Measure power on device first. |
| P-R5 | `styled_ink` | 45.8 MB raster per stroke. Resident memory ≈350–500 MB. | Tile the raster. Protected. |
| S-R1 | `reader.cpp` | `errors_[page]` is never cleared after a failed prefetch, so a transient error sticks. | Clear on a successful re-render. |
| S-R2 | Passcode | A corrupt `ui-passcode.json` disables the lock. `lockFailures_` is held only in memory, so a restart resets the back-off. | Fail closed on corruption. Persist the failure count. Storage format, so it needs approval. |
| R1 | Route tests | The five route tests are new to CI. A failure that only appears under CI timing would show here first. | Fix the root cause. Never loosen the timing. |
| R2 | Colour rendering | Gallery 3 rendering of the dotted guides and hollow Wi-Fi bars is untested on the panel. | Device checklist items 6–7. |
| R3 | Dead handlers | `SnapHighlights` action, the reading-light menu, `MenuLayers`/`MenuLibrary`, `wifiSelected_`. | A cleanup PR with no behaviour change. |

## 10. Physical-device validation checklist

Run on a Paper Pro with the `verified-tablet-candidate` from §8. Capture the device journal (`e2` markers).

| # | Check | Pass when | Markers |
|---|---|---|---|
| 1 | S1: open a book, lock (power button), touch the Marker to the passcode screen, unlock | No session end; the book is still open | no `reader:shell-fail`, no `reader:end-exception` |
| 2 | Write 20 strokes, undo 5, redo 5 | Only the footer and the toolbar tile flash, never the page | `styled:batch-us`, `reader:scene-regions` |
| 3 | Tap each tool tile, then Select | Only the tapped tiles refresh; the selected tile shows immediately | `ui:tool-command-refused` absent |
| 4 | Turn a page and tap close mid-turn | Returns to the library after the page lands | `ui:nav-satisfied` then `ui:document-closed` |
| 5 | Ask Tom, close Tom, force a worker failure (Wi-Fi off) | Library or book stays correct; no library over a book | `tom:*`, no `reader:shell-fail` |
| 6 | Wi-Fi sheet at four signal strengths, plus a WEP/enterprise network | Bars match strength; hollow bars visible; unsupported rows dotted with no bars | — |
| 7 | Note and Tom writing areas | Dotted guides visible but quiet; no colour fringing; handwriting unaffected | — |
| 8 | Low-battery notice while the notebook sheet is open | The sheet stays intact; the notice appears after it closes | `ui:notice-set` |
| 9 | Companion: approve while a transfer runs | The tap is not lost; repeating it works | — |
| 10 | Keyboard: type in Notes while saving | Text survives a refused send | `ui:tool-command-refused` |
| 11 | Every control with a fingertip, both hands | Every target hits first time | — |
| 12 | Sleep and wake ×10 with a book open | Sleep screen presents; scene restores | `power:sleep-screen-presented`, `power:scene-restored` |
| 13 | Idle 30 min, book open | No growth in wakeups or memory | `reader:doc-tick-us`, `reader:slow-tick-us` |
| 14 | Pen-down to ink | No regression from `22b97b7` | `reader:page-first-ink-us`, `styled:draw-us` |
| 15 | Open and close the library drawer; switch library tabs and Settings panes; open and close the document menu and settings sheet | Each change completes in one step, with no part of the old view left over and no coloured tab residue | `reader:live-banded` absent for UI changes |
| 16 | Turn ten pages in each direction | Each page replaces the previous in one step | `reader:scene-regions` |
| 17 | Draw colour and black strokes; erase them (Marker eraser and toolbar eraser), then undo and redo | The erased area clears regionally, without shadows or colour residue, and without a complete refresh | `ui:cleanup-requested`, `ui:retirement-quality-submit` |
| 18 | Select on, collapse the toolbar, tap the handle; select text; tap Highlight, Note, Tom and Copy with a finger and with the Marker; long-press a word and use its card with the Marker | The handle reopens the toolbar; every command runs; no ink is left on the strip or card | `ui:selection-action` (…1 done) for each; any …0 or …2 names the refusal |
| 19 | Select a passage; tap Note, Tom, Highlight and Copy once each, with a finger and with the Marker, also a few px below the strip | Each responds on the first tap: the panel or notice appears at once, no second tap needed | `ui:semantic-tap` (id) then `ui:selection-action` …1 |
| 20 | Draw colour and black strokes; erase them with the Marker eraser and the toolbar eraser | As the eraser lifts, the erased area refreshes locally and comes back clean with no ghost; no dark box, no full-screen flash | `ui:erase-lift-cleanup` |
| 21 | Ask Tom; read the answer | No dotted guides under the answer; the writing area keeps them | — |

## 11. Beta-readiness recommendation

**Go for a closed beta**, on these conditions:

1. §8 is green on the final head.
2. Checklist items 1–5 and 12–14 pass on a device.
3. The P-R1 to P-R3 refresh proposals are decided by the refresh owner, with device measurements before any change.

**Before a wider beta:**
- S-R2: the passcode should fail closed;
- P-R4/P-R5 power and memory measurements;
- the dead-handler cleanup.
