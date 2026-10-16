# Full-refresh root cause

**Primary cause established for the inspected Inkbound source and pinned vendor module: a local cleanup request sets the vendor's full-image promotion bit.**

This explains how correct small damage becomes a full-panel update. It does not require fullscreen Qt damage, a faulty rectangle constructor, or an area threshold in the display controller.

**2026-10-14 update:** exact installed21a identity is established, and matched device pilots now corroborate the source/backend diagnosis for palette, toolbar and erasing behavior. Stock's static wrapper has the same bit0 full-image mechanism, while its ordinary renderer callsite passes flags0. The public arguments of individual stock calls remain unobserved. See the matched evidence below.

## Matched device evidence — connected run

Matched actions now corroborate the source/backend diagnosis. All numeric pairs below are **raw lower queue fields**, not public enums. Public arguments were not intercepted. [Raw-data summary](evidence/20261014/matched-pilot-summary.json), [operator observations](evidence/20261014/operator-reports.json).

| Action | Xochitl | Installed Inkbound21a | First established policy problem |
|---|---|---|---|
| Pen palette open/dismiss three cycles | local quality pairs2/16 and5/16; operator reports only popup area changed/no whole-screen flash | eight full1620×2160 quality2/15 records during trial/surrounding UI changes; operator reports whole-screen flashes | local `QualityRegion` cleanup sets bit0; vendor expands it (F/G/H) |
| Erase three dots | mostly local1/14 feedback with local quality/restoration; operator reports local clearing/no whole-screen flash |335 observed1/13 fast updates and two full1620×2160 quality2/15 records; operator reports whole-screen flash during/after erase | erase feedback uses ordinary fast-ink class; cleanup requests full semantics (E/F/G/H) |
| Toolbar show/hide three cycles | local UI quality2/16/5/16; operator reports only toolbar area changed |22 fast1/13 records; operator reports leftover controls/outlines | UIUpdate collapses to FastInk mechanics; sidebar retirement is not represented like palette cleanup (E/H) |

The palette is a strong geometric control: its sealed-source bounds are472×1094 (516,368px), below the874,800px guard. No page transition or ink removal was requested. Its cleanup still produces whole-panel quality work. This isolates the quality-to-full-flag mapping from the large-damage hypothesis for this interaction.

After toolbar closing, a dedicated read-only source-pixel check found all218,176 pixels white in **each**112×1948 toolbar column, with identical double-read hashes. No pixels/images were exported. This rules out retained toolbar pixels in those sampled CPU-source columns at that time; paired with the operator's prior ghost report and fast-mode trace, it supports a downstream refresh/ghosting explanation. It does not cover the top/footer or provide a simultaneous calibrated optical image. [Source hashes](evidence/20261014/inkbound-toolbar-closed-source-hash.jsonl).

The palette/erase captures retain race/capacity diagnostics and remain lossy. Counts describe retained unique update identities, not exact application requests or flashes. Broad8/6 background entries are kept separate from the observed2/15 full quality class. Independent reset trials, original Qt damage/caller capture, and the wider stock policy remain unfinished; the core divergence is now backed by matched device action evidence.

## The exact failure chain

