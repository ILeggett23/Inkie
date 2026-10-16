#!/bin/sh
# SPDX-License-Identifier: AGPL-3.0-only
# No device action is reachable before the generated candidate gate and hashes.
set -eu
umask 077
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd -P)
cd "$ROOT"
if [ ! -f LAUNCH-GATE ] || [ -L LAUNCH-GATE ]; then
  echo 'Inkbound: missing candidate gate; refusing launch.' >&2
  exit 78
fi
IFS= read -r gate < LAUNCH-GATE || exit 78
if [ "$gate" != 'OFFLINE CANDIDATE READY FOR SCOPED DEVICE ACCEPTANCE' ]; then
  echo 'Inkbound: BUILD-ONLY — NOT SAFE/READY TO LAUNCH.' >&2
  exit 78
fi
command -v sha256sum >/dev/null 2>&1 || { echo 'Inkbound: SHA-256 verifier unavailable.' >&2; exit 78; }
sha256sum -c SHA256SUMS >/dev/null || { echo 'Inkbound: candidate integrity check failed.' >&2; exit 78; }
# Approval is supplied separately after candidate-specific review. Never package it.
APPROVAL=/run/inkbound-offline-acceptance.sha256
if [ ! -f "$APPROVAL" ] || [ -L "$APPROVAL" ]; then
  echo 'Inkbound: current scoped device acceptance approval is required.' >&2
  exit 78
fi
# Trusted installation requires root ownership and no group/other writes.
[ "$(stat -c '%u:%a' "$APPROVAL")" = '0:600' ] || exit 78
IFS= read -r approved < "$APPROVAL" || exit 78
actual=$(sha256sum RELEASE-MANIFEST.json)
actual=${actual%% *}
[ "$approved" = "$actual" ] || { echo 'Inkbound: approval is for another candidate.' >&2; exit 78; }
export INKBOUND_OFFLINE=1
export INKBOUND_CONNECTED_ENABLED=0
export INKBOUND_LIVE_INFERENCE=0
export INKBOUND_COMPANION_POLLING=0
exec "$ROOT/bin/inkbound-direct-handwriting-spike3" --offline-arm
