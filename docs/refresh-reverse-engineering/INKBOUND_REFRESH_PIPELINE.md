# Inkbound refresh pipeline

**CONFIRMED — SOURCE:** the current reader uses a direct `EPFramebuffer` adapter and manually composed RGB32 pixel planes. Its reading UI is not a QML item hierarchy. Old QTFB/AppLoad work describes a different runtime. See [identity](README.md#identity-and-preservation).

**Connected-device update:** installed21a and its exact executable are identified. Its sealed source verifies833/833 files. The adapter/policy files and the `damage`, `takeChange` and `openNotes` bodies below match that source. [Comparison evidence](evidence/20261014/refresh-source-21a-comparison.json). Matched toolbar, palette and erase pilots now corroborate the full-flag and fast-UI findings. Manual testing has ended; no readiness action is pending. Do not substitute the newer working checkout for the sealed installed source.

References below point into the immutable [captured source](evidence/source/); line numbers match the checkout at capture time. The exhaustive term search is [refresh-search.tsv](evidence/refresh-search.tsv), with [64 explicit full-change lines](evidence/explicit-full-sites.tsv). The search includes dirty/damage, framebuffer, QTFB, refresh, rectangles/regions, classification, modes, cleanup and ghosting terms.

## Call graphs

```text
Marker input (evdev; existing exclusive owner/recovery lease)
  session.cpp:consumeEvents / Engine::accept (core.cpp)
    segment -> segment bounds -> Rect::include into pending bounding box
    scheduler: 8 ms nominal interval, Down/Up force submission
    Engine::flush
      ux::Display::submit
        Shell::presentationDamage (styled/notes/Tom damage included)
        Shell::compose(region) -> final borrowed RGB32 surface
        Display::live -> quarter-page bands
        applyRefresh(FastInk) -> VendorDisplay::submit
          submitChecked(region, content=0, mode=0, flags=0)
            Runtime::region -> EPFramebuffer::swapBuffers(QRect,...)

Finger input / worker publication / selection change
  Display::readerBoundary -> Touch::pollTap -> Shell::finger / tick
    manual pixel-plane rendering and Shell::damage / change_.regions / change_.full
    Shell::takeChange -> old/new UI retirement cleanup rectangles
    RefreshPlan / cleanup queues / existing RefreshScheduler
      ordinary change: compose(region) -> live -> applyRefresh(UIUpdate/ContentUpdate)
        VendorDisplay::submit(region, Mono0/Pen0/flags0)
      settled cleanup: compose(region) -> applyRefresh(Cleanup)
        VendorDisplay::clean(region, Mono0/UI3/flags1)
      full change: compose(full panel) -> applyRefresh(PageTransition)
        VendorDisplay::readerPage(full panel, Mono0/UI3/flags1)

Vendor boundary (saved module, static analysis)
  QRect wrapper 0x33340 -> content/mode maps -> region overload 0x32f60
    clip against image bounds
    flags bit0 OR private forced-update byte -> full-image QRegion
    Acep2::swapBuffers_impl 0x35600 -> partition by modes/content
      serialized batch -> Swtcon::update 0x389e0
        align/clip -> owned pixel conversion snapshots -> pending update jobs
          asynchronous waveform/transport generation -> DRM transport commit
            display controller -> optical output (unmeasured)
```

The public swap returns no reliable status/completion token. Current adapter discards the result; `Submit::Accepted` means its local checks passed and the void-compatible call returned. Owned snapshot conversion precedes normal return in the inspected path; this is a source reuse contract, not panel completion. See [saved call graph](evidence/analysis/SWAP-CALLGRAPH.md).

## Every vendor refresh entry in the current adapter

All entries ultimately call [Runtime::region](evidence/source/src/vendor_runtime.cpp#L92), which converts half-open `Rect{x0,y0,x1,y1}` to `QRect(x,y,width,height)` without additional padding.

| Source/function | Caller/trigger | Rectangle | Content / mode / flags | Expansion/merging | Can become fullscreen? |
|---|---|---|---|---|---|
| `vendor.cpp:35 initialPage` | bare direct-session initialization | entire 1620×2160 | Mono0 / UI3 / 1 | initializes visible white pixels | already full |
| `vendor.cpp:44 initialPreparedPage` | `ux::Display::initialPage`, legacy `ReaderDisplay` | entire panel | 0 / 3 / 1 | prepared pixels copied if distinct buffer | already full |
| `vendor.cpp:49 readerPage` | `applyRefresh(PageTransition)`, sleep/restore, legacy reader transition | entire panel, ignores input intent rectangle | 0 / 3 / 1 | explicit full rectangle | already full |
| `vendor.cpp:41 submit` | FastInk, UIUpdate, ContentUpdate through `applyRefresh`; legacy reader regions | input/banded region | 0 / 0 / 0 | rejects fast area >874,800 pixels; Display bands beforehand | caller may supply broad regions; backend forced-state remains possible |
| `vendor.cpp:42 clean` | `applyRefresh(Cleanup)` after erase, removal, UI retirement, generated-content accumulation | input local region | 0 / 3 / 1 | no local expansion; vendor bit0 expands it | **yes: proven full-image promotion** |
| `vendor.cpp:43 ui` | available method; current reader policy does not call it | input region | 0 / 3 / 0 | no local expansion | no unconditional bit0 promotion; effective backend behavior unqualified |

`VendorDisplay::submitChecked` at lines 23–33 checks lease, clipping, reentrancy and budgets. The quarter-panel rejection is **Inkbound code**, despite a Display comment calling it a vendor refusal. It does not prove a vendor hardware limit.

## Damage and merging

- `core.cpp:29–41` draws a circular brush along a segment and returns bounds padded by brush radius, clipped to surface. Engine uses radius 2 for ordinary pen and 4 for its eraser feedback. Styled authoritative geometry may contribute larger damage via `presentationDamage`.
- `core.cpp:55,64–69`: segments since last flush are one bounding-box union. Pending damage clears after accepted submission. This is not a union over the entire completed stroke.
- `session.h:39–44`: nominal cadence 8,000,000 ns, forced Down/Up; actual frequency depends on input, composition and synchronous swap duration. No measured frequency is claimed.
- `ux_shell.cpp:219–220`: `Shell::damage` clips, removes covered extras, and sets `change_.full` if an incoming rectangle exceeds 874,800 pixels or eight extra slots are exhausted. It does not first attempt a region union to avoid that escalation.
- `tomDamage` bands a large sheet before that guard, but eight-slot exhaustion remains possible if enough extras accumulate.
- `panel_damage.h:11`: 64-pixel analysis tiles, tight changed-pixel bounds with 4-pixel padding. When its output array is full it merges the least extra-area pair. Widely separated changes can thus eventually produce a broad box; full escalation then happens in `Shell::damage`.
- `refresh_plan.h:15`: 16-region fixed planner. Covered inputs are removed; overlapping boxes merge only when union-box area is no greater than their summed areas; edge overlaps can be trimmed. Disjoint boxes, including merely touching ones, remain separate. Display has explicit fallback paths for capacity/subtraction failures, some using one bounding union for cleanup.
- `ux_display.h:43–49`: large fast regions become vertical bands within Inkbound's quarter-panel guard. Banded cleanup does not solve flag1 promotion; each cleanup call can cover the panel.

## UI hierarchy / damage audit

| Component | Current implementation and source boundary | Damage risk |
|---|---|---|
| PDF/EPUB viewport | prepared background + manual compose; `ux_shell.h:211`, `composeZoom` | page/view transitions set full; zoom transforms regions; no current QML damage stage |
| ink canvas | `Engine`, `styled::Raster`, `Shell::presentationDamage` | bounding unions and authoritative changed pixels can enlarge damage |
| selection/highlight | `semanticPaint` old/new regions; line highlight geometry | cleanup retirement and `openNotes(Highlight)` can request full |
| toolbar/menus | `paintChrome`, `toolsPaint`, `ux::regions` | top strip spans full width; toolbar column spans page height; menu/tool retirement adds cleanup |
| Tom overlay | `tomDamage`, `tomTick`, region composition | streaming bands; UI retirement cleanup; some transitions explicitly full |
| note overlay | `openNotes`, `notesTick`, regional damage within panel | open/close/reset explicitly full; canvas content also queues cleanup |
| dialogs/view controls | `viewPaint`, `finger` view-controls branch | `finger` sets full even after some consumed controls with regional painting |
| sleep/restore | `Display::sleepTick` | intentional full page, distinct from ordinary UI |

`paintChrome` and `pageBackgrounds` can recompute/copy a full CPU image without issuing a full display update. The first display-area decisions are the explicit change/cleanup records and their mapping to vendor parameters.

Current QML contribution: **CONFIRMED absent from this reader UI path (SOURCE)**. It remains **UNKNOWN for live Xochitl**, and needs measurement. Qt 6.8.2's ordinary software renderer collects node regions, accounts for transparency and previous locations, and invalidates its background on resize. Those generic rules do not establish behavior of reMarkable's custom renderer. [Qt 6.8.2 source](https://raw.githubusercontent.com/qt/qtdeclarative/v6.8.2/src/quick/scenegraph/adaptations/software/qsgabstractsoftwarerenderer.cpp).

Historical AppLoad path: `fbmanagement.cpp:222` distinguishes `UPDATE_ALL` and `UPDATE_PARTIAL`; the former calls default `markedUpdate()`, the latter carries QRect. `FBController::markedUpdate` calls `QQuickPaintedItem::update(rect)`. Consequently older full-item requests could enlarge scene damage. `allowScaling` also uses integer division before multiplication and merits a separate historical audit. Neither behavior is the active current-reader root cause. [Qt's exact 6.8.2 painted-item update implementation](https://raw.githubusercontent.com/qt/qtdeclarative/v6.8.2/src/quick/items/qquickpainteditem.cpp).
