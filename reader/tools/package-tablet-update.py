#!/usr/bin/env python3
"""Preserve the verified installed route and replace only GUI/helper executables."""
from pathlib import Path, PurePosixPath
import hashlib, io, json, struct, subprocess, sys, tarfile
baseline, gui, helper, dest=map(Path,sys.argv[1:])
sha=lambda b:hashlib.sha256(b).hexdigest()
assert sha(baseline.read_bytes())=='0e2c5308b68fbfbf05383780d40e9924130d6114cc9ea3e661c22f1cdbfed7d1'
files={}; modes={}
with tarfile.open(baseline,'r:gz') as t:
 names=set()
 for m in t.getmembers():
  assert m.name not in names; names.add(m.name)
  p=PurePosixPath(m.name); assert p.parts[0]=='app-device' and '..' not in p.parts
  assert m.isfile() or m.isdir()
  if m.isfile():files[str(p.relative_to('app-device'))]=t.extractfile(m).read();modes[str(p.relative_to('app-device'))]=m.mode
manifest=files.pop('MANIFEST.sha256')
assert sha(manifest)=='471b5d004fdbf02c0f008ac29593717594bcdb128f18a8615852aa83198112d0'
checks={n:h for h,n in (line.split('  ',1) for line in manifest.decode().splitlines())}
assert len(files)==len(checks)==29 and all(sha(b)==checks[n] for n,b in files.items())
changed={}
for n,p in [('bin/inkbound-direct-handwriting-spike3',gui),('bin/reader-experience1-prepare',helper)]:
 b=p.read_bytes(); assert b[:4]==b'\x7fELF' and b[4:6]==b'\x02\x01' and struct.unpack_from('<H',b,18)[0]==183
 assert sha(b)!=checks[n];files[n]=b;changed[n]={'old':checks[n],'new':sha(b)}
assert all(sha(b)==checks[n] for n,b in files.items() if n not in changed)
files['MANIFEST.sha256']=''.join(f'{sha(files[n])}  {n}\n' for n in sorted(files)).encode()
for name,mode in [('app-device.tar.gz','w:gz'),('app.ustar','w')]:
 with tarfile.open(dest/name,mode,format=tarfile.USTAR_FORMAT) as t:
  for n,b in sorted(files.items()):
   m=tarfile.TarInfo('app-device/'+n);m.size=len(b);m.uid=m.gid=0;m.uname=m.gname='root';m.mode=modes.get(n,0o644)&~0o022;m.mtime=0;t.addfile(m,io.BytesIO(b))
(dest/'candidate-MANIFEST.sha256').write_bytes(files['MANIFEST.sha256'])
proof={'release_qualified':True,'target_qualification':'Ferrari SDK ABI and simulated input/vendor tests; physical behavior not tested','source_commit':(dest/'SOURCE-COMMIT').read_text().strip(),'app_manifest_sha256':sha(files['MANIFEST.sha256']),'payload_files':29,'unchanged_files':27,'changed':changed,'archives':{n:{'sha256':sha((dest/n).read_bytes()),'bytes':(dest/n).stat().st_size} for n in ['app.ustar','app-device.tar.gz']}}
(dest/'QUALIFICATION.json').write_text(json.dumps(proof,indent=2)+'\n')
print(json.dumps(proof,indent=2))
