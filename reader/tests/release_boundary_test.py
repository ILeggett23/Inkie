#!/usr/bin/env python3
"""Local read-only preservation regression; no runtime/hardware readiness claim.

The lead explicitly authorized an underlay.cpp atomicFile adapter deviation.
Its current hash is pinned by local-reader-release/BASELINE.json deviations.
"""
import hashlib
import json
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = ROOT.with_name('reader-interaction3')
PROVENANCE = json.loads((BASE / 'PROVENANCE.json').read_text())


def discrepancies(root, expected):
    failures = []
    for name, digest in expected.items():
        path = root / name
        actual = hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else 'MISSING'
        if actual != digest:
            failures.append(f'{name}: expected {digest}, actual {actual}')
    return failures


class ReleaseBoundary(unittest.TestCase):
    def test_original_complete_source_is_preserved(self):
        self.assertEqual(len(PROVENANCE['sourceHashes']), 309)
        self.assertEqual(discrepancies(BASE, PROVENANCE['sourceHashes']), [])

    def test_authorized_underlay_deviation_is_pinned(self):
        manifest = json.loads((ROOT / 'docs/local-reader-release/BASELINE.json').read_text())
        self.assertEqual(hashlib.sha256((BASE / 'PROVENANCE.json').read_bytes()).hexdigest(),
                         manifest['provenance_sha256'])
        deviations = manifest['deviations']
        # D1 (user-approved 2026-09-30) sustained-admission deviations follow the original two.
        self.assertEqual([item['path'] for item in deviations], ['src/underlay.cpp', 'src/prepare.cpp', 'src/core.cpp', 'src/core.h', 'src/session.cpp', 'src/session.h'])
        self.assertEqual(discrepancies(ROOT, {item['path']: item['sha256'] for item in deviations}), [])

    def test_remaining_protected_boundary_is_preserved(self):
        protected = {name: digest for name, digest in PROVENANCE['protectedUnchanged'].items()
                     if name not in ('src/underlay.cpp', 'src/prepare.cpp', 'src/core.cpp', 'src/core.h', 'src/session.cpp', 'src/session.h')}
        self.assertEqual(len(protected), 14)
        self.assertEqual(discrepancies(ROOT, protected), [])


if __name__ == '__main__':
    unittest.main()
