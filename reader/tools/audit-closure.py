#!/usr/bin/env python3
"""Static SDK-only dependency inventory. Never loads or executes target ELF."""
import argparse
import hashlib
import json
import pathlib
import re
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--binary', required=True, type=pathlib.Path)
p.add_argument('--sysroot', required=True, type=pathlib.Path)
p.add_argument('--readelf', default='readelf')
p.add_argument('--qml-scan', required=True, type=pathlib.Path)
p.add_argument('--output', required=True, type=pathlib.Path)
a = p.parse_args()
root = a.sysroot.resolve(strict=True)
def resolve(path):
    for _ in range(32):
        if path.is_symlink():
            target = path.readlink()
            path = root / str(target).lstrip('/') if target.is_absolute() else path.parent / target
        else:
            resolved = path.resolve(strict=True)
            resolved.relative_to(root)
            return resolved
    raise RuntimeError('SDK symlink cycle')
def elf(path, flag):
    return subprocess.check_output([a.readelf, '--debug-dump=no-follow-links', flag, str(path)], text=True)
def inspect(path, name):
    header, dynamic, versions = elf(path, '-hW'), elf(path, '-dW'), elf(path, '-VW')
    if not re.search(r'Machine:\s+AArch64', header):
        raise RuntimeError('non-AArch64 dependency: ' + name)
    needed = sorted(set(re.findall(r'\(NEEDED\).*?\[([^\]]+)\]', dynamic)))
    search = re.findall(r'\((?:RPATH|RUNPATH)\).*?\[([^\]]*)\]', dynamic)
    if any(any(not x.startswith('$ORIGIN') or '..' in x for x in value.split(':')) for value in search):
        raise RuntimeError('unreviewed library search path: ' + name)
    req_text = versions.split("Version needs section", 1)[-1] if 'Version needs section' in versions else ''
    requires = sorted(set(re.findall(r'Name: (\S+)', req_text)))
    required_by_library = {}
    current_library = None
    for line in req_text.splitlines():
        match = re.search(r'File: (\S+)', line)
        if match:
            current_library = match.group(1)
            required_by_library[current_library] = []
        match = re.search(r'Name: (\S+)', line)
        if match and current_library:
            required_by_library[current_library].append(match.group(1))
    return dict(name=name, bytes=path.stat().st_size,
                sha256=hashlib.sha256(path.read_bytes()).hexdigest(), needed=needed,
                requiredVersions=requires, requiredByLibrary=required_by_library, searchPaths=search)
def locate(name):
    if '/' in name: raise RuntimeError('dependency name contains path')
    candidates = []
    for prefix in ('lib', 'usr/lib', 'lib64', 'usr/lib64'):
        path = root / prefix / name
        if path.exists() or path.is_symlink(): candidates.append(resolve(path))
    candidates = sorted(set(candidates))
    if len(candidates) != 1:
        raise RuntimeError(f'SDK dependency absent or ambiguous: {name}')
    return candidates[0]
binary = inspect(a.binary, a.binary.name)
program = elf(a.binary, '-lW')
interpreters = re.findall(r'Requesting program interpreter:\s*([^\]]+)', program)
if len(interpreters) != 1: raise RuntimeError('interpreter missing/ambiguous')
interpreter = resolve(root / interpreters[0].lstrip('/'))
qml_scan = json.loads(a.qml_scan.read_text())
plugins = [] # This prototype has no QML or platform window.
imports = []
for module in qml_scan:
    item = {k: module[k] for k in ('name', 'type', 'plugin', 'optional', 'relativePath') if k in module}
    imports.append(item)
    if module.get('plugin'):
        directory = pathlib.Path(module['path'])
        directory.resolve().relative_to(root)
        for plugin in module['plugin'].split():
            plugins.append(directory / ('lib' + plugin + '.so'))
records = {}
queue = [(locate(n), n) for n in binary['needed']]
queue += [(interpreter, interpreters[0])]
queue += [(resolve(p), '/' + p.relative_to(root).as_posix()) for p in plugins]
while queue:
    path, name = queue.pop(0)
    key = '/' + path.relative_to(root).as_posix()
    if key in records: continue
    record = inspect(path, name)
    record['sdkTargetPath'] = key
    records[key] = record
    queue.extend((locate(n), n) for n in record['needed'])
all_required = sorted(set(v for r in [binary, *records.values()] for v in r['requiredVersions']))
for record in [binary, *records.values()]:
    for library, required in record['requiredByLibrary'].items():
        provider = locate(library)
        definitions = elf(provider, '-VW').split('Version needs section', 1)[0]
        provided = set(re.findall(r'Name: (\S+)', definitions))
        if not set(required) <= provided:
            raise RuntimeError(f'SDK version definitions missing for {record["name"]}: {library}')
result = dict(schemaVersion=1, kind='local-sdk-static-closure', targetExecuted=False,
              tabletRuntimeVerified=False, sdkVersionDefinitionsSatisfied=True,
              binary=binary, interpreter=interpreters[0],
              qmlImports=imports, sdkFiles=list(records.values()), requiredVersions=all_required,
              limitations=['SDK file identities are local comparison metadata, not tablet proof.',
                           'Runtime-selected TLS, image, font and platform plugins may add requirements.'])
a.output.parent.mkdir(parents=True, exist_ok=True)
a.output.write_text(json.dumps(result, indent=2, sort_keys=True) + '\n')
print(f'SDK_CLOSURE=PASS files={len(records)} QML_IMPORTS={len(imports)} TARGET_EXECUTED=no')
