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
| D8 | Erased colour and black ink leave a yellow tint | Every cleanup went out in UI mode. The vendor plugin (libqsgepaper `3800e9f3…`, `swapBuffers_impl` 0x35900–0x35f60, read statically) splits a regional swap by screen mode: below its temperature threshold, Content regions get the content waveform class (2, auto −2) and UI regions the lighter UI class (5, auto −1), each PixelMode 15, flags 0. Content class does not change the waveform. Stock cleans erased ink with lower waveform 2 over the erased bounds as the eraser lifts (`STOCK_REFRESH_TRACE.md`); UI-mode cleanup left the pigment. Two earlier attempts failed on the device: waiting until saved (`2ae400e`), and a fast dark update before the quality one (`1c67668`: the dark box stayed, since fast and quality updates are not applied in order). | Removal cleanups use `VendorDisplay::cleanContent` (Content mode, regional, flags 0, removed class kept): the eraser's area when it lifts (`liftCleanup_`, `ui:erase-lift-cleanup`), and erase results, undo/redo and highlight/layer removals once settled (`removal_`, `ui:removal-content-cleanup`). UI updates stay in UI mode. Content mode was unused since a complete Content-mode page swap blocked (run 13, later traced to the maintenance lock); it stays regional only, at the user's approval of this protected change. | [auto] `erase_cleanup_runs_when_the_eraser_lifts` (Content mode, class 3), `ui-damage-tests` (`cleanContent`). Optics and idle safety [device] |
| D10 | Coloured ink: half of a stroke stays in fast-mode colour | Ink got a quality update only through the adaptive counter: a 5×6 grid cell (324×360 px) is cleaned after 96 fast updates. A stroke across two cells came out half refreshed. | As stock does after red strokes (local quality 2/16, 5/16 after fast 1/14), each coloured stroke (not black; any highlighter) gets one local quality update once the pen is up and ink settled (`strokeArea_`, `inkPlan_`). Black ink is unchanged. | [inspect]; device check 22 |
| D11 | Erasing a snapped highlight leaves its yellow (5dbf477) | A removal's own damage (the saved erase result) went out as a fast update in the same boundary as its Content-mode cleanup, and the eraser-lift cleanup went out before that boundary's fast plan. Fast and quality updates are not applied in submission order, so the fast one could land last and leave the pigment. The display already kept UI-cleanup areas out of the fast plan, but not removals. | The eraser-lift area and settled removals are presented together in one step (`removalNow`), and their areas are taken out of that boundary's fast plan; the Content-mode cleanup alone draws them. | [auto] `erase_cleanup_runs_when_the_eraser_lifts`: a quality update is the last one over every area a fast update touched. Optics [device] |
| D9 | Dotted guides in Tom's answer | `ruledPaper` was drawn under a native reply. | Guides only where the reader writes to Tom. | [inspect]; device check 21 |

Device checks 19–21 below.

### Device report follow-up 3: Note from a selection, highlight residue, failed pages

From the 2026-10-19 tablet session on `56d39cb` (journal read with the user's approval): the strip tap ran Note
(`ui:selection-action` 1201 at 00:17:00); the Notes runtime then showed no progress, the sheet read "Saving…" and took no
ink.

| # | Issue | Root cause | Fix | Verified |
|---|---|---|---|---|
| D12 | Note from a selection opens frozen, "Saving…", no ink | (a) `Panel::open` bound, then queued `native.create`, then `native.window`: three commands, each re-reading the shared store and `create` writing it twice, with the canvas off until all three ran. (b) The Notes worker ran at nice 19 and lost the CPU to page rendering right after a book opened (page helpers run at nice 10; one store write is ~0.6 s at normal priority, `styled:batch-write-us`). (c) A refused first command left an open sheet with no binding. (d) "Saving…" was drawn whenever no note existed yet. (e) Page ink results were not taken while Notes was open, so the ink stayed "busy" and nothing counted as settled. | One `native.bind` with `create` and `window` creates the note in one store session; the canvas takes ink at once (`creating_`), and the worker runs a queued command before ink while no note is bound; early ink with no note (create failed) is dropped with a message, never failing the session. Worker at the page-ink priority. A refused open closes the sheet. Status "New note — write any time" / "Opening…". Page ink results are taken under the sheet and presented when it closes. `NOTES_COMMAND op=… ms=…` and `notes:command-ms` journal every command. A failed create removes the highlight it added (no orphan). | [auto] `selection_note_takes_ink_at_once`. Timing [device] |
| D13 | Erasing a highlight still leaves yellow | A coloured stroke's own refresh (UI mode) and the erase of that stroke (Content mode) could go out in the same pass; only the fast plan was cut, so the UI-mode update could land last. | Removed areas also leave the UI quality set; if they cannot be cut out, that debt is cleaned with the removal. | [auto] `erased_fresh_highlighter_ends_with_removal_cleanup`. Optics [device] |
| D14 | A page that failed to render once stays refused (report S-R1) | `errors_[page]` was never cleared. | An explicit turn clears it and retries; prefetch still skips the page. | [inspect] |

Not changed, pending the user's decision: erasing a freehand highlighter stroke whole (it is cut along the eraser path in
the protected `styled_ink.cpp`).

