# Particle convergence and gesture-scoped selection

Requested2026-10-21 after the combined611c792 app was installed. No tablet access, installation, launch, provider request or Companion deployment is included in this change.

## Root causes

- Particle stage1 used a common entry-edge position, while later stages switched to each destination; the discontinuity could span the panel. A final special case moved remaining grains back to a margin. The renderer also blanked all neutral text until consolidation. Together these produced visible popping and late specks.
- Area-only quality thresholds forced large panels to one intermediate source. Playback started before CPU preparation, and its wall-clock index skipped stages after delayed submissions.
- `Shell::selectRange` explicitly requested complete source blocks for Selection-tool drags. The48-point endpoint walk-in could miss narrow glyphs, and endpoints alone could include unmarked intermediate words/columns. The72px selection-end allowance could capture a neighboring line.

## Changes

`sand_transition.*` retains bounded regional source ownership but uses a denser fixed reservoir of fine actual-artwork grains, continuous smoothstep displacement, progressive solid ink including antialias fringes, and exact canonical consolidation without extra margin specks. Preparation is excluded from playback. Accepted sources advance consecutively with bounded spacing, stall and total timeouts; no catch-up burst or unbounded frame queue is introduced. Input/resource/scene cancellation and local cleanup remain authoritative. Full remains at most four intermediate sources; large sparse targets under the8MiB crop cap retain the requested quality. `reveal_runtime.cpp` reports source acceptance time through the existing display callback. No waveform, full-update bit, vendor owner or protected input code changes.

`semantic::selectStrokeRangeChecked` validates the same immutable page/view geometry, inverts its affine transform, clips the input vector to the visible source crop and intersects exact convex glyph quads. It selects only touched words, preserving separate ranges when unmarked words/columns lie between them in source order. Whole words remain the unit; this is not a pixel-ink contour or arbitrary rectangular text crop. Capacity refusals remain explicit. Both finger and stylus Selection routes use it. Handle edits retain word-range semantics but require starting on the endpoint word, so a nearby different line is not silently taken as that handle. Existing explicit vertical highlighter paragraph behavior and dictionary long-press behavior remain separate and unchanged.

## Regression evidence

Tests cover continuous single-grain tracks in both directions, no late-margin pixels, exact consolidation and final pixels, progressive neutral/antialias treatment, consecutive stages under120ms simulated submission latency, no catch-up burst, preparation clock alignment, source/global timeouts, cancellation, bounded cache and100 warm allocation-free cycles. The twelve authored RGB golden states were deliberately regenerated and both complete directional sequences inspected; they are software renderings, not optical evidence.

Geometry tests cover tightly spaced rows and an interline gap, reversed vectors, a narrow glyph missed by fixed sampling, a rotated quad's empty bounding-box corner, unmarked source-order column text and viewport clipping. Real Shell routes in PDF and EPUB compare both finger and pen gestures with the expected partial word range and verify that selection never creates ink/notes. Existing capacity, eraser exclusion, highlight, source-binding, toolbar and pagination regressions remain required. Test timing/settled-state limits are not relaxed.

Fresh release/sanitizer/SDK results and exact source/package identity are recorded in the task report and draft PR. Earlier611c792 checks qualify only the previous app. Physical smoothness, visible panel grain motion, cleanup and input latency still require separately authorized device validation; no LCD-like fluidity or zero-ghosting claim is made.
