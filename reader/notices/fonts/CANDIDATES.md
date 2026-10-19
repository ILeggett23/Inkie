# Evaluated cursive candidates (not distributed)

Inkbound ships Caveat (SIL OFL 1.1, `Caveat-OFL.txt`) for its cursive roles. The faces below were compared with it on
host candidate sheets (`ib-preview --script-sheet`, see `docs/LAYOUT-ARCHITECTURE.md`). None is bundled, packaged or
redistributed; they are listed here with their licences so the comparison can be reproduced. Sources and SHA-256
digests are recorded under `evaluatedCandidates` in `reader/assets/fonts/manifest.json`.

| Face | Licence | Copyright holder | Verdict |
| --- | --- | --- | --- |
| Dancing Script | SIL Open Font License 1.1 | The Dancing Script Project Authors | Runner-up: joined, width 0.99 of Source Serif 4 at 1.25x, but hairline connectors thin out in a fast 1-bit pass |
| Homemade Apple | Apache License 2.0 | Font Diner (designer per google/fonts metadata; the Apache text names no holder) | Rejected: 1.49x the plain line width |
| Playwrite GB S | SIL Open Font License 1.1 | The Playwrite Project Authors | Rejected: 1.37x the plain line width |

The full licence texts are the upstream `OFL.txt` (Dancing Script, Playwrite GB S) and `LICENSE.txt` (Homemade Apple)
at google/fonts commit `47d54ca1ebb14fd864695f1d39dc734f39017c33`; their digests are in the manifest. Bundling any of
these later requires adding the file and its full licence text to `files` and `licenses` in the manifest.
