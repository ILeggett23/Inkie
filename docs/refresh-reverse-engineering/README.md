# Stage 1 refresh investigation

**Status, 2026-10-14: primary root cause corroborated by matched stock/Inkbound palette, toolbar and erase pilots. The wider stock policy investigation remains incomplete.** Start with [FULL_REFRESH_ROOT_CAUSE.md](FULL_REFRESH_ROOT_CAUSE.md).

The decisive finding is a mismatch between a regional cleanup abstraction and the pinned vendor implementation. `Cleanup -> VendorDisplay::clean(rect)` sends flag bit 0. The vendor region overload replaces the damage with the full image when that bit is set. A small cleanup rectangle therefore cannot constrain this refresh.

Read [FULL_REFRESH_ROOT_CAUSE.md](FULL_REFRESH_ROOT_CAUSE.md) first. The other requested deliverables are:

- [INKBOUND_REFRESH_PIPELINE.md](INKBOUND_REFRESH_PIPELINE.md)
- [STOCK_REFRESH_TRACE.md](STOCK_REFRESH_TRACE.md)
- [STOCK_VS_INKBOUND.md](STOCK_VS_INKBOUND.md)
- [REFRESH_POLICY_MODEL.md](REFRESH_POLICY_MODEL.md)
- [STAGE_2_RECOMMENDATIONS.md](STAGE_2_RECOMMENDATIONS.md)

Supporting work: [instrumentation and rollback](INSTRUMENTATION.md), [experiment protocol](EXPERIMENT_PROTOCOL.md), [source identity](evidence/source-identity.json), [verified vendor facts](evidence/vendor-static-verified.json), [VM request trace](evidence/inkbound-policy-probe.jsonl), [VM log](evidence/vm-diagnostics.log), and [validation](evidence/VALIDATION.md).

The initial disconnected pass collected no stock traces. The connected pass obtained stock/Inkbound toolbar, palette and erase pilots, plus stock25-cycle palette, red ink, PDF and EPUB observations. These are lossy metadata samples and operator reports, not complete call traces or calibrated optical measurements. Seven diagnostic executables were staged only in `/tmp` and have now all been removed. The observer is stopped and stock Xochitl is healthy. No product/system library, boot configuration or service was changed by the agent; no input was injected. The user has stopped manual testing. Continue with [offline binary/source analysis](OFFLINE_CONTINUATION.md); [current checkpoint](CHECKPOINT-20261014.md) supersedes earlier pending-test instructions.

## Evidence notation

`CONFIRMED` means directly established within the stated evidence class. `HIGH CONFIDENCE` means converging source/ABI evidence with an explicitly missing runtime step. `LIKELY` is a testable hypothesis. `UNKNOWN` has no sufficient observation. A confirmed source property is not a confirmed stock policy or optical result. `DEVICE_QUEUE_SAMPLE` is deliberately incomplete metadata sampling; `OPERATOR_REPORT` is human observation, distinct from a calibrated optical recording.

Evidence classes are `SOURCE`, `HOST_STATIC_ELF`, `VM_FAKE_VENDOR_ACTUAL_POLICY`, `PUBLIC_REFERENCE`, and, when available, `DEVICE_TRACE` / `OPTICAL`. Their separation is part of every conclusion.

## Identity and preservation

Authoritative checkout inspected: `/Users/zayway/Documents/ChatGPT/Remarkable Reading Experience/work/reader-integration`. Its parent Git repository has no commits. The captured 253-file `src` manifest digest is `eea410ecc130cbb4864c6023e50cc31ab901077424cab20d14fcf28f509bc908`.

The initial pass compared against historical20x and found source drift. The fresh device check identifies installed **21a**, app manifest `a0c929b782443a9fa955d1b79e2cd493e2ac0e801cd717360539f4b638a4c882`, actual executable `f7bed145e84de633ba1ce3cda82b17dc99b304e8b3d91c685be53b950d7a9eb8`. The sealed21a source verifies833/833 entries; the current working checkout matches794/833. [Exact identities](evidence/20261014/verified-21a-source-tree.json), [refresh-source comparison](evidence/20261014/refresh-source-21a-comparison.json). The decisive vendor/policy files and the shell damage/cleanup bodies match the captured source. Use [sealed21a source snapshots](evidence/installed-21a-source/src/) to attribute installed behavior.

The device verifies OS3.27.3.0/build20260612085811 and root partition `/dev/mmcblk0p3` (read-only root mount). Vendor SHA256 `3800e9f3d01f40fa3f5cc96ca49bcd982471ce5b033320f8788707520c8ddd08` matches the saved module and reader pin; Qt hashes also match. Xochitl SHA256 `9749880d…120e` has a distinct statically linked display path and existing XOVI/AppLoad/resource-rebuilder extensions. A read-only Xochitl copy and bounded disassembly are kept under ignored, local-only `private-device/`; they must not be published or included in an evidence export. No proprietary module was executed by these diagnostic helpers.

The Stage 1 findings above describe the observed, installed source. The user subsequently authorized implementation and explicitly required regional color cleanup. That work is isolated under `work/refresh-fix21a/reader`; the installed app and newer feature trees remain unchanged. See [Xochitl color evidence and current implementation](XOCHITL_LOCAL_COLOR_IMPLEMENTATION.md) and the workspace `CONTINUE-LOCAL-COLOR.md` checkpoint. Source/VM validation does not constitute device or optical qualification.
