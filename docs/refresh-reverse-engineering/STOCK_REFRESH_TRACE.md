# Stock refresh observations

**2026-10-14: device pilots collected; manual testing has ended. The complete stock policy is partially recovered.** These observations are Xochitl with the user's existing XOVI, AppLoad and resource-rebuilder loaded. No Inkbound owner was active during stock trials. They are not a pristine-stock control or a complete function-call trace.

## Identity and method

OS3.27.3.0/build20260612085811; root partition `/dev/mmcblk0p3` mounted read-only. Xochitl SHA256 `9749880daa2f10844e77b560ec0ecddd1634d43eb328af637c7026edf3ef120e`, PID16545 in initial stock captures and22907 after normal Inkbound exit. Vendor and Qt files match the pinned analysis. [Preflight](evidence/device-preflight-20261014T111153Z.log), [capabilities](evidence/20261014/device-capabilities.log).

The kernel lacks the proposed tracefs/uprobe route; Xochitl statically links its display implementation. A bounded, non-stopping helper opened `/proc/PID/mem` read-only and sampled known update-list metadata every nominal10ms. It made no vendor call, target memory write, debugger attachment, service change or input injection. [Method and limitations](READ_ONLY_SAMPLING.md).

The table's rectangles are **sampled lower update geometry**, not original Qt dirty rectangles. Numeric pairs are raw lower waveform/control selection and PixelMode fields, not public EPScreenMode or EPContentType. Public flags, caller/TID, original Qt QRegion, exact request times and optical completion are UNKNOWN.

## Controlled pilots

| Action | Sampled lower rectangles | Content type | Public screen mode | Public partial/full flag | Timing/coverage | Operator observation |
|---|---|---|---|---|---|---|
| Three separated black dots | examples4×4,8×4,8×6 around contact positions; lower pair1/3 | UNKNOWN | UNKNOWN | UNKNOWN | useful run reports113 sampled IDs; terminal log truncated,37 complete rows recoverable;341 read misses,11 unstable reads | three dots; no whole-screen flash reported |
| Toolbar open/close three cycles | includes112×2080 and112×1904 toolbar partitions, plus glyph/background partitions; lower pairs2/16,5/16 and8/6 | UNKNOWN | UNKNOWN | UNKNOWN |59 sampled IDs from62 constructor increments in first45s; complete file; zero read/unstable/invalid/capacity counts; following135s had no counter change | only toolbar area changed |
| Erase three existing dots separately | most local feedback60–70px across; pair1/14 dominates; quality/restoration partitions include380×460,372×444; other large/full geometry also occurs | UNKNOWN | UNKNOWN | UNKNOWN |560 sampled IDs from702 constructor increments;45s complete file;0 read errors,25 unstable reads,263 bounded queue-walk hits | dots cleared locally; no whole-screen flash reported |
| Pen palette open/dismiss3 cycles | local popup quality2/16,5/16; other8/6 partitions | UNKNOWN | UNKNOWN | UNKNOWN | matched pilot; retained metadata is lossy | only popup area changed; no whole-screen flash |
| Same palette25 cycles | examples400×1076 and400×852; quality2/16,5/16; full/background8/6 also occurs | UNKNOWN | UNKNOWN | UNKNOWN |79 rows,78 unique IDs across four45s windows;78 constructor increments;0 read errors,2 unstable reads,0 capacity hits | completed25; no whole-screen flash |
| Three short red strokes | incremental examples8×6/8×4;1/14 plus local quality2/16,5/16 | UNKNOWN | UNKNOWN | UNKNOWN |393 unique IDs from404 constructor increments;3 read errors,26 unstable reads,197 capacity hits | red appeared after local update; no whole-screen flash |
| PDF3 next/back cycles | large quality2/16, including1508×2088 and1508×2160; smaller footer/UI partitions | UNKNOWN | UNKNOWN | UNKNOWN |73 unique IDs from91 constructor increments;1 read error,1 unstable read,58 capacity hits | all six turns completed without whole-screen flash |
| EPUB fresh retry, one confirmed next/back cycle | first-window quality2/16 includes1508×2160/1508×2088;8/6 background partitions | UNKNOWN | UNKNOWN | UNKNOWN |12 unique first-window IDs from14 constructor increments;0 diagnostic errors/capacity hits | one confirmed cycle; no whole-screen flash |

