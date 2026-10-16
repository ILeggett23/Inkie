#!/usr/bin/env python3
"""Deterministic local package preparation. Never installs or calls target devices.

Evidence schema 1 binds full suite runs to the current source/config digest and
actual runtime/test executables. A BUILD-ONLY archive is explicitly unlaunchable.
This tool does not turn SDK/VM results into target-platform qualification.
"""
from pathlib import Path, PurePosixPath
import argparse, gzip, hashlib, io, json, os, re, struct, tarfile

OWNER = '38830b88f2988f291b388d989117e6584c37372cbbf3d1212e48f2f58dbecf38'
READY = 'OFFLINE CANDIDATE READY FOR SCOPED DEVICE ACCEPTANCE'  # Retained gate name; V1 scope includes connected client.
BUILD_ONLY = 'BUILD-ONLY — NOT SAFE/READY TO LAUNCH'
SOURCE_DIRS = ('src', 'recovery', 'reused', 'tests', 'tools', 'config', 'scripts', 'notices', 'third_party')
SOURCE_TOP = ('CMakeLists.txt', 'LICENSE', 'docs/local-reader-release/EPUB-BUILD.json', 'docs/local-reader-release/BASELINE.json')
FORBIDDEN = ('-----BEGIN OPENSSH PRIVATE KEY-----', '-----BEGIN PRIVATE KEY-----', '-----BEGIN RSA PRIVATE KEY-----')

class Invalid(ValueError):
    pass

def require(ok, message):
    if not ok:
        raise Invalid(message)

def digest(data):
    return hashlib.sha256(data).hexdigest()

def canonical(value):
    return (json.dumps(value, sort_keys=True, indent=2, ensure_ascii=False) + '\n').encode()

def read(path):
    require(path.is_file() and not path.is_symlink(), 'missing/nonregular/symlink file: ' + str(path))
    data = path.read_bytes()
    require(not any(re.search(rb'(?m)^' + re.escape(x.encode()) + rb'[ \t]*\r?\n[A-Za-z0-9+/=]{32}', data) for x in FORBIDDEN), 'private key material: ' + str(path))
    return data

def relative(name):
    p = PurePosixPath(name)
    require(name and not p.is_absolute() and '..' not in p.parts and '\\' not in name and
            all(re.fullmatch(r'[A-Za-z0-9_.+-]+', part) and not part.startswith('.') for part in p.parts), 'unsafe member path: ' + name)
    return name

def inventory(root):
    files = {}
    for name in SOURCE_TOP:
        files[name] = digest(read(root / name))
    for folder in SOURCE_DIRS:
        for p in sorted((root / folder).rglob('*')):
            if p.is_dir():
                require(not p.is_symlink(), 'symlink source directory: ' + str(p))
                continue
            name = p.relative_to(root).as_posix()
            if '__pycache__' in p.parts or p.suffix == '.pyc':
                continue
            relative(name)
            files[name] = digest(read(p))
    require(any(x.startswith('src/') for x in files), 'empty production source inventory')
    return files

