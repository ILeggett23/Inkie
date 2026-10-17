# Inkie — design concept, 2026-10-17

Inkbound is now **Inkie**: bubbly, vibrant and written in cursive, made for the reMarkable Paper Pro's colour panel
(E Ink Gallery 3). This document records the concept and the reasons behind each decision. The spatial system (zones,
rects, gesture bands, sheet geometry) is unchanged and lives in `docs/LAYOUT-ARCHITECTURE.md`; the tokens live in
`reader/src/ib/theme.h`, the type rules in `reader/src/ib/text.h` and `type.h`.

## 1. Concept: ink drops on white paper

Inkie looks like a notebook someone decorated with bright stickers and a felt-tip pen. The page is clean white paper;
everything Inkie adds on top — tools, sheets, buttons, headings — is a rounded "bubble" with a bold black outline, a
flat pop of colour and a solid sticker shadow. Words are handwritten in cursive. The book's own words stay in the book's
type, so you always know what is the book and what is Inkie.

Why it fits the Paper Pro and Gallery 3:

- **White stays white.** Device white is the panel's brightest, sharpest state, and every page is read on it. Tinted
  paper would dull every page and every refresh.
- **Colour is flat process colour.** Gallery 3 mixes cyan, magenta and yellow pigments (plus white). Inkie's pops are
  those colours and their two-pigment mixes, at full saturation, with no tints, gradients or transparency, so the
  panel can render them as solid fields rather than dithered approximations.
- **Edges are black.** The fast monochrome pass that follows a tap redraws black outlines and black text immediately;
  the colour arrives with the regional quality pass (`vendor.h` classifies composed regions as Color content). A bubble
  therefore reads crisply before its colour settles.
- **Small text is never coloured.** Coloured text that a later monochrome update replaces can linger faintly, so text
  is black, or white on the darkest pops.
- **The sleep screen is the showpiece.** E-ink keeps its last image without power, so the sleep screen (wordmark, yellow
  moon, pop bubbles) stays bright on the cover-closed tablet.

## 2. Palette

| Token | RGB | Pigments | Used for |
| --- | --- | --- | --- |
| `PopCyan` | `#00AEEF` | cyan | My files, selected state (`ToolSelected`), progress, search, Define |
| `PopMagenta` | `#EC008C` | magenta | Notes, "go" actions (Send, Done, Create), Note sheet, eraser, Quick Browse knob |
| `PopYellow` | `#FFE500` | yellow | Favorites, highlighter tool, zoom badge, width choice, moon |
| `PopGreen` | `#00B04F` | cyan + yellow | Shared, toggles on, Copy, selection tool, battery |
| `PopOrange` | `#FF7A00` | magenta + yellow (yellow-heavy) | Recents, layers tool |
| `PopViolet` | `#9B30FF` | cyan + magenta (magenta-heavy) | Tom, document menu |
| `PopBlue` | `#1060FF` | cyan + magenta | Settings, searching |
| `PopRed` | `#FF2A2A` | magenta + yellow | Trash, destructive actions, low battery |
| `InkPrimary` | `#0A0A0A` | — | outlines, text, glyphs |
| `Paper` | `#FFFFFF` | white | the page and every surface body |

`onPop(colour)` picks the ink for anything drawn on a pop: black where it reads (cyan, yellow, green, orange), white on
magenta, violet, blue and red.

**One colour per place.** My files cyan, Notes magenta, Favorites yellow, Recents orange, Shared green, Tom violet,
Trash red, Settings blue (`theme::SectionColor`). The colour follows the place everywhere it appears: the heading's
marker swash, the drawer row, the sheet's grab tab, the selection strip's action dot (Note is magenta like Notes, Tom
violet like Tom). Settings pages each have their own colour in the page list.

## 3. Shape language

