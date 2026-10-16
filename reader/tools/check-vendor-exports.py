#!/usr/bin/env python3
"""Inspect the linked executable, never execute a vendor module."""
import json,struct,sys
from pathlib import Path
p=Path(sys.argv[1]);b=p.read_bytes();assert b[:6]==b'\x7fELF\x02\x01'
h=struct.unpack_from('<HHIQQQIHHHHHH',b,16);assert h[1]==183
ss=[struct.unpack_from('<IIQQQQIIQQ',b,h[5]+i*h[10]) for i in range(h[11])];names=[]
for s in ss:
 if s[1]!=11:continue
 q=ss[s[6]];strings=b[q[4]:q[4]+q[5]]
 for off in range(s[4],s[4]+s[5],s[9]):
  n,info,other,section,_,_=struct.unpack_from('<IBBHQQ',b,off)
  if section and n and info>>4 in (1,2,10) and other&3 in (0,3):names.append(strings[n:strings.index(b'\0',n)].decode())
required={'_ZN6QImageC1EPhiixNS_6FormatEPFvPvES2_','_ZN6QImageC2EPhiixNS_6FormatEPFvPvES2_'}
assert required<=set(names),'Missing required constructor capture hooks'
# GNU ld may retain standard string ABI definitions needed by linked Qt DSOs.
# Reader, codec, atomic-wait and other implementation internals must stay local.
abi=('_ZNSt7__cxx1112basic_string','_ZNKSt7__cxx1112basic_string')
reserved={'_IO_stdin_used','__bss_start','_edata','_end'}
unexpected=[s for s in names if s not in required|reserved and not s.startswith(abi)]
assert not unexpected,'Unexpected dynamic definitions: '+str(unexpected)
print('VENDOR_EXPORT_POLICY_PASS '+json.dumps({'binary':str(p),'defined_dynamic_symbols':len(names),'constructor_hooks':len(required),'standard_abi_support':[s for s in names if s not in required]},sort_keys=True))
