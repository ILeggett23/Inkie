#!/usr/bin/env bash
# Native ARM64 cloud runner, original Ferrari SDK. Never starts the GUI or vendor runtime.
set -euo pipefail
umask 0077
test "${INKBOUND_FERRARI_SDK_VERIFIED:-}" = 1
root=$(pwd -P)
deps="$root/.tablet-deps"
out="$root/.tablet-output"
mkdir -p "$out"
python3 reader/tools/source-fingerprint.py "$root/reader" > "$out/SOURCE-before.json"
sys="$INKBOUND_FERRARI_SYSROOT"
loader="$sys/usr/lib/ld-linux-aarch64.so.1"
test -x "$loader"
common=(-G Ninja -DCMAKE_TOOLCHAIN_FILE="$INKBOUND_FERRARI_TOOLCHAIN_FILE" -DBUILD_VENDOR_RUNTIME=ON -DMUPDF_INCLUDE="$deps/mupdf-1.28.3-source/include" "-DMUPDF_ARCHIVES=$deps/mupdf/lib/libmupdf.a;$deps/mupdf/lib/libmupdf-third.a")
targets=(sustained-session-tests ui-damage-tests eink-refresh-tests styled-integration-tests styled-publication-tests tom-reply-ink-tests tom-panel-tests notes-foundation-tests ux-behavior-tests selection-route-tests semantic-tests reader-experience1-prepare)
export QT_QPA_PLATFORM=offscreen
export QT_PLUGIN_PATH="$sys/usr/lib/plugins"
export ASAN_OPTIONS=detect_leaks=0:abort_on_error=1
export UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
for cfg in release asan-ubsan; do
 build="$root/.tablet-build-$cfg"
 args=(-DCMAKE_BUILD_TYPE=Release)
 if test "$cfg" = asan-ubsan; then args+=('-DSPIKE_SANITIZER=address,undefined' '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g1 -DNDEBUG'); fi
 cmake -S reader -B "$build" "${common[@]}" "${args[@]}"
 cmake --build "$build" --parallel 2 --target "${targets[@]}"
 run() { "$loader" --library-path "$sys/usr/lib:$sys/lib" "$build/$1" "${@:2}"; }
 for test_name in sustained-session-tests ui-damage-tests eink-refresh-tests styled-integration-tests styled-publication-tests tom-reply-ink-tests tom-panel-tests; do run "$test_name"; done
 run notes-foundation-tests "$root/reader" "$sys"
 for test_name in semantic-tests selection-route-tests ux-behavior-tests; do
  run "$test_name" "$root/reader" "$root/.tablet-data-$cfg/$test_name" "$build" "$sys"
 done
 printf '%s_TARGET_TESTS_PASS\n' "$cfg"
done
cmake -S reader -B .tablet-build-gui "${common[@]}" -DCMAKE_BUILD_TYPE=Release -DINKBOUND_E2_PROVISIONAL=ON -DINKBOUND_LIFECYCLE_PROFILE_SHA256=3307c67e7fc6960d61e23e0294a260b330acad16b530220c93b66d03816a7789
cmake --build .tablet-build-gui --parallel 2 --target inkbound-direct-handwriting-spike3
python3 reader/tools/source-fingerprint.py "$root/reader" > "$out/SOURCE-after.json"
cmp "$out/SOURCE-before.json" "$out/SOURCE-after.json"
git rev-parse HEAD > "$out/SOURCE-COMMIT"
for binary in .tablet-build-gui/inkbound-direct-handwriting-spike3 .tablet-build-release/reader-experience1-prepare; do
 readelf -h "$binary" | grep -q 'Machine:.*AArch64'
 ! readelf -d "$binary" | grep -Eq 'RPATH|RUNPATH'
 readelf -l "$binary" | grep -q '/lib/ld-linux-aarch64.so.1'
 readelf -d "$binary" >> "$out/ELF-DYNAMIC.txt"
done
nm -D .tablet-build-gui/inkbound-direct-handwriting-spike3 | grep -E ' T _ZN6QImageC[12]EPhiixNS_6FormatEPFvPvES2_' > "$out/QIMAGE-EXPORTS.txt"
test "$(wc -l < "$out/QIMAGE-EXPORTS.txt")" = 2
python3 reader/tools/package-tablet-update.py "$deps/baseline-app.tar.gz" .tablet-build-gui/inkbound-direct-handwriting-spike3 .tablet-build-release/reader-experience1-prepare "$out"
echo TABLET_SDK_BUILD_AND_SIMULATED_REGRESSIONS_PASS
