# Reader erasing, notebook scope and toolbar references

## Installed-build finding

Read-only tablet diagnostics on 2026-10-20 matched the installed manifest
`0ec952d43ec8867e824edc43c0b3d2e700f4e58533e1d66be64294c11d0bbc26` and app
`ea280b1ddd18e0822fefc65e4cf9fd3e371fb8e85f123e80771ffcba46371064` to PR11's
ARM qualification job118189314394. That build removed ink with quality updates while the eraser moved.
It was subsequently reverted by PR13 after the owner reported incomplete erasing. PR12's background
tilt reader and PR13's restored masked live eraser were not installed. Do not confuse a green PR with
the build actually on the tablet.

This candidate starts from PR13 `fe5b2f3`, retains its fast masked erase and PR12's nonblocking
orientation reader, and includes the separately owner-approved PR10 landscape Notes/assistant
implementation. It does not merge those GitHub pull requests or deploy Companion. PR3's older
busy-empty Notes fix overlaps the retained `NotePresence` path and is not blindly reapplied.

## Standard erase contract

The same contract applies to Marker Plus and toolbar erasers, with black, coloured and highlighter ink:

1. Give immediate masked regional feedback through the established pen/erase path.
2. Queue a regional removal-quality presentation at the first safe boundary after contact ends.
3. Keep the removed material's content classification through that presentation and the final saved/rendered
   erase result. An accepted early cleanup can pay the grid's debt while the RGB source already looks white;
   that must not erase the information required by the final cleanup.
4. Preserve neighboring content, retain failed cleanup for retry, and settle the removed-content history
   only after the final render/save and pending presentations are clear.

The existing regional Content-mode removal path and complete-update policy remain unchanged. There
are no new waveform numbers, new full-update flags, invented stock padding, erase delays or dark-fill
passes. `ui:erase-lift-to-submit-us` records software processing-lift to a timestamp immediately before the first overlapping successful
cleanup call; it is not an optical-completion measurement.

Six production Shell/Display/vendor-boundary regressions exercise the three materials with both erasers.
They check masked live feedback, preservation of the removed class after white pixels/grid payment,
the later vendor request, the restored page/neighbor, absence of complete requests and retirement of
temporary history. The vendor boundary is simulated in these tests; physical cleanup remains a device gate.

## Stock evidence and limits

Three stock black contacts had first local quality queue observations27.649–34.615ms after digitizer
lift; three red contacts22.779–36.609ms; three yellow-highlighter contacts30.191–36.093ms. These
are positive observations from partial metadata samples, not exact request timestamps or a fixed30ms law.
The owner initially reported all clean without a whole-screen flash, then reported yellow tint after red
and pale yellow tint after yellow highlighter on the later white-paper check. Preserve both reports.
The owner identifies the residue as physical pigment; source-pixel whiteness was not independently read.

Matching stock's request structure does not prove completely residue-free Gallery3 output. The candidate
must be checked on the panel before claiming an optical match. Never copy stock embedded raw1/14 or2/16
values into the standalone module's public content/mode enums.

## Reading focus and data preservation

Standalone notebook creation has no library button, creation menu, Shell command or asynchronous PDF
generation path. Existing notebooks remain readable as their original PDFs; existing notes, annotations,
imports, storage formats and data are retained. Search occupies the former bottom search/create control.

Book-linked Notes remain a reading feature. Saving an assistant reply without an open PDF/EPUB asks the
reader to open a document and preserves the reply in History; it no longer starts notebook creation.
Legacy format/view helpers used by compatibility tests are not a live creation route in the application.

## Toolbar references

The owner supplied IMG_0640–0642. The tool family is redrawn as original vectors: upright monochrome
nibs, the filled chisel highlighter, dotted eraser, dotted selection rectangle with four square handles,
stacked diamond layers, and square selected tiles. A separate colour dot identifies the rail tool's ink.
The palette uses the reference order while stable semantic tool IDs and persisted tool/ink values stay fixed:

| Ballpoint | Fineliner | Highlighter |
|---|---|---|
| Pencil | Mechanical pencil | Calligraphy nib |
| Marker | Paintbrush | Shader |

Only presentation/target placement changes. No vendor images are extracted or bundled; fonts and stored
palette values are unchanged. Software previews test geometry/painting, not physical panel colour or ghosting.

## Release checks

Require current-head host release, ASan/UBSan, ARM SDK and Companion contract checks. Verify the package's
exact source/manifest, both executables, protected payloads and rollback/data boundaries before seeking
installation approval. Verify the combined portrait/landscape Notes, assistant, selection and erase paths,
and retain PR12's nonblocking tilt behavior. Tablet installation, launch, paid calls, Companion deployment
and GitHub PR merges remain separately authorized steps; none is implied by a software test pass.
