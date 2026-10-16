"""Local-only deterministic Interaction3 artifacts. No device operations."""
from pathlib import Path
import hashlib,json,tarfile,gzip,io,difflib,subprocess
r=Path(__file__).resolve().parents[1];b=r.with_name('reader-ux2');out=r/'outputs'
if r.name != "reader-interaction3":
 raise SystemExit("Historical Interaction3 packager cannot package reader-integration. Complete integration and its fresh release manifest/acceptance gates first.")
sha=lambda data:hashlib.sha256(data).hexdigest();fh=lambda p:sha(p.read_bytes())
subprocess.run(['python3',str(r/'tools/baseline.py')],check=True)
subprocess.run(['python3',str(r/'tools/analyze-evidence.py')],check=True)
baseline=json.loads((r/'docs/BASELINE.json').read_text())
for p in (r/'reused/qualified-ux2').rglob('*'):
 if p.is_file() and p.relative_to(r/'reused/qualified-ux2').as_posix()!='tests/ux_perf.cpp':assert p.read_bytes()==(b/p.relative_to(r/'reused/qualified-ux2')).read_bytes(),p
protected=['src/'+n for n in ['core.cpp','core.h','marker.cpp','marker.h','session.cpp','session.h','vendor.cpp','vendor.h','vendor_runtime.cpp','recovery_boundary.cpp','underlay.cpp','prepare.cpp','page_helper.cpp']]
protected += ['recovery/'+p.name for p in sorted((b/'recovery').iterdir()) if p.is_file()]
for n in protected:assert fh(r/n)==fh(b/n),n
log=(r/'evidence/FINAL-VALIDATION-EVICTION.log').read_text()
for cfg in ['release','asan','ubsan']:assert 'CONFIG_PASS='+cfg in log
for marker in ['SEMANTIC_TESTS_PASS cases=20','SEMANTIC_FRESH_PROCESS_REOPEN_PASS','UX_TESTS_PASS cases=14','READER_TESTS_PASS cases=11','UNDERLAY_TESTS_PASS cases=33','DIRECT_HOST_TESTS_PASS cases=60','RECOVERY_ADVERSARIAL_PASS cases=35','CACHE_TESTS_PASS']:assert log.count(marker)>=3,marker
assert 'UX_BINARY_CLEAN_REBUILD_IDENTICAL' in (r/'evidence/BINARY-REPRODUCTION.log').read_text()
audit=json.loads((r/'evidence/builder-audit/artifact-audit.json').read_text())
for n,v in audit.items():assert fh(r/'artifact'/n)==v['sha256']
assert fh(r/'artifact/inkbound-recovery-owner')=='38830b88f2988f291b388d989117e6584c37372cbbf3d1212e48f2f58dbecf38'
changes={};diff=[]
for p in sorted((r/'src').glob('*')):
 if not p.is_file():continue
 q=b/'src'/p.name
 if not q.exists() or p.read_bytes()!=q.read_bytes():
  n=p.relative_to(r).as_posix();changes[n]={'baseline':fh(q) if q.exists() else None,'interaction3':fh(p)}
  diff.extend(difflib.unified_diff(q.read_text().splitlines(True) if q.exists() else [],p.read_text().splitlines(True),fromfile='accepted-ux2/'+n,tofile='reader-interaction3/'+n))
(r/'docs/protected-source.diff').write_text(''.join(diff))
pin=json.loads((r/'reused/mupdf-source.json').read_text())
for n,h in pin['licenseFiles'].items():assert fh(r/'notices/mupdf'/n)==h
assert fh(r/'fixtures/inkbound-device-test.pdf')==pin['fixtureSha256']
dictionary=json.loads((r/'fixtures/dictionary-test.json').read_text());assert dictionary['license']=='AGPL-3.0-only; project-authored fixture' and len(dictionary['entries'])==8
with tarfile.open(b/'outputs/reader-ux2-candidate.tar.gz') as t:
 prefix='inkbound-reader-ux2/';launcher=t.extractfile(prefix+'launch.sh').read();policy=t.extractfile(prefix+'docs/RECOVERY-POLICY.json').read()