1. A menu/tool/status/palette or semantic scene changes. `Shell::takeChange`, [ux_shell.cpp:224–226](evidence/source/src/ux_shell.cpp#L224), records old and new regions for cleanup relative to the last submitted scene. Erase/removal also contributes through `Display::track`, `change.cleanup`, and `cleanup_`.
2. Once pen/eraser contact and durable saving have settled, [Display::readerBoundary](evidence/source/src/ux_display.h#L143) builds a quality plan, subtracting overlapping fast damage when feasible.
3. `applyRefresh(region, Cleanup)`, [refresh_scheduler.h:12–13](evidence/source/src/refresh_scheduler.h#L12), chooses `QualityRegion` and calls `VendorDisplay::clean(region)`.
4. [vendor.cpp:42](evidence/source/src/vendor.cpp#L42) sends `content=0, mode=3, flags=1`, preserving the small input rectangle.
5. [vendor_runtime.cpp:92](evidence/source/src/vendor_runtime.cpp#L92) sends that QRect to the vendor wrapper at ELF address `0x33340`.
6. The region overload at `0x32f60` clips the input, then at `0x32fdc` tests **flags bit 0**. If set, execution goes to `0x33248`, obtains **the framebuffer image's entire rect**, constructs a QRegion from it at `0x3326c`, and rejoins backend dispatch at `0x32ff0`.

**The first semantic error is at the Cleanup-to-`clean` policy mapping. The first geometric fullscreen expansion is inside `EPFramebuffer::swapBuffers(QRegion,...)`, before Acep2/controller dispatch.** The abstraction calls this `QualityRegion`, but the actual flag means the rectangle cannot limit the operation.

## Independent evidence

The saved vendor ELF was freshly rehashed: `3800e9f3d01f40fa3f5cc96ca49bcd982471ce5b033320f8788707520c8ddd08`. [inspect_vendor.py](tools/inspect_vendor.py) independently decoded the AArch64 branch and checked the full-image call/continuation targets directly from its file bytes. [Result](evidence/vendor-static-verified.json). Symbol/register semantics were cross-checked with the preserved static call graph and bounded disassembly; no module was loaded.

The trusted builder VM compiled the copied current `core.cpp`, `vendor.cpp` and policy headers with an in-memory VendorCalls implementation. Three repetitions produced identical policy parameters:

| Intent, input rect x100 y800 w100 h50 | Rectangle at VendorCalls | Content | Mode | Flags | Consequence in inspected vendor code |
|---|---|---|---|---|---|
| FastInk | 100×50 | 0 | 0 | 0 | no full-bit promotion |
| UIUpdate | 100×50 | 0 | 0 | 0 | no full-bit promotion |
| ContentUpdate | 100×50 | 0 | 0 | 0 | no full-bit promotion |
| Cleanup | **100×50** | 0 | 3 | **1** | **full-image promotion** |
| PageTransition | **1620×2160** | 0 | 3 | 1 | intentionally full |

These are **VM_FAKE_VENDOR_ACTUAL_POLICY** records, not stock/device traces. The vendor consequence is **HOST_STATIC_ELF**, not observed optical output. [Raw probe](evidence/inkbound-policy-probe.jsonl), [run log](evidence/vm-diagnostics.log).

The existing `eink_refresh_tests.cpp` passed 644 checks on the copied sources. It explicitly expects local `Cleanup` to use flags1 and asserts the fake vendor received a small rectangle. Its scope stops before vendor expansion. This explains why passing tests did not catch the semantic mismatch; the test label `exact_vendor_modes=true` does not prove a local physical update.

## Additional fullscreen causes

| Class | First divergence in Inkbound | Mechanism | Evidence |
|---|---|---|---|
| F + G + H | Cleanup mapping, then vendor region overload | local removal/UI retirement carries full bit; backend expands it | CONFIRMED source/static/probe |
| B / I | `Shell::damage`, ux_shell.cpp:219–220 | a single rect >25% panel or >8 surviving extras sets full | CONFIRMED SOURCE; excessive for a particular interaction needs trace |
| H / I | explicit `change_.full` assignments | note open/close/reset, view controls, various library/settings changes, page/document transitions | CONFIRMED SOURCE; intent varies |
| C -> B | `changedRegions` / cleanup overflow unions | fixed-capacity least-cost or fallback bounding union can become broad and trip guards | CONFIRMED mechanism; frequency UNKNOWN |
| D / E | all reader intents use Mono; ordinary UI/content use Pen | classification does not preserve color semantics; quality/local alternative not qualified | CONFIRMED SOURCE; contribution to ghosting UNKNOWN |

Some page/document/sleep fullscreen requests are appropriate. The investigation does not justify suppressing them globally. The 64 source lines containing full assignments are indexed in [explicit-full-sites.tsv](evidence/explicit-full-sites.tsv), without treating their count as a runtime frequency.

## Backend promotions beyond explicit flags

The region wrapper also reads a private forced-update byte at `this+0xe0`; when set it resets the byte and ORs bit0 (`0x33238–0x33244`). Thus a flags0 request is not a universal guarantee of local output. Its runtime setter/cadence is not established here.

Acep2 also contains a flags0 all-Animation-mode branch that constructs an entire-image rectangle (`0x3582c–0x35898`). Other full-update branches compare mode-specific area, including a 0.95 panel fraction in the full-bit branch. These are conditional backend mechanisms, not evidence that Xochitl uses them or that 95% is a generic partial-refresh threshold. No claim is made that ordinary Pen0/flags0 is automatically promoted on every call.

## Ten engineering answers at this checkpoint

1. **Primary root cause:** regional cleanup carries bit0; the pinned backend expands it to full image. CONFIRMED source/static mechanism, corroborated by matched palette/erase device metadata and operator-reported flashes. Individual live public calls were not intercepted.
2. **Qt/QML contribution:** no current-reader QML scene; manual region policy is responsible here. Historical AppLoad and live stock Qt damage require separate observation.
3. **Unnecessary complete updates:** local cleanup and several UI routes request complete/full semantics. Their necessity and acceptable replacement need stock/optical evidence; they are broader than the local-clean abstraction promises.
4. **Backend promotion:** yes, bit0 and private forced state; additional conditional mode branches exist. Proven statically for the exact saved module.
5. **Stock live ink:** preliminary queue samples include4×4/8×6 regions, raw lower pair1/3; operator reports no whole-screen flash. First useful log is truncated and lossy; exact public parameters remain UNKNOWN.
6. **Stock erasing:** a bounded pilot sampled560 updates, predominantly local60–70px regions with raw pair1/14, plus quality/background work. Operator reports local clearing/no whole-screen flash. Coverage is incomplete.
7. **Stock ordinary UI:** toolbar pilot sampled59 update IDs from62 constructor increments, including112px-wide partitions with raw pairs2/16 and5/16; operator reports only toolbar area changing. Original Qt damage/public parameters remain UNKNOWN.
8. **Stock color:** three red strokes produced incremental local1/14 metadata followed by local quality work; operator reports red appearing after a local update without a whole-screen flash. Public content/mode/flags remain UNKNOWN. Raw1/14 also occurs during erasing, so it is not exclusively an erase mode.
9. **Stock cleanup/ghosting:**25 palette cycles had no reported whole-screen flash and retained local UI classes; this does not establish a universal escalation threshold. Raw8/6 background/full geometry entries occur in several trials. Offline code inspection confirms a guarded1500ms single-shot display timer, but its runtime firing/optical effect and a general ghosting law remain UNKNOWN. Inkbound's96-update counter is not a discovered stock counter.
10. **Stage 2:** separate update extent from quality, qualify a local cleanup path on the exact runtime, reduce only demonstrated premature full escalation, preserve classification/damage identities, and extend evidence through backend and optical output. Details in [STAGE_2_RECOMMENDATIONS.md](STAGE_2_RECOMMENDATIONS.md); nothing implemented.

**The core stock-relative divergence is explained; the complete stock policy remains partially recovered.** Source identity, exact vendor control flow, matched palette/erase lower geometry and operator observations converge on the cleanup/full-flag defect. Exact stock public arguments, original Qt damage, universal merging/escalation rules and calibrated optical behavior remain unknown. The user has stopped manual testing; no further hands-on trials are pending. Continue through [offline analysis](OFFLINE_CONTINUATION.md), without Stage2 implementation.
