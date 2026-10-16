#!/bin/bash
# SPDX-License-Identifier: AGPL-3.0-only
# Assemble the E2 bundle (never a package): Release inert harnesses, e2-probe,
# the phase-A wrapper, optional SDK runtime libraries, MANIFEST.sha256 and a
# record of the source identity. Staged on the tablet at /home/root/inkbound-e2/bundle.
#   make-e2-bundle.sh <release-build-dir> <source-sha256-file> <out-dir> [sdk-sysroot]
set -euo pipefail
build=${1:?release build dir}; source=${2:?source identity file}; out=${3:?out dir}; sys=${4:-}
test ! -e "$out" || { echo "refusing existing $out" >&2; exit 64; }
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$out/bin"
for b in dispatch-v2-inert launcher-v2-inert e2-probe; do
 cp "$build/$b" "$out/bin/$b"; chmod 0755 "$out/bin/$b"
 grep -q INKBOUND_E2_PROVISIONAL_BUILD "$out/bin/$b" && { echo "provisional build in phase A-D bundle" >&2; exit 65; }
done
cp "$here/e2-manager-probe.sh" "$out/e2-manager-probe.sh"; chmod 0755 "$out/e2-manager-probe.sh"
cp "$source" "$out/source-sha256.txt"
if test -n "$sys"; then
 mkdir "$out/lib"
 for l in ld-linux-aarch64.so.1 libsystemd.so.0 libstdc++.so.6 libgcc_s.so.1 libc.so.6 libcap.so.2 liblzma.so.5 libm.so.6 libpthread.so.0; do cp -L "$sys/usr/lib/$l" "$out/lib/$l"; chmod 0755 "$out/lib/$l"; done
fi
(cd "$out" && find . -type f ! -name MANIFEST.sha256 | LC_ALL=C sort | sed 's|^\./||' | xargs sha256sum >MANIFEST.sha256)
sha256sum "$out/MANIFEST.sha256"
