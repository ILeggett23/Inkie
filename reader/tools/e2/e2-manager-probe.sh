#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# E2 phase A: the inert dispatcher (10, incl. bus-noise, reader-fail, reader-exec-fail) and launcher (11) PID1 scenarios,
# Release only, against the device's own systemd, staged under /home/root/inkbound-e2.
# The "stock" is always a synthetic transient unit; the watched real stock unit is
# only READ (its InvocationID must be unchanged). POSIX sh (the tablet has BusyBox
# only). Never packaged.
#   e2-manager-probe.sh <bundle-dir> [--libs device|bundled]
set -eu
base=/home/root/inkbound-e2
out=$base/A
bundle=${1:?bundle dir}
libs=device
if test "${2:-}" = --libs; then libs=${3:?device or bundled}; fi
watch=${E2_WATCH_UNIT:-xochitl.service}
resolved=$(readlink -f "$bundle" 2>/dev/null || true)
case "$resolved" in "$base"/*) ;; *) echo "E2_A_REFUSED bundle outside $base" >&2; exit 64 ;; esac
case "$resolved" in *..*) echo "E2_A_REFUSED bundle path" >&2; exit 64 ;; esac
bundle=$resolved
test "$(id -u)" = 0 || { echo "E2_A_REFUSED not root" >&2; exit 64; }
case "$libs" in device|bundled) ;; *) exit 64 ;; esac
mkdir -p -m 0700 "$out"
log=$out/phase-a.txt
say() { echo "$*" | tee -a "$log"; }
# Diagnostic runs only: E2_A_CASES="kind:scenario ..." selects cases, E2_A_KEEP_GOING=1
# records failures and continues. A qualifying phase-A run sets neither.
cases=${E2_A_CASES:-}
keep=${E2_A_KEEP_GOING:-0}
failed=0
fail() { say "E2_A_FAIL $*"; if test "$keep" = 1 && test "${inCase:-0}" = 1; then failed=$((failed+1)); caseFailed=1; return 1; fi; exit 1; }
say "ARGV $0 $*"
say "DATE $(date -u +%Y-%m-%dT%H:%M:%SZ) LIBS=$libs WATCH=$watch"
# Bundle integrity before anything runs.
(cd "$bundle" && sha256sum -c MANIFEST.sha256 >/dev/null 2>&1) || { say "E2_A_REFUSED manifest"; exit 65; }
say "MANIFEST $(sha256sum "$bundle/MANIFEST.sha256" | cut -d' ' -f1)"
# Staging must be executable (a noexec /home would make every case fail opaquely).
opts=$(awk '$5=="/home"{print $6}' /proc/self/mountinfo | tail -n 1)
say "HOME_MOUNT_OPTIONS ${opts:-not-a-mountpoint}"
case ",$opts," in *,noexec,*) say "E2_A_REFUSED noexec staging"; exit 66 ;; esac
# The binaries must load with the selected libraries (e2-probe without arguments exits 64).
if test "$libs" = bundled; then loadCheck=$("$bundle/lib/ld-linux-aarch64.so.1" --library-path "$bundle/lib" "$bundle/bin/e2-probe" 2>&1 && true; echo "rc=$?")
else loadCheck=$("$bundle/bin/e2-probe" 2>&1 && true; echo "rc=$?"); fi
case "$loadCheck" in *rc=64) ;; *) say "E2_A_REFUSED binaries do not load with libs=$libs: $(echo "$loadCheck" | head -n 2 | tr '\n' ' ')"; exit 68 ;; esac
test ! -e /run/inkbound-owner3/active || { say "E2_A_REFUSED foreign reservation present"; exit 67; }
if systemctl list-units --all --plain --no-legend 'inkbound-*' | grep -q .; then say "E2_A_REFUSED inkbound units present"; exit 67; fi
say "MANAGER_VERSION $(busctl get-property org.freedesktop.systemd1 /org/freedesktop/systemd1 org.freedesktop.systemd1.Manager Version)"
watchBefore=$(systemctl show "$watch" --property=InvocationID --value 2>/dev/null || true)
say "WATCH_BEFORE invocation=$watchBefore state=$(systemctl show "$watch" --property=ActiveState --value 2>/dev/null || true)"
pass=0
units=
cleanup() { if test -n "$units"; then systemctl stop $units >/dev/null 2>&1 || true; systemctl reset-failed $units >/dev/null 2>&1 || true; fi; }
trap cleanup EXIT
trap 'cleanup; exit 130' INT TERM
expect() { case "$result" in $1) ;; *) fail "$kind $scenario result: $result" ;; esac; }
runCase() {
 kind=$1 scenario=$2
 session=$(tr -d '-' </proc/sys/kernel/random/uuid)
 root=$out/rt-$session
 stock=inkbound-stock-v2-test-$session.service
 owner=inkbound-owner-v2-$session.service
 reader=inkbound-direct-v2-$session.service
 fuse=inkbound-fuse-v2-$session.service
 units="$reader $fuse $owner $stock"
 mkdir -m 0700 "$root"
 cp "$bundle/bin/$kind-v2-inert" "$root/binary"
 chmod 0755 "$root/binary"
 if test "$libs" = bundled; then
  printf '#!/bin/sh\nexec %s/lib/ld-linux-aarch64.so.1 --library-path %s/lib %s/binary "$@"\n' "$bundle" "$bundle" "$root" >"$root/helper"
 else
  printf '#!/bin/sh\nexec %s/binary "$@"\n' "$root" >"$root/helper"
 fi
 chmod 0755 "$root/helper"
 window=1s
 if test "$scenario" = start-limit; then window=60s; fi
 if test "$kind" = dispatch; then
  systemd-run --quiet --unit="$stock" --property=Type=exec --property=Restart=no --property=KillMode=control-group --property=RuntimeMaxSec=40s --property=TimeoutStopSec=500ms --property=StartLimitBurst=4 --property=StartLimitIntervalSec=$window "$root/helper" stock "$root" "$session" "$scenario"
 else
  systemd-run --quiet --unit="$stock" --property=Type=exec --property=Restart=no --property=KillMode=control-group --property=RuntimeMaxSec=40s --property=TimeoutStopSec=500ms "$root/helper" stock "$root" "$session" "$scenario"
 fi
 for iteration in $(seq 1 100); do test -f "$root/stock-start" && break; sleep 0.02; done
 test -f "$root/stock-start" || fail "$kind $scenario synthetic stock did not start"
 if test "$kind" = dispatch && test "$scenario" != start-limit; then sleep 1.3; fi
 if test "$scenario" = production-start; then "$root/helper" starter "$root" "$session" "$scenario"; else
  systemd-run --quiet --unit="$owner" --property=Type=exec --property=NotifyAccess=none --property=Restart=no --property=KillMode=control-group --property=RuntimeMaxSec=30s --property=TimeoutStartSec=2s --property=TimeoutStopSec=500ms --property=TimeoutAbortSec=500ms --property=CapabilityBoundingSet= --property=NoNewPrivileges=yes --property=Delegate=no --property=KillSignal=SIGKILL --property=FinalKillSignal=SIGKILL --property=SendSIGKILL=yes "$root/helper" owner "$root" "$session" "$scenario"
 fi
 for iteration in $(seq 1 150); do
  state=$(systemctl show "$owner" --property=ActiveState --value)
  if test "$state" = inactive || test "$state" = failed; then break; fi
  sleep 0.1
 done
 sleep 0.1
 evidence=$out/evidence-$kind-$scenario-$session
 mkdir "$evidence"
 for file in "$root"/*; do if test -f "$file" && test "$(basename "$file")" != binary; then cp -a "$file" "$evidence/"; fi; done
 journalctl --no-pager -u "$owner" -u "$reader" -u "$fuse" -u "$stock" >"$evidence/journal.log" || true
 systemctl show $units >"$evidence/units.txt" || true
 say "CASE kind=$kind scenario=$scenario session=$session $(cat "$evidence/result" 2>/dev/null || echo no-result)"
 if test -f "$evidence/report-detail"; then say "REPORT_DETAIL $(cat "$evidence/report-detail")"; fi
 if test -f "$evidence/failure"; then fail "$kind $scenario $(cat "$evidence/failure")"; fi
 test -f "$root/result" || fail "$kind $scenario no result"
 count=$(wc -l <"$root/stock-start" | tr -d ' ')
 result=$(cat "$root/result")
 if test "$kind" = dispatch; then
  case "$scenario" in
   normal|production-start|bus-noise) expect "RESTORED committed=1 takeover=1 witnessed=0 restored=1 released=1 report=1 bound=0 durable=1 *"; test "$count" = 2 && test "$(cat "$root/report-durable")" = 1 && test "$(cat "$root/power-ack")" = 1 || fail "$scenario markers" ;;
   failed-save) expect "RESTORED committed=1 takeover=1 witnessed=0 restored=1 released=1 report=1 bound=0 durable=0 *"; test "$count" = 2 && test "$(cat "$root/report-durable")" = 0 && test "$(cat "$root/power-ack")" = 0 || fail "$scenario markers" ;;
   power-frozen) expect "RESTORED committed=1 takeover=1 witnessed=0 restored=1 released=1 report=0 bound=0 durable=0 *"; test "$count" = 2 && test -f "$root/reader-frozen" || fail "$scenario markers" ;;
   reader-crash) expect "RESTORED committed=1 takeover=1 witnessed=0 restored=1 released=1 report=0 bound=0 durable=0 *"; test "$count" = 2 && test -f "$root/reader-crashing" || fail "$scenario markers" ;;
   start-limit) expect "REFUSED committed=0 takeover=0 witnessed=0 restored=0 released=0 report=0 bound=0 durable=0 closure=0 stage=stock-start-limit:window*"; test "$count" = 1 || fail "$scenario markers" ;;
   launcher-death) expect "ABORTED committed=0 takeover=0 witnessed=0 restored=0 released=1 report=0 bound=0 durable=0 *"; test "$count" = 1 || fail "$scenario markers" ;;
   reader-exec-fail) expect "ABORTED committed=1 takeover=0 witnessed=0 restored=0 released=1 report=0 bound=0 durable=0 *abort-before-takeover"; test "$count" = 1 || fail "$scenario markers" ;;
   reader-fail) expect "ABORTED committed=1 takeover=0 witnessed=0 restored=0 released=1 report=0 bound=0 durable=0 *abort-before-takeover"; test "$count" = 1 && test -f "$root/reader-exited-early" || fail "$scenario markers" ;;
  esac
 else
  case "$scenario" in
   commit-exit|inode-replacement|destructor-retain) expect COMMITTED_OWNER_RETAINED_AND_RELEASED; test "$count" = 1 || fail "$scenario markers" ;;
   takeover-release) expect COMMITTED_OWNER_RETAINED_AND_RELEASED; test "$count" = 2 || fail "$scenario markers" ;;
   *) expect UNCOMMITTED_SAFE_ABORT_RELEASED; test "$count" = 1 || fail "$scenario markers" ;;
  esac
 fi
 test ! -e /run/inkbound-owner3/active || fail "$scenario reservation left"
 cleanup
 units=
 # Remove only this nonce's synthetic session socket and staging root.
 rm -rf "$root" "/run/inkbound-owner-v2/$session"
 if test "${caseFailed:-0}" = 0; then pass=$((pass+1)); fi
}
if test -z "$cases"; then
 for scenario in normal failed-save power-frozen reader-crash launcher-death production-start start-limit bus-noise reader-fail reader-exec-fail; do cases="$cases dispatch:$scenario"; done
 for scenario in commit-exit death-before queued-death inherited-holder wrong-ticket clock-epoch deadline created-abort inode-replacement destructor-retain takeover-release; do cases="$cases launcher:$scenario"; done
else say "DIAGNOSTIC_SELECTION cases=$cases keep_going=$keep (not a qualifying run)"; fi
total=0
for item in $cases; do
 total=$((total+1)); caseFailed=0; inCase=1
 if test "$keep" = 1; then runCase "${item%%:*}" "${item#*:}" || true; else runCase "${item%%:*}" "${item#*:}"; fi
 inCase=0
 if test "$caseFailed" = 1; then cleanup; units=; rm -rf "$root" "/run/inkbound-owner-v2/$session"; fi
done
# Post-checks from the procedure.
if systemctl list-units --all --plain --no-legend 'inkbound-*' | grep -q .; then fail "units remain"; fi
test ! -e /run/inkbound-owner3/active || fail "reservation remains"
if test -d /run/inkbound-owner-v2 && test -n "$(ls -A /run/inkbound-owner-v2)"; then fail "owner-v2 runtime entries remain"; fi
watchAfter=$(systemctl show "$watch" --property=InvocationID --value 2>/dev/null || true)
say "WATCH_AFTER invocation=$watchAfter"
test "$watchBefore" = "$watchAfter" || fail "watched stock invocation changed"
if test -n "${E2_A_CASES:-}"; then say "E2_A_DIAGNOSTIC_DONE cases=$total passed=$pass failed=$failed libs=$libs"; exit 0; fi
test "$pass" = 21 || fail "only $pass of 21 cases passed"
say "E2_A_PASS cases=$pass dispatch=10 launcher=11 libs=$libs"