### Temperature warning (user request, 2026-10-19)

Xochitl's `tempindicator` module (strings and disassembly of the local stock copy): a status-bar temperature indicator,
a quick-settings entry, `TempWarningPopupWindow` and a configured `warningDegrees` (read from module configuration with a
code default of 0; the configured value is not in a plain file on this tablet). Inkie now:

- reads the panel sensor (`rm_hwmon_temp`, the one the display driver uses), the CPU and the battery gauge
  (`device::readTemperature`), every 60 s, every 10 s while warm;
- calls the tablet too warm when the gauge reports `Overheat`/`Hot` or the panel reads 40 °C or more, and clears it at
  37 °C or below (`device::HeatWatch`; 40/37 are Inkie's choice, not a recovered stock value);
- shows a red-bulb thermometer in the status bar (also the reader's clock-only bar), says how warm it is in quick
  settings, and shows a notice once per warm episode; `ui:temperature-warning` / `ui:temperature-cleared` journal it.

[auto] `status-screen-tests` (sysfs fixtures, hysteresis edges, the bulb only while warm). Device check 23.

### Inkie memory, speed and reliability (user request, 2026-10-19)

From a recorded device question (journal, 2ae400e): Send → Inkie knew what was written after 16.3 s, first words 2.4 s
later, done 1.6 s after that. The reading window covers the helper start, a config re-check, the create POST, the queue,
the provider reading the handwriting and the event stream.

| # | Change | Where | Verified |
|---|---|---|---|
| M1 | Whole conversation: every completed turn is sent, tagged with where it was asked (a page change no longer drops history); a new conversation starts with the last two exchanges of the most recent ones; transcription carries none | `tom_store.cpp` `history()` | [auto] `tom-store-tests` |
| M2 | Long-term notes: `<library>/inkie/memory.json` (60 notes, 240 chars, 4096 total); a `memory` event once per request before the final status; sent with each question and part of consent; Settings → Inkie shows, forgets and clears them | `tom_store.*`, `tom_stream.cpp`, `companion_client.*`, `tom_runtime.cpp`, `ib/settings_screen.*`, `library_view.*`, `ux_shell.cpp`; Companion `memory.mjs`, `worker.mjs` | [auto] store, client, session, runtime, library route, `tom_memory.test.mjs` |
| S1 | A Send during the 5-minute connection check waits for it instead of being refused | `tom_runtime.cpp` | [inspect] (the check runs every 5 min; not reachable in a host test) |
| S2 | One TLS connection per helper run (config, create, status, stream) instead of a handshake each | `companion_http.*` | [auto] existing HTTP/receiver suites |
| S3 | Provider 429/5xx and network errors before anything streams are retried twice (Retry-After up to 5 s); used-up quota and client errors are not; partial streams are never retried | Companion `worker.mjs` | [auto] `tom_reliability.test.mjs` |
| S4 | Reading handwriting asks the model for low reasoning effort (as drawing requests already do); answers unchanged | Companion `worker.mjs` | [auto]; timing [device] |
| S5 | Drawings use flat colour where the subject naturally has it or a textbook convention exists; diagrams stay ink | `picture_style.inc`, Companion `picture_plan.mjs` (byte-identical; parity fixtures regenerated) | [auto] picture parity |
| S6 | `TOM_DIAGNOSTICS=true` logs reading time and first-text time per request | Companion `worker.mjs` | [inspect] |

Not changed (proposed, needs measurement first): reasoning effort for typed answers (pinned by
`composition_plan.test.mjs` as "legacy requests retain their payload"), raising `TOM_MAX_OUTPUT_TOKENS` from 800, one
model call for read-and-answer, a long-lived helper across requests.

### Landscape, PR 1 (user request, 2026-10-19; docs/LANDSCAPE.md)

Native, staged landscape as the user chose: an open PDF's reading view turns with the tablet when Auto-rotate (quick
settings) is on. The orientation is a presentation parameter of page planning and caching, never a stored view setting.
That change to the protected prepared-page storage was approved. Turned pages live in `<document>/turn-<o>/`, carry
`orientation` in their identity, and are re-planned and checked by the loader. Ink stays in source coordinates. The
chrome is laid out from the landscape zones and painted upright, with targets kept in panel pixels. Selection, sheets,
menus and the palette are portrait only for now (a notice). EPUB reflow is PR 2. [auto] status-screen, ib-layout,
pdf-viewport, prepare-document-view, touch-drag and the `pdf_turns_with_the_tablet_and_keeps_ink_written_turned`
route. Device checks 29–33; the first run also confirms the accelerometer's axis signs (`ui:orientation`).

### Device report follow-up 4: erasing leaves yellow (historical approach; reverted by PR13)

The user: erasing black or coloured ink with the Marker's eraser end or the toolbar eraser leaves a yellow ghost, while
Undo (two-finger double tap) removes the same ink completely.

| # | Issue | Root cause | Fix | Verified |
|---|---|---|---|---|
| D15 | Erased black and coloured ink leaves yellow; Undo of the same ink leaves none | Undo's removal is one Content-mode update from the ink on the panel to the page beneath. The eraser first showed each sample with the vendor's masked fast erase (pen mode, flags 2), driving the erased pixels to white in the pen waveform. That leaves Gallery 3's yellow pigment (black ink is made of the colour pigments too), and the panel then held those pixels as white: the Content-mode cleanup at lift (D8, D11) went from white to white, had nothing to drive, and the yellow stayed. | Erasing never sends a fast update. Each eraser sample is composed only; while the eraser moves, what it has erased is removed with the same Content-mode update as Undo, at most every 100 ms (`eraseLive_`, `liveRemoval`). The rest goes with the lift cleanup, which covers all of it (`ui:erase-live-removals` counts the live removals). Both erasers, in the page, Notes and Inkie. The vendor's masked erase is left in place, unused by the reader. | [auto] `eraser_removes_ink_like_undo`: black ink (Marker's eraser end) and red ink (toolbar eraser); a Content-mode removal goes out while the eraser moves; no fast update touches an erased ink pixel; every erased pixel is covered by a Content-mode update; the page beneath shows. `onlyRegional` now refuses the masked erase anywhere. Optics [device], check 38 |

**D15 reverted (2026-10-20).** On the tablet, erasing with Content-mode removals while the eraser moved left parts
of the ink unerased or marked (user report). The eraser is back to stock's live erase (the masked fast erase, lower 1/14
as in the 2026-10-14 stock erase pilot) with the Content-mode cleanup when it lifts. The yellow residue that D15 aimed
at is open again; the next step is a visual test of stock Xochitl erasing black, coloured and highlighter ink, then
Inkie variants compared against it.

### Device report follow-up 5: every tap takes seconds with Auto-rotate on (user report, 2026-10-19)

The user, on `fbb431d`: "It takes a couple of seconds after clicking anything instead of it being instant." The
journal (read with the user's approval) shows the session from 17:32 to 17:34 UTC: 150 shell ticks of 0.75–0.77 s each,
all while Auto-rotate was on (`ui:auto-rotate` 1), none in the 5 s it was off. The earlier landscape build never had
Auto-rotate turned on, so it felt instant.

| # | Issue | Root cause | Fix | Verified |
|---|---|---|---|---|
| D16 | With Auto-rotate on, every tap and screen change waits up to about 0.75 s, and several in a row add up to seconds | `Shell::pollOrientation` read the accelerometer on the shell's own thread every 300 ms. On the Paper Pro one reading of the three raw axes takes about 0.75 s (`reader:slow-tick-us` 754 000–771 000), so most ticks blocked on it. | `device::AccelReader` reads on its own thread, one reading per 300 ms at most. The shell takes the newest reading without waiting. Off, the reader idles. `ui:orientation-read-ms` journals the first reading's duration. The watch is unchanged (three settled readings to turn). | [auto] `status-screen-tests`: `take()` and `run()` return at once while a 400 ms reading is under way, each reading is taken once, a paused reader reads nothing more, and fixture readings arrive through the thread. Also run locally under ThreadSanitizer and ASan/UBSan. The `pdf_turns_with_the_tablet…` route turns through the reader. Device check 39 |

Turning takes about as long as before (three readings, each about 0.75 s on the tablet, so about 2 s), but nothing
waits for it.

### Landscape, PR 3 (user request, 2026-10-19; docs/LANDSCAPE.md)

Notes and Inkie open upright in landscape. Sheets keep composing in their own portrait pixels; `ib::layout::SheetMap`
shows their lower 1620 rows at the foot of the turned screen, so the writing areas keep their size and stored ink never
moves. The taller sheets (Inkie's answer sheet and tall composer, the expanded note, the Notes hub, Layers) start at the
top of the landscape screen, and Inkie's zoom badge moves into its header. Compose, damage, coverage, taps, drags and
gestures go through the same map. The pen's fast path writes in sheet pixels through an approved change to the protected
input path: an optional turn and offset in `spike::Transform`, set through `SessionDisplay::inputFrame` whenever the
session builds an Engine. [auto] sustained-session (`INPUT_FRAME_PASS`), ib-layout, ib-surface and ib-preview targets,
and the `turned_note_sheet_…`/`turned_inkie_sheet_…` routes. Device checks 40–44.

## 9. Remaining risks and proposals (protected or policy code: not changed)

| # | Area | Risk | Proposal |
|---|---|---|---|
| P-R1 | `ux_display.h` / `vendor.cpp` (refresh policy) | `qualityContent` scans the aligned footprint and exits early on colour; it is not a single-pixel classifier. Classification may repeat across retention and submission (now also once before compose, to keep the removed class). Quality updates are no longer banded (D1); pen-mode updates still are. Some overflow paths union debt, while `accumulateDamage` merges the least-cost pair. | Measure the specific repeated work, banding and overflow path on device before proposing a protected change. Sparse sampling is not equivalent to the current colour-preservation guarantee and is not approved here. |
| P-R2 | Live ink | Live-ink damage is unioned with stale damage, so a stroke can trigger a larger refresh. | Keep live-ink damage separate. Needs the vendor-authority owner. |
| P-R3 | Lock screen | A wrong passcode triggers a `FullRefreshReason::Security` flash. | A regional refresh for the error line. The full refresh stays for unlock. |
| P-R4 | Session loop | The 2 ms loop plus 1–2 ms worker polls make ≈6–8 k wakeups/s idle. | Event-driven waits. Measure power on device first. |
| P-R5 | `styled_ink` | 45.8 MB raster per stroke. Resident memory ≈350–500 MB. | Tile the raster. Protected. |
| S-R1 | `reader.cpp` | Fixed (D14): an explicit turn retries a failed page. | — |
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
| 20 | Draw colour and black strokes; erase them with the Marker eraser and the toolbar eraser; undo and redo | As the eraser lifts, only the erased area refreshes and comes back clean with no yellow tint; no dark box, no full-screen flash; the app never stalls | `ui:erase-lift-cleanup`, `ui:removal-content-cleanup` |
| 21 | Ask Tom; read the answer | No dotted guides under the answer; the writing area keeps them | — |
| 22 | Write a word in red across the middle of the page, then pause | The whole word settles to full colour, not half; black writing never flashes | `ui:retirement-quality-submit` |
| 23 | Warm the tablet (a warm room, or a hair dryer at a distance), then let it cool | A notice and a red-bulb thermometer appear at 40 °C (panel sensor) or when the battery reports overheating; quick settings says how warm; both clear below 37 °C | `ui:temperature-warning`, `ui:temperature-cleared` |
| 24 | Ask Inkie something on one page, turn several pages, ask "what did I ask you before?" | Inkie answers from the earlier turn | `tom:first-text` |
| 25 | Tell Inkie "My name is Sam and I like short answers"; start a new conversation; ask "what's my name?" | Inkie says Sam; Settings → Inkie lists the notes | — |
| 26 | Settings → Inkie: Forget one note; Clear all (tap twice) | The note goes; Clear asks once more, then the list is empty | `tom:memory-forget`, `tom:memory-clear` |
| 27 | Write a question by hand and Send; time it | Shorter than the 16 s reading time recorded before (needs the new Companion deployed) | `tom:read-answer-start` → `tom:ocr-end` |
| 28 | Ask for a drawing of a red apple and of a labelled diagram | The apple is coloured; the diagram stays ink | — |
| 29 | Quick settings: turn Auto-rotate on; open a PDF; turn the tablet clockwise, then anticlockwise | The page turns upright and sharp after the tilt settles; toolbar, close, footer and clock sit on the landscape edges | `ui:orientation` (raw angle), `ui:turn`, `ui:turned` |
| 30 | In landscape: write and erase, then turn back upright | The ink is where it was written, in both orientations | — |
| 31 | In landscape: swipe left and right; tap the footer | Pages turn in the reading direction; the footer shows the toolbar | `reader:gesture` |
| 32 | In landscape: long-press a word, tap the palette's active pen, swipe down from the top | "Turn the tablet upright for this"; nothing opens sideways | `ui:turn-portrait-only` |
| 33 | Turn Auto-rotate off; open the library while turned | Nothing turns while Auto-rotate is off; the library stays portrait | — |
| 34 | In landscape: open the pen palette, pick a colour and width; open the document menu; open quick settings from the clock | Each opens upright beside the landscape toolbar or at the top right, and acts | — |
| 35 | In landscape: long-press a word; select a passage and highlight it | The definition card and the selection strip are upright on the page; the highlight lands on the text | — |
| 36 | Open an EPUB with Auto-rotate on and turn the tablet; read a few pages; turn back | Longer lines at the same type size; the same passage after each turn | `ui:turn`, `ui:turned` |
| 37 | Turn an EPUB, read on, then close it and reopen it upright | The book reopens where you stopped reading | — |
| 38 | Write black and red ink; erase part of each with the Marker's eraser end, then with the toolbar eraser; erase a long scribble | Ink disappears under the eraser as it moves and nothing is left unerased (D15 reverted; residue colour compared with stock) | `ui:erase-live-removals`, `ui:erase-lift-cleanup`, `ui:removal-content-cleanup` |
| 39 | Turn Auto-rotate on in quick settings; tap the toolbar, menus and page turns; then turn the tablet | Taps respond at once, as with Auto-rotate off; the page turns about 2 s after the tilt settles | `ui:orientation-read-ms`, no `reader:slow-tick-us` near 750 000 |

| 40 | In landscape: open Document notes and Ask Inkie from the document menu, and Layers and Inkie from the toolbar | Each opens upright at the foot of the screen (the tall ones fill its height) with the page beside it; its controls act where drawn; a tap on the page beside it closes it | — |
| 41 | In landscape: write a note, expand it, write more, type a title with the keyboard; tap Done; turn upright and reopen the note | The ink is where it was written on the note's paper, in both orientations; nothing moves | — |
| 42 | In landscape: write a question to Inkie and Send; read the answer; pinch the reply, pan it, tap the zoom badge | The answer sheet fills the height with "You asked: …" in its header; the reply zooms and pans; the badge in the header resets it | `tom:response-visible` |
| 43 | In landscape, in a note: erase with the pen's eraser and with the note's eraser; lasso some ink and move it | Erased ink leaves no residue (one cleanup as the eraser lifts); the lasso and the moved ink follow the pen | `ui:erase-lift-cleanup` |
| 44 | In a turned sheet: write fast across the writing area's edge and beside the sheet | The ink appears under the pen tip at once, with no offset; nothing is drawn beside the sheet or outside the paper | — |

## 11. Beta-readiness recommendation

**Go for a closed beta**, on these conditions:

1. §8 is green on the final head.
2. Checklist items 1–5 and 12–14 pass on a device.
3. The P-R1 to P-R3 refresh proposals are decided by the refresh owner, with device measurements before any change.

**Before a wider beta:**
- S-R2: the passcode should fail closed;
- P-R4/P-R5 power and memory measurements;
- the dead-handler cleanup.

## Reader standard follow-up — 2026-10-20

See [READER-ERASING-STANDARD.md](READER-ERASING-STANDARD.md) for the exact installed-PR11 finding, current masked-erase standard, removed-class retention through final cleanup, intentional retirement of standalone notebook creation, owner-approved integration of landscape Notes/assistant support, and reference toolbar vectors. Earlier D15 remains reverted; it is not the standard erase path. Stock red/highlighter late tint reports remain unresolved optical evidence, not a clean-colour guarantee.
