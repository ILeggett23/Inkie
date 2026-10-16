# Refresh policy model

**Stock policy is partially recovered. Manual testing has ended.** The model includes exact-runtime queue metadata, operator reports and offline executable/source analysis. [Extended pilot data](evidence/20261014/extended-pilot-summary.json), [current checkpoint](CHECKPOINT-20261014.md), [offline route](OFFLINE_CONTINUATION.md).

Initial stock observations: tiny4×4/8×6 ink updates use raw lower pair1/3; local erase updates mostly use1/14; toolbar partitions include112px-wide regions with2/16 and5/16. Some8/6 queue entries span large/background or full geometry. These numbers are private lower fields, not public EPScreenMode/EPContentType values. Sampling cannot recover the original Qt damage, call flags, exact request cadence or a universal cleanup law. Do not import these numbers into Inkbound.

**HIGH CONFIDENCE — matched pilots/source:** ordinary UI and erasing are separate stock lower classes from live ink. Inkbound21a instead uses1/13 for toolbar and erase feedback, then full-panel2/15 for palette/erase cleanup. The operator reports local/nonflashing stock actions, Inkbound palette/erase flashes, and Inkbound toolbar remnants. [Comparison](evidence/20261014/matched-pilot-summary.json). A safe local-quality replacement for Inkbound is still UNKNOWN; these observations do not waive prior UI-partial/idle deadlock constraints.

## Confirmed mechanism model

```text
scene or direct caller supplies damage
  + content-map spatial classification
  + screen-mode-map spatial classification
  + update flags / private force state
    -> clip damage
    -> optionally replace it with whole-image region
    -> partition regions by mode/content and choose lower update parameters
    -> capture converted pixels into owned updates
    -> overlap dependencies / batch publication
    -> asynchronous controller transport
```

**CONFIRMED — HOST_STATIC_ELF:** content-map assignment unions the rectangle into the selected one of four slots and subtracts it from other slots (`setTypeForRect`, `0x32600`). Dump labels establish Mono0, Grayscale1, Color3; slot2 semantics remain UNKNOWN. A classification map does not itself establish pixel contents or a waveform.

**CONFIRMED — HOST_STATIC_ELF:** screen-mode map has six regions, with names independently present in its dump table and Qt metaobject: Pen0, Mono1, Animation2, UI3, Content4, Sleep5. `setModeForRegion` can translate requested modes through map configuration, then updates region membership. Public screen modes, internal waveform/control numbers, PixelMode and lower flags are separate namespaces. [Enum/ABI analysis](evidence/analysis/ENUM-ABI.md).

**CONFIRMED — HOST_STATIC_ELF:** public bit0 and a private forced-update byte can replace local damage with the full image. The normal saved renderer callsite supplies flags0. Acep2 iterates local QRegion rectangles on several paths; its flags0 Pen path uses internal PixelMode13. This is not a claim that all requests remain local.

**HIGH CONFIDENCE — static control flow:** private mode/content/timer state influences batching, ordering and escalation. Serialized begin/update/end publication and overlap dependencies exist. A general merge distance, time window, maximum stock region count and full-screen area threshold cannot be recovered from these facts alone.

**CONFIRMED — HOST_STATIC_STOCK_ELF:** stock analyst mode-map function0x9c0ee0 translates requested modes and partitions six QRegion slots through union/subtraction. Stock initializes a single-shot1500ms display timer at owner+0xe0. Its scheduling helper checks owner thread and a temperature-related threshold (default28, environment override possible). These are static configuration/mechanism facts, not a general dirty-merge delay, measured cleanup interval or recovered optical ghosting rule. [Verified checks](evidence/20261014/stock-static-verified.json), [bounds of inference](OFFLINE_CONTINUATION.md).

## Per-content stock policy: observation gaps

