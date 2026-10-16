# Inkbound 21b behavior and cleanup repair

Status: installed and payload-verified; full release and sanitizer retry pass; native Companion/Tom connectivity passes. Visible cleanup/UI acceptance is pending. The app has not yet been launched for that check.

## User-visible changes

- Black erasing uses a per-pixel vendor erase mask derived from actual composed RGB changes, followed by the existing color-aware local cleanup. Unchanged neighboring black, red and shaded pixels are excluded from the live erase mask.
- Document/Notes menus and the collapsed toolbar handle are composed above page ink. Dismissal retains bounded restoration and local cleanup.
- Selection remains selected when its toolbar closes, including after a page/view rebind; the hidden toolbar stays hidden.
- Both writing shortcuts store their own tool, color and width. Any supported tool can occupy either shortcut, including the same tool type in both.
- Definition cards support Expand/Collapse and reading controls for longer entries. Finger hold still opens only the definition, including supported dash-connected compounds. Explicit text-selection actions still accept the pen.
- PDF pinch completion immediately presents a correctly transformed temporary preview while the canonical page/ink binding finishes. Pen admission remains gated until that binding is ready. EPUB and final PDF rendering retain their regional refresh paths.
- Existing RGB-aware color cleanup, Notes/Tom overlay retirement, illustration download/resume and bounded transfer continuation remain included. The book limit remains 64 MiB.
- A checksum- and identity-bound terminal receipt retires an already imported but remotely canceled delivery, preserving its local book and allowing newer queued files to proceed.

## Why the erase repair differs from the earlier candidate

The user identified the remaining black erase artifact as a faint ghost/shadow. The disposable page's recorded stroke history corroborated complete black-stroke removal while preserving a nearby red stroke. That supports a display cleanup problem rather than changing the eraser's geometry.

Static inspection of the exact pinned vendor module shows that its masked Pen conversion reads an independent Gray8 mask as well as RGB. The mask starts zero-filled. Selecting the masked update flag without supplying that mask is insufficient; that incomplete experiment was never installed. R10 captures the separate validated mask, supplies only actual changed pixels for the bounded update, and restores the original mask bytes after the synchronous vendor source-copy/conversion boundary. It leaves proprietary binaries, the OS and boot configuration untouched. The earlier stock traces observed local erase feedback mostly in lower class 1/14, while old Inkbound used 1/13. These private lower fields are distinct from public API enums, and Xochitl has a different display ABI. The R10 mapping is verified only against the pinned standalone module. This establishes the software data flow and a closer match to the observed stock erase class; tablet observation must establish whether the faint ghost is resolved.

## Evidence and qualification boundaries

The production app was freshly built from R10: 845 files, manifest `943ebcde5edde7e7b9ebbbea86ab2b9f154d445e9048e161cb0b003267bc1b8b`. All pinned build variants and four native export audits passed. The original full gate passed seven of nine stages; release and sanitizer suites stopped on the same obsolete library test assumption. A separate diagnostic showed the selected secondary shortcut correctly persisting from PDF to EPUB, so a second tap closed the palette before the test attempted its Snap control.

The final R11 validation source, manifest `e93355c6077d6a9355527fc9826fbdf4a5ee98da45b79cba8870ad96ec36e13c`, differs only in `tests/library_route_tests.cpp`. It opens the palette according to its actual state and still requires Highlighter selection and Snap persistence. All 29 library cases, all 129 release invocations and the complete ASan/UBSan list pass. The 25 UX and 30 semantic cases pass in both configurations. All 845 source inputs and 154 production artifacts match before/after; effective compile identity covers 702 source inputs with the single explicitly recompiled test overlay. No sanitizer report or source/compile mismatch was found. This qualifies the exact app bytes; it does not recast the original failed run as a clean pass. See `qualification-r11/QUALIFICATION.json` and the retained base/retry evidence archive.

Companion source: 31 production inputs, manifest `e8105a3641e7eb9aa42e351885bf9cff331592af20466b682d2679bf0396c0fb`. All 125 Node tests and browser checks at 390/768/1280 pixels pass. Version `990ac5ed-92fa-475b-b652-abd20ab09fe8` serves 100%; runtime and all bindings match the retained baseline, public health passes, and the three checked live JavaScript assets match source exactly. Baseline `54476a93-db0f-4f9f-9456-3ab67cd9ea03` is retained for rollback. No migration, queue reset or new provider inference was initiated.

The previously installed candidate's normal exit produced a durable report with zero unsaved data, and the owner restored Xochitl. Its pinned provisional E2 profile remains unchanged. This release does not claim broader lifecycle qualification or optical equivalence to every Xochitl policy. Automated vendor fakes and static binary inspection do not substitute for visible tablet acceptance; no camera is available.

## Installed tablet and native validation

All 29 payload files verify under app manifest `471b5d004fdbf02c0f008ac29593717594bcdb128f18a8615852aa83198112d0`. Book/note data hashes and metadata were unchanged at installation, and Xochitl remained active. Rollback is `/home/root/inkbound-e2/E/app-21b-20261015-pre-r11`, manifest `364071dcee85fbadcf382c87fe2f1fa3a951714944855d7535039a5750d8de71`. System libraries, firmware, SSH and boot configuration were not altered.

The new native helper completed one bounded incoming Companion sync, returning to paired with zero pending jobs. The real previously blocking receipt now records `remote_terminal: canceled`, imported=true, acked=false; its already imported book source still matches the original checksum. No queue reset or new production upload was used. Tom configuration GET succeeds with scene version5,16384 scene tokens and the existing illustration model. No new provider inference or illustration generation was initiated; those observations do not qualify live provider output.

The user confirmed the earlier black erase residue was a faint ghost/shadow. The newly installed masked erase has not yet been exercised on the tablet. Final visible acceptance, the native first-erase mask-restoration log and this candidate's normal owner exit remain pending. The stock app is left running until the user is ready for supervised validation.
