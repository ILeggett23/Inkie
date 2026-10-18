# Inkie — design, 2026-10-17 (v2, "stationery")

Inkbound is now **Inkie**. This document records the design and the reason for each decision. The spatial system
(zones, rects, gesture bands, sheet geometry) is unchanged and lives in `docs/LAYOUT-ARCHITECTURE.md`. Tokens are in
`reader/src/ib/theme.h` and the type rules in `reader/src/ib/text.h` and `type.h`.

v1 (bubbles, pop shadows, pills, a colour for every control) was replaced after review: it read as a generic Android
concept, and several of its choices worked against the panel. v2 keeps Inkie's personality (script, colour, ink drops)
and draws it the way a good notebook is made, within what E Ink Gallery 3 can actually show.

## 1. What the panel can do

The reMarkable Paper Pro uses E Ink Gallery 3. Each design rule in §2 comes from one of these constraints.

| Fact | Consequence for Inkie |
| --- | --- |
| Four particles per pixel: cyan, magenta, yellow and white. There is no colour filter and no black particle ([E Ink](https://www.eink.com/brand/detail/Gallery_3), [E Ink launch](https://www.eink.com/news/detail/E%20Ink_Launches_E%20Ink_Gallery_3_Color_ePaper_for_Sustainable_Digital_Reading), [MyGica](https://www.mygica.com/e-ink-color-technologies/)). | Black is a full mix, so text edges are rougher than on a monochrome panel ([The eBook Reader](https://blog.the-ebook-reader.com/2024/09/10/gallery-3-screen-comparison-vs-kaleido-3-and-regular-bw-e-ink-video/)). Text is near-black and never thin. |
| About seven or eight clean states: white, black, C, M, Y and the two-pigment mixes R, G and B. Anything in between is blended: purple reads light blue, cyan greenish, and white fill looks grainy ([IRIS Optronics](https://iris-opt.com/en/blog/eink-acep-gallery-vs-chlcd/), [epdoptimize](https://github.com/paperlesspaper/epdoptimize)). | Only C, M, Y, R, G and B are used. No orange, violet, pink, pastel tints or greys. |
| Colours look muted next to a phone. Reviewers describe red as closer to burnt orange and green as pea soup, and colour is best seen in good light ([Engadget review](https://www.engadget.com/mobile/tablets/remarkable-paper-pro-review-writing-in-color-is-nice-but-itll-cost-you-173024590.html), [Tom's Guide](https://www.tomsguide.com/tablets/remarkable-paper-pro), [TechCrunch](https://techcrunch.com/2024/09/04/remarkables-paper-pro-adds-color-light-and-more-but-keeps-the-focus-on-focus)). | Colour must work as an area, not as a hairline or a word. Every colour shape is at least 10 px thick (`EdgeBand`) and meets a black rule, so its edge stays crisp even when the hue is dull. |
| Update times: black and white 350 ms; colour 500 ms (fast), 750–1000 ms (standard), 1500 ms (best) ([CNX Software](https://www.cnx-software.com/2022/04/26/e-ink-gallery-3-color-epaper-offers-faster-refresh-rate-higher-resolution/), [New Atlas](https://newatlas.com/electronics/e-ink-gallery-3-color-epaper/), [Engadget](https://www.engadget.com/e-ink-gallery-3-announced-182802114.html)). A colour change can leave a tinted ghost ([Svartling](https://www.svartling.net/2026/05/the-e-ink-dilemma-why-remarkable-pures.html), a single report). | **Colour is identity; ink is state.** Colour marks what does not change while you work (which place, which book, which sheet). Everything that changes on a tap (selected, on/off, progress, the current page) is drawn in black and white, which the fast pass serves without a colour ghost. |
| The Paper Pro's own pens offer black, gray, white, blue, red, green, yellow, cyan and magenta. Highlighters offer yellow, blue, pink, orange, green and gray ([Engadget](https://www.engadget.com/mobile/tablets/the-new-remarkable-paper-pro-adds-a-color-display-120059067.html), [overkill.wtf](https://overkill.wtf/remarkable-paper-pro-review-the-next-step-in-the-evolution-of-the-notebook/)). | Inkie's palette matches the device's own colours, so its UI sits naturally next to Xochitl's pens. |

Some sources disagree, for example on colour resolution (300 ppi in E Ink's release, 150 ppi for colour in one Good
e-Reader article). The figures above are design inputs, not measurements on this tablet; see §8.

## 2. Rules

1. **Paper is white and text is black.** There are no grey fills and no grey text. Hierarchy comes from size, face and
   weight. The one grey (`Disabled`) marks rare disabled controls.
2. **Six colours, all clean states:** `PopCyan`, `PopMagenta`, `PopYellow`, `PopGreen`, `PopBlue`, `PopRed`.
3. **Colour is identity, ink is state.** Selected tools, cells, rows, tabs and checkboxes are ink with paper on top,
   as on the native toolbar.
4. **Colour is an area at least 10 px thick, met by a black rule.** It is never a line or a word.
5. **One colour per place**, the same everywhere the place appears: My files and Recents cyan, Notes magenta, Favorites
   yellow, Shared green, Tom blue, Trash red. Settings is ink, because it is a utility rather than a place.
6. **Rules are dotted ink** (2 px dots every 6 px), never a grey line.
7. **No shadows.** A card's edge is one 2 px ink rule, and every surface paints only inside the rect it reports for
   damage.

## 3. The look: a well-made notebook

| Element | Treatment |
| --- | --- |
| Cards (popovers, palette, document menu, notices, layers, document settings, selection strip) | `bubble()` / `panel()` / `popover()`: white, radius 12, a 2 px ink rule, no shadow. |
| Lower sheets (Note, Tom, highlight, Notes hub, Quick Browse) | `contextSheet()`: square paper the full width of the screen, with a 10 px band in the sheet's colour along the top edge, closed by an ink rule, like an index card. Note is magenta, Tom blue, Notes hub magenta, Quick Browse cyan, and the highlight sheet uses the highlight's own ink. Recents is the same sheet from the top, with its cyan band along the lower edge. |
| Toolbar | Two families, divided by a dotted rule. **Writing tools** (pen, highlighter, eraser) are the objects themselves: slanted pen silhouettes (`penGlyph`, 64 px) with a paper body, an ink outline, a solid tip whose shape names the tool (ballpoint ball, fineliner tube, pencil cone, brush, fountain nib, chisel marker, soft shader, highlighter chisel, block eraser) and a band in the tool's current ink. **Usability tools** (panel handle, lasso select, a stack of sheets for layers, undo, redo, a dog-eared page for the document menu) are 48 px outline icons with one stroke weight. The active tool sits on an 84 px rounded ink tile, drawn in paper. The pen palette, the Note tool row and the selection strip's Highlight use the same writing glyphs. |
| Buttons | Rounded rectangles (radius 12). Primary is ink with paper text (Send, Done, Write a note), destructive is red with white text, secondary is paper with an ink rule, and quiet buttons are text only. |
| Choices, segmented controls, palette cells | Outlined cells; the chosen one is ink. |
| Tabs and filters | Underlined text tabs over a dotted rule; the chosen tab is heavier, on a solid ink bar. |
| On/off | A checkbox: ink with a paper tick when on. |
| Headings | `swash()`: the heading written over a slanted highlighter stroke in the place's colour, with an ink foot along its base, set under the lower x-height so the letters stay black on white. In ink (Settings) it becomes a plain underline. |
| Drawer and Settings page list | Coloured index tabs at the left edge, as on a divided notebook. The current row is ink. |
| Covers without art | A clothbound book: flat cloth in one of the six colours (picked from the title's hash, so a book keeps its colour), a paper label plate with the title, the format printed on the cloth, an ink spine for EPUB and a clean folded corner for PDF. |
| Settings groups | Small spaced capitals in print, followed by a dotted rule. |
| Progress (footer, Quick Browse) | A solid ink line along a dotted rule. Quick Browse has an ink knob for this page and an open ring where you started. |
| Lock screen | Paper keys with an ink rule. Digits are in Literal, because a passcode is read exactly. Entered digits are solid ink dots. |
| Keyboard | Paper keys with printed characters (Literal); the modifiers and Done are ink. |
| Battery | An ink gauge that turns red at 20% or below. |

**Where the fun is:**
- the script;
- the wordmark's ink-drop dot;
- highlighter swashes on headings;
- coloured index tabs and sheet edges;
- cloth covers;
- the sleep screen, which shows the wordmark, a yellow moon and three drops of the panel's own pigments (cyan,
  magenta, yellow). E-ink holds its last image without power, so this stays on the closed tablet.

## 4. Type: words written, data printed

| Text | Face |
| --- | --- |
| Titles, headings, list and menu rows, settings labels, buttons and tab labels, from `Label` (30 px) up | Caveat at role × 1.25 (`ScriptBold` for semibold), so a 30 px label draws at 37.5 px. |
| Metadata, counts, dates, notes, captions and control labels under 30 px (`Control`, `Meta`, `Caption`) | Source Sans 3, so fine print reads as fine print and titles are always larger than the metadata under them (a v1 defect). |
| Reading text: definitions, notices, typed notes and questions (`Face::Serif`) | Source Serif 4, never script. |
| The book's own words (`SerifItalic`) | Source Serif 4 Italic. |
| Characters read exactly: pairing code, passcode, keyboard keys (`Literal`) | Source Sans 3. |
| Tom's typed reply (`type::voice`) | Script at Tom's pen sizes. |

- **Plain text instead of cursive** (Settings › Accessibility) draws every face as named. Colours are unchanged.
- Label-fitted controls are sized with `text::span()` (the wider of the two faces), so every target rect is identical
  in both modes (tested).
- `ScriptBold` is an emboldened rasterisation of Caveat. No heavier file can ship, because the tablet update keeps the
  baseline's font assets.

## 5. Wordmark

The wordmark is "Inkie" in heavy script (or semibold sans with Plain text). The dot of the i is a magenta drop of ink.
It is placed over the stem of a dotless ı, found by rendering that glyph into scratch, so it sits correctly in either
face and does not depend on the caller's clip.

## 6. Name

User-visible text says "Inkie". Binary and package names, the app id, storage paths, the Companion pairing name and
provider prompts keep "Inkbound": renaming them would break the installed update, stored data and the Companion
contract.

## 7. What moved

`ib-preview --targets` on the shipping base compared with v2: **471 of 488 targets are identical** (v1: 434). The
changes are:
- the wordmark hit area, which follows the shorter name (127 × 112 px, still above `MinTarget`);
- the six Auto-sleep choice cells, which move by up to 12 px because their labels are now printed at `Control` size.

Toolbar slots, close, sheets, palette cells, rows, Notes filters and every fixed control are unchanged. No target moves
between cursive and plain text.

## 8. Not changed, and open questions

- **Pen and highlighter colours.** Stroke colours come from the RGB table in `styled_ink.cpp`, which is protected
  styled-ink code. `theme::Ink` must match what is drawn, so it keeps today's muted values (pink, orange and purple
  dither on the panel). Proposal for the owner, keeping the persisted index order:

  | Ink | Proposed value |
  | --- | --- |
  | Black | `#0A0A0A` |
  | Pink | magenta `#E6007E` |
  | Green | `#00A650` |
  | Orange | no clean state: keep it (it dithers), or re-purpose the slot as cyan `#00A8E8` |
  | Yellow | `#FFE100` |
  | Purple | no clean state: keep it (it reads light blue), or re-purpose the slot |
  | Blue | `#1E50DC` |
  | Red | `#E8202A` |

  Re-purposing a slot changes how existing strokes look, because indices are persisted, so it needs a storage
  decision.
- **Selection wash.** The transient text-selection tint (`Selection`) is a light cyan. On the panel it dithers to a
  sparse cyan grain. It is transient and reads as a selection, so it stays. A solid cyan would hide the text's edges.
- **Refresh policy.** No new waveform, flag or timer. Colour areas only appear when a sheet or screen opens, through
  the existing regional quality path, and state changes are monochrome.
- **Previews.** `ib-preview` writes a `-panel` view of every surface: the eight clean states, muted and
  error-diffused. It is an approximation for spotting problems, not a measurement.

Pending physical validation, in normal and high contrast:
- how each colour renders;
- the time for colour to settle after a sheet opens;
- ghosting when a coloured band closes;
- legibility of 37.5 px script labels and 26 px printed metadata;
- the sleep screen's colours after hours on a closed cover.
