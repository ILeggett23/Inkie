#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Assemble the E2 phase-E app root (never a package): provisional owner + reader, the normal
# import helper, the pinned provisional profile, reader assets and the test document.
# Staged on the tablet at /home/root/inkbound-e2/E/app (chown -R 0:0 after extraction).
#   make-e2-app.sh <source-root> <provisional-build> <release-build> <profile> <test.pdf> <out-dir>
set -euo pipefail
src=${1:?source}; prov=${2:?provisional build}; rel=${3:?release build}; profile=${4:?profile}; pdf=${5:?test pdf}; out=${6:?out}
test ! -e "$out" || { echo "refusing existing $out" >&2; exit 64; }
mkdir -p "$out/bin" "$out/config" "$out/test"
for b in inkbound-lifecycle-owner inkbound-direct-handwriting-spike3; do
 cp "$prov/$b" "$out/bin/$b"; chmod 0755 "$out/bin/$b"
 grep -q INKBOUND_E2_PROVISIONAL_BUILD "$out/bin/$b" || { echo "$b is not the E2 provisional build" >&2; exit 65; }
done
cp "$rel/reader-experience1-prepare" "$out/bin/"; chmod 0755 "$out/bin/reader-experience1-prepare"
grep -q INKBOUND_E2_PROVISIONAL_BUILD "$out/bin/reader-experience1-prepare" && { echo "helper must be the normal build" >&2; exit 65; }
grep -q '^qualification=provisional-e2-' "$profile" || { echo "profile is not provisional" >&2; exit 65; }
cp "$profile" "$out/config/lifecycle-profile.provisional-e2"; chmod 0644 "$out/config/lifecycle-profile.provisional-e2"
python3 - "$src" "$out" <<'EOF'
import json, shutil, sys
from pathlib import Path
src, out = Path(sys.argv[1]), Path(sys.argv[2])
cfg = json.loads((src / 'config/offline-release.json').read_text())
for name in cfg['bundledAssets']:
    (out / name).parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src / name, out / name)
EOF
cp "$pdf" "$out/test/e2-test.pdf"
(cd "$out" && find . -type f ! -name MANIFEST.sha256 | LC_ALL=C sort | sed 's|^\./||' | xargs sha256sum >MANIFEST.sha256)
sha256sum "$out/MANIFEST.sha256"
