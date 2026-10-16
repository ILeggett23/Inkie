"""Reuse the established SDK closure audit; inspect, never execute runtime targets."""
from pathlib import Path
import json,subprocess,re,hashlib
r=Path(__file__).resolve().parents[1];e=r/'evidence';sysroot=Path('/home/zayway.guest/inkbound-sdk-5.7.119/sysroots/cortexa53-crypto-remarkable-linux')
def symbols(p):
 rows=subprocess.check_output(['readelf','--wide','--dyn-syms',str(p)],text=True)
 return [(f[7].split('@')[0],f[6],f[4]) for line in rows.splitlines() if len(f:=line.split())>=8 and f[0].rstrip(':').isdigit()]
result={}
for name in ['inkbound-direct-handwriting-spike3','reader-experience1-prepare','inkbound-recovery-owner']:
 p=r/'artifact'/name
 subprocess.run(['python3',str(r/'tools/audit-closure.py'),'--binary',str(p),'--sysroot',str(sysroot),'--qml-scan',str(e/'empty-qml.json'),'--output',str(e/(name+'-closure.json'))],check=True)
 closure=json.loads((e/(name+'-closure.json')).read_text())
 elf=subprocess.check_output(['readelf','--wide','-h','-l','-d','-n','-V',str(p)],text=True);(e/(name+'-elf.txt')).write_text(elf)
 assert 'AArch64' in elf and 'GNU_RELRO' in elf and 'BIND_NOW' in elf
 assert not re.search(r'\((RPATH|RUNPATH)\)',elf)
 stack=next(line for line in elf.splitlines() if line.strip().startswith('GNU_STACK'));assert 'E' not in stack.split()[-2]
 exported=set()
 for f in closure['sdkFiles']:exported|={n for n,idx,b in symbols(sysroot/f['sdkTargetPath'].lstrip('/')) if idx!='UND'}
 missing={n for n,idx,b in symbols(p) if idx=='UND' and b!='WEAK'}-exported;assert not missing,missing
 data=p.read_bytes();assert b'injected-write-error' not in data and b'UNDERLAY_TEST_FAIL' not in data
 if name=='reader-experience1-prepare':
  assert b'libqsgepaper.so' not in data and b'/dev/input' not in data
  assert b'TEXT_EXTRACT' in data
 if name=='inkbound-direct-handwriting-spike3':
  all_symbols=subprocess.check_output(['nm',str(p)],text=True)
  assert not re.search(r'\b(fz_new_context_imp|pdf_open_document|fz_run_page)\b',all_symbols), 'MuPDF leaked into live executable'
 if name=='inkbound-recovery-owner':assert hashlib.sha256(data).hexdigest()=='38830b88f2988f291b388d989117e6584c37372cbbf3d1212e48f2f58dbecf38'
 result[name]=dict(bytes=len(data),sha256=hashlib.sha256(data).hexdigest(),buildId=re.search(r'Build ID: (\w+)',elf)[1],needed=closure['binary']['needed'],strongImportsResolved=True,symbolVersionsSatisfied=True,relro='FULL',stack='RW',rpath=[])
(e/'artifact-audit.json').write_text(json.dumps(result,indent=2)+'\n');print('ARTIFACT_AUDIT_PASS',json.dumps(result))
