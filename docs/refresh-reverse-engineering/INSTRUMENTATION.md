# Stock instrumentation and rollback plan

**2026-10-14 state:** manual testing and sampling have ended; all temporary device executables are removed and stock is healthy. The original uprobe plan below is unavailable on the actual stock kernel, and stock does not map the standalone plugin. No uprobe/hook/debugger was installed or enabled. The connected pilots used a bounded, read-only queue sampler for Xochitl and installed Inkbound21a; see [READ_ONLY_SAMPLING.md](READ_ONLY_SAMPLING.md). A separate diagnostic hashed two known Inkbound source toolbar columns without exporting pixels. No refresh parameters, pixel data or target memory were changed; observer overhead and incomplete coverage remain limitations. Continue with [offline analysis](OFFLINE_CONTINUATION.md).

## Preferred first method: bounded kernel uprobes

Trace existing running processes at vendor entry points, using kernel-managed uprobes and an isolated tracefs instance. This avoids launching another framebuffer owner, replacing a library or restarting Xochitl. It requires kernel uprobe support, tracefs instances, access rights, and the expected module actually mapped in Xochitl. Check those first. Linux documents file-offset probes and register fetches in [Uprobe tracer](https://www.kernel.org/doc/html/latest/trace/uprobetracer.html); independent trace instances and process filtering are covered in [ftrace documentation](https://docs.kernel.org/trace/ftrace.html).

The tracing attachment is a **device-side experiment with risk**: breakpoints and event collection can alter timing, a wrong ABI can misdecode values, and a broad/high-rate trace can consume CPU or buffers. It is documented here rather than executed. No gdb attach, ptrace stop, live text patch, private vendor call or guessed object method invocation is part of this plan.

## Read-only preflight

Run [readonly-device-preflight.sh](tools/readonly-device-preflight.sh) through the existing pinned-host-key/key-only SSH connection. It reports OS/build, kernel command line and root source, service state, executable/module hashes, loaded module mappings, TIDs and available tracers/kernel config. It does not read environment variables, keys, documents, framebuffer pixels or network credentials.

Before attachment require:

1. Operator confirms responsive stock UI and existing SSH connection. Maintain a second SSH channel for rollback. Record active root partition; do not modify it or boot state.
2. Fresh firmware, Qt, vendor and executable identities match the analysis. If they differ, regenerate symbol/ABI analysis from authorized read-only copies; do not reuse offsets.
3. The actual target process maps the expected vendor file. If absent, stop this attach plan. Xochitl may embed or use another display path; find its actual call boundary before choosing new probe offsets. A zero-hit trace of an unmapped library is not stock behavior evidence.
4. tracefs is already accessible with supported event register syntax and per-instance filtering. Do not mount or reconfigure the system automatically if missing.
5. No Inkbound recovery session, display takeover or unrelated tracer is active. Stock and Inkbound captures are separate; use the existing qualified lifecycle for any later Inkbound launch.

## Exact draft boundaries

[inspect_vendor.py](tools/inspect_vendor.py) checks the pinned ELF offline and calculates file offsets from PT_LOAD segments. It never assumes virtual address equals file offset. [prepare_trace.py](tools/prepare_trace.py) emits [11 draft definitions](evidence/tracefs-probes-DRAFT.txt) without executing them.

| Event | Captured values | Purpose / limit |
|---|---|---|
| swap_rect | this, caller x30, QRect x1/x2, content w3, mode w4, flags w5 | direct caller request before promotion |
| swap_region | region/content-map/mode-map pointers, flags w4, private force byte | renderer request and force-state observation; pointers are not region geometry |
| swap_impl | same map pointers/flags after wrapper | identify backend dispatch; geometry enumeration still required |
| content_rect | QRect + content | spatial classification assignment |
| screen_region | QRegion pointer + requested mode | mode assignment; translated effective map may differ |
| lower_update | QRect + waveform/control integer + PixelMode + lower flags | actual rectangle emitted to Swtcon; flags are a separate namespace |
| ghost_control / acep_ghost_control | raw mode | detect explicit ghost-control calls; mode semantics not invented |
| color_send / color_schedule / color_delay | caller, this | correlate delayed color mechanisms without invoking them |

Kernel trace records add timestamp, task/TID and CPU. Preserve full values, event names and ordering. Normalize return addresses using actual mapped ELF load bias; x30 alone may identify a PLT/local helper rather than the original UI caller. Do not force unwind stacks in the first low-rate trial.

The inspected AArch64 QRect member calls pack signed inclusive `(left,top)` in x1 and `(right,bottom)` in x2. Width/height are `right-left+1` and `bottom-top+1`; Inkbound's own `Rect` is half-open. [decode_trace.py](tools/decode_trace.py) distinguishes these and keeps public full-bit and lower flags separate. Seven synthetic checks verify decoding, timestamp precision, namespace separation, full/small geometry and offset-plan validation. Those checks do not validate a kernel attach.

## Unresolved pre-backend QRegion capture

The first probe set captures opaque QRegion/map pointers and complete lower QRect calls. That can expose full-image promotion for direct QRect requests, but **does not satisfy full Qt QRegion enumeration**. It must not be represented as complete damage instrumentation.

The next preparation step is to inspect the exact loaded Qt6Gui `QRegion::begin/end/rectCount` implementation and its allocation layouts, verify all empty/single/multi-rect cases against the matching SDK in an isolated process, then design bounded safe reads at the vendor boundary. Track each content-map slot and translated mode-map slot separately. Private layouts must be pinned to both Qt module identity and ABI; faulted/oversized reads invalidate the affected event. Do not infer a multi-rect region from a bounding box. An exact-runtime diagnostic shim that iterates public `QRegion` APIs is an alternative, but inserting it into existing Xochitl normally requires a launch/restart and needs separate risk review. It is not the default attach method.

For Qt propagation attribution, capture the original QRegion entering region-swap before the wrapper mutates scope, then the implementation region and lower update rectangles. A full rectangle at the lower boundary alone cannot establish fullscreen QML damage.

## Bounded capture sequence — proposal, not executed

Use an unpredictable per-run group/instance name `ib_refresh_<run>`. Preserve tracefs root state read-only. Create only that instance and those named events. Never clear global `uprobe_events` or another tracer's buffer. Select a supported monotonic instance clock; record it.

Start with the minimum request/lower boundaries and one low-rate toolbar trial. Verify registers/rectangles against known visible geometry and capture enabled-event formats. Restrict events to all current target TIDs; account for new threads through supported event-fork filtering or invalidate any coverage gap. TID filtering is distinct from TGID. Do not accidentally drop render/timer threads.

Use a bounded 10-second capture initially, per-CPU buffers sized for that experiment, streaming to host if needed. Arrange an independent prestarted rollback watchdog before enabling events; it must disable/remove only this run's events if SSH disconnects. Do not start tracing without a working bounded rollback. Record buffer loss/overruns, tracer faults and CPU/service responsiveness before/after. Add the other probes only after proving acceptable overhead. Choose and record an overhead limit with the operator before attaching; do not reuse the historical rejected AppLoad instrumentation or relax an old acceptance gate.

Action labels can use the instance's trace_marker for begin/end. They are operator markers; SSH transport timestamps are not exact touch or optical timestamps. Keep a synchronized camera/action record for true timing and visible flash area.

## Rollback for every trial

1. Disable this instance's tracing and each owned event, including on error/timeout/signal. Save raw trace and loss statistics.
2. Remove only the `ib_refresh_<run>` dynamic events created by the run; remove the now-empty owned instance. Keep log/evidence files.
3. Compare the unrelated tracefs root state/events with preflight. Check Xochitl PID, service health, SSH responsiveness and user-observed display responsiveness. No service restart is an automatic recovery step.
4. If detach or health checks fail, preserve evidence and request a specific recovery action. Do not reboot, kill Xochitl or change boot settings as a fallback.

No rollback was required on the tablet in this run because nothing was installed or attached. The VM only contains a small isolated diagnostic directory and archive; no existing build folders were removed.

## Alternatives if kernel tracing is unavailable

- Prefer existing vendor debug logs only if already active and sufficiently informative; enabling logging through environment/service configuration is a behavior/timing change requiring review.
- An LD_PRELOAD interposer must preserve every argument, region/map object and errno, forward exactly once, avoid recursion, and write to a bounded nonblocking buffer. Validate call binding, symbol visibility and object lifetimes before a stock trial. It cannot be assumed to intercept internal/direct/virtual calls, static linking or `-Bsymbolic`. A stock restart solely to add it is a risk that must be documented first.
- `strace` of DRM ioctls is a lower-transport fallback with greater stop overhead and incomplete content/waveform semantics. A DRM transport frame is not a panel dirty rectangle on this stack.
- If no safe method yields the required fields, leave those fields UNKNOWN. Do not replace them with optical guesses or invoke `EPFramebuffer::instance()` in a second process to inspect stock.
