# UI, input and refresh repairs — 2026-10-16

Base: Inkie `85a4451357f478ad04c7d02d667eebb0110a42ca`; isolated branch `fix/ui-input-refresh`. This is native C++ canvas rendering, not a DOM application. The previous builder remains retired. Existing Mac source checkouts, the backup release and tablet data are unchanged.

## Confirmed source defects and changes

| Boundary | Finding | Repair |
| --- | --- | --- |
| Reader damage queue | `Shell::damage` promoted a region above one quarter of the display, or eight queued regions, to `change.full`. | Retain regional intent; bounded accumulation merges the least costly pair at capacity. `Display::live` already bands large fast updates. Page transitions retain their explicit complete update. |
| Document settings | `viewPaint`, tab interaction and closing settings explicitly requested a complete page update. | Opening/closing uses panel bounds; state edits and arriving thumbnails use changed pixels. Unconsumed taps do not paint. |
| Library preferences | Individual setting toggles repainted the library and unconditionally requested a complete update. | Use the existing old/new library diff for tool handle, handedness, progress, Tom preferences, contrast, motion and sleep settings. The existing mostly-new-screen policy remains. |
| Wi-Fi | Every status/password repaint requested a complete update. | Diff old/new composed pixels and submit bounded regional damage. |
| Reader Selection input | Reservation happened before hardware tool identity was known. Eraser contacts could reach text-range selection. | At eraser Down, hand the reserved contact to styled ink and external-contact ownership; preserve Selection mode. Also reject eraser ranges at semantic dispatch. |
| Cleanup color retention | UI retirement/replacement could classify only newly white pixels, losing the removed pigment class. | Sample the previously displayed footprint before composition and retain its class through the existing local cleanup queue. No new waveform constants or full-update flags. |
| Notes state | A settled asynchronous removal left Note mode pointing at an empty selected record. | Return that state to Hub, disable its ink owner, and retire selection/keyboard/playback controls. Busy/error snapshots do not trigger this transition. Refused delete commands retain the current view. |
| Tom color | The reply color flag inspected only the current 640-pixel viewport. | Classify retained stroke styles across the reply, including color below the visible area. |
| Tom scrolling | Viewport-only jobs rebuilt and compared the whole playback plan. | Reuse the immutable plan and retain the existing playback cursor. A completed reply must not animate again when scrolled. |
| Tom streamed handwriting | A newer prefix canceled an in-flight prefix even when request/style were identical. | Allow a valid immutable prefix to finish and appear while later text waits. Changed requests, style changes and visual jobs still cancel. Final completion still requires the current job identity. |
| Raster replacement | Every replacement compared individual pixels across the entire canvas. | Skip identical rows, find changed row spans and copy only those spans. |
| Offscreen rendering | Rasterization walked every stamp of entirely offscreen transformed segments. | Conservatively reject expanded endpoint bounds after the existing budget checks; preserve point history and all visible brush geometry. |

## UI audit and retained behavior

| Component | Partial-update opportunity / disposition |
| --- | --- |
| Reader toolbar, pen palette, document menu | Existing opaque compositing and regional damage retained. Existing black/red occlusion, close, tool persistence and secondary shortcut regressions run with this patch. |
| Definitions and selection actions | Existing regional overlay retirement, compound definitions, expansion and pen action support retained; hardware eraser is excluded from semantic dispatch. |
| Document settings and thumbnails | Regional opening/closing and pixel-diff state updates implemented above. |
| Library settings, Wi-Fi | Preference/status edits use regional comparisons. Large section changes remain explicit scene transitions. |
| Notes Hub, Note, Layers, keyboard | Existing pixel-diff damage and opaque popup ordering retained; asynchronous note removal is now a state transition to Hub. |
| Tom input, response, picker, keyboard | Existing changed-pixel damage and opaque overlays retained. Whole-reply color classification, prefix preparation and viewport plan reuse repaired. |
| PDF/EPUB page open/close, reflow | Actual document/geometry transitions retain complete updates; this patch does not suppress required page restoration. |
| Sleep/wake, passcode, failure restoration | Existing complete scene restoration and recovery ownership retained. |
| Companion pairing, notebook creation, destructive-library confirmation | Existing modal lifecycle retained. Further incremental painting needs measured evidence before changing asynchronous ownership or persistence. |

## Validation and limits

`ui-damage-tests` verifies queue saturation without losing rectangles, large regional quality requests and 30 consecutive color-to-white cleanups at a simulated vendor boundary. The Mac runs its queue-only mode; Linux is required for the full input/vendor headers.

Production-path regressions cover settings opening/closing, Selection plus the hardware eraser in PDF and EPUB, repeated highlighter erasing with an untouched neighbor, asynchronous note deletion, completed-reply scrolling, and offscreen color at the bottom of a tall asset. Existing persistence, compositor and raster suites remain required.

The GitHub workflow uses Qt 6.8.2 and the original pinned MuPDF 1.28.3 source, in release and ASan/UBSan configurations. Its loader alias supports native Linux test fixtures only; it is not a tablet SDK or deployable app. No recovery key, real books, tablet credentials or provider keys are supplied to CI. Workflow results must be recorded before declaring these repairs validated.

Tom changes reduce avoidable local preparation/rendering work. They do not establish faster provider reasoning or image generation. No model, reasoning budget, illustration quality, consent, cost cap or review policy is downgraded. No live provider request, production deployment or tablet installation has been performed.
