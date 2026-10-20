# Inkie layout architecture — 2026-10-17 (written for Inkbound; zones unchanged by the Inkie redesign)

Base: `fix/ui-input-refresh` (PR #1, installed on the tablet). This work: branch `claude/zealous-bohr-qi7lnt`, draft PR #2 (the
`design/layout-cursive` work). Inkbound is a C++20 direct-display reader; every surface is painted by the presentation kit
(`reader/src/ib/`) into device-pixel buffers on the 1620×2160, 229 ppi panel (8 px ≈ 0.89 mm). There is no DOM, QML or scale
factor.

Sources, in precedence order: the user's instructions; verified on-device behaviour (`docs/refresh-reverse-engineering/`);
the shipping source; reMarkable documentation; the mockups. The mockup handoff folder lives on the user's Mac and was not
reachable from the cloud session that did this work, so this document maps **shipping surfaces from source**; a
mockup-by-mockup cross-check is listed as pending at the end. Deliberate source departures from the mockups (neutral R=G=B
chrome, `InkPrimary 0xff1b1b1b`, device pixels, 8 persisted inks, the 112 px toolbar, Source Sans 3 / Source Serif 4 / Caveat,
Tom's generated pen strokes) are kept.

Every rect below is printed by code (`ib::layout::zones`) and checked by `ib-layout-tests`.

## 1. The power user, as layout rules

| # | Rule | How the source enforces it |
| --- | --- | --- |
| R1 | Every core action is at most 2 interactions from the reading surface. | Selection strip carries Define, Highlight, Note, Tom, Copy; toolbar shortcuts keep two remembered tools; gestures cover undo, redo, recents, Quick Browse (§6). |
| R2 | Chrome is hidden while reading. | Clean state shows only the page, the quiet footer and (optionally) the toolbar handle in slot 0. |
| R3 | Nothing sits under the writing hand. | Toolbar on the side away from the writing hand (`setToolbarSide`); close on the far top corner; palettes attach to the toolbar edge. |
| R4 | No panel shrinks or repaginates the page. | Panels are opaque overlays composited over an unchanged page; content zone is always `{0,0,1620,2060}`. No layout change touches EPUB reflow. |
| R5 | Controls never move between screens or modes. | One zone per role; the three lower sheets (Note, Tom, highlight detail) share top, header and close position; the cursive switch changes glyphs only (targets identical, tested). |
| R6 | No confirmation for reversible actions. | Confirmations exist only for destructive, irreversible ones (delete note, delete conversation, delete permanently, remove highlight). |
| R7 | Finger navigates, Marker writes. | One gesture policy (`touch.cpp`, `gesture_policy.h`); hardware-eraser ownership per PR #1. Unchanged. |
| R8 | Targets ≥ 96 px (10.6 mm); visual size is separate from the hit area. | `theme::MinTarget`, `hitArea()`. |
| R9 | Gesture bands stay free for the system. | Popovers keep out of the top chrome row (y < 112), which contains the top-edge band; the footer band is chrome taps only. |

## 2. Zones

Hit-test rule for every zone: painters register targets from the same rect they paint (`Targets`, later targets win), and
input routes through those targets; `toolbarSlot()` is shared by the toolbar painter and the touch router. Reserved bands are
owned by the touch recogniser: taps there still reach chrome targets, swipes that start there are system gestures.

### Portrait, right-handed (toolbar left — default)

| Zone | Rect `{x0,y0,x1,y1}` | Contents and hit rule |
| --- | --- | --- |
| screen | {0,0,1620,2160} | |
| content | {0,0,1620,2060} | The page. Swipes turn pages; long press selects; Marker writes. Never resized by chrome. |
| footer | {0,2060,1620,2160} | Chapter and "n of N". Tap reveals chrome; swipe up opens Quick Browse. |
| toolbar | {0,0,112,2060} | 9 slots × 112: handle, pen shortcut, highlighter shortcut, eraser, selection, layers, undo, redo, document menu. `RailBlank` covers the rest so a tap there never reaches the page. |
| close | {1508,0,1620,112} | Circled close, opposite top corner. |
| palette | {110,112,582,1302} | Attached pen/highlighter/eraser palette, level with the pen shortcut; overlaps the toolbar edge by 2 px. Document menu attaches to slot 8 the same way. |
| strip / popover | {112,112,1620,2060} | Selection strip, highlight colours, definition card (`layout::popoverBounds`). |
| sheet | {0,1160,1620,2160} | Lower sheet: Note, Tom, highlight detail. 46% of height (see §2.3). |
| sheetExpanded | {0,300,1620,2160} | Tallest a sheet may grow (Note 300, Tom 480). |
| reservedTop | {0,0,1620,80} | Swipe down: Recents (book) / Recents section (library). |
| reservedStatus | {1080,0,1620,80} | Swipe down from here: quick settings (status panel). |
| reservedBottom | {0,2060,1620,2160} | Swipe up: Quick Browse (in a book). Same rect as the footer. |

### Portrait, left-handed (toolbar right)

Mirror image: toolbar {1508,0,1620,2060}, close {0,0,112,112}, palette {1038,112,1510,1302}, strip/popover
{0,112,1508,2060}. Content, footer, sheets and reserved bands are unchanged (the status corner stays top-right: it is the
device's, not the hand's).

### Landscape (the reading view; docs/LANDSCAPE.md)

The panel stays physical portrait (every buffer 1620×2160). In landscape the reading chrome is laid out in an upright
logical frame of 2160×1620 from these zones (`layout::setTurn`, `reader::toolbarRect`/`closeRect`/`footerRect`/
`toolbarSlot`/`penPanelRect`, `layout::popoverBounds`) and mapped to the panel by `layout::Frame`. The page, ink and text
geometry stay physical. `ib-layout-tests` checks that the landscape chrome equals these zones in both turns and that
Frame round-trips points, rects and pixels; `ib-preview` renders `reader-chrome-landscape`.

| Zone | Right-handed | Left-handed |
| --- | --- | --- |
| screen / content / footer | {0,0,2160,1620} / {0,0,2160,1520} / {0,1520,2160,1620} | same |
| toolbar | {0,0,112,1520} | {2048,0,2160,1520} |
| close | {2048,0,2160,112} | {0,0,112,112} |
| palette | {110,112,582,1302} | {1578,112,2050,1302} |
| strip / popover | {112,112,2160,1520} | {0,112,2048,1520} |
| side sheet / expanded | {1160,0,2160,1620} / {300,0,2160,1620} | {0,0,1000,1620} / {0,0,1860,1620} |
| reserved top / status / bottom | {0,0,2160,80} / {1620,0,2160,80} / {0,1520,2160,1620} | same |

Blocker found while specifying it: a 1000 px side sheet leaves 860 px of writing width, but Note and Tom strokes are stored
relative to a 1480 px writing area. Landscape needs a view transform for ink (storage unchanged) before it can ship.

### 2.3 Sheet height: a recorded deviation

The brief asks for lower sheets at 35–45% of the height. They stay at 1000 px (46.3%): the 1480×680 writing area is fixed by
stored strokes, and header (136) plus quoted passage need the remaining 320 px. Reaching 45% would move every header control
28 px and crush the subtitle line. Approved by the user on 2026-10-17.

### 2.4 Other surfaces in the system

| Surface | Rect / anchor | Zone rule |
| --- | --- | --- |
| Library mast, heading row | menu {62,40,174,152}; heading 202–290; content columns 96–1524 | Same mast on every library section and Settings. |
| Search \| create pill | {698,1992,922,2092} | Bottom centre of every library section; Quick Browse does not exist outside a book, so the band is free there. |
| Drawer | {0,0,474,2160} | Full-height left; sections, Tom, Shared, Leave/Wi-Fi, Trash, Settings. |
| Create menu | bottom edge 1990, x 540–1080 | Rises from the pill. |
| Layers sheet | {720,120,1596,1640} | Side sheet beside the toolbar; the page stays visible at the left. |
| Document settings | {600,120,1596,2040} | Side sheet. |
| New notebook | {200,240,1420,1520} | Centred card over the library. |
| Quick Browse | {0,1746,1620,2160} | Rises from the bottom band it is opened from. |
| Recents | {0,0,1620,560} | Drops from the top band it is opened from. |
| Keyboard | {0,1260,1620,2160} | Lower region, same family as the sheets. |
| Status panel | {876,160,1580,816} | Under the status corner it is opened from. |
| Zoom badge | {700,24,920,100} | Top chrome row; tap returns to fit. |

## 3. Grid, shape and rule

> **Inkie redesign (2026-10-17, v2):** cards have radius 12 and one 2 px ink rule with no shadow; lower sheets stay square with a 10 px coloured top edge; radii are 8/12/16; dividers are dotted ink. See `docs/INKIE-DESIGN.md` §3. Spacing, zones and targets below are unchanged.

**Spacing** (`theme.h`, device px): S1 8, S2 16, S3 24, S4 32, S5 48, S6 64, S7 96. Gutter 48 (screen sides);
`layout::SheetMargin` 70 (sheet content); library content 96–1524; `layout::TopRow` 112 (top chrome row, = toolbar item).
Targets: MinTarget 96, ComfortTarget 112, TargetGap 16.

**Radius**: native surfaces — toolbar, palettes, popovers, document menu, lower sheets, buttons, primary actions — are square.
RadiusSmall 10 (wells, keys, quote blocks), RadiusMedium 18 (small menus, panels), RadiusLarge 28 (notice cards), pill =
height/2 (chips, search pill, toggles). Fixed here: the Notes hub and highlight detail used a rounded 34 px sheet; both now
use the square `contextSheet` like Note and Tom.

**Rule weight**: one UI rule, `Hairline` 2 px; `StrokeStrong` 3 px marks the current item. Ruled-paper writing guides stay
1 px because they are paper, not UI. Fixed here: nine 1 px UI rules (list dividers, the selection-strip dividers, the
definition-card action rule, Tom's footer rule, the pill divider, the ink-actions popup).

## 4. Type roles

> **Inkie redesign (v2):** role sizes are unchanged. From `Label` (30 px) up, UI faces draw in Caveat at role × 1.25 (ScriptBold for semibold); `Control`, `Meta` and `Caption` stay printed, as do `Serif` reading text, italic serif (the book's voice) and `Literal`. Plain text restores the faces listed here. See `docs/INKIE-DESIGN.md` §4.

Every text size is a role from `theme.h`; about 45 literal sizes were mapped to the nearest role (all within 2 px, except
the passcode keypad 52→58 and the sleep title 54→58). Checked against the base tree with `ib-preview --targets` (every
registered target on every previewed surface, both hands): all targets are identical except the new Plain-text row, the
library wordmark's hit area (2 px wider each side: 37→38 px) and the Settings › Reading › Auto sleep choice cells (2–12 px
left: their 27 px labels now use the 28 px Control role).

| Role | Size / line | Default face | Uses |
| --- | --- | --- | --- |
| Code | 110 / 120 | Source Sans 3 | Companion pairing code |
| Display | 72 / 84 | — | Reserved |
| Title | 58 / 70 | Source Serif 4 Subhead, Sans | Wi-Fi and Companion page titles, sleep title, keypad digits |
| Heading | 44 / 54 | Sans, Serif Semibold | Section and settings titles, definition word, dialog titles |
| Subhead | 38 / 46 | Sans (Semibold) | Sheet and popover titles, wordmark, key caps |
| Body | 34 / 50 | Source Serif 4 | Definitions, typed notes and questions, quotes, inputs |
| Label | 30 / 38 | Sans | Controls, list titles, menu rows, settings rows |
| Control | 28 / 36 | Sans | Labels inside width-fitted controls: selection strip, choice cells, steppers, text actions. Kept at 28 so no width-fitted control moves |
| Meta | 26 / 34 | Sans | Metadata, settings notes, subtitles |
| Caption | 23 / 30 | Sans | Smallest permitted text (2.6 mm) |

## 5. Cursive roles

> **Inkie redesign:** cursive is no longer limited to the roles below: every word from `Label` up is written. The roles remain the places whose plain-text face differs, and Tom's typed reply still follows his pen sizes. The candidate and legibility work below set the smallest script size (30 px role → 37.5 px).

Cursive is a face (`Face::Script`, Caveat) used only through the roles in `ib/type.h`. Each names its plain face; `text.cpp`
resolves the face once at the leaves (measure, metrics, draw), so wrap/fit/block and every painter are unchanged. Caveat is
drawn at `ScriptScale` 1.25× the role size. Rects and line heights come from the role, so a face switch swaps glyphs inside
unchanged rects (all registered targets identical in both modes, tested on library, settings, Notes hub, Tom).

| Role | Cursive? | Plain face | Justification |
| --- | --- | --- | --- |
| Tom's typed reply (`voice`) | Yes | Source Serif 4 | Keeps Tom's voice when pen strokes are off. Follows Settings › Tom › Handwriting size (role 34/40/48 → 42.5/50/60 px = Tom's pen sizes 42/50/60). |
| Section heading, one per screen (`ScriptHeading`): library section, "Settings", Notes hub | Yes | Source Sans 3 | A single short line read once on arrival, never scanned; it is the only cursive on most screens. |
| Notes-hub note titles (`ScriptTitle`) | Yes | Source Serif 4 | The hub pages 6 rows at a time with a thumbnail, colour rule and source line per row, so titles are recognised, not scanned in long lists; titles are short. |
| One-line empty states (`ScriptEmpty`): "Nothing here yet", "Trash is empty", "No matching titles", "No notes in this view.", "Notes you write while reading appear here.", "Books you open appear here.", "No networks found", "No matching time zones" | Yes | Source Serif 4 Italic (as before) | One short line each. |
| Notes-hub previews of typed note text | No | Source Serif 4 | The user's own words; 2-line previews are read for content. Cursive here could also read as their ink. |
| "Write to Tom…" / "Continue the conversation…" placeholders | No | Serif Italic | Grey cursive on ruled paper would look like faint handwriting where the user is about to write. |
| Settings page titles beside "Settings" | No | Sans | Repeats the selected nav label; one cursive heading per screen. |
| Pull quotes, linked passages | No | Source Serif 4 | Book text stays book type. |
| Controls, menus, labels, settings rows, numbers, metadata, dates, page counts, search fields | No | Source Sans 3 | Scanning, alignment and numerals. |
| Book body text | No | — | Rendered by the document engines, untouched. |

### Legibility at 229 ppi (host-measured from the candidate sheets)

| Role size | Caveat drawn | x-height | Width vs Source Serif 4 | Verdict |
| --- | --- | --- | --- | --- |
| 30 | 37.5 px | 13 px (1.44 mm) | 0.93 | Rejected: under the 14 px (1.6 mm) floor |
| 34 (minimum) | 42.5 px | 15 px (1.66 mm) | 0.93 | Accepted; line 50–52 px |
| 38 | 47.5 px | 17 px (1.89 mm) | 0.93 | Accepted |
| 44 | 55 px | 19.6 px (2.18 mm) | 0.93 | Accepted (headings) |

- Numerals and punctuation: Caveat has its own (handwritten) numerals and punctuation; cursive roles carry numbers only
  inside Tom's prose and note titles. Dates, page counts and metadata stay Sans.
- Non-Latin: Caveat covers Latin, Latin Extended and Cyrillic. Greek falls back to Source Serif 4 Italic at the role size
  (not the 1.25× cursive size). CJK has no bundled face in any role (unchanged).
- High contrast: cursive is drawn in `InkPrimary` (unchanged) or `InkSecondary` (empty states, darkened to `0xff2b2b2b`).
- Colour layer: cursive multiplied under a highlight wash keeps dark strokes (sheet row "colour wash").
- Regional refresh: in a simulated fast 1-bit pass and a 16-gray quality pass every size keeps its letterforms and joins on
  the host; 30 is rejected on the x-height floor alone. The simulation is a threshold, not the panel's optics: **panel
  confirmation pending.**
- The switch: Settings › Accessibility › "Plain text instead of cursive" (`ui-prefs.json` `plainText`, optional field,
  version unchanged). It is applied like High contrast and repaints through the existing old/new regional library diff.
  No complete update, no new timer, no waveform constant.

### Family with Tom's handwriting and the user's ink

- `ib/handwriting` (Tom's pen in the Tom sheet) traces Caveat itself: same x-height, slant and weight by construction, and
  the Voice sizes now land on the pen sizes.
- Tom's native-ink alphabet (`handwriting_ink.cpp`, saved replies, mixed replies and diagram labels) is a separate centre-line
  alphabet: at its default size 34 the cap height is 34 px, x-height 20.4 px (0.6 of cap), slant 0.10 (5.7°), fineliner
  "thick". Caveat at the Body role is 15 px x-height, 0.51 x/cap. They do **not** match today. Matching x-height would need
  Caveat at 57 px (lines 34% wider) or a smaller native size (changes Tom's stored ink geometry). Left as an open decision;
  not changed in this PR.
- The user's own ink is always real strokes in the styled-ink layer; no cursive role draws into a writing area.

### Candidates

Caveat stays. Joined OFL/Apache faces were compared through the real text path (`ib-preview --script-sheet`):

| Face | Licence | x-height at role 34 | Width vs Serif | Verdict |
| --- | --- | --- | --- | --- |
| Caveat (bundled) | OFL 1.1 | 15 px | 0.93 | Ships; Tom's pen is traced from it |
| Dancing Script | OFL 1.1 | 16 px | 0.99 | Runner-up: joined, but hairline connectors thin out in a 1-bit pass |
| Homemade Apple | Apache 2.0 | — | 1.49 | Rejected: would reflow |
| Playwrite GB S | OFL 1.1 | — | 1.37 | Rejected: would reflow |

Sources and digests: `reader/assets/fonts/manifest.json` (`evaluatedCandidates`, not packaged); licences:
`reader/notices/fonts/CANDIDATES.md`.

## 6. Interaction budget

Interactions counted from the clean reading surface (a tap, swipe or stroke each). Before = `fix/ui-input-refresh`.

| Task | Before | After | Path |
| --- | --- | --- | --- |
| Resume the last book | 1 | 1 | Tap its cover in My files |
| Switch book while reading | 2 | 2 | Swipe down from top edge → tap a recent |
| Write | 0 | 0 | Marker on the page with the remembered tool |
| Switch pen ↔ highlighter | 1–2 | 1–2 | Tap the shortcut (toolbar shown) / handle + shortcut |
| Undo / redo | 1 | 1 | Two- / three-finger tap |
| Select | 1 | 1 | Long press (or a highlighter stroke) |
| Define | 2 | 2 | Select → Define |
| Note on a passage | 2 | 2 | Select → Note |
| Ask Tom about a passage | 2 | 2 | Select → Tom |
| Ask Tom about the page | 3 | 3 | Handle → document menu → Ask Tom |
| Return to reading | 1 | 1 | Close the sheet or card |
| Jump within the book | 2 | 2 | Swipe up from the bottom → tap the track |

No task got longer. The change in this PR that touches the loop is placement: a definition card that cannot fit below its
word no longer lands over the close or the handle, so "return" is never blocked by a card.

## 7. Power-user affordances

| Affordance | Status |
| --- | --- |
| Remembered per-tool settings and both toolbar shortcuts | Shipping (primary/secondary shortcut, colours per shortcut). |
| List/grid toggle and sort in My Files | Shipping. |
| Search | Shipping in the library (pill). **Unsupported in a book**: `Chrome::search` exists but nothing routes it. Deferred. |
| Recents/favorites via the native gesture | Shipping: top-edge swipe; Favorites in the drawer. |
| Two/three-finger undo/redo | Shipping. |
| One-gesture Quick Note | **Unsupported.** Needs a new gesture in the protected touch path; proposal in PR #2. |
| Manual clean refresh | **Unsupported.** Needs a user-requested complete update through the protected vendor refresh authority; proposal in PR #2. |
| Density preference | **Deferred**, no control added. |
| Type Folio shortcuts | **Unsupported**: Inkbound opens only the touchscreen and Marker evdev devices; no keyboard events reach it. |

## 8. Systems constraints held

- Refresh: opening/closing any panel damages the union of old and new bounds (`notes::Panel::mark`, shell panel damage);
  the highlight sheet's new top is covered by that union. State edits submit changed-pixel diffs. No full-update flag,
  waveform constant or timer was added. No EPUB reflow path is touched.
- Input: no change to marker, session, touch or styled-ink code. Popover bounds are a placement change only.
- State: no change to reading position, zoom, tools, toolbar side, unsent handwriting or Tom storage; `ui-prefs.json` gains
  one optional field.

## 9. Tools

- `ib-preview <reader> <out>` — every kit surface (both hands where chrome differs; normal, high contrast, plain), zone maps
  (portrait and landscape spec), and the Caveat cursive sheet. `--script-sheet <name>` renders the sheet for whatever
  `Caveat.ttf` the font directory holds. Painter-level: no shell compositing, refresh scheduling or panel optics. CI uploads
  the PNGs as `inkbound-host-previews`.
- `ib-preview <reader> <out> --targets` — every surface's registered targets as text; diff two trees to find any moved
  control.
- `ib-layout-tests <reader>` — zones vs shipping rects, target containment, popover placement grid, landscape coherence,
  cursive resolution and width, plain/cursive target parity, controls and pen strokes unaffected, one close position.

## 10. Pending physical validation

- Cursive legibility at 34/38/44 on the panel, after regional fast and quality passes, under the colour layer, in high
  contrast.
- Highlight sheet at its new top: open/close leaves no stale region.
- Side-by-side with Xochitl for toolbar, palettes and sheets.
- Mockup-by-mockup mapping of the 34 `inkbound-consistent/` screens and `redesign4-previews/` against §2.4 (handoff folder not
  available to this session).
