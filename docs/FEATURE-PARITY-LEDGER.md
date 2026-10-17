# Feature parity ledger — layout architecture and cursive role

Every shipping capability on `fix/ui-input-refresh`, where it was, where it is after PR #2, and a verdict. Locations are
zones from `docs/LAYOUT-ARCHITECTURE.md` §2 or the named surface. "Same" means the same rect and the same target id.
Method: besides reading the painters, `ib-preview --targets` was run on the base tree and on this branch and the two
target lists diffed (500 targets on 33 surface states, both hands). Verdicts: **Kept** (unchanged location), **Kept, restyled** (same location and target; glyphs, size role or rule weight
changed), **Moved** (location changed; justified), **New**. There are no **Removed** rows.

## Library (My files, Favorites, Recents, Trash)

| Capability | Before | After | Verdict |
| --- | --- | --- | --- |
| Drawer menu button, device status | Mast | Same | Kept |
| Wordmark (home) | Mast, hit {710,40,909,152} | Hit {708,40,911,152} (Subhead 38 instead of 37) | Kept, restyled |
| Section heading | Heading row, Sans 44 | Same rect; cursive heading role (plain: Sans 44) | Kept, restyled |
| Sort (Last opened / title) | Heading row right | Same | Kept |
| Grid / list toggle | Heading row right | Same | Kept |
| Folders, breadcrumb, Up | Under heading | Same | Kept |
| Document grid (3 columns) / list rows, favorite star, covers, format treatment | Content column 96–1524 | Same | Kept |
| Empty states ("Nothing here yet", "Trash is empty", "No matching titles") | Content top, Serif Italic | Same position; cursive empty role (plain: Serif Italic) | Kept, restyled |
| Search field and clear | Heading row (search mode) | Same | Kept |
| Search \| create pill | {698,1992,922,2092} | Same | Kept |
| Pager | Right of pill | Same | Kept |
| Create menu (Notebook) | Rises from pill | Same | Kept |
| Manage menu: Rename, Move to folder, Move to Trash, Restore, Delete permanently (+confirm) | Anchored menu | Same | Kept |
| Notice line | {96,292,1524,334} | Same | Kept |

## Drawer

| Capability | Before | After | Verdict |
| --- | --- | --- | --- |
| My files, Notes, Favorites, Recents | Drawer {0,0,474,2160} | Same | Kept |
| Tom, Shared | Drawer, after rule | Same | Kept |
| Leave Inkbound / Wi-Fi (user-ended session) | Drawer foot | Same | Kept |
| Trash, Settings | Drawer foot | Same | Kept |

## Notes hub and notes

