#!/bin/bash
set -euo pipefail
umask 0077 # Match production helper/main private-directory creation.
r=${INKBOUND_SOURCE_ROOT:-/home/zayway.guest/reader-integration}
sys=/home/zayway.guest/inkbound-sdk-5.7.119/sysroots/cortexa53-crypto-remarkable-linux
cfg=${1:-release};d=$(mktemp -d /tmp/reader-interaction3-validation.XXXXXX)
echo "CONFIG=$cfg DATA=$d"
run(){ if [[ "$1" == roles-v2-tests || "$1" == roles-sampled-tests ]]; then sudo -n env ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1 "$sys/usr/lib/ld-linux-aarch64.so.1" --library-path "$sys/usr/lib" "$r/build-$cfg/$1" "${@:2}"; else "$sys/usr/lib/ld-linux-aarch64.so.1" --library-path "$sys/usr/lib" "$r/build-$cfg/$1" "${@:2}"; fi; }
export ASAN_OPTIONS=detect_leaks=1:abort_on_error=1 UBSAN_OPTIONS=halt_on_error=1
for t in ink-tests gate-tests recovery/owner-proof-tests window-limits-tests; do run "$t"; done
run qt-origin-tests
run protocol-v2-tests
run progress-v2-tests
run control-v2-tests
run runtime-v2-tests
run roles-v2-tests
run lease_fuse-v2-tests
run control-sampled-tests
run roles-sampled-tests
run launcher-v2-tests
run lifecycle-v2-tests
run sustained-session-tests
run session-power-tests
run wake-sources-tests
run power-events-tests
run reader-quiescence-tests "$r" "$d/quiescence" "$r/build-$cfg" "$sys"
run interrupted-styled-route-tests "$r" "$d/interrupted-styled" "$r/build-$cfg" "$sys"
run interrupted-panel-route-tests "$r" "$d/interrupted-panels" "$r/build-$cfg" "$sys"
run styled-progress-route-tests "$r" "$d/styled-progress" "$r/build-$cfg" "$sys"
run panel-progress-route-tests "$r" "$d/panel-progress" "$r/build-$cfg" "$sys"
run reader-progress-route-tests "$r" "$d/reader-progress" "$r/build-$cfg" "$sys"
run reader-cache-admission-tests "$r" "$d/cache-admission" "$r/build-$cfg" "$sys"
run reader-tests "$r" "$d/reader" "$r/build-$cfg" "$sys"
run page-server-tests "$r" "$d/page-server" "$r/build-$cfg" "$sys"
run reader-reopen "$d/reader/application" "$d/reader"
run cache-tests "$d/cache"
run underlay-tests "$r" "$d/underlay"
run ux-tests "$r" "$d/ux" "$r/build-$cfg" "$sys"
run ux-reopen "$d/ux"
run semantic-tests "$r" "$d/semantic" "$r/build-$cfg" "$sys"
run semantic-reopen "$d/semantic"
run offline-dictionary-tests "$r/assets/dictionary/wordnet.ibd"
run dictionary-chain-tests "$r"
run semantic-dictionary-tests "$r"
run durable-file-tests
run source-file-tests
run document-integration-tests
run document-backend-tests "$r/fixtures/inkbound-device-test.pdf"
run companion-client-tests
run document-validation-tests
run library-view-tests
run library-manage-tests
run library-route-tests "$r" "$d/library-route" "$r/build-$cfg" "$sys"
run styled-boundary-tests "$r" "$d/styled-boundary" "$r/build-$cfg" "$sys"
run ux-behavior-tests "$r" "$d/ux-behavior" "$r/build-$cfg" "$sys"
run companion-continuation-tests "$r" "$d/companion-continuation" "$r/build-$cfg" "$sys"
run tom-ink-tests
run structured-diagram-tests "$r" "$d/visual-previews"
run composition-contract-tests "$r"
run picture-request-tests "$r"
run picture-panel-tests "$r"
run ink-trace-tests "$r"
run visual-quality-tests "$r"
run visual-runtime-tests "$r"
run tom-playback-tests
run tom-reply-ink-tests
run post-integration-route-tests "$r" "$d/post-integration" "$r/build-$cfg" "$sys"
run eink-refresh-tests
run refresh-extent-tests
run vendor-maintenance-tests
run vendor-timer-tests
run illustration-request-tests "$sys"
run notes-generated-tests
run notes-foundation-tests "$r" "$sys"
run notes-route-tests "$r" "$d/notes-route" "$r/build-$cfg" "$sys"
run reveal-route-tests "$r" "$d/reveal-route" "$r/build-$cfg" "$sys"
run v1-panel-effects-tests "$r" "$d/v1-panel-effects" "$r/build-$cfg" "$sys"
run touch-drag-tests
run selection-intent-tests
run selection-route-tests "$r" "$d/selection-route" "$r/build-$cfg" "$sys"
run frontlight-tests
run status-screen-tests "$r"
run time-settings-tests
run timezone-view-tests "$r"
run timezone-route-tests "$r" "$d/timezone-route" "$r/build-$cfg" "$sys"
run status-route-tests "$r" "$d/status-route" "$r/build-$cfg" "$sys"
run bookmarks-tests
run cover-tests "$r" "$d/covers" "$r/build-$cfg" "$sys"
run semantic-range-tests
run document-controls-tests
run pdf-viewport-tests "$r/fixtures/library/authored-three-pages.pdf"
run document-view-store-tests
run epub-view-store-tests
run view-reader-support-tests "$sys"
run prepare-document-view-tests "$r"
run prepare-epub-view-tests "$r"
run epub-reflow-tests "$r" "$sys"
run epub-raster-tests "$r" "$sys"
run epub-reading-fraction-tests "$r"
run epub-annotation-reflow-tests "$r"
run epub-annotation-cache-tests "$r"
run pdf-view-route-tests "$r" "$d/pdf-view-route" "$r/build-$cfg" "$sys"
run epub-view-route-tests "$r" "$d/epub-view-route" "$r/build-$cfg" "$sys"
run interaction20r-route-tests "$r" "$d/interaction20r-route" "$r/build-$cfg" "$sys"
run gesture-route-tests "$r" "$d/gesture-route" "$r/build-$cfg" "$sys"
run startup-window-tests "$r" "$d/startup-window" "$r/build-$cfg" "$sys"
run underlay-capacity-tests "$r" "$d/underlay-capacity"
run long-stroke-route-tests "$r" "$d/long-stroke" "$r/build-$cfg" "$sys"
run tom-auto-reply-tests
run tom-store-tests
run tom-canvas-tests
run highlight-blocks-tests
run tom-panel-tests
run tom-inline-panel-tests "$r"
run tom-runtime-tests "$sys"
run tom-route-tests "$r" "$d/tom-route" "$r/build-$cfg" "$sys"
run tom-ink-route-tests "$r" "$d/tom-ink-route" "$r/build-$cfg" "$sys"
run tom-source-route-tests "$r" "$d/tom-source-route" "$r/build-$cfg" "$sys"
run offline-runtime-tests "$r" "$d/offline-runtime" "$r/build-$cfg" "$sys"
# R05.5: genuine disk exhaustion on a separate 128 MiB private tmpfs (page rasters are ~14 MiB each) owned by this user.
mkdir -m 0700 "$d/full"
sudo -n mount -t tmpfs -o size=128m,mode=0700,uid=$(id -u),gid=$(id -g),nosuid,nodev,noexec inkbound-storage-full "$d/full"
trap 'sudo -n umount "$d/full" 2>/dev/null || true' EXIT
run storage-full-route-tests "$r" "$d/full" "$d/storage-full" "$r/build-$cfg" "$sys"
# Helpers of the fail-closed child phases may still be exiting; allow a bounded wait.
for i in $(seq 1 50); do sudo -n umount "$d/full" 2>/dev/null && break; sleep 0.2; done
if mountpoint -q "$d/full"; then echo "STORAGE_TMPFS_STILL_MOUNTED" >&2; exit 1; fi;trap - EXIT;rmdir "$d/full"
run pdf-policy-tests "$r/fixtures/inkbound-device-test.pdf"
run styled-integration-tests
run styled-publication-tests
run product-feature-tests
run product-store-cache-tests
run tools-view-tests
run localized-reveal-tests
run receiver-tests
run receiver-interactive-tests
run tom-session-tests
run companion-view-tests
run wifi-tests
run user-messages-tests
run semantic-crop-tests
run ib-surface-tests "$r"
run sleep-settings-tests
( unset OPENSSL_CONF OPENSSL_MODULES LD_LIBRARY_PATH; /usr/bin/openssl req -x509 -newkey rsa:2048 -nodes -keyout "$d/tls-key.pem" -out "$d/tls-cert.pem" -days 1 -subj /CN=127.0.0.1 -addext subjectAltName=IP:127.0.0.1 ) >"$d/tls-fixture.log" 2>&1
OPENSSL_CONF=/etc/ssl/openssl.cnf QT_PLUGIN_PATH="$sys/usr/lib/plugins" run companion-http-tests "$d/tls-cert.pem" "$d/tls-key.pem"
OPENSSL_CONF=/etc/ssl/openssl.cnf QT_PLUGIN_PATH="$sys/usr/lib/plugins" run tom-http-tests "$d/tls-cert.pem" "$d/tls-key.pem"
python3 "$r/tools/finish-test-run.py" "$d" "$r" "$cfg"
echo "CONFIG_PASS=$cfg"
