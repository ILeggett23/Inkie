from pathlib import Path
import json,re,math,hashlib
r=Path(__file__).resolve().parents[1];log=(r/'evidence/MATCHED-PERFORMANCE.log').read_text();rows={}
for line in log.splitlines():
 if line.startswith(('UX_PERF ','UX_BASELINE_P95 ','INTERACTION_PERF ')):
  row=dict(re.findall(r'(\w+)=([^ ]+)',line));name=row['mode']
  for k,v in list(row.items()):
   if v.isdigit():row[k]=int(v)
  rows.setdefault(name,{}).update(row)
base=rows['B_hidden']['processing_p99_ns']
for mode in ['B_text_ready','C_highlight','D_definition_idle','E_semantic_store_active','F_semantic_prefetch']:
 row=rows[mode]
 for field,value in [('accepted',404),('contacts',4),('submits',404),('live_cpp_allocations',0),('text_calls',0),('dictionary_calls',0),('semantic_storage_calls',0),('selection_layout_calls',0),('marker_waits',0),('ui_waits',0),('dropped_accepted',0)]:assert row[field]==value,(mode,field)
 row['p99_delta_ns']=row['processing_p99_ns']-base;row['p99_ratio']=row['processing_p99_ns']/base
f=rows['F_semantic_prefetch'];overlaps=[]
for line in log.splitlines():
 if line.startswith('TEXT_EXTRACT page=1 '):
  d=dict(re.findall(r'(\w+)=(\d+)',line));a,b=int(d['start_ns']),int(d['end_ns'])
  if a<f['end_ns'] and b>f['start_ns']:overlaps.append({'start':a,'end':b,'overlapNs':min(b,f['end_ns'])-max(a,f['start_ns'])})
assert overlaps,'F did not overlap actual text extraction'
assert rows['E_semantic_store_active']['store_active_samples']>0
envelope=json.loads((r/'evidence/fixture-text-geometry.json').read_text());g=envelope['payload'];m=g['transform']
def rounding(x):return math.floor(x+0.5) if x>=0 else math.ceil(x-0.5)
areas={}
for a,b in g['words']:
 chars=g['chars'][a:b];word=''.join(c['text'] for c in chars)
 if word not in ['READER','DEVICE']:continue
 points=[(rounding(m[0]*q[i]+m[2]*q[i+1]+m[4]),rounding(m[1]*q[i]+m[3]*q[i+1]+m[5])) for c in chars for q in [c['quad']] for i in range(0,8,2)]
 x0=max(0,min(x for x,y in points)-3);x1=min(1620,max(x for x,y in points)+4);y0=max(0,min(y for x,y in points)-3);y1=min(2160,max(y for x,y in points)+7)
 areas[word]={'rect':[x0,y0,x1,y1],'pixels':(x1-x0)*(y1-y0)}
w=areas['READER']['pixels'];menu=960*120;panel=1080*460
cost={'wordBounds':areas,'menuPixels':menu,'panelPixels':panel,'selectReader':{'submits':2,'pixels':w+menu},'replaceMenuWithDefiningPanel':{'submits':3,'pixels':w+menu+panel},'definitionCompletionMaximum':{'submits':2,'pixels':w+panel},'dismissDefinition':{'submits':2,'pixels':w+panel},'highlightReaderAndDismissMenu':{'submits':2,'pixels':w+menu},'highlightDurabilityAck':{'submits':0,'pixels':0},'coalescing':'Definition completion may merge into the preceding dirty set if it arrives before presentation. No full-page semantic submit.'}
result={'baseline':'A=exact accepted UX2 B_hidden source trace','rows':rows,'actualTextExtractionOverlap':overlaps,'maxSingleProcessRssKiB':list(map(int,re.findall(r'Maximum resident set size \(kbytes\): (\d+)',log))),'cost':cost,'scope':'Fake display/input; actual local MuPDF text extraction and semantic IO. E queues eight real production-worker jobs in an isolated stress store. F waits for adjacent pixel preparation,then overlaps real text extraction. No optical claim.'}
(r/'evidence/SEMANTIC-SUMMARY.json').write_text(json.dumps(result,indent=2)+'\n')
assert (r/'evidence/final-unselected.pgm').read_bytes()==(r/'evidence/final-dismissed-definition.pgm').read_bytes()
print(json.dumps(result,indent=2))