| Content/action class | Dirty bounds/padding | Mode/content/full flag | Batching/frequency | Cleanup | Finding |
|---|---|---|---|---|---|
| live black ink | sampled4×4/8×6 bounds; padding UNKNOWN | public values UNKNOWN; lower1/3 | exact cadence/merging UNKNOWN | UNKNOWN | preliminary local samples + reported no full flash; truncated |
| black UI chrome | local toolbar/popup partitions; padding UNKNOWN | public values UNKNOWN; lower2/16,5/16 | exact timing UNKNOWN |25 palette cycles had no reported full-flash escalation | HIGH CONFIDENCE for tested UI locality; universal policy UNKNOWN |
| grayscale page content | color composition unclassified in pilot | UNKNOWN | UNKNOWN | UNKNOWN | no content-controlled grayscale trial |
| color UI/highlighting | red strokes have incremental local bounds; other classes UNKNOWN | public values UNKNOWN; red lower1/14 + quality work | red reportedly appears after local update | no reported whole-screen flash | HIGH CONFIDENCE for tested red-stroke outcome; highlighting UNKNOWN |
| erase | mostly60–70px local feedback | public values UNKNOWN; lower1/14 + quality/restoration | timing/merging UNKNOWN | locally cleared/no reported full flash | HIGH CONFIDENCE for three-dot pilot only |
| overlay removal | tested palette retirement stays local | public values UNKNOWN; lower quality classes | exact scheduling UNKNOWN | popup locally restored per operator | HIGH CONFIDENCE for palette; overlapping/ink overlays UNKNOWN |
| page transition | PDF/EPUB large quality regions up to1508×2160 | public values UNKNOWN; lower2/16 dominates | exact cadence UNKNOWN | no reported full flash in aligned pilots | PDF6 turns, EPUB one captured next/back cycle; not universal |
| document close / large change | UNKNOWN | UNKNOWN | UNKNOWN | UNKNOWN | UNKNOWN |
| sleep/wake | UNKNOWN | UNKNOWN | UNKNOWN | UNKNOWN | UNKNOWN |

No N-pixel stock padding, millisecond batching interval, ghost score, update count, or mode constant is selected for implementation.

## Inkbound policy, for comparison

| Class | Known current behavior | Confidence |
|---|---|---|
| ordinary ink / UI / content | Mono0/Pen0/flags0; segment union or manual regions; large live boxes banded | CONFIRMED SOURCE + VM policy |
| finished erase / removals | settle until no pen/eraser/saving; union changed/eraser regions; `clean` Mono0/UI3/flags1 | CONFIRMED SOURCE; vendor promotes |
| menu/semantic/status/tools/palette change | compare last software-submitted scene with current scene, queue old/new cleanup | CONFIRMED SOURCE; vendor promotes |
| page/document/sleep scene | full 1620×2160, Mono0/UI3/flags1 | CONFIRMED SOURCE |
| color | color may exist in composed RGB; adapter forces Mono0 | CONFIRMED SOURCE; physical appearance UNKNOWN |
| generated ink/content accumulation | default96 updates per 5×6 cell, counting only `generated=true` FastInk/ContentUpdate; defer during pen/erase/save/nonquiet animation | CONFIRMED SOURCE; VM verifies count/gating |
| human ink / ordinary UI counter | not counted by that adaptive generated-content policy | CONFIRMED SOURCE + VM |
| failed cleanup | 500 ms retry delay; quality failure nonterminal in Display | CONFIRMED SOURCE |

Do not attribute Inkbound's 8 ms cadence, 4 px pixel-diff padding, 25% area guard, 8/16 region capacities, 96 counter or 500 ms retry to stock software.

## Public implementation comparison

Quill claims fast mono and quality/color partial helpers, with complete refresh kept explicit. This supports a test hypothesis that quality and locality can coexist. Its takeover lifecycle differs from Xochitl; copied constants and return tokens are unsafe evidence substitutes. The local historical Quill header and this module disagree on color numbering. [Public README](https://github.com/MaximeRivest/quill), [public adapter](https://github.com/MaximeRivest/quill/blob/main/src/vendor_probe.cpp).

Inkbound comments cite prior UI-partial/Color/Content-path deadlocks. Those comments are **historical reports**, not newly reproduced stack evidence. Any local quality/color candidate must be qualified for idle, delayed timer/event processing and ownership behavior before Stage 2 changes the policy.
