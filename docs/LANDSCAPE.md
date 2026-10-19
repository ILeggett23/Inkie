# Landscape — design (2026-10-19)

The user asked for landscape and portrait as in Xochitl, with a rotation lock in quick settings. They chose **native,
staged** landscape: pages are re-rendered sharp for landscape and ink follows. They also approved changing the protected
prepared-page/view storage to add an orientation.

- **PR 1 (this work):** PDFs in landscape, with landscape reading chrome, the orientation sensor and the rotation lock.
- **PR 2:** EPUB landscape reflow.
- **Later:** library, settings, Notes and Inkie sheets.

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
- Only an open PDF in the reading view, without legacy (pre-styled) ink, rotates.
- The library, settings, EPUB, Notes, Inkie, document settings and menus stay portrait. Opening one of them shows it in
  portrait (turn the tablet), as stock does for screens without a landscape layout.
- The rotation lock (quick settings) defaults to locked. When auto-rotate is on, a sustained tilt changes orientation
  after it settles, and never during pen contact or while a page is preparing.

**7. Sensor.**
- `device::readAccel` reads the raw axes and applies the mount matrix.
- `device::OrientationWatch` classifies portrait, clockwise, anticlockwise or flat:
  - a 30° margin, so a tablet at an angle keeps its orientation;
  - the reading must hold for 3 polls at 300 ms;
  - flat or upside down keeps the last orientation.
- The first device run journals the raw readings (`ui:orientation`) to confirm the axis signs.

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
- Write, erase and select text in landscape, then turn back. Ink and highlights are where they were written.
- Swipe left and right. Pages turn in the direction the page reads.
- Lock rotation in quick settings and turn the tablet. Nothing rotates.
- Open the library, an EPUB, Notes or Inkie while turned. They show in portrait.
