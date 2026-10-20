# Landscape — design (2026-10-19)

The user asked for landscape and portrait as in Xochitl, with a rotation lock in quick settings. They chose **native,
staged** landscape: pages are re-rendered sharp for landscape and ink follows. They also approved changing the protected
prepared-page/view storage to add an orientation.

- **PR 1 (this work):** PDFs in landscape, with landscape reading chrome, the orientation sensor and the rotation lock.
- **PR 2:** EPUB landscape reflow.
- **PR 3:** Notes and Inkie sheets upright, with the pen's fast path in them.
- **Later:** library, settings, document settings, Quick Browse and recents.

## What the code fixes today (studied before designing)

- **Every buffer is physical portrait.** `underlay::Width/Height/Stride` = 1620×2160 (stride 1632). The prepared page,
  ink layer, styled ink, Notes, Inkie, `clean_`/`chrome_` and lock frames are all this size. A landscape buffer of
  2160×1620 with stride 2176 holds exactly the same number of pixels (1632×2160 = 2176×1620 = 3,525,120).
- **The PDF viewport is fixed.** `pdfview::Width/Height` = 1620×2060 in `pdf_viewport.h`, the MuPDF backend and
  `preparePdfView`.
- **The PDF view already handles rotation.** It supports rotation, zoom, crop and pan, re-rendered sharp by the page
  helper. The prepared page records `pageToSurface`/`surfaceToPage`. Styled ink stores strokes in source coordinates
  and renders them through that matrix, as `pdf-view-route-tests` proves ("zoom rotation crop keep canonical ink source
  geometry"). Semantic text geometry is extracted through the same matrix.
- **The loader checks the view exactly.** `underlay.cpp` re-plans every PDF view page from its stored `view_settings`
  and requires the stored matrices to match `pdfview::plan` exactly (`view-ink-geometry`, `view-config-identity`).
- **Reading chrome is painted at portrait rects.** Toolbar, close, footer, handle, palette and strip are painted into
  `clean_` (page and footer) and `chrome_` (plus toolbar and close). Only four places build a canvas over shell frames.
- **Touch is physical.** It reports physical coordinates and recognises swipes, the top and bottom edge bands and the
  status corner with portrait constants (`gesture_policy.h`, `touch.cpp`).
- **Marker input is mapped to physical pixels** (`spike::Transform`, rotation 0). The fast pen path draws at the physical
  tip, so it is right in any orientation.
- **The sensor:** `lis2dw12_accel` (`iio:device0`, raw x/y/z, mount matrix `0,1,0;1,0,0;0,0,-1`). Xochitl's
  `orientationsensor` module and a quick-settings orientation lock are in the stock binary.

## Design

**1. Orientation is presentation, not a document setting.** The user's PDF view (zoom, crop, pan, rotation), view sets,
bookmarks and positions stay orientation-free. The device orientation is layered on when a page is planned:

- `pdfview::plan(bounds, view, orientation)`:
  - the viewport becomes the landscape content area in physical pixels, 1520×2160;
  - its origin is 0 or 100 px across, depending on which way the tablet is turned;
  - an extra quarter turn is added to the view rotation;
  - the stored pan is turned with the screen and clamped to the landscape range.
- Portrait planning is byte-identical, so existing caches stay valid.

**2. Prepared pages carry the orientation.**
- `preparePdfView` takes it as one more helper argument.
- It renders the raster at the planned size and places it at the viewport origin.
- It records `"orientation"` in the metadata, and in the `view_id` identity only when it is not 0.
- Each orientation has its own cache directory beside the portrait one.
- The loader re-plans with the orientation and checks it as strictly as today.
- Older caches have no field: they are portrait, unchanged.

**3. Logical and physical frames.** `ib::layout::Frame` maps points and rects both ways for portrait, landscape-left
(turned anticlockwise) and landscape-right (turned clockwise):

| Orientation | Logical size | Logical (x, y) to physical |
|---|---|---|
| Portrait | 1620×2160 | (x, y) |
| Turned clockwise | 2160×1620 | (y, 2159−x) |
| Turned anticlockwise | 2160×1620 | (1619−y, x) |

The page, ink, styled ink and semantic text stay physical. The chrome and its targets are logical.

**4. Landscape chrome.**
- The reader chrome geometry (`toolbarRect`, `toolbarSlot`, `closeRect`, footer, `topRect`, `toolHandleRect`,
  `penPanelRect`, `popoverBounds`) follows a global orientation, as it already follows the toolbar side. The landscape
  zones come from `layout::zones(side, Landscape)`.
- The shell rotates the physical frame into an upright scratch frame, paints the chrome there, and rotates the painted
  rect back.
- Targets are hit-tested in logical coordinates. Page operations convert through the frame (alongside `contentPoint`,
  which already maps for EPUB zoom).

**5. Input.**
- Touch takes the orientation and recognises in logical space, so swipes, edge bands and the status corner follow the
  screen (approved change to `touch.*`).
- Marker writing stays physical. Marker taps on chrome convert to logical before hit-testing.

**6. What rotates in PR 1.**
- **What turns:** an open PDF's reading view, when the page has no legacy ink or legacy highlight pinned to its portrait
  page (the same rule as a view change).
- **Works in landscape:** writing, erasing and the highlighter (ink stays in source coordinates), page turns by swipe,
  the toolbar's tools, undo and redo, the tool handle, the close button, the footer and the clock.
- **Still portrait for now:** text selection, Define, highlight colours, Notes, Inkie, the pen palette, document
  settings and menus, quick settings, recents, Quick Browse, pinch and two-finger pan, and page effects. In landscape
  these show "Turn the tablet upright for this" (`ui:turn-portrait-only`). Their upright layouts are the next step.
- **Library, settings and EPUB** stay portrait (EPUB reflow is PR 2).
- **When it turns:** Auto-rotate (quick settings) is off by default. When it is on, the reading view turns once the tilt
  has settled, and only between actions: no contact, saving, selection, panel, menu or sheet, and nothing preparing.
  Each turn prepares the page again (`ui:turn`, then `ui:turned` when shown).

**7. Sensor.**
- `device::readAccel` reads the raw axes and applies the mount matrix.
- `device::OrientationWatch` classifies portrait, clockwise, anticlockwise or flat:
  - a 30° margin, so a tablet at an angle keeps its orientation;
  - the reading must hold for 2 readings, one every 200 ms; while Auto-rotate runs the sensor samples at 100 Hz
    instead of its 12 Hz default (restored afterwards), so a reading takes a fraction of the 0.77 s it took at 12 Hz;
  - flat or upside down keeps the last orientation.
- The first device run journals the raw readings (`ui:orientation`) to confirm the axis signs.
- Readings come from their own thread (`device::AccelReader`). One reading takes about 0.75 s on the tablet; read on the
  shell's thread it held up every tap while Auto-rotate was on (D16).

## PR 2 (`feat/landscape-b`): upright surfaces and EPUB

**Upright in landscape now:** the pen palette, the document and reading-light menus, quick settings (status-corner
swipe or the clock), text selection with its strip and highlight colours, Define, and notices. Each is laid out in the
reader's frame, painted upright, and hit on the panel:
- The palette and menus attach to the landscape toolbar.
- `ib::status::panelRect()` puts quick settings at the reader's top right.
- `semantic::transient` places its panels from the passage turned upright; the washes stay in panel pixels.

**EPUBs turn.**
- **Turning:** stages a landscape twin of the book's layout through the existing stage helper: lines a third longer,
  the same type size, 590 tall instead of 800. The text-anchor mapping carries the reading position; turning back
  stages the book's own layout (the same id).
- **Nothing is activated.** Leaving a turned EPUB turns it upright first, so the book's own layout keeps the position.
- **Ink** stays on the layout it was written on, as with any layout change.
- **A refused turn** (the layout cannot be made) is not retried for that book until the tablet is held another way;
  a page pinned upright by older ink or highlights says so once (D17–D19).
- **Rendering:** turned pages use `pdfview::fitTurned` (`turn_fit.h`, shared with the PDF planner and the loader: exact
  float coefficients) and live in `<document>/turn-<o>/epub-views/<id>/page-N`. The EPUB store's reclaim and strict
  inspection cover that tree.

**Still portrait (a notice), with why:**
- **Notes and Inkie sheets** were portrait here; PR 3 turns them (below).
- **Document settings, Quick Browse and recents:** full-height portrait sheets with no landscape layout yet.

## PR 3 (`feat/landscape-c`): Notes and Inkie upright

**One sheet frame.** Notes and Inkie keep laying out and composing in their own portrait pixels ("sheet pixels",
1620×2160), so their stored geometry is untouched. Turned, `ib::layout::SheetMap` shows the sheet's lower 1620 rows
upright at the foot of the landscape screen, centred: sheet (x, y) is logical (x + 270, y − 540), and logical maps to
the panel through `ib::layout::Frame`. A turned sheet's top is never above `TurnedSheetTop` (540).

| Sheet | Upright top | Turned |
|---|---|---|
| Note, Inkie compact, Highlight, Inkie history | 1160 | unchanged; the 1480×680 writing area keeps its place |
| Inkie answer sheet and tall composer | 480 | 540; answer area {70,680,1550,1320} and writing areas unchanged; the one line between header and paper ("You asked: …") becomes the header's subtitle |
| Note expanded | 300 | 540; area {70,740,1550,2040} (note ink is relative to the area's top-left and scroll, so only the 1480 width is stored geometry) |
| Notes hub | 120 | 540, four notes a page |
| Layers | {720,120,1596,1640} | the same side sheet, {720,540,1596,2060} |
| Keyboard, pen pickers, ink actions | — | unchanged (all in the lower 900 rows) |

- Turned sheets have ink side rules (`contextSheet`), since they stand on the page.
- Inkie's reply zoom works turned: pinch and two-finger pan reach it through the same frame, and its badge sits in the
  sheet header, left of the tools (there is no page row above a tall turned sheet).

**Presentation.** The shell maps every path of an open sheet through `SheetMap`:
- compose: the shown window is composed by the panel in sheet pixels and put upright on the panel; the strips beside it
  show the reader scene the sheet opened over. The panel's own backdrop is that scene brought into sheet pixels.
- damage (`repaintArea`, `takeDamage`, `inkDamage`, `presentationDamage`, reply cleanup, picker regions): split into
  full-width sheet bands before mapping, so each presented piece covers whole sheet rows and Inkie retires its damage
  band by band as upright;
- `presentationCovered`: presented panel rects mapped back (rows above the shown window count as covered);
- finger taps, ink-selection drags, reply scrolling, pinch and pan. A tap on the page beside a turned sheet reaches it
  as a tap above the sheet, so it closes the sheet as a tap on the page above it does upright.

**Pen fast path** (approved protected change, `core.*` and `session.*` only):
- `spike::Transform` gains an optional `turn` (0, 90, 270) and `offsetX/offsetY`: after the marker mapping, the point
  is brought upright and offset, then kept on the surface. All zero is today's mapping, unchanged.
- `SessionDisplay::inputFrame(Transform&)` lets the display set them; `spike::engineTransform()` applies it whenever the
  session builds an Engine, keeping the marker's axes, size, rotation and mirror.
- `ux::Display` forwards to `Shell::inputFrame`: while a turned sheet is open, pen points arrive in sheet pixels, so the
  canvases, fast ink and damage need no other change. The shell moves its presented version on whenever that frame
  changes, so the session builds its next Engine before it admits another contact.
- `styled_ink.*`, `marker.*` and `touch.*` are unchanged.

**Tests.** `sustained-session-tests` (the frame's mapping, clamping and validity; `engineTransform` keeps the marker's
axes; the session's sink and fast ink in sheet pixels); `ib-layout-tests` (SheetMap round trips; every turned sheet
paints and hits only in the shown rows; Inkie's areas keep their place); `ib-preview` turned surfaces (targets baseline
extended, portrait unchanged); ux-behavior routes `turned_note_sheet_takes_ink_that_stays_put_upright` and
`turned_inkie_sheet_takes_ink_that_stays_put_upright` (open from the menu while turned, controls hit where drawn, write
through the session's Engine frame, stored points and pixels, regional updates only, the same ink after turning back).

## Delivery (commits on `feat/landscape-a`, one draft PR)

1. Sensor, watch and rotation lock (quick settings, `ui-prefs.json`). [auto] fixture sysfs tests.
2. Frame mapping and landscape reading-chrome geometry in the kit, with `ib-preview` landscape targets. [auto]
   `ib-layout-tests` round trips.
3. Landscape PDF planning, rendering and prepared metadata. [auto] `pdf-viewport-tests`, prepared round trip.
4. Shell: orientation state, logical chrome painting, hit-testing, Touch in logical space, Marker taps, page requests
   with orientation, forced portrait elsewhere. [auto] route tests: rotate, write, select, turn pages, lock.
5. Docs and device checks.

## Device checks (added to the stabilization report)

- Turn the tablet with a PDF open (auto-rotate on). The page re-renders upright and sharp; the toolbar, close and footer
  sit on the landscape edges.
- Write and erase in landscape, then turn back. The ink is where it was written.
- Long-press a word, open the palette or swipe down in landscape. A notice asks for portrait; nothing opens sideways.
- Swipe left and right. Pages turn in the direction the page reads.
- Lock rotation in quick settings and turn the tablet. Nothing rotates.
- Open the library while turned. It shows in portrait. (EPUBs turn since PR 2; Notes and Inkie since PR 3: device checks
  40–44 in the stabilization report.)
