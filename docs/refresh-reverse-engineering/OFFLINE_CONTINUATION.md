# Continue without operator testing — 2026-10-14

The user has stopped hands-on tests. All observers are stopped, seven temporary device executables and two stop flags have been removed, and stock Xochitl is active. [Cleanup record](evidence/20261014/device-cleanup.log). No further readiness prompts, action windows or background capture are pending. No Stage2 implementation occurred.

## What can be obtained another way

| Method | Useful evidence | Limit |
|---|---|---|
| Static analysis of the exact saved stock executable and vendor module | flag promotion, mode-map partitioning, timer setup/guards, update construction, queue publication and conditional escalation | establishes executable mechanisms, not which branch a specific action took or the resulting optical appearance |
| Audit of sealed installed21a source | UI damage, old/new restoration, merging/capacity guards, refresh intent and outgoing vendor arguments | source-to-artifact identity must remain explicit; current working source differs |
| Offline analysis of retained queue logs | geometry/classes over time, repeated palette behavior, large reader regions, comparison with source | lossy samples; timestamps are first observations, not exact call times; public arguments remain unavailable |
| Existing VM policy harness with actual policy and fake vendor | reproducible intent-to-argument mapping and region transport | cannot prove panel behavior, vendor idle safety or optical cleanup |

These methods already explain the primary defect: Inkbound's local cleanup sends bit0, and the exact vendor wrapper replaces the region with the full image. Matched palette and erase pilots corroborate that mechanism. More manual repetitions are not necessary to establish it.

Automated stylus/touch injection could reduce repetitive work, but changes the experiment and may omit real pen handling. It is neither needed nor enabled here. Permanent hooks, library replacement, boot changes and Xochitl patching remain outside this stage's safe route.

## Additional offline findings recovered after manual testing stopped

All stock addresses below refer only to SHA256 `9749880daa2f10844e77b560ec0ecddd1634d43eb328af637c7026edf3ef120e`. Names marked analyst are inferred from bounded dataflow, not recovered proprietary source names. [Reproducible byte checks](tools/inspect_stock.py), [results](evidence/20261014/stock-static-verified.json). The proprietary executable/disassembly stays in ignored local-only storage.

**CONFIRMED — HOST_STATIC_STOCK_ELF: spatial mode assignment.** Analyst function0x9c0ee0 checks requested mode against5, reads a translation table at map+0x30, and updates six8-byte QRegion slots. It unions the incoming region into the selected slot via QRegion::operator+= (PLT0x451c40) and subtracts it from other slots via operator-= (PLT0x44f8a0). This is spatial classification partitioning. It does not imply temporal coalescing, a merge-distance threshold or a maximum dirty-rectangle count.

**CONFIRMED — HOST_STATIC_STOCK_ELF: configured display timer.** Initialization at0x9c6a68–0x9c6b1c constructs an embedded QTimer at owner+0xe0, sets its interval to1500ms, and enables single-shot behavior. Analyst schedule helper0x9c9330 checks the caller thread against QObject::thread and conditionally starts that same+0xe0 timer. It compares a display-state float against a cached integer threshold initialized from `EPFB_HIGHTEMP_TMODE_THRESHOLD`, default28 if parsing is unavailable. The standalone module's exported `scheduleTModeUpdate` has matching thread/temperature scheduling logic with its different+0xf8 timer offset. No process environment was read; the runtime override is unknown.

**HIGH CONFIDENCE — matched static structure:** stock0x9c9330 corresponds to the module's TMode scheduling mechanism. The1500ms constant is a timer configuration, not a discovered general batching delay or ghosting cleanup interval. The connected traces did not measure this timer firing.

The timer connection references callback0x9d7dc0. Its initial path constructs an Update with full-panel bounds after private-state checks. The complete callback's final classification, scheduling conditions and optical effect remain unresolved. A separate3000ms timer belongs to another allocated helper at helper+0x58; it must not be conflated with the1500ms timer or assigned a ghosting role merely because the number exists.

## Remaining recoverable questions

Further bounded static work can follow the force-state setters, timer callback, mode-specific Acep2 branches and pending-batch publication. Retained logs can be compared against those mechanisms. No guessed constants should be promoted into implementation.

Exact optical ghosting, cleanup effectiveness, real refresh latency and every content-dependent transition cannot be proved by offline replay. Original Qt damage, live public flags/content, precise batching windows and universal full-refresh triggers remain UNKNOWN unless independently measured later. The six requested reports now preserve this distinction; the complete stock policy is not claimed finished.
