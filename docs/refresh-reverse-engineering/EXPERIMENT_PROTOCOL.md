# Controlled experiment protocol

**Historical experiment plan; manual execution has ended at the user's request.** Limited connected pilots are recorded in [STOCK_REFRESH_TRACE.md](STOCK_REFRESH_TRACE.md) and [STOCK_VS_INKBOUND.md](STOCK_VS_INKBOUND.md). The original [planned actions](evidence/planned-experiments.csv) are preparation, not completed acceptance evidence or pending requests. Continue through [offline analysis](OFFLINE_CONTINUATION.md).

## Common trial record

Use separate run IDs for each action/repetition/application. Record source/executable/module identity, firmware, root partition, page/document hash or non-sensitive fixture ID, UI geometry, pen/color/width, initial pixel/ghosting state, idle delay, action/camera markers, and actual interaction count. Capture stock first and equivalent Inkbound actions with the same visible content and coordinates. Do not mix prior artifacts or a mocked preview with current device evidence.

Each event row should contain monotonic timestamp, TID, raw caller/module offset, original Qt/direct damage rectangles and count, content regions, requested/effective mode regions, public flags, force state, lower update rectangles/flags, and eventual optical flash/restoration. Mark missing fields UNKNOWN. Record swap call duration separately if entry/return capture is validated; a return timestamp is not optical completion.

One action per baseline trial. Use at least three independent repetitions, resetting to the same state with the reset action separately marked. Record any reset/full-refresh state rather than assuming a known ghost-free baseline. Preserve spontaneous background refreshes and failed trials. All human actions are performed by the operator; do not inject pen/touch/power events.

## Ink / erasing

Test dot, short line, long continuous line, rapid handwriting and separated strokes at documented coordinates. Plot bounds over time; distinguish one bounding box per contact, per-submit segment bounds, multiple rectangles, and batching. Vary speed while retaining comparable geometry. Do not choose a policy from a single stroke.

Erase one stroke, erase continuously, then erase after 1/5/15 seconds idle. Observe immediate feedback, post-contact restoration and delayed cleanup separately. Test undo and redo independently. Save before/after optical regions to identify ghosted ink versus incorrect underlying pixels. Verify accepted geometry remains in the document; a flashing white region is not proof of correct restoration.

## UI / reader / overlay

Repeat toolbar/button/open/close/submenu/tool/width and dialog actions independently. For menus over ink compare both exposed underlying pixels and nearby cleanup. Test a second overlapping overlay and dismiss in both orders.

Use stable PDF and EPUB fixtures for open/close, next/previous, zoom/pan, selection/deselection, one-word/line/paragraph highlights and removal. Mark an unsupported stock operation explicitly instead of substituting another operation. Match physical content/scale where Inkbound and Xochitl differ.

## Region merging and timing

Vary separation of two changes: overlapping, touching, and gaps of 1/4/8/16/32/64/128/256 pixels where the UI permits. Vary actual inter-action intervals near 10/25/50/100/200 ms. Measure actual event spacing from trace, not requested SSH delay. Ordinary fingers cannot reliably hit 10 ms; report inaccessible timing bins rather than inject input or claim exact pacing.

Increase independent changes until the observable application or backend switches from a multi-region union to a bounding box or full update. Count original, map-partitioned and lower rectangles separately. A region split caused by content classification is not a merger. Report repeated distributions and nearest supported thresholds with uncertainty; do not claim a universal merge distance from one layout.

## Ghosting and escalation

Run a fixed menu open/close cycle 25 times and separately 50 times, at recorded cadence and initial state. Repeat at least three runs where practical. Compare per-region refresh count and elapsed time to mode changes and local/full cleanup. Repeat a same-area modification with different geometry and a distant-area control to distinguish global from per-region counters.

Look for independent counter-, elapsed-time-, affected-area-, transition- and content-related escalation. Label evidence as correlation until controlled changes distinguish the alternatives. Trace ghost-control and delayed color calls if safe, and retain background timers. No cleanup is disabled to make the test pass.

## Full-refresh triggers

Test large page changes, app/document transitions, color-heavy pages, repeated small modifications and sleep/wake. Orientation/layout only where supported and approved as an operator action. For each full optical refresh determine the earliest full scope: original scene damage, explicit public full bit, force byte, mode-specific backend branch, lower rectangle, or controller-only behavior. A full-frame DRM transport commit does not by itself prove a full-panel optical update.

## Completion criteria

Stage 1 is complete only when the report can connect repeated matched stock and Inkbound interactions through damage, classification, flags, backend geometry and optical outcome; identify the first differing boundary; explain erase/overlay restoration; and bound unknown cleanup/merging rules without invented constants. Failed attach attempts, source analysis and fake-vendor passes do not satisfy that criterion.