| Capability | Before | After | Verdict |
| --- | --- | --- | --- |
| Hub heading ("Notes") | Library heading row / sheet header | Same rect; cursive heading role | Kept, restyled |
| Filters: All, Linked, Pages, Favorites, Tom | Chips under heading | Same | Kept |
| Entry: thumbnail, colour rule, title, excerpt/body preview, source, star | 250 px rows | Same rows; title in cursive title role, preview unchanged | Kept, restyled |
| Hub empty states | Content top | Same; cursive empty role | Kept, restyled |
| Hub pager, close, create, layers (sheet form) | Sheet header / bottom | Same | Kept |
| Hub as a sheet over a book | Rounded sheet from y=120 | Square native sheet from y=120 | Kept, restyled (one sheet shape) |
| Note sheet: close, delete (+confirm), all notes, favorite, expand, Tom, select ink | Header, right to left | Same | Kept |
| Note: passage quote, writing area 1480 wide, pen picker, colours, undo/redo, type/ink, save | Lower sheet | Same | Kept |
| Note ink actions: smaller, larger, colour, export, delete, playback speed, finish, retry | Writing area popup | Same; popup outline 1 px → 2 px | Kept, restyled |
| Highlight detail: colours, undo/redo, Write a note, Go to source, Remove (+confirm), close | Lower sheet from y=1220, rounded; close at {1508,1228,1604,1328} | Lower sheet from y=1160, square; close at {1478,1180,1574,1276} (= Note's close) | **Moved**: the three lower sheets now share top, header and close position, so the close never moves between them (rule R5). Sheet damage is the union of old and new bounds. |

## Reader chrome

| Capability | Before | After | Verdict |
| --- | --- | --- | --- |
| Clean footer: chapter, "n of N", menu mark when the handle is off | Footer {0,2060,1620,2160} | Same | Kept |
| Toolbar handle | Slot 0 | Same | Kept |
| Toolbar: pen shortcut, highlighter shortcut, eraser, selection, layers, undo, redo, document menu | Slots 1–8, toolbar side | Same | Kept |
| Handedness (toolbar side) | Settings › Reading › Toolbar position | Same | Kept |
| Circled close | Opposite top corner | Same | Kept |
| Zoom badge (tap to fit) | {700,24,920,100} | Same | Kept |
| Pen palette: 9 tips, 3 widths, 8 colours (persisted order), Snap to Text | Palette zone | Same | Kept |
| Document menu: View and zoom / Text and layout, Page overview, Bookmark, Document notes, Ask Tom about this page, About | Attached to slot 8 | Same | Kept |
| Reading light menu: Off, Lower, Higher, Back | Attached to slot 8 | Same | Kept |
| Quick Browse: track, return to start, previous/next page | {0,1746,1620,2160} | Same | Kept |
| Recents sheet: 5 books, All books | {0,0,1620,560} | Same | Kept |
| Layers: add, rename, lock, up, down, delete, activate, visibility, undo/redo, pager | {720,120,1596,1640} | Same | Kept |
| Document settings: View, Reading, Pages, About; fit, zoom, rotate, crop, pan; font size, line width, family, spacing, margins, alignment; page overview; bookmarks; quiet effects | {600,120,1596,2040} | Same | Kept |
| New notebook: template, title, folder, pages, import | {200,240,1420,1520} | Same | Kept |

## Selection, highlight, definition

| Capability | Before | After | Verdict |
| --- | --- | --- | --- |
| Selection wash and handles | Over the passage | Same | Kept |
| Selection strip: Define, Highlight/Colour, Note, Tom, Copy | Above the passage (≥112), else placed near | Same rule; fallback bounds now start below the top chrome row | Kept (placement fix) |
| Highlight colours (7) and remove | Beside the strip | Same | Kept |
| Definition card: close, expand/collapse, previous/next, Write note, Ask Tom | Below the word, else placed near (could reach y=24) | Below the word, else placed near within the popover zone (y ≥ 136) | Kept (placement fix: no longer covers close/handle) |

## Tom

| Capability | Before | After | Verdict |
| --- | --- | --- | --- |
| Sheet header: close, history, expand/half, ink tools; title and source line | Lower sheet header | Same | Kept |
| Quoted passage / "You asked" line | Under header | Same | Kept |
| Writing area (stroke question) and typed question | {70,1360,1550,2040} / expanded | Same | Kept |
| Reply in Tom's pen (reveal, finish writing) | Writing area | Same pixels (tested) | Kept |
| Typed reply (accessibility) | Serif 34 / 52 regardless of hand size | Cursive Voice role; follows Handwriting size (34/40/48 → 42.5/50/60 px); plain text gives Serif at the role size | Kept, restyled |
| Reply scrolling up/down | Answer area right edge | Same | Kept |
| Footer: clear, keyboard, Save as note / Open saved note, Explain this, Send/Stop, Retry/Send again, status | Footer row | Same; footer rule 1 px → 2 px | Kept, restyled |
| Output picker and detail (reply, typed, write, annotated, illustrated, detail) | Picker | Same | Kept |
| Mixed handwriting + illustration replies | Writing area | Same | Kept |
| History: conversations, questions, paging, back, new, delete (+confirm) | Lower sheet | Same | Kept |
| Offline / consent / provider status | Status line | Same | Kept |

## Settings

| Capability | Before | After | Verdict |
| --- | --- | --- | --- |
| Page list (General … About) | Nav column | Same | Kept |
| "Settings" heading | Nav column top, Sans 44 | Same rect; cursive heading role | Kept, restyled |
| General: time zone, Leave | Pane | Same | Kept |
| Wi-Fi: status, Choose; Wi-Fi view (networks, password, "No networks found") | Pane / full screen | Same; empty line in cursive empty role | Kept |
| Battery, Storage, About readings | Pane | Same | Kept |
| Security: passcode set/change/off, Companion manage, "What Tom sends" | Pane | Same | Kept |
| Accessibility: High contrast, Reduce motion | Pane | Same rows | Kept |
| Accessibility: Plain text instead of cursive | — | New "Text" group, last on the page, so no existing row moves (id 992) | **New** |
| Reading: toolbar position, handle, progress, page-turn effects, sleep on cover | Pane | Same | Kept |
| Reading: Auto sleep choices (Never … 60 min) | Pane row | Same row; cell edges 2–12 px further left (27 px labels → Control role 28) | Kept, restyled |
| Tom: replies in, handwriting size (now also sizes typed replies), line by line, connection | Pane | Same | Kept |
| Time zone view ("No matching time zones") | Popover | Same; empty line in cursive empty role | Kept |

## Gestures and system

| Capability | Before | After | Verdict |
| --- | --- | --- | --- |
| Swipe pages; long-press select; selection drag and vertical scroll | Content | Same (input code unchanged) | Kept |
| Two-finger tap undo, three-finger tap redo | Anywhere | Same | Kept |
| Top-edge swipe: Recents | Reserved top band | Same | Kept |
| Status-corner swipe: quick settings (light, Wi-Fi, battery) | Reserved status corner | Same | Kept |
| Bottom-edge swipe: Quick Browse | Reserved bottom band | Same | Kept |
| Pinch zoom, two-finger pan, reset | Content | Same | Kept |
| Hardware eraser ownership | Per PR #1 | Same | Kept |
| Passcode lock and keypad | Full screen | Same; keypad digits 52 → 58 (Title role) | Kept, restyled |
| Sleep screen | Full screen | Same; title 54 → 58 (Title role) | Kept, restyled |
| On-screen keyboard | {0,1260,1620,2160} | Same | Kept |
| Notice / confirm cards | Centred | Same | Kept |
| Companion pairing view | Full screen | Same | Kept |
| Status bar battery percentage | Mast | Same; now shows "73%" instead of the truncated "7…" (fix) | Kept, fixed |

## Unsupported or deferred (no control added)

In-book search, one-gesture Quick Note, manual clean refresh, density preference, Type Folio shortcuts, landscape layout.
Reasons and proposals: `docs/LAYOUT-ARCHITECTURE.md` §7 and PR #2.

**Result: no capability lost.** One move (highlight detail onto the shared lower-sheet geometry), one new preference row,
and restyles that keep every target rect.
