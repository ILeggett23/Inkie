#!/bin/bash
set -euo pipefail
export PATH=/home/zayway.guest/inkbound-tools/venv/bin:/usr/bin:/bin
export RM_SDK_ROOT=/home/zayway.guest/inkbound-sdk-5.7.119
export RM_SDK_INSTALLER=/home/zayway.guest/inkbound-cache/remarkable-production-image-5.7.119-ferrari-public-aarch64-toolchain.sh
export INKBOUND_DEVICE_OS_VERSION=3.27.3.0 INKBOUND_SDK_SOFTWARE_RELEASE=3.27.0.97 INKBOUND_SDK_IMAGE_VERSION=5.7.119
exec /home/zayway.guest/inkbound-input-source/scripts/configure-ferrari-sdk.sh -- bash -c '
set -eu
r=${INKBOUND_SOURCE_ROOT:-/home/zayway.guest/reader-integration}
for cfg in ${UX_CONFIGS:-release}; do
 san=""; [[ "$cfg" != asan ]] || san=address; [[ "$cfg" != ubsan ]] || san=undefined
 cmake -S "$r" -B "$r/build-$cfg" -DCMAKE_BUILD_TYPE=Release -DSPIKE_SANITIZER="$san" -DBUILD_VENDOR_RUNTIME=ON -DMUPDF_INCLUDE=/home/zayway.guest/inkbound-mupdf-integration/reader-epub-target/source/include "-DMUPDF_ARCHIVES=/home/zayway.guest/inkbound-mupdf-integration/reader-epub-target/lib/libmupdf.a;/home/zayway.guest/inkbound-mupdf-integration/reader-epub-target/lib/libmupdf-third.a"
 if [[ ${UX_REPRODUCE:-0} == 1 ]]; then
  test "$cfg" = release
  cmake --build "$r/build-$cfg" --clean-first --target inkbound-direct-handwriting-spike3 reader-experience1-prepare -j2
  cmp "$r/build-$cfg/inkbound-direct-handwriting-spike3" "$r/artifact/inkbound-direct-handwriting-spike3"
  cmp "$r/build-$cfg/reader-experience1-prepare" "$r/artifact/reader-experience1-prepare"
  echo UX_BINARY_CLEAN_REBUILD_IDENTICAL
  continue
 fi
 python3 "$r/tools/source-fingerprint.py" "$r" /home/zayway.guest/inkbound-mupdf-integration/reader-epub-target/lib/libmupdf.a /home/zayway.guest/inkbound-mupdf-integration/reader-epub-target/lib/libmupdf-third.a > "$r/build-$cfg/.compile-inputs.pending.json"
 clean=""
 if ! cmp -s "$r/build-$cfg/.compile-inputs.pending.json" "$r/build-$cfg/.compile-inputs.json"; then clean=--clean-first; fi
 # A new content identity always rebuilds: copied source mtimes may predate old
 # objects even when bytes changed, including ABI-changing class headers.
 cmake --build "$r/build-$cfg" $clean -j2
 mv "$r/build-$cfg/.compile-inputs.pending.json" "$r/build-$cfg/.compile-inputs.json"
 if [[ ${UX_TESTS:-0} == 1 ]]; then bash "$r/tools/run-local-tests.sh" "$cfg"; fi
done
'