def elf(data):
    """Read ELF64 little-endian target ABI, imports, versions and hardening."""
    require(len(data) >= 64 and data[:7] == b'\x7fELF\x02\x01\x01', 'not ELF64 little-endian version 1')
    hdr = struct.unpack_from('<HHIQQQIHHHHHH', data, 16)
    etype, machine, version, entry, phoff, shoff, flags, ehsize, phsize, phnum, shsize, shnum, shstr = hdr
    require(machine == 183 and etype in (2, 3) and version == 1, 'not AArch64 executable ABI')
    require(phsize == 56 and shsize == 64 and phnum <= 128 and 0 < shnum <= 65535, 'invalid ELF tables')
    require(phoff + phnum * phsize <= len(data) and shoff + shnum * shsize <= len(data), 'truncated ELF tables')
    sections = [struct.unpack_from('<IIQQQQIIQQ', data, shoff + i * shsize) for i in range(shnum)]
    programs = [struct.unpack_from('<IIQQQQQQ', data, phoff + i * phsize) for i in range(phnum)]
    def block(offset, size):
        require(offset <= len(data) and size <= len(data) - offset, 'ELF range overflow')
        return data[offset:offset + size]
    def cstring(blob, offset):
        require(offset < len(blob), 'ELF string offset')
        end = blob.find(b'\0', offset)
        require(end >= offset, 'ELF unterminated string')
        return blob[offset:end].decode('ascii')
    interp = [block(p[2], p[5]).rstrip(b'\0').decode('ascii') for p in programs if p[0] == 3]
    require(interp == ['/lib/ld-linux-aarch64.so.1'], 'unexpected target ELF interpreter')
    needed, rpath, versions = [], [], {}
    now, relro, stack = False, any(p[0] == 0x6474e552 for p in programs), None
    for p in programs:
        if p[0] == 0x6474e551:
            stack = 'RW' if p[1] == 6 else 'UNSAFE'
    for s in sections:
        if s[1] not in (6, 0x6ffffffe):
            continue
        require(s[6] < len(sections), 'ELF linked strings')
        linked = sections[s[6]]
        strings = block(linked[4], linked[5])
        value = block(s[4], s[5])
        if s[1] == 6:
            require(len(value) % 16 == 0, 'bad ELF dynamic size')
            for at in range(0, len(value), 16):
                tag, n = struct.unpack_from('<qQ', value, at)
                if tag == 1:
                    needed.append(cstring(strings, n))
                if tag in (15, 29):
                    rpath.append(cstring(strings, n))
                now |= tag == 24 or (tag == 30 and bool(n & 8)) or (tag == 0x6ffffffb and bool(n & 1))
        else:
            at, seen = 0, set()
            while at < len(value):
                require(at not in seen and at + 16 <= len(value), 'bad ELF version chain')
                seen.add(at)
                ver, count, fileoff, aux, nxt = struct.unpack_from('<HHIII', value, at)
                require(ver == 1 and 0 < count <= 1024, 'bad ELF version header')
                lib = cstring(strings, fileoff)
                pos, vs = at + aux, []
                for _ in range(count):
                    require(pos + 16 <= len(value), 'bad ELF version aux')
                    h, fl, other, nameoff, anext = struct.unpack_from('<IHHII', value, pos)
                    vs.append(cstring(strings, nameoff))
                    pos += anext
                versions[lib] = sorted(set(vs))
                if not nxt:
                    break
                at += nxt
    require(needed and len(needed) == len(set(needed)), 'missing/duplicate ELF dependencies')
    require(not rpath and relro and now and stack == 'RW', 'ELF hardening failure (RPATH, RELRO, BIND_NOW, stack)')
    return {'architecture': 'aarch64', 'class': 'ELF64', 'interpreter': interp[0], 'needed': sorted(needed),
            'requiredSymbolVersions': versions, 'relro': 'FULL', 'stack': stack, 'rpath': rpath}

def pack(entries, prefix):
    buf = io.BytesIO()
    with gzip.GzipFile(fileobj=buf, mode='wb', filename='', mtime=0, compresslevel=9) as z:
        with tarfile.open(fileobj=z, mode='w', format=tarfile.GNU_FORMAT) as t:
            for name, (data, mode) in sorted(entries.items()):
                relative(name)
                m = tarfile.TarInfo(prefix + '/' + name)
                m.size, m.mode, m.mtime = len(data), mode, 0
                m.uid = m.gid = 0
                m.uname = m.gname = 'root'
                t.addfile(m, io.BytesIO(data))
    return buf.getvalue()

def required_suite_executables(root, cfg):
    """Check the explicit reviewed executable set against the full shell runner.
    This intentionally recognizes only the current static run/for forms; a new
    dynamic runner shape requires review instead of silently losing coverage.
    """
    script = read(root / 'tools/run-local-tests.sh').decode()
    names = set()
    for line in script.splitlines():
        line = line.strip()
        if not line or line.startswith('#') or line.startswith('run(){'):
            continue
        loop = re.fullmatch(r'for t in ([A-Za-z0-9_./+ -]+); do run "\$t"; done', line)
        if loop:
            names.update(loop.group(1).split())
            continue
        call = re.search(r'(?:^|\s)run ([A-Za-z0-9_./+-]+)(?:\s|$)', line)
        if call:
            names.add(call.group(1))
        elif re.search(r'(?:^|\s)run\s', line):
            raise Invalid('unreviewed dynamic suite executable invocation: ' + line)
    require(names, 'full suite runner has no executables')
    # Nested production preparation helpers are not direct shell invocations.
    names.add('reader-experience1-prepare')
    for name in names:
        relative(name)
    reviewed = cfg.get('requiredSuiteExecutables')
    require(isinstance(reviewed, list) and len(reviewed) == len(set(reviewed)) and
            set(reviewed) == names, 'reviewed required executable set differs from full suite runner/helpers')
    return names

