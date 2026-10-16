#!/usr/bin/env python3
"""Package-boundary regression tests; no target operations or VM build required."""
import importlib.util, io, json, os, shutil, struct, subprocess, sys, tarfile, tempfile, unittest
from pathlib import Path
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
SELECTED = Path(os.environ.get('INKBOUND_PACKAGE_SOURCE', '/Users/zayway/Documents/ChatGPT/Remarkable Reading Experience/work/reader-integration'))
spec = importlib.util.spec_from_file_location('offline_package', ROOT / 'tools/package-offline-reader.py')
pkg = importlib.util.module_from_spec(spec)
spec.loader.exec_module(pkg)

class PackageTests(unittest.TestCase):
    def test_retained_real_target_elf_abi_and_dependencies(self):
        for name in json.loads((ROOT / 'config/offline-release.json').read_text())['binaries']:
            audit = pkg.elf((SELECTED / 'artifact' / name).read_bytes())
            self.assertEqual(audit['architecture'], 'aarch64')
            self.assertEqual(audit['relro'], 'FULL')
            self.assertEqual(audit['stack'], 'RW')
            self.assertEqual(audit['rpath'], [])
            self.assertTrue(audit['needed'])
        self.assertEqual(pkg.digest((SELECTED / 'artifact/inkbound-recovery-owner').read_bytes()), pkg.OWNER)

    def test_wrong_architecture_and_truncation_fail_closed(self):
        original = bytearray((SELECTED / 'artifact/inkbound-recovery-owner').read_bytes())
        struct.pack_into('<H', original, 18, 62)
        with self.assertRaises(pkg.Invalid):
            pkg.elf(original)
        with self.assertRaises(pkg.Invalid):
            pkg.elf(b'\x7fELF\x02\x01\x01')

    def test_determinism_modes_and_no_special_members(self):
        entries = {'launch.sh': (b'#!/bin/sh\nexit 78\n', 0o755), 'LAUNCH-GATE': (pkg.BUILD_ONLY.encode(), 0o644)}
        a = pkg.pack(entries, 'inkbound-reader-offline')
        self.assertEqual(a, pkg.pack(entries, 'inkbound-reader-offline'))
        with tarfile.open(fileobj=io.BytesIO(a)) as t:
            for m in t:
                self.assertTrue(m.isfile())
                self.assertEqual(m.uid, 0)
                self.assertEqual(m.gid, 0)
                self.assertEqual(m.mtime, 0)
            self.assertEqual(t.getmember('inkbound-reader-offline/launch.sh').mode, 0o755)
        for bad in ('../escape', '/absolute', 'a/../b', '.secret', 'a\\b', 'a\nfile'):
            with self.assertRaises(pkg.Invalid):
                pkg.pack({bad: (b'', 0o644)}, 'inkbound-reader-offline')

    def test_symlinks_and_keys_rejected(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            (path / 'real').write_text('safe')
            (path / 'link').symlink_to(path / 'real')
            with self.assertRaises(pkg.Invalid):
                pkg.read(path / 'link')
            (path / 'real').write_text('-----BEGIN OPENSSH PRIVATE KEY-----\n' + 'A' * 64 + '\n')
            with self.assertRaises(pkg.Invalid):
                pkg.read(path / 'real')

    def test_arbitrary_pass_or_stale_evidence_cannot_qualify(self):
        for evidence in ({'result': 'PASS'}, {'schema': 1, 'sourceConfigSha256': 'old', 'runtimeSha256': {}}):
            with self.assertRaises(pkg.Invalid):
                pkg.checked_evidence(evidence, 'current', {})
        blockers, records = pkg.checked_evidence(None, 'current', {})
        self.assertTrue(blockers)
        self.assertFalse(records)

    def test_log_test_binary_compile_identity_and_leak_gates(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            for n, data in [('log', b'observed suite output'), ('compiled', b'{}'), ('test', b'actual hashed test binary')]:
                (path / n).write_bytes(data)
            run = {'exitCode': 0, 'completeSuite': True, 'command': ['runner'], 'sourceConfigSha256': 'current',
                   'logPath': str(path / 'log'), 'logSha256': pkg.digest((path / 'log').read_bytes()),
                   'compiledInputsPath': str(path / 'compiled'), 'compiledInputsSha256': pkg.digest(b'{}'),
                   'testExecutables': {'a-test': {'path': str(path / 'test'), 'sha256': pkg.digest((path / 'test').read_bytes())}},
                   'detectLeaks': True, 'abortOnError': True, 'haltOnError': True}
            products = {'schema': 1, 'sourceConfigSha256': 'current',
                        'compiledInputsSha256': pkg.digest(b'{}'), 'runtimeSha256': {'reader': 'binary'},
                        'testSha256': {'a-test': pkg.digest((path / 'test').read_bytes())}}
            (path / 'products').write_bytes(pkg.canonical(products))
            run['buildProductsPath'] = str(path / 'products')
            run['buildProductsSha256'] = pkg.digest((path / 'products').read_bytes())
            ev = {'schema': 1, 'sourceConfigSha256': 'current', 'runtimeSha256': {'reader': 'binary'},
                  'configurations': {n: dict(run) for n in ('release', 'asan', 'ubsan')}}
            blockers, _ = pkg.checked_evidence(ev, 'current', {'reader': 'binary'})
            self.assertFalse(blockers)
            for invalid in (False, True, '0', 0.0):
                ev['configurations']['release']['exitCode'] = invalid
                with self.assertRaises(pkg.Invalid):
                    pkg.checked_evidence(ev, 'current', {'reader': 'binary'})
            ev['configurations']['release']['exitCode'] = 0
            for invalid in ('runner', [], [''], ['runner', False], ['runner\0']):
                ev['configurations']['release']['command'] = invalid
                with self.assertRaises(pkg.Invalid):
                    pkg.checked_evidence(ev, 'current', {'reader': 'binary'})
            ev['configurations']['release']['command'] = ['runner']
            with self.assertRaises(pkg.Invalid):
                pkg.checked_evidence(ev, 'current', {'reader': 'binary'}, {'src/main.cpp': 'new-source'})
            original_products = (path / 'products').read_bytes()
            products['runtimeSha256'] = {'reader': 'stale-binary'}
            (path / 'products').write_bytes(pkg.canonical(products))
            for r in ev['configurations'].values():
                r['buildProductsSha256'] = pkg.digest((path / 'products').read_bytes())
            with self.assertRaisesRegex(pkg.Invalid, 'compiled release runtime mismatch'):
                pkg.checked_evidence(ev, 'current', {'reader': 'binary'})
            (path / 'products').write_bytes(original_products)
            for r in ev['configurations'].values():
                r['buildProductsSha256'] = pkg.digest(original_products)
            ev['configurations']['asan']['detectLeaks'] = False
            with self.assertRaises(pkg.Invalid):
                pkg.checked_evidence(ev, 'current', {'reader': 'binary'})
            ev['configurations']['asan']['detectLeaks'] = True
            (path / 'test').write_text('new binary')
            with self.assertRaises(pkg.Invalid):
                pkg.checked_evidence(ev, 'current', {'reader': 'binary'})

    def test_full_suite_required_set_includes_new_targets_and_refuses_omission(self):
        cfg = json.loads((ROOT / 'config/offline-release.json').read_text())
        required = pkg.required_suite_executables(SELECTED, cfg)
        for name in ('protocol-v2-tests', 'progress-v2-tests', 'control-v2-tests',
                     'lease_fuse-v2-tests', 'power-events-tests', 'v1-panel-effects-tests',
                     'runtime-v2-tests', 'interrupted-styled-route-tests',
                     'reader-experience1-prepare', 'reader-reopen', 'ux-reopen', 'semantic-reopen'):
            self.assertIn(name, required)
        bad = dict(cfg); bad['requiredSuiteExecutables'] = [n for n in cfg['requiredSuiteExecutables'] if n != 'v1-panel-effects-tests']
        with self.assertRaisesRegex(pkg.Invalid, 'reviewed required executable set differs'):
            pkg.required_suite_executables(SELECTED, bad)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            for n, data in [('log', b'observed'), ('compiled', b'{}'), ('test', b'actual')]:
                (path / n).write_bytes(data)
            for omitted in ('protocol-v2-tests', 'v1-panel-effects-tests'):
                present = required - {omitted}
                tests = {n: {'path': str(path / 'test'), 'sha256': pkg.digest(b'actual')} for n in present}
                products = {'schema': 1, 'sourceConfigSha256': 'current', 'compiledInputsSha256': pkg.digest(b'{}'),
                            'runtimeSha256': {}, 'testSha256': {n: pkg.digest(b'actual') for n in present}}
                (path / 'products').write_bytes(pkg.canonical(products))
                run = {'exitCode': 0, 'completeSuite': True, 'command': ['runner'], 'sourceConfigSha256': 'current',
                       'logPath': str(path / 'log'), 'logSha256': pkg.digest(b'observed'),
                       'compiledInputsPath': str(path / 'compiled'), 'compiledInputsSha256': pkg.digest(b'{}'),
                       'buildProductsPath': str(path / 'products'), 'buildProductsSha256': pkg.digest(pkg.canonical(products)),
                       'testExecutables': tests}
                ev = {'schema': 1, 'sourceConfigSha256': 'current', 'runtimeSha256': {}, 'configurations': {'release': run}}
                with self.assertRaisesRegex(pkg.Invalid, 'required suite executable set mismatch: release; missing=' + omitted):
                    pkg.checked_evidence(ev, 'current', {}, required_tests=required)

    def test_launcher_build_only_stops_before_any_runtime(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            shutil.copyfile(ROOT / 'scripts/offline-launch.sh', path / 'launch.sh')
            (path / 'LAUNCH-GATE').write_text(pkg.BUILD_ONLY + '\n')
            (path / 'bin').mkdir()
            marker = path / 'executed'
            (path / 'bin/inkbound-direct-handwriting-spike3').write_text('#!/bin/sh\ntouch "' + str(marker) + '"\n')
            (path / 'bin/inkbound-direct-handwriting-spike3').chmod(0o755)
            result = subprocess.run(['sh', str(path / 'launch.sh')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 78)
            self.assertIn('BUILD-ONLY', result.stderr)
            self.assertFalse(marker.exists())

    def test_launcher_ready_requires_integrity_and_external_scoped_approval(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            shutil.copyfile(ROOT / 'scripts/offline-launch.sh', path / 'launch.sh')
            (path / 'LAUNCH-GATE').write_text(pkg.READY + '\n')
            # Missing checksums fails without touching any runtime.
            result = subprocess.run(['sh', str(path / 'launch.sh')], capture_output=True, text=True)
            self.assertEqual(result.returncode, 78)

    def test_package_allowlist_real_assets_and_v1_cannot_be_promoted(self):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp)
            source = path / 'source'
            for d in ('config', 'scripts', 'src', 'reused', 'docs/local-reader-release', 'notices/mupdf', 'notices/dictionary', 'assets/dictionary', 'tools'):
                (source / d).mkdir(parents=True, exist_ok=True)
            for n in ('config/offline-release.json', 'scripts/offline-launch.sh'):
                shutil.copyfile(ROOT / n, source / n)
            shutil.copyfile(SELECTED / 'tools/run-local-tests.sh', source / 'tools/run-local-tests.sh')
            (source / 'src/main.cpp').write_text('// package test snapshot\n')
            (source / 'CMakeLists.txt').write_text('# test snapshot\n')
            for n in ('LICENSE', 'reused/mupdf-source.json', 'docs/local-reader-release/EPUB-BUILD.json', 'docs/local-reader-release/BASELINE.json', 'assets/dictionary/wordnet.ibd', 'assets/dictionary/manifest.json',
                      'notices/dictionary/LICENSE.wordnet', 'notices/dictionary/ATTRIBUTION.md'):
                shutil.copyfile(SELECTED / n, source / n)
            for n in json.loads((SELECTED / 'docs/local-reader-release/BASELINE.json').read_text())['protected']:
                (source / n).parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(SELECTED / n, source / n)
            for p in (SELECTED / 'notices/mupdf').iterdir():
                shutil.copyfile(p, source / 'notices/mupdf' / p.name)
            # Bait never enters the allowlist.
            (source / 'fixtures').mkdir()
            (source / 'fixtures/private-book.pdf').write_text('private fixture')
            (source / 'fake-helper').write_text('fake inference')
            result = pkg.prepare(source, SELECTED / 'artifact', path / 'output')
            self.assertEqual(result['classification'], pkg.BUILD_ONLY)
            self.assertIn('V1 binary route', ' '.join(result['remainingGates']))
            with tarfile.open(result['path']) as t:
                names = t.getnames()
                self.assertTrue(any(n.endswith('/assets/dictionary/wordnet.ibd') for n in names))
                self.assertTrue(any(n.endswith('/notices/dictionary/LICENSE.wordnet') for n in names))
                self.assertFalse(any('fixtures' in n or 'fake-helper' in n or '/tests/' in n for n in names))
                manifest = json.load(t.extractfile('inkbound-reader-offline/RELEASE-MANIFEST.json'))
                self.assertEqual(manifest['classification'], pkg.BUILD_ONLY)
                self.assertEqual(manifest['features']['connected']['liveInference'], False)
                sums = t.extractfile('inkbound-reader-offline/SHA256SUMS').read().decode().splitlines()
                for line in sums:
                    sha, n = line.split('  ', 1)
                    self.assertEqual(sha, pkg.digest(t.extractfile('inkbound-reader-offline/' + n).read()))
            with tarfile.open(result['projectSourceSnapshot']['path']) as source_tar:
                self.assertTrue(any('/notices/mupdf/' in n for n in source_tar.getnames()))
                source_manifest = json.load(source_tar.extractfile('inkbound-reader-offline-source/SOURCE-MANIFEST.json'))
                for n, sha in source_manifest['files'].items():
                    self.assertEqual(sha, pkg.digest(source_tar.extractfile('inkbound-reader-offline-source/' + n).read()))
            original_read = pkg.read
            def mutate_after_inventory(p):
                data = original_read(p)
                if p == source / 'scripts/offline-launch.sh':
                    (source / 'src/main.cpp').write_text('// changed while preparing\n')
                return data
            with mock.patch.object(pkg, 'read', side_effect=mutate_after_inventory):
                with self.assertRaisesRegex(pkg.Invalid, 'source/config changed'):
                    pkg.prepare(source, SELECTED / 'artifact', path / 'refused-output')
            self.assertFalse((path / 'refused-output').exists())
            # All-true fabricated lifecycle claims cannot promote this old path.
            with mock.patch.object(pkg, 'checked_evidence', return_value=([], {})), mock.patch.object(pkg, 'qualification', return_value=([], {'all': True})):
                result2 = pkg.prepare(source, SELECTED / 'artifact', path / 'output2')
            self.assertEqual(result2['classification'], pkg.BUILD_ONLY)

if __name__ == '__main__':
    unittest.main(verbosity=2)
