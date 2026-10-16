# Read-only fallback for the connected tablet

**Current state:** manual testing and sampling have ended. All seven device helper executables and both stop flags are removed; stock Xochitl is healthy. [Cleanup](evidence/20261014/device-cleanup.log). The following describes the retained method, not active instrumentation.

The 2026-10-14 preflight found no tracefs filesystem in `/proc/filesystems`, no uprobe/ftrace/tracefs registration symbols among36,667 kallsyms entries, and no installed perf/bpftrace/trace-cmd/gdb. Xochitl does not map the standalone scenegraph plugin. The original uprobe plan cannot run on this stock kernel. No module is loaded and no mount/boot configuration is changed to work around this.

Xochitl SHA256 `9749880daa2f10844e77b560ec0ecddd1634d43eb328af637c7026edf3ef120e` was copied read-only for local private inspection. RTTI identifies an Acep2 vtable address point at0x1606918; slot+0xb8 leads to an analyst-identified swap implementation at0x9d8400. Its dataflow uses x1 as region, x2 as a mode-region map and w3 as flags. The framebuffer image is at object+0x88. These differ from standalone plugin arguments/layout; no public C++ signature is claimed from the stripped function alone.

The bounded fallback samples existing display-update metadata through `/proc/<pid>/mem` opened O_RDONLY. Queue sampling does not attach ptrace, stop or signal the target, inject code, patch memory, invoke vendor functions, read pixel data, or change library/service/boot configuration. The separate source-hash command described below reads only known Inkbound toolbar columns and exports hashes/counts, never pixels. Running a helper in `/tmp` is temporary diagnostic instrumentation. Its rollback is natural exit within45seconds; ending the helper releases its read-only FD. No target-side state requires restoration.

The sampler reads only the pinned global display context and bounded current/pending/active update-list metadata. It prints geometry, update identity, raw lower mode/flag fields, and monotonic observation timestamps. A function-byte guard and external executable SHA check are required. Two consecutive node reads must agree; inconsistent nodes are rejected and counted. Reads can still race allocation reuse, so agreement is not a global consistency guarantee.

**Limit:** this is `DEVICE_QUEUE_SAMPLE`, not a complete function trace. Transient updates can be missed; an observation timestamp is not a request timestamp. Public content type, public mode, public flags, thread and caller are unavailable. No request rate, exact batching window, complete region count, or absence of fullscreen refresh can be inferred solely from these samples. Raw lower fields remain numeric until independently mapped.

Static layout evidence for this binary:

-0xb9be98–0xb9bea0: global display context pointer at0x1b1d768.
-0x9d7a30–0x9d7a90: publication reads pending batches at context+0x78 and building updates at+0x4118; batch nodes have an inner update list at+0x10.
-0x9d7aac–0x9d7b48:192-byte list-node allocation; Update payload follows16-byte links, geometry at payload+0x38, identity+0x48, lower waveform field+0x68.
-0x9cdc20–0x9cdcc8: Update initialization/identity and default lower-control fields.
-Qt6Gui exact hash `cdc0c0a7…aada5`: QRegion begin/end/rectCount layouts were inspected locally; no Qt method is invoked in Xochitl by the sampler.

The active-list interpretation and lower-field labels require baseline validation before controlled actions. Unknown or out-of-panel geometry causes rejection, not invented data. Zero sampled updates means idle or missed updates, never proof that no refresh occurred.

## Inkbound profile and toolbar source check

The same metadata sampler was validated on installed21a PID21777 with the exact executable and plugin hashes checked first. It finds the plugin mapping's load bias, checks its function bytes, reads the context pointer at bias+0x91e68, uses building-list offset0x40e8 and constructor counter bias+0x92720. The shared Update payload offsets were independently checked in that plugin's saved copy/conversion code. This is a different profile from stock's static layout.

Version6 also provides `--inkbound-ui-hash PID`: it reads the known RGB alias at context+0x40e0 and computes two-pass FNV1a64 hashes/white-pixel counts for left/right toolbar columns x0/1508, y112, width112, height1948, stride6528. No pixel bytes or images are exported or retained. The dedicated check found both columns completely white and stable after toolbar closing. It is a source-pixel check, not an optical image or completion fence.

Each sampler invocation retained a45-second duration bound,2-second CPU soft limit,32MiB address-space bound,24-node queue walk and1024-identity record cap. Versions1–6 used CLOCK_MONOTONIC for the duration, which excludes suspend; version7 uses CLOCK_BOOTTIME for that deadline while observation timestamps remain MONOTONIC. No suspend trial was run. Later capture wrappers continued into a new bounded window after exit7, recording the incomplete window and preserving a final incomplete status. Identity/layout startup failures stop capture. No validity check or capacity bound was removed to obtain a passing observation.