def checked_evidence(evidence, source_sha, bins, source_files=None, static_deps=None, required_tests=None):
    blockers = []
    if evidence is None:
        return ['Fresh full-suite structured evidence not supplied'], {}
    require(evidence.get('schema') == 1 and evidence.get('sourceConfigSha256') == source_sha, 'test evidence belongs to another source/config snapshot')
    require(evidence.get('runtimeSha256') == bins, 'test evidence runtime executable hashes mismatch')
    result = {}
    for cfg in ('release', 'asan', 'ubsan'):
        run = evidence.get('configurations', {}).get(cfg)
        if not run:
            blockers.append('Fresh complete ' + cfg + ' suite absent')
            continue
        require(type(run.get('exitCode')) is int and run['exitCode'] == 0 and run.get('completeSuite') is True and
                isinstance(run.get('command'), list) and bool(run['command']) and
                all(isinstance(v, str) and bool(v) and '\x00' not in v for v in run['command']), 'failed/partial structured suite: ' + cfg)
        logfile = Path(run['logPath'])
        require(digest(read(logfile)) == run.get('logSha256'), 'suite log changed: ' + cfg)
        require(run.get('sourceConfigSha256') == source_sha, 'suite source snapshot mismatch: ' + cfg)
        require(run.get('compiledInputsPath') and run.get('compiledInputsSha256'), 'missing compiler input identity: ' + cfg)
        compiled = read(Path(run['compiledInputsPath']))
        require(digest(compiled) == run['compiledInputsSha256'], 'compiler input identity changed: ' + cfg)
        compiled_files = json.loads(compiled)
        require(isinstance(compiled_files, dict), 'invalid compiler input map: ' + cfg)
        if source_files is not None:
            for name, expected in source_files.items():
                if name == 'CMakeLists.txt' or (name.startswith(('src/', 'recovery/', 'reused/', 'tests/')) and name.endswith(('.cpp', '.h', '.hpp', '.c', 'CMakeLists.txt'))):
                    require(compiled_files.get(name) == expected, 'compiled source missing/stale: ' + cfg + ':' + name)
        if static_deps is not None:
            for name, expected in static_deps.items():
                require(compiled_files.get('dependency:' + name) == expected, 'compiled static dependency missing/stale: ' + cfg + ':' + name)
        require(run.get('buildProductsPath') and run.get('buildProductsSha256'), 'missing build output identity: ' + cfg)
        products_data = read(Path(run['buildProductsPath']))
        require(digest(products_data) == run['buildProductsSha256'], 'build output identity changed: ' + cfg)
        products = json.loads(products_data)
        require(products.get('schema') == 1 and products.get('sourceConfigSha256') == source_sha and
                products.get('compiledInputsSha256') == run['compiledInputsSha256'], 'build output source/input binding mismatch: ' + cfg)
        if cfg == 'release':
            require(products.get('runtimeSha256') == bins, 'compiled release runtime mismatch')
        tests = run.get('testExecutables', {})
        require(len(tests) > 0, 'missing actual test executable hashes: ' + cfg)
        if required_tests is not None:
            require(set(tests) == set(required_tests), 'required suite executable set mismatch: ' + cfg +
                    '; missing=' + ','.join(sorted(set(required_tests) - set(tests))) +
                    '; extra=' + ','.join(sorted(set(tests) - set(required_tests))))
        hashes = {}
        for name, record in tests.items():
            relative(name)
            value = digest(read(Path(record['path'])))
            require(value == record['sha256'], 'test executable changed: ' + name)
            hashes[name] = value
        require(products.get('testSha256') == hashes, 'compiled test output mismatch: ' + cfg)
        if cfg == 'asan':
            require(run.get('detectLeaks') is True and run.get('abortOnError') is True, 'ASan leak/abort gate absent')
        if cfg == 'ubsan':
            require(run.get('haltOnError') is True, 'UBSan halt gate absent')
        result[cfg] = {'exitCode': 0, 'completeSuite': True, 'command': run['command'], 'logSha256': run['logSha256'],
                       'compiledInputsSha256': run['compiledInputsSha256'], 'buildProductsSha256': run['buildProductsSha256'], 'testExecutables': hashes,
                       'detectLeaks': run.get('detectLeaks'), 'abortOnError': run.get('abortOnError'), 'haltOnError': run.get('haltOnError')}
    return blockers, result