subprocess.run(['sh','-n'],input=launcher,check=True);assert launcher.startswith(b'#!/bin/sh\n') and b'\r' not in launcher
prov={'revision':'reader-interaction3','classification':'NON-RELEASE DEVICE-TEST CANDIDATE / NOT PHYSICALLY QUALIFIED / SEPARATE APPROVAL REQUIRED','forwardBaseline':{'candidate':'fb2edd226ff05d28622fa434e6fcb92ec79af3a73d73aba08b55a275fde91dae','source':'bd581fb917471573fc3d56b75d7bb4feecb4a325e4dadfc0348f47b7e33c9d62','evidence':'2229125abfecf95b2e1baddb9fa8023bacaf15b3c9f9888610b122e232cd7c9f','formalOpeningA':'UNCHANGED - INCOMPLETE DUE DEADLINE','physicalReportSha256':baseline['physicalReport']},'protectedUnchanged':{n:fh(r/n) for n in protected},'integrationChanges':changes,'sourceHashes':{p.relative_to(r).as_posix():fh(p) for folder in ['src','recovery','reused'] for p in sorted((r/folder).rglob('*')) if p.is_file()},'binaries':audit,'dictionary':{'provider':dictionary['provider'],'sha256':fh(r/'fixtures/dictionary-test.json'),'license':dictionary['license'],'entries':8,'fullDictionary':False,'ownedInterfaceSha256':baseline['dictionaryInterface']},'fixture':pin['fixtureSha256'],'dataDirectory':'/home/root/.local/share/inkbound/reader-interaction3','tabletAccess':'NONE','physicalTest':'NOT RUN'}
(r/'PROVENANCE.json').write_text(json.dumps(prov,indent=2)+'\n')
def pack(entries):
 buf=io.BytesIO()
 with gzip.GzipFile(fileobj=buf,mode='wb',filename='',mtime=0) as z:
  with tarfile.open(fileobj=z,mode='w') as t:
   for name,data,mode in sorted(entries):
    assert not name.startswith('/') and '..' not in Path(name).parts and not any(s.startswith('.') for s in Path(name).parts)
    m=tarfile.TarInfo(name);m.size=len(data);m.mode=mode;m.uid=m.gid=0;m.uname=m.gname='root';m.mtime=0;t.addfile(m,io.BytesIO(data))
 return buf.getvalue()
root='inkbound-reader-interaction3/'
entries=[(root+'bin/'+n,(r/'artifact'/n).read_bytes(),0o755) for n in audit]
manifest={'id':'inkbound-reader-interaction3','name':'Inkbound Reader Interaction 3','version':'0.1.0-test','description':'Finger text selection, semantic highlights and offline test dictionary. Separate physical approval required.','application':'launch.sh','qtfb':False}
entries += [(root+'launch.sh',launcher,0o755),(root+'external.manifest.json',json.dumps(manifest).encode()+b'\n',0o644),(root+'docs/RECOVERY-POLICY.json',policy,0o644)]
for n in ['LICENSE','README.md','PROVENANCE.json','fixtures/inkbound-device-test.pdf','fixtures/dictionary-test.json']:entries.append((root+n,(r/n).read_bytes(),0o644))
for folder in ['docs','notices']:
 for p in sorted((r/folder).rglob('*')):
  if p.is_file():entries.append((root+p.relative_to(r).as_posix(),p.read_bytes(),0o644))
entries.append((root+'SHA256SUMS',''.join(sha(data)+'  '+name[len(root):]+'\n' for name,data,mode in sorted(entries)).encode(),0o644))
candidate=pack(entries);assert candidate==pack(entries)
with tarfile.open(fileobj=io.BytesIO(candidate)) as t:
 for m in t.getmembers():
  assert m.isfile() and not any(s in m.name for s in ['/tests/','prepared.bin','semantic-text.json','semantic-highlights.json','vocabulary.json','annotations.ink','known_hosts','id_ed25519']) and not m.name.endswith(('.so','.a'))
  assert b'-----BEGIN OPENSSH PRIVATE KEY-----' not in t.extractfile(m).read()
 for n,v in audit.items():assert sha(t.extractfile(root+'bin/'+n).read())==v['sha256'] and t.getmember(root+'bin/'+n).mode==0o755
(out/'reader-interaction3-candidate.tar.gz').write_bytes(candidate)
sources=[]
for folder in ['src','recovery','reused','tests','tools','docs','fixtures','notices']:
 for p in sorted((r/folder).rglob('*')):
  if p.is_file() and not any(s.startswith('.') or s=='__pycache__' for s in p.relative_to(r).parts):sources.append(('reader-interaction3-source/'+p.relative_to(r).as_posix(),p.read_bytes(),0o644))
for n in ['LICENSE','README.md','CMakeLists.txt','PROVENANCE.json']:sources.append(('reader-interaction3-source/'+n,(r/n).read_bytes(),0o644))
source=pack(sources);assert source==pack(sources);(out/'reader-interaction3-source.tar.gz').write_bytes(source)
evidence=[]
for p in sorted((r/'evidence').rglob('*')):
 if p.is_file() and p.suffix in ['.log','.txt','.json','.png'] and not any(s.startswith('.') for s in p.relative_to(r/'evidence').parts):evidence.append(('reader-interaction3-evidence/'+p.relative_to(r/'evidence').as_posix(),p.read_bytes(),0o644))
ev=pack(evidence);assert ev==pack(evidence);(out/'reader-interaction3-local-evidence.tar.gz').write_bytes(ev)
result={'status':'BUILT / LOCAL QUALIFICATION ONLY','artifacts':{p.name:{'bytes':p.stat().st_size,'sha256':fh(p)} for p in sorted(out.glob('*.tar.gz'))},'binaries':audit,'dictionary':prov['dictionary'],'archiveReproduction':'ALL THREE GENERATED TWICE BYTE IDENTICAL','binaryReproduction':'BOTH NEW BINARIES CLEAN-REBUILT BYTE IDENTICAL IN SAME BUILD PATH; OWNER REUSED','tabletAccess':'NONE','physicalTest':'NOT RUN'}
(out/'ARTIFACTS.json').write_text(json.dumps(result,indent=2)+'\n');(out/'SHA256SUMS').write_text(''.join(v['sha256']+'  '+n+'\n' for n,v in result['artifacts'].items()));print(json.dumps(result,indent=2))
