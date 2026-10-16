# Shared refresh controller

This extends the installed QoL source `c16bafa174a2fafc34277b25c285e0c758b97a1b`. Reader, Notes and Tom share the existing Display/controller/vendor boundary. The implementation preserves regional RGB quality and vendor maintenance; it does not copy private Xochitl constants or claim to measure physical ghosting.

## Decisions

| Event | Decision |
| --- | --- |
| Human/generated ink and content playback | Existing fast bounded updates; record covered regions in shared history. |
| Settled UI, erase, color replacement, overlay dismissal | Existing regional quality, retaining the previous content class through removal. |
| Whole UI scene replacement, library grid/list changes, page turns | Compare canonical pixels with the software-submitted scene; refresh only changed regions with local quality. Large area/region count/color does not authorize a complete refresh. |
| Active pen/eraser or pending durable work | Retain scene/cleanup debt; defer cleanup. A valid prepared page retains the previous page-first bootstrap behavior without bypassing unsaved ink or contact checks. |
| Initialization, document/layout binding, security, sleep/wake, recovery | Explicit complete request with a named reason. These preserve the existing justified restoration paths. |
| Failed local cleanup | Preserve unsubmitted regions and the existing retry delay. Successfully submitted prefix regions are not repeated merely because a later region was refused; an unavailable vendor source is not passed to the compositor. |

`Change.full` now requests a canonical software scene replacement. It no longer means the vendor complete-update bit. Explicit complete requests carry `FullRefreshReason`; `VendorDisplay` rejects absent/invalid reasons and records `REFRESH_COMPLETE_REQUEST reason=... count=...`. This records application requests, not optical flashes or private vendor promotion.

The controller extends the previous 5-by-6 history grid and existing default 96-update limit to human ink and fast UI feedback. This is an Inkbound estimate, not a recovered Xochitl ghosting threshold. History can request regional cleanup only. Accepted UI quality or cleanup retires debt actually covered; a complete refresh resets the history. No new automatic global ghost threshold or background full refresh is introduced.

Scene comparison reuses one fixed 64-row strip (414,720 bytes) and the existing tile/padding/least-cost bounded merging behavior. It avoids another full-frame allocation, skips unchanged pixels, preserves distant damage and leaves stride padding untouched. The strip is composed only off-contact; the live pen path allocates no comparison storage. Previous/current RGB classification remains in the verified vendor adapter.

## Validation and limits

Regressions cover no-op/small/distant/full-scene pixel differences, edge clipping, output capacity, untouched padding, shared history/contact/save gating, normal quality retiring covered debt, mandatory complete reasons, long human writing, cleanup retry ownership and real library scene changes. Existing Reader/Notes/Tom, eraser/color, page-turn, selection, persistence and recovery suites remain required.

The portable damage/scene-diff test passes locally. Full host release, ASan/UBSan and original ARM SDK gates must pass at the final source before packaging. No new tablet operation, installation, live inference or Companion change is part of this implementation verification. Physical cleanup and exact Xochitl equality remain unqualified by software tests.

## Close intent during asynchronous work

One host run timed out waiting for Tom to close; at timeout it was active, ready, idle and had no unsaved canvas work. The original failure is retained. Source inspection showed that a Close tap during `view.busy`/pending work returned a notice and discarded the request. A deterministic panel fixture exercises that path, including a still-unsaved commit. Close intent is now retained; new handwriting admission stops, while `canLeave()` and `closed()` still require contact, saves, commands and failure guards to settle. Neither timeout nor durability checks are weakened. The exact tap-time state of the original CI timeout was not captured; this demonstrates and repairs the lost-intent path without asserting it was the only possible timeout cause.
