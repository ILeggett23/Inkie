"""Query the public OSV API for pinned PyPI dependency advisories. No secrets."""
import json
from pathlib import Path
import sys
import urllib.request

root=Path(__file__).resolve().parent.parent
packages=[]
for line in (root/'requirements.txt').read_text().splitlines():
    if line and not line.startswith('#'):
        name,version=line.split('==');packages.append({'package':{'name':name,'ecosystem':'PyPI'},'version':version})
request=urllib.request.Request('https://api.osv.dev/v1/querybatch',json.dumps({'queries':packages}).encode(),{'Content-Type':'application/json'},method='POST')
try:
    with urllib.request.urlopen(request,timeout=20) as response:result=json.load(response)
except Exception:
    print('Dependency advisory check unavailable; not a clean audit.');raise SystemExit(2)
report={'packages':packages,'results':result['results']}
print(json.dumps(report,indent=2))
sys.exit(1 if any(r.get('vulns') for r in result['results']) else 0)
