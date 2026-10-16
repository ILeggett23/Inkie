#!/usr/bin/env python3
# SPDX-License-Identifier: AGPL-3.0-only
"""Write the E2 phase-E PROVISIONAL lifecycle profile (outputs/E2-PROCEDURE.md).

Generous but finite values, to be tightened from phase D measurements. The
qualification label starts "provisional-e2", which only an INKBOUND_E2_PROVISIONAL
build accepts and every normal build refuses, so this file can never be pinned
into a package. Prints the SHA-256 to pass as INKBOUND_LIFECYCLE_PROFILE_SHA256
to the provisional build only.
"""
import argparse, hashlib, re
from pathlib import Path

MS = 1_000_000
S = 1000 * MS
# Domain order: canonical, notes, tom, preparation (helper), resume.
KINDS = ['durable-prefix', 'durable-prefix', 'durable-prefix', 'cancelable-helper-prefix', 'durable-prefix']

def profile(manager, date, start_s, stop_s, challenge_ms=200, health_age_ms=300, work_age_s=10, prep_age_s=10, ceiling_h=0):
    kill = stop_grace = max(1500 * MS, stop_s * S)
    drain = stop_grace + kill + 500 * MS   # termination <= drainLead; phase E checks drain < 5 s logind delay
    # health exchange (cadence + challenge) and health age must fit inside grant - drainLead
    grant = drain + max(2 * S, health_age_ms * MS + 600 * MS)
    startup = max(30 * S, start_s * S)
    lines = [
        ('format', 'inkbound-lifecycle-profile-1'), ('selection', 'qualified'),
        ('qualification', 'provisional-e2-' + date), ('managerVersion', manager),
        ('powerRoutes', 'provisional-e2-autosleep-wakelock-' + date),
        ('healthCadence', 50 * MS), ('challengeLifetime', challenge_ms * MS), ('healthMaxAge', health_age_ms * MS),
        ('grantLifetime', grant), ('drainLead', drain), ('startupBudget', startup),
        ('stopGrace', stop_grace), ('killGrace', kill), ('proofWindow', max(10 * S, 2 * (stop_grace + kill))),
        ('sleepAllowance', startup + 20 * S), ('clockSkew', 10 * MS), ('workers', len(KINDS)),
    ]
    lines += [('worker%d' % n, '%s,32,%d' % (k, (prep_age_s if k == 'cancelable-helper-prefix' else work_age_s) * S)) for n, k in enumerate(KINDS)]
    fuse = stop_grace + kill - 250 * MS
    lines += [
        ('fuseBootstrapLifetime', fuse), ('fuseLeaseLifetime', fuse), ('fuseDispatchMargin', 250 * MS),
        ('readerCeiling', ceiling_h * 3600 * S if ceiling_h else 600 * S), ('ownerCeiling', (ceiling_h * 3600 + 100) * S if ceiling_h else 700 * S), ('callBudget', 100 * MS),
        ('startBudget', startup - S), ('stopBudget', kill),
        ('powerScope', 'verified-platform'), ('powerEvidence', 'provisional-e2-unqualified-' + date),
        ('powerAllRelevantSleepRoutes', 1), ('powerFinitePreSleepBarrier', 1),
        ('powerDelayBudgetUsec', 5_000_000), ('powerMaximumSignalLagUsec', 500_000),
        ('sustainedWindowUsec', 1_000_000), ('sustainedContactsPerWindow', 40), ('sustainedSamplesPerWindow', 8000),
        ('sessionPowerKeyName', '30370000.snvs:snvs-powerkey'), ('sessionCoverName', 'Hall effect sensors'),
        ('sessionIdleUsec', 120_000_000),
    ]
    return ''.join('%s=%s\n' % kv for kv in lines)

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--manager-version', required=True, help='exact Manager Version recorded in phase A')
    p.add_argument('--date', required=True, help='YYYYMMDD of the E2 session')
    p.add_argument('--start-seconds', type=int, default=30, help='phase D worst start-to-running, rounded up')
    p.add_argument('--stop-seconds', type=int, default=2, help='phase D worst stop, rounded up')
    p.add_argument('--challenge-ms', type=int, default=200, help='challenge reply window (E2 run 4: full-page draw ~0.9 s)')
    p.add_argument('--health-age-ms', type=int, default=300, help='maximum main-progress age')
    p.add_argument('--suffix', default='', help='label suffix to distinguish provisional revisions')
    p.add_argument('--work-age-s', type=int, default=10, help='durable worker maxPendingAge')
    p.add_argument('--prep-age-s', type=int, default=10, help='preparation (helper) worker maxPendingAge')
    p.add_argument('--ceiling-hours', type=int, default=0, help='D2: unit runtime ceilings in hours (0 = legacy 600/700 s)')
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    if not re.fullmatch(r'[!-~]{1,64}', a.manager_version) or not re.fullmatch(r'\d{8}', a.date):
        raise SystemExit('bad manager version or date')
    if not re.fullmatch(r'[a-z0-9]{0,8}', a.suffix) or not (50 < a.challenge_ms < a.health_age_ms <= 10000):
        raise SystemExit('bad suffix or health timing')
    text = profile(a.manager_version, a.date + a.suffix, a.start_seconds, a.stop_seconds, a.challenge_ms, a.health_age_ms, a.work_age_s, a.prep_age_s, a.ceiling_hours)
    a.out.write_text(text)
    a.out.chmod(0o644)
    print(hashlib.sha256(text.encode()).hexdigest())

if __name__ == '__main__':
    main()
