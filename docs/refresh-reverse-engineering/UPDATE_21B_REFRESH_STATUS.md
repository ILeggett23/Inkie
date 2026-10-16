# Update 21b: zoom and color response cleanup

Status on 2026-10-15: integrated source and focused SDK checks pass. Full release qualification, new installation and optical comparison remain pending. The candidate is in `work/21b-integration/reader`; the authoritative Reader checkout and tablet were not modified by these checks.

## Applying the Xochitl findings

The confirmed finding is that quality/color updates can retain regional geometry; a complete-update flag can override a small rectangle. Stock queue observations also support local color settling. The numeric identifiers from Xochitl's embedded backend are not copied into Inkbound's different standalone module. See [the evidence and interface mapping](XOCHITL_LOCAL_COLOR_IMPLEMENTATION.md).

| Operation | Integrated behavior | Evidence |
|---|---|---|
| PDF pinch, pan, reset | Preserve regional presentation through asynchronous view preparation and first styled replay; use existing viewport bounds and banding | `Shell::applyPdfView`, `pinch`, `twoFingerPan`, `resetZoom`, `tick`; production Shell/Display regressions |
| EPUB pinch, pan, reset | Recompose the affected reading viewport using regional requests and intact RGB | Same Shell gesture functions; production Shell/Display regressions |
| Colored Tom response completion | Preserve 21b's one-shot cleanup after playback; defer while contact or reply work remains active | `Panel::takeColorCleanup` → `Shell::tomTick` → Display cleanup debt |
| Colored Tom response zoom/pan | Retain RGB and request cleanup for the reply viewport after its transform changes | `Panel::pinch`, `viewportPan`; picture panel control regression |
| Erasing/overlay restoration | Classify restored RGB while retaining the richer removed content class, rather than classifying only the now-white pixels | Vendor removal/quality classification and Display retirement regressions |

Regional UI/cleanup uses the verified standalone public UI mode 3 and flags 0, with actual pixels selecting Mono 0, Grayscale 1 or Color 3. Color alone does not select a complete update. Genuine page/scene transitions keep their explicit transition policy. A zoom may change nearly the entire reading viewport: regional does not mean a tiny update or guarantee the absence of visible settling.

Exact Xochitl padding, batching, cleanup delays and general ghosting escalation are still unknown. These changes implement the supported locality/color behavior; they do not establish an exact optical match.

## Current validation

Reader source manifest: `ee425f0679806de2ddc55bc976bf75ce9164208be699019a4bf2e893d11b3f55`, 845 files. Every manifest entry was checked in the VM before and after the focused run.

- PDF route: 9 cases; EPUB route: 13 cases, with real parser/render helpers, persistent annotation identity and view restoration checks.
- UI/cleanup: 14 scenarios through production Shell/Display with a fake vendor backend, including RGB PDF/EPUB zoom, pan and reset without complete requests.
- Colored picture panel: 27 control checks, including one-shot cleanup and retained colored geometry through zoom/pan.
- Refresh extent/classification: 26 checks. Receiver, Companion continuation, durable writes and dynamic-export audit also pass.
- Source-file streaming primitive: a 66 MiB SHA/copy test, bounded memory, mutation rejection and durable fault checks pass. This does **not** establish end-to-end large-book upload/import support, which remains incomplete.
- Companion: 124 local tests pass. An earlier converter timeout and a host Node crash remain recorded; neither is presented as a clean original run or as a diagnosed product defect.

The first broader PDF run timed out because its Canvas test helper only acknowledged a new view after a full request. Production `session.cpp` acknowledges changed page versions after the presentation boundary, including regional updates. The helper now follows that contract; the original PDF assertions and timeout were retained, and the suite passed. The failure log is included alongside the successful logs.

Evidence: `work/21b-integration/evidence/integrated21b-focus-r7-evidence.tar.gz`, SHA256 `6c4dc6924de3a179fb89dcc4fd6aa1cf712ba46bb8dc9029d3a2cecd6ce4a7d6`. Machine-readable state: `work/21b-integration/FOCUS-STATE.json`.

The full release gate has not run: the builder has approximately 2.32 GiB free, below its unchanged 12 GiB preflight floor. No additional cache reclamation, device action, production deployment or paid provider call was performed.