These are actions within pilots, not independently reset trials. Exact batching, padding, update rate, merge threshold and cleanup policy are not inferred from them. Constructor increments are not necessarily accepted requests; sampled IDs are not a complete submission count. In particular, the25-cycle result does not prove a universal ghosting threshold. Raw1/14 occurs during both red ink and erasing; it is not an exclusive erase label. [Extended counts and diagnostics](evidence/20261014/extended-pilot-summary.json).

## Preserved failed/limited trials

- Dot attempt1: operator explicitly missed the15-second window. Idle log has no sampled updates; not a pen observation.
- Dot attempt2:113 sampled IDs reported, but terminal truncation lost rows. Preserve [truncated log](evidence/20261014/dots-attempt2-TRUNCATED.txt) and [37 recovered update rows](evidence/20261014/dots-attempt2-recovered-PARTIAL.jsonl) as partial evidence.
- Dot retry: complete45-second log has zero constructor-counter change and zero sampled updates; operator reported completion/no flash. Time alignment is unresolved. Do not count this as a successful captured pen repetition.
- Erase exit7 records bounded/incomplete sampling, not a Xochitl failure. The queue-walk cap was retained. No capacity limit was relaxed to obtain a passing capture.
- PDF word selection was attempted, but the operator reported the PDF did not support it. The incomplete log is preserved as an unsupported experiment, not evidence of selection/dismissal policy.
- Initial EPUB6-turn report has no aligned metadata activity during its capture. A first long retry has4 unassigned records and no completed action confirmation. Fresh retry first window captures the confirmed one-cycle pilot; its later2 records (raw2/18 and6/16, full geometry) have no action/optical attribution. Do not assign them to the confirmed nonflashing cycle or count additional cycles completed.

[Complete toolbar file](evidence/20261014/stock-toolbar-long-window.jsonl), [complete erase file](evidence/20261014/stock-erase-three-dots.jsonl), [machine-readable pilot summary](evidence/20261014/stock-pilot-summary.json), [operator reports](evidence/20261014/operator-reports.json).

## Full geometry is not a visible full-screen flash

The dot pilot contains full1620×2160 raw8/6 entries before the first captured dot and approximately30seconds later. They cannot be assigned a cause from this pilot. Toolbar and erase pilots also contain8/6 background/large partitions. Do not label every full geometry record a complete-refresh flag, ghosting cleanup or optical flash. Operator reports and queue metadata are distinct evidence classes; no calibrated camera recording was made.

## Stock executable mechanisms, independently checked

The exact Xochitl executable has a different static display ABI/layout from standalone libqsgepaper. RTTI identifies Acep2's vtable at0x1606918 and its backend slot+0xb8 at0x9d8400. Analyst-identified region wrapper0x9bbee0 carries flags in w3, with bit0 promotion at0x9bbf68 ->0x9bc0c0. Its ordinary renderer call at0x9c2ce4 sets w3=0. [Verified byte/control-flow facts](evidence/20261014/stock-static-verified.json).

This provides source-independent stock-executable evidence for preserving local region semantics on the ordinary renderer path. It does not expose a particular action's live public arguments. Reusing the plugin's function offsets or argument count for Xochitl would be wrong.

## Remaining experiments

No further operator tests are pending. Color/PDF/EPUB/palette repetition now have the limited pilots above. Selection/highlight/removal, document open/close, dialogs, overlapping overlays, undo/redo, continuous/delayed erasing, long/rapid strokes, controlled merge sweeps, sleep/wake and orientation remain unqualified. The [original experiment plan](evidence/planned-experiments.csv) is historical planning, not an execution ledger; NOT_RUN rows have not been relabeled as independent trials. Continue with [offline analysis](OFFLINE_CONTINUATION.md), preserving unknowns.

Quill remains a public comparison implementation, not stock policy evidence. Its constants and claimed return tokens were not imported. [Quill](https://github.com/MaximeRivest/quill), [public source index](evidence/PUBLIC_SOURCES.md).
