# Stage 2 recommendations — not implemented

**Recommendations only; Stage2 is not implemented or authorized by this investigation.** Matched pilots establish a narrow repair target. A replacement local-quality path still requires exact-runtime safety and optical validation before landing; no renderer rewrite or copied stock constants are justified.

Connected-device pilots now support separating live ink, erasing and UI lower update classes. They also show why full queue geometry must not be equated with a visible flash. Stock's static backend differs from the standalone plugin. These findings justify further exact-runtime qualification; they do not authorize copying raw mode pairs or implementing Stage2 from the pilot.

Matched pilots now demonstrate both failure directions: palette/erase cleanup produces whole-panel quality work and flashes, while ordinary toolbar updates use fast-ink mechanics and leave operator-visible remnants even though sampled source columns have been restored to white. The minimum later repair must address both quality and extent. Clearing every full flag or disabling cleanup would leave the second defect unresolved.

1. **Separate refresh extent from quality.** The existing `RefreshIntent`/`RefreshMechanics` vocabulary can be retained, but `QualityRegion` must not map to a flag that changes the region to the full image. Qualify an exact-runtime local-quality request first. Do not simply clear the bit everywhere: prior UI-partial/Color/Content deadlock reports remain unresolved constraints.
2. **Keep complete refresh explicit.** Retain intentional initialization, page/document/sleep transitions. Add request-reason visibility so a local overlay operation cannot silently become `PageTransition` without a recorded cause. The current quarter-area/eight-extra rules should be changed only for actions whose pre-policy trace proves a safe regional alternative.
3. **Preserve damage end to end.** Retain current regional compose, original old/new bounds and planner storage. Adjust fixed-capacity union/escalation only where matched traces demonstrate excess. Distinguish CPU repaint size, requested refresh area, backend update area and optical flash area.
4. **Preserve classification through the boundary.** Current UI/content intents collapse to Mono/Pen. Add a content-aware path only after target firmware has verified monochrome quality, grayscale and color-partial behavior, including timer/idle handling. Do not import Quill's historical Color1 or waveform names.
5. **Extend validation past the fake vendor.** The current suite validates the defective flag mapping. Later regression evidence should explicitly verify local quality semantics against the pinned backend and then the panel. Fake vendor tests remain useful for coverage/identity/region transport, but cannot establish physical locality or ghosting removal.
6. **Tune cleanup from stock evidence.** Determine whether menus, palettes, highlights, erasing and repeated changes need immediate, delayed or count-triggered cleanup. Do not transplant the generated-content96 counter into a universal GhostingTracker, and do not disable cleanup to make flashing disappear.

## Minimum components justified now

| Candidate | Recommendation | Why |
|---|---|---|
| DamageTracker | reuse existing Change / damage / pending-region mechanisms | damage already exists; evidence supports tracing and targeted escalation fixes |
| RefreshController | clarify existing intent-to-vendor mapping | confirmed quality/extent mismatch is here |
| ContentClassifier | preserve explicit content metadata when verified | current all-Mono collapse is demonstrated; classification rules still need evidence |
| WaveformPolicy | no new constants yet; qualify narrow alternatives | exact stock/effective mode and idle safety unresolved |
| GhostingTracker | defer new architecture; retain existing generated counter | no stock ghosting law measured |
| RegionMerger | reuse RefreshPlan / changedRegions; measure overflow | no evidence justifies a new scheduler or guessed merge window |
| EPFramebufferBackend | retain existing adapter, define true extent semantics | exact vendor full-bit promotion established; no framebuffer replacement needed |

## Acceptance before landing any Stage 2 repair

Require the same document, geometry, repeat count and optical recording for stock and Inkbound. Verify normal UI/overlay removal/erase locality, complete underlying-pixel restoration, accepted ink preservation and final endpoints, correct color classification, legitimate full-refresh behavior, responsive idle/timer paths, and existing recovery/ownership checks. Keep current source, exact executable, library hashes and pre/post state in the trial record.

No proposed component or policy has been added to Inkbound during Stage 1.