| Element | Inkie treatment |
| --- | --- |
| Floating surfaces (popovers, palette, document menu, cards, notices, layers, document settings) | `bubble()`: rounded 28 px, 3 px ink outline, 8 px solid ink shadow. The shadow is drawn **inside** the reported rect, so damage bounds and targets are unchanged. |
| Lower sheets (Tom, Note, highlight, Notes hub, Quick Browse, Recents) | Rounded 40 px top corners, 3 px outline, a pop grab tab in the sheet's accent. |
| Buttons, primary actions, chips, toggles, choice cells, segmented controls | Pills. Primary = magenta, destructive = red, secondary = white, all outlined; selected chips and cells fill cyan. |
| Toolbar | A rounded rail inside the 112 px column; each tool is a circle that fills with the tool's own colour when active (pen cyan, highlighter yellow, eraser magenta, selection green, layers orange, document menu violet). |
| Icons beside labels | `dot()`: a pop circle with an outline and the glyph in `onPop` ink (drawer, folders, create menu, selection strip). |
| Headings | `swash()`: the heading in heavy cursive on a marker bar in the place's colour. |
| Covers without art | Pop covers: a flat colour picked from the title's hash (a book keeps its colour), cursive title, format on a white pill. |
| Swatches | Circles with a 3 px outline; selected sits in a cyan ring. |
| Progress | The footer gains a thin cyan bubble track; Quick Browse a thick one with a magenta knob. |

## 4. Type: cursive everywhere

- Every UI face (`Text`, `Sans`, `Serif`, `Subhead`, `Display` and their weights) draws in **Caveat**: Script, or
  ScriptBold for semibold/medium (`text.cpp`, `resolved()`).
- Size: role size × 1.25, never under **38 px** (`ScriptFloor`). Caveat's x-height is 0.357 em, so the smallest
  cursive has a 13.6 px (1.5 mm) x-height. Captions and metadata therefore set wider; fitted rects ellipsize rather
  than grow.
- ScriptBold is an emboldened rasterisation of the same Caveat bytes, about 1/26 of the size wider. No heavier file
  can ship, because the tablet update keeps the baseline's font assets (`package-tablet-update.py`).
- Two faces never become cursive:
  - **SerifItalic** is the book's voice: quoted passages, excerpt previews and Tom's writing placeholder.
  - **Literal** (Source Sans 3) is for characters that must be read exactly, such as the Companion pairing code.
- Book pages are rendered by the document engines and never change.
- **Plain text instead of cursive** (Settings › Accessibility) restores Source Sans 3 / Source Serif 4 everywhere; the
  colours stay. Label-fitted controls are sized with `text::span()`, the wider of the two faces, so the switch changes
  glyphs only: every target rect is identical in both modes (tested).

## 5. Name

User-visible text says "Inkie": the wordmark (cursive, with the dot of the i as a magenta bubble), lock and sleep
screens, Settings copy ("Inkie saves your work…", the About group), "Leave Inkie", "Inkie's copy of…" and the power
notice. Unchanged on purpose: binary and package names (`inkbound-direct-handwriting-spike3`, the app id), storage
paths and files, the Companion pairing device name, provider prompt text and source identifiers. Renaming those would
break the installed update, stored data and the Companion contract; they are invisible to the reader.

## 6. What moved

`ib-preview --targets` on the shipping base vs this redesign: 434 of 488 targets are identical. Changed targets are all
label-fitted controls that grew to fit cursive labels — the wordmark hit area (+36 px), Notes filter chips (≤ 42 px),
Settings choice cells (≤ 64 px, Auto sleep), document-settings choices (≤ 18 px), the Quick Browse stepper (24 px) and
selection-strip segments (≤ 7 px) — plus the Plain-text row. Toolbar slots, close, sheets, palette cells, rows and
every fixed control are unchanged, and no target moves between cursive and plain text.

## 7. Not changed, and why

- **Pen and highlighter colours.** Stroke colours come from the RGB table in `styled_ink.cpp` (protected styled-ink
  code), and the palette swatches must match what is drawn, so `theme::Ink` keeps today's values. Proposal for the
  owner, keeping the persisted index order: Black `#0A0A0A`, Pink → `#EC008C`, Green → `#00B04F`, Orange → `#FF7A00`,
  Yellow → `#FFE500`, Purple → `#9B30FF`, Blue → `#1060FF`, Red → `#FF2A2A`. That is one table in `styled_ink.cpp`
  and `theme::Ink` changed together. Existing strokes would render in the new colours (indices are stored, not RGB).
- **Refresh policy.** No new waveform, flag or timer. Coloured UI goes through the existing regional quality path.

## 8. Pending physical validation

- How each pop renders on the Gallery 3 panel (hue, saturation, any dithering), and the time to settle colour after a
  tap, in normal and high contrast.
- Ghosting when colour bubbles close over white (sheets, popovers, the selection strip) through the regional cleanup.
- Legibility of 38 px cursive captions and of faux-bold headings on the panel.
- The sleep screen's colours after hours on a closed cover.
