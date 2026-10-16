from pathlib import Path
import hashlib,json
r=Path(__file__).resolve().parents[1];b=r.with_name('reader-ux2');sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
assert sha(b/'outputs/reader-ux2-candidate.tar.gz')=='fb2edd226ff05d28622fa434e6fcb92ec79af3a73d73aba08b55a275fde91dae'
data={'basis':'User-accepted forward-development baseline; formal Opening A remains incomplete due deadline','files':{p.relative_to(b).as_posix():sha(p) for folder in ['src','recovery','reused','artifact','outputs'] for p in sorted((b/folder).rglob('*')) if p.is_file()},'physicalReport':sha(b/'PHYSICAL-QUALIFICATION-RESULT.md'),'dictionaryInterface':sha(r/'reused/owned/DictionaryProvider.h')}
p=r/'docs/BASELINE.json'
if p.exists():assert json.loads(p.read_text())==data
else:p.write_text(json.dumps(data,indent=2)+'\n')
print('UX2_FORWARD_BASELINE_PRESERVED',len(data['files']),'files; historical report unchanged')
