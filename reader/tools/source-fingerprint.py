#!/usr/bin/env python3
"""Content identity for compile inputs; mtimes from transferred tarballs are not proof."""
from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1]); files=[]
for folder in ('src','recovery','reused','tests'):
 files.extend(p for p in (root/folder).rglob('*') if p.is_file() and (p.suffix in ('.cpp','.c','.h','.hpp','.cmake','.inc') or p.name=='CMakeLists.txt'))
files.extend(root/p for p in ('CMakeLists.txt','tools/build-cached-sdk.sh','tools/source-fingerprint.py'))
identity={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(files)}
for arg in sys.argv[2:]:
 p=Path(arg);identity['dependency:'+str(p)]=hashlib.sha256(p.read_bytes()).hexdigest()
print(json.dumps(identity,sort_keys=True,indent=2))
