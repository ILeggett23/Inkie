# Stock versus Inkbound

**Matched stock/Inkbound pilots are available for toolbar, palette and erase actions.** The comparisons distinguish lower queue observations, source-backed mechanisms and remaining missing measurements. They are not complete call traces or calibrated optical qualification. [Matched summary](evidence/20261014/matched-pilot-summary.json).

| Matched action | Stock lower observations / operator | Inkbound lower observations / operator |
|---|---|---|
| Pen palette3 cycles | local2/16 and5/16; no reported whole-screen flash | eight full-panel2/15 records in trial; reported whole-screen flashes |
| Erase3 dots | predominantly local1/14, then quality/restoration; no reported whole-screen flash |335 fast1/13 records plus two full-panel2/15 records; reported whole-screen flash |
| Toolbar3 cycles | UI-quality local partitions; only toolbar area visibly changed |22 fast1/13 records; controls/ghosted outlines reported; subsequent sampled source columns are fully white |

The first source-backed discrepancy for the palette is `applyRefresh(Cleanup) -> VendorDisplay::clean -> flags1`; the first geometric fullscreen expansion is the vendor wrapper. For toolbar remnants, current UIUpdate uses fast-ink mechanics rather than a qualified local UI/quality path. Actual stock public flags/content/mode and original Qt regions remain unobserved; the static renderer uses flags0 at its ordinary callsite.

| Equivalent action | Xochitl | Inkbound source behavior | First known escalation / issue class |
|---|---|---|---|
| live dot/stroke | preliminary4×4/8×6 lower rectangles, raw1/3, operator reports no whole-screen flash; public args/rate UNKNOWN | incremental segment bounding union; 8 ms nominal scheduling; Mono0/Pen0/flags0 | no demonstrated unconditional full promotion; D/E if color semantics required |
| open/close document menu | user reports local; no captured parameters | manual damage of menu + chrome; old/new menu scene enters `uiCleanup`; Cleanup sends flags1 | Cleanup mapping F/H, then vendor full-image promotion G |
| open/close pen tool palette | matched3 cycles: local quality classes2/16,5/16; no reported whole-screen flash;25-cycle stock run also no reported full flash | tools damage + last-submitted old/new palette retirement; local cleanup uses flags1; matched run includes8 full quality records and reported flashes | F/H/G |
| dismiss selection/dictionary/status/palette | UNKNOWN | `takeChange` old/new regions -> quality queue -> `clean` | F/H/G |
| erase/undo/remove highlight or layer | erase pilot: local60–70px rectangles, mostly raw1/14; operator reports local clearing/no whole-screen flash; other removals UNKNOWN | changed-pixel composition/live feedback then settled removal cleanup | F/H/G; actual damage growth depends on affected strokes |
| note open/close/reset | UNKNOWN | `openNotes` / `notesTick` explicitly set `change_.full` | application full request I/H before vendor |
| view-control interaction / dismiss | UNKNOWN | several `finger` branches set full even when `viewPaint` computed regional differences | application full request I/H |
| PDF/EPUB page transition | stock PDF6 turns: large quality2/16 regions, no reported whole-screen flash; EPUB one confirmed captured next/back cycle likewise; document open/close UNKNOWN | full composition + `readerPage` with flags1; equivalent Inkbound reader turns not captured | intentional full request; exact parity UNKNOWN |
| several disjoint small changes | UNKNOWN | independent planner regions until shell-extra capacity / pixel-diff merge / fallback union | C/B when limits lead to broad/full damage |
| color/highlight | three red strokes: incremental1/14 plus quality work; red appeared after local update/no reported whole-screen flash; highlighting UNKNOWN | colored RGB pixels can be composed, but every current vendor refresh uses Mono0 | classification D, effective mode E; Inkbound color unmeasured |
| generated-content repeated partials | UNKNOWN | 5×6 cell counters; default 96 generated updates queues local cleanup -> flags1 | H/F/G; no stock counter equivalence |

## Demonstrated parameter divergence inside Inkbound

For a supplied 100×50 region, actual policy execution with a fake vendor produces `(Mono0, Pen0, flags0)` for ordinary UI/content and `(Mono0, UI3, flags1)` for cleanup. The saved vendor changes the latter into a whole-image region. All three repetitions agree. This isolates the error even without a stock trace: quality selection currently changes spatial scope.

The standalone plugin's saved renderer supplies flags0. Freshly inspected Xochitl has a different static implementation: analyst-identified region wrapper0x9bbee0 accepts region/mode-map/flags dataflow, and an ordinary renderer call at0x9c2ce4 sets w3=0 immediately before calling it. Its full-bit branch at0x9bbf68 also constructs a full-image region. This is **stock executable static evidence**, not a live capture of an action's public arguments. Reusing the plugin's offsets/signature for stock would be incorrect.

## A–I classification audit

| Class | Finding |
|---|---|
| A: Qt/QML damage | Current-reader path has no QML scene. Stock and old AppLoad damage remain unmeasured. |
| B: expansion | Explicit shell >quarter-area / >8-extra escalation; vendor bit0 full-image expansion. |
| C: merging | Engine bounding union; fixed output pixel-diff merging; cleanup union fallback. Harmful runtime incidence UNKNOWN. |
| D: classification | All current reader calls force Mono, including color-capable pixels. |
| E: mode | ordinary UI/content use Pen0; qualified local-quality mode UNKNOWN. |
| F: flag | `QualityRegion` sends full-image flag bit0. Confirmed mismatch. |
| G: backend promotion | region overload full bit/private forced state; conditional Acep2 modes. |
| H: cleanup | cleanup on menu/selection/palette/status retirement and removals; adaptive counter only for generated content. |
| I: other | explicit whole-scene decisions for overlays/settings; stale deployment/source identity; source comments/tests overstate regional semantics. |

The user has stopped manual testing. Retain missing original damage/public-call fields as UNKNOWN and use [offline analysis](OFFLINE_CONTINUATION.md) for further mechanism recovery. The captured matches explain the primary defect without requiring another operator test. A full public-call/optical policy reconstruction has not been completed.