def qualification(record, source_sha, bins, audits):
    if record is None:
        return ['Sustained lifecycle runtime qualification not supplied', 'Actual target runtime inventory not supplied'], {}
    require(record.get('schema') == 1 and record.get('sourceConfigSha256') == source_sha and record.get('runtimeSha256') == bins, 'qualification belongs to another candidate')
    blockers = []
    required = ('runtimeWired', 'singleRestorationAuthority', 'independentTermination', 'foregroundProgressCannotBeMasked',
                'admissionClosedBeforeDrain', 'allWorkerDomainsDrained', 'terminalDurabilityDistinguished', 'boundedSustainedOperation', 'allTargetPowerPathsCovered')
    for k in required:
        if record.get('lifecycle', {}).get(k) is not True:
            blockers.append('Lifecycle gate missing: ' + k)
    proofs = record.get('proofArtifacts', {})
    require(proofs, 'qualification has no proof artifact identities')
    for name, value in proofs.items():
        require(digest(read(Path(value['path']))) == value['sha256'], 'qualification proof changed: ' + name)
    target = record.get('targetInventory', {})
    if not target.get('deviceModel') or not target.get('firmware') or not target.get('inventoryArtifact'):
        blockers.append('Actual target model/firmware/runtime inventory absent')
    else:
        require(digest(read(Path(target['inventoryArtifact']['path']))) == target['inventoryArtifact']['sha256'], 'target inventory changed')
        libs = target.get('libraries', {})
        for audit in audits.values():
            for lib in audit['needed']:
                item = libs.get(lib, {})
                if not item.get('sha256') or item.get('architecture') != 'aarch64' or not set(audit['requiredSymbolVersions'].get(lib, [])).issubset(item.get('providedSymbolVersions', [])):
                    blockers.append('Unqualified actual target dependency: ' + lib)
        if not target.get('vendorLibraryAbiVerified') or not target.get('powerBackendVerified'):
            blockers.append('Target vendor ABI / finite authenticated power coverage unqualified')
    clean = json.loads(json.dumps(record))
    for p in clean.get('proofArtifacts', {}).values():
        p.pop('path', None)
    if clean.get('targetInventory', {}).get('inventoryArtifact'):
        clean['targetInventory']['inventoryArtifact'].pop('path', None)
    return sorted(set(blockers)), clean

