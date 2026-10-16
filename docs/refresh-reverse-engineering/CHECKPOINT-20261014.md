# Current Stage1 checkpoint — 2026-10-14

**Manual testing has ended at the user's request. No further hands-on action is pending.** The primary fullscreen defect is explained by exact source/backend evidence and matched tablet pilots. The complete stock refresh policy remains partially recovered. Continue through [offline analysis](OFFLINE_CONTINUATION.md); do not start Stage2.

## Device state after verified cleanup

All observer processes stopped. Seven own temporary diagnostic executables and two stop flags removed after identity/no-observer checks. Xochitl is active, PID22907. No Inkbound child or owner unit remains. Stock executable, standalone vendor module and installed Inkbound manifest hashes are unchanged. [Cleanup log](evidence/20261014/device-cleanup.log). No system library, Xochitl binary, boot configuration or service was modified by the agent. Operator-created scratch annotations remain normal document content.

The kernel has no usable tracefs/uprobe or hardware-breakpoint route. No kernel mounts/modules, debugger attachment, target memory writes, injected input or vendor calls were used. Sampling opened /proc/PID/mem read-only; it is non-stopping and lossy. Private stock executable/disassembly stays ignored and local-only.

## Exact identity

- Paper Pro/Ferrari, kernel6.12.49, OS3.27.3.0/build20260612085811; boot root /dev/mmcblk0p3 mounted read-only.
- Xochitl SHA256 `9749880daa2f10844e77b560ec0ecddd1634d43eb328af637c7026edf3ef120e`; stock trials used PID16545 before normal Inkbound launch, PID22907 after normal exit. Existing XOVI/AppLoad/resource-rebuilder environment is retained; this is not a pristine-stock control.
- Standalone libqsgepaper SHA256 `3800e9f3d01f40fa3f5cc96ca49bcd982471ce5b033320f8788707520c8ddd08`; stock statically links a distinct display implementation.
- Installed21a manifest `a0c929b782443a9fa955d1b79e2cd493e2ac0e801cd717360539f4b638a4c882`; executable `f7bed145e84de633ba1ce3cda82b17dc99b304e8b3d91c685be53b950d7a9eb8` verified both on disk and running.
- Sealed21a source at `/Users/zayway/Documents/Codex/2026-10-12/read-the-provided-handoff-in-full/work/tom21a/reader` verifies833/833 entries. Current reader-integration matches794/833; use sealed source for installed attribution. Decisive refresh/policy files and focused shell bodies match.

## Established result

`Cleanup -> QualityRegion -> VendorDisplay::clean(rect)` sends Mono0/UI3/flags1. The exact vendor region wrapper tests bit0 and replaces even a small rectangle with QImage::rect. Palette472×1094 is below Inkbound's quarter-panel guard, yet the matched Inkbound trial contains8 full-panel quality2/15 updates and operator-reported flashes. Stock equivalent remains local quality2/16,5/16 with no reported whole-screen flash. Erasing corroborates the same divergence. Ordinary Inkbound toolbar updates use fast-ink1/13 and leave reported outlines; later sampled source toolbar columns are white.

Current native-reader rendering is manual C++ raster/direct EPFramebuffer, so QML scene damage is not responsible for this demonstrated path. Separate explicit full decisions and region/area guards also exist in Inkbound source.

## Retained stock observations

- Dots: small4×4/8×6 raw1/3 samples, truncated/partial capture; no reported whole-screen flash. Retry timing unresolved.
- Toolbar3 cycles: local UI classes; only toolbar area reportedly changed.
- Erase3 dots: mostly local1/14; dots reportedly cleared locally without whole-screen flash.
- Palette3 cycles and25-cycle repeat: local quality classes; no reported whole-screen flash or count-triggered full escalation in that repeat.
- Red3 strokes: local1/14 plus quality work; red reportedly appeared after local update/no whole-screen flash.
- PDF6 turns: large quality2/16 areas, including1508×2088 and1508×2160; no reported whole-screen flash. Selection unsupported by this PDF; no selection-policy conclusion.
- EPUB: original six-turn operator report missed capture. Fresh retry confirms one next/back cycle with12 first-window updates/no reported whole-screen flash. Two later metadata records are unassigned; additional cycles unconfirmed.

[Extended log summary](evidence/20261014/extended-pilot-summary.json), [operator reports](evidence/20261014/operator-reports.json), [stock observations](STOCK_REFRESH_TRACE.md), [matched comparison](STOCK_VS_INKBOUND.md).

Public call parameters, original Qt damage, exact request timing/caller/TID, complete merge/escalation laws and calibrated optical behavior remain UNKNOWN. Raw lower fields are not public enum constants; full geometry alone is not an optical flash. No further manual repetition is needed to establish the demonstrated cleanup/full-flag defect.

## Offline continuation

The saved stock executable now independently verifies ordinary flags0 rendering, full-bit promotion, spatial mode assignment and a guarded1500ms single-shot display timer. The timer's universal cleanup/ghosting meaning is not established. Further investigation can follow force-state setters, callback/queue paths and existing traces without operator participation. [Details and limits](OFFLINE_CONTINUATION.md).

No product changes, new scheduler, waveform changes or Stage2 implementation occurred. Qualification of a replacement local-quality path remains future work.