def prepare(root, binaries, out, evidence=None, qual=None, mupdf_source=None):
    cfg = json.loads(read(root / 'config/offline-release.json'))
    require(cfg.get('schema') == 1 and cfg.get('architecture') == 'aarch64' and cfg.get('entryPoint') == '--offline-arm', 'unsupported package configuration')
    require(cfg.get('connected') == {'enabled': False, 'liveInference': False, 'companionPolling': False, 'privateDraftAutoSend': False}, 'offline connected defaults must all be disabled')
    require(all(cfg.get('offline', {}).get(k) is True for k in ('library', 'localImport', 'pdf', 'epub', 'dictionary')), 'required local features cannot be reduced')
    require(cfg.get('ownerSha256') == OWNER, 'recovery owner baseline changed')
    required_bins = ['inkbound-direct-handwriting-spike3', 'reader-experience1-prepare', 'inkbound-recovery-owner']
    require(cfg.get('binaries') == required_bins, 'runtime binary allowlist changed without package review')
    sources = inventory(root)
    baseline = json.loads(read(root / 'docs/local-reader-release/BASELINE.json'))
    deviations = {d['path']: d for d in baseline['deviations']}
    # D1 (user-approved 2026-09-30) adds the sustained-admission core/session deviations.
    require(set(deviations) == {'src/underlay.cpp', 'src/prepare.cpp', 'src/core.cpp', 'src/core.h', 'src/session.cpp', 'src/session.h'}, 'protected deviation set changed')
    for name, baseline_sha in baseline['protected'].items():
        require(digest(read(Path(baseline['source_directory']) / relative(name))) == baseline_sha, 'original protected baseline changed: ' + name)
        require(sources.get(name) == deviations.get(name, {}).get('sha256', baseline_sha), 'protected source mismatch: ' + name)
    source_sha = digest(canonical(sources))
    entries, audits, hashes = {}, {}, {}
    for name in required_bins:
        data = read(binaries / name)
        require(b'INKBOUND_E2_PROVISIONAL_BUILD' not in data, 'E2 provisional timing build can never be packaged: ' + name)
        hashes[name] = digest(data)
        audits[name] = elf(data)
        require(set(audits[name]['needed']).issubset(cfg['runtimeLibraries']), 'dependency outside reviewed allowlist: ' + name)
        entries['bin/' + name] = (data, 0o755)
    require(hashes['inkbound-recovery-owner'] == OWNER, 'pinned independent recovery owner binary changed')
    for name in cfg['bundledAssets']:
        relative(name)
        require(name == 'LICENSE' or name.startswith(('assets/dictionary/', 'notices/dictionary/', 'assets/fonts/', 'notices/fonts/')), 'asset outside explicit allowlist')
        entries[name] = (read(root / name), 0o644)
    for p in sorted((root / 'notices/mupdf').rglob('*')):
        if p.is_file():
            name = p.relative_to(root).as_posix()
            relative(name)
            entries[name] = (read(p), 0o644)
    require(any(n.startswith('notices/mupdf/') for n in entries), 'MuPDF corresponding license notices absent')
    mupdf_pin = json.loads(read(root / 'reused/mupdf-source.json'))
    epub_build = json.loads(read(root / 'docs/local-reader-release/EPUB-BUILD.json'))
    require(epub_build['source']['sha256'] == mupdf_pin['sourceArchiveSha256'], 'MuPDF source pin changed')
    for name, expected in mupdf_pin['licenseFiles'].items():
        require(digest(entries['notices/mupdf/' + relative(name)][0]) == expected, 'MuPDF inherited notice mismatch: ' + name)
    for info in epub_build['addedNotices'].values():
        require(digest(entries[relative(info['path'])][0]) == info['sha256'], 'EPUB enabled-dependency notice mismatch')
    upstream = read(mupdf_source) if mupdf_source else None
    if upstream is not None:
        require(len(upstream) == mupdf_pin['sourceArchiveBytes'] and digest(upstream) == mupdf_pin['sourceArchiveSha256'], 'MuPDF corresponding source archive mismatch')
    fonts = json.loads(entries['assets/fonts/manifest.json'][0])
    require(fonts.get('format') == 'inkbound-fonts-v1' and fonts.get('files') and
            all(digest(entries['assets/fonts/' + n][0]) == sha for n, sha in fonts['files'].items()) and
            all(digest(entries['notices/fonts/' + n][0]) == sha for n, sha in fonts['licenses'].items()), 'Inkbound font asset/license identity mismatch')
    dictionary = json.loads(entries['assets/dictionary/manifest.json'][0])
    require(dictionary.get('entries') == 151384 and dictionary.get('format') == 'ibwn3-v1' and
            digest(entries['assets/dictionary/wordnet.ibd'][0]) == dictionary['sha256'] and
            digest(entries['notices/dictionary/LICENSE.wordnet'][0]) == dictionary['licenseSha256'], 'WordNet asset/license identity mismatch')
    launcher = read(root / 'scripts/offline-launch.sh')
    require(launcher.startswith(b'#!/bin/sh\n') and b'\r' not in launcher and b'--offline-arm' in launcher, 'offline launcher contract missing')
    entries['launch.sh'] = (launcher, 0o755)
    entries['config/offline-release.json'] = (read(root / 'config/offline-release.json'), 0o644)
    blockers, runs = checked_evidence(evidence, source_sha, hashes, sources, {n: v['sha256'] for n, v in epub_build['archives'].items() if '/reader-epub-target/' in n}, required_suite_executables(root, cfg))
    lifecycle_blocks, qualified = qualification(qual, source_sha, hashes, audits)
    blockers += lifecycle_blocks
    if upstream is None:
        blockers.append('Pinned MuPDF full corresponding-source archive not supplied')
    # A self-declared all-true record cannot promote the inherited V1 path.
    # READY needs a reviewed executable/owner/fuse/power integration contract.
    blockers.append("This packager only supports the retained V1 binary route; sustained V2 owner/fuse/offline entry/power package contract is not integrated")
    state = BUILD_ONLY if blockers else READY
    manifest = {'schema': 1, 'classification': state, 'sourceConfigSha256': source_sha, 'sourceHashes': sources,
                'baseline': {'manifestSha256': sources['docs/local-reader-release/BASELINE.json'], 'protected': baseline['protected'], 'deviations': baseline['deviations'], 'ownerSha256': OWNER},
                'runtimeSha256': hashes, 'elf': audits, 'features': cfg, 'tests': runs, 'lifecycleAndTargetQualification': qualified,
                'assets': {n: digest(data) for n, (data, _) in entries.items() if n.startswith(('assets/', 'notices/'))},
                'correspondingSource': {'projectSnapshotIncludedSeparately': True, 'mupdfArchiveSha256': mupdf_pin['sourceArchiveSha256'], 'mupdfArchiveSupplied': upstream is not None},
                'remainingGates': sorted(set(blockers)), 'physicalAcceptance': 'NOT RUN', 'tabletActions': 'NONE',
                'updateRollback': {'activation': 'stage separate version; hash and schema checks; atomic current symlink replacement after scoped approval',
                                   'data': 'preserve data and previous code; never overwrite newer annotations with backup',
                                   'rollback': 'requires explicit schema compatibility proof; otherwise preserve data and return to stock through owner contract'}}
    entries['RELEASE-MANIFEST.json'] = (canonical(manifest), 0o644)
    entries['LAUNCH-GATE'] = ((state + '\n').encode(), 0o644)
    entries['external.manifest.json'] = (canonical({'id': cfg['id'], 'name': cfg['name'], 'version': cfg['version'],
                                                  'description': state, 'application': 'launch.sh', 'qtfb': False}), 0o644)
    entries['SHA256SUMS'] = (''.join(digest(data) + '  ' + n + '\n' for n, (data, _) in sorted(entries.items())).encode(), 0o644)
    # Capture source once and refuse a moving tree before emitting any artifacts.
    source_entries = {n: (read(root / n), 0o644) for n in sources}
    require({n: digest(data) for n, (data, _) in source_entries.items()} == sources and inventory(root) == sources,
            'source/config changed during package preparation')
    source_entries['SOURCE-MANIFEST.json'] = (canonical({'sha256': source_sha, 'files': sources}), 0o644)
    source_archive = pack(source_entries, 'inkbound-reader-offline-source')
    archive = pack(entries, cfg['id'])
    require(archive == pack(entries, cfg['id']), 'nonreproducible archive')
    out.mkdir(parents=True, exist_ok=True)
    path = out / ('inkbound-reader-offline-' + ('build-only' if blockers else 'candidate') + '.tar.gz')
    path.write_bytes(archive)
    (out / 'RELEASE-MANIFEST.json').write_bytes(canonical(manifest))
    result = {'classification': state, 'path': str(path.resolve()), 'bytes': len(archive), 'sha256': digest(archive),
              'manifestSha256': digest(canonical(manifest)), 'sourceConfigSha256': source_sha, 'remainingGates': manifest['remainingGates']}
    (out / 'PACKAGE.json').write_bytes(canonical(result))
    (out / 'SHA256SUMS').write_text(result['sha256'] + '  ' + path.name + '\n')
    # Corresponding source is separate from the runnable package. No books,
    # credentials, fixtures, test helper executables or private state are copied.
    source_path = out / 'inkbound-reader-offline-source.tar.gz'
    source_path.write_bytes(source_archive)
    result['projectSourceSnapshot'] = {'path': str(source_path.resolve()), 'sha256': digest(read(source_path))}
    if upstream is not None:
        upstream_path = out / 'mupdf-1.28.3-source.tar.gz'
        upstream_path.write_bytes(upstream)
        result['upstreamCorrespondingSource'] = {'path': str(upstream_path.resolve()), 'sha256': digest(upstream)}
    (out / 'PACKAGE.json').write_bytes(canonical(result))
    return result

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--source', type=Path, default=Path(__file__).resolve().parents[1])
    p.add_argument('--binaries', type=Path)
    p.add_argument('--out', type=Path)
    p.add_argument('--evidence', type=Path)
    p.add_argument('--qualification', type=Path)
    p.add_argument('--mupdf-source', type=Path, help='Exact pinned full MuPDF source archive, copied separately; never included in runnable package')
    p.add_argument('--print-source-manifest', action='store_true')
    args = p.parse_args()
    try:
        if args.print_source_manifest:
            files = inventory(args.source)
            print(canonical({'sourceConfigSha256': digest(canonical(files)), 'files': files}).decode(), end='')
            return
        require(args.binaries and args.out, '--binaries and --out required')
        load = lambda path: json.loads(read(path)) if path else None
        print(canonical(prepare(args.source, args.binaries, args.out, load(args.evidence), load(args.qualification), args.mupdf_source)).decode(), end='')
    except (Invalid, KeyError, json.JSONDecodeError, OSError, UnicodeError, struct.error) as e:
        p.exit(2, 'PACKAGE REFUSED: ' + str(e) + '\n')

if __name__ == '__main__':
    main()
