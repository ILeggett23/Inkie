#!/usr/bin/env python3
"""Convert the pinned public GNU GCIDE corpus to a bounded, positional index."""
from pathlib import Path
import argparse,hashlib,html,json,os,re,struct,tarfile,unicodedata
SOURCE_SHA='ae9b0187a1d1e7ee86218c707503b5082894c0ba61eeee26d226bca169e7fbd0'
def compact(value):return json.dumps(value,ensure_ascii=False,separators=(',',':')).encode('utf8')
def convert(archive,output):
 assert hashlib.sha256(archive.read_bytes()).hexdigest()==SOURCE_SHA
 assert not output.exists();output.mkdir(parents=True,mode=0o700)
 with tarfile.open(archive) as tar:
  files={m.name:tar.extractfile(m).read() for m in tar.getmembers() if m.isfile() and (re.fullmatch(r'gcide-0\.54/CIDE\.[A-Z]',m.name) or m.name in ['gcide-0.54/webfont.txt','gcide-0.54/COPYING'])}
 table=files['gcide-0.54/webfont.txt'].decode('utf8');heading=next(s for s in table.splitlines() if 'Uc  ASC' in s);uc,asc=heading.index('Uc'),heading.index('ASC');escapes={};entities={'br':' ','and':'and','or':'or'}
 for line in table.splitlines():
  match=re.match(r'^[0-7]{3}\s+\d+\s+([0-9a-f]{2})\s',line)
  if not match:continue
  value=line[uc:asc].strip()
  if not value or len(value)>3:continue
  escapes[match[1]]=value
  entity=re.search(r'<([A-Za-z0-9]+)/',line[:uc])
  if entity:entities[entity[1]]=value
 omitted=0
 def text(value):
  nonlocal omitted
  unknown=[]
  value=re.sub(r"\\'([0-9a-f]{2})",lambda m:escapes.get(m[1],''),value)
  def entity(m):
   name=m[1]
   if name in entities:return entities[name]
   accents={'mac':'\u0304','til':'\u0303','ring':'\u030a','cir':'\u0302','cr':'\u0306','acute':'\u0301','grave':'\u0300','dot':'\u0307','sdot':'\u0323','car':'\u030c','um':'\u0308'}
   for suffix,mark in accents.items():
    if name.endswith(suffix) and len(name)==len(suffix)+1:return unicodedata.normalize('NFC',name[0]+mark)
   unknown.append(name);return ''
  value=re.sub(r'<([A-Za-z0-9]+)/',entity,value)
  if unknown:omitted+=1;return ''
  value=re.sub(r'<[^>]*>','',value);return ' '.join(html.unescape(value).split())
 def key(value):
  value=''.join(c for c in unicodedata.normalize('NFKD',value).casefold() if not unicodedata.combining(c));value=value.replace('’',"'").strip(" \t\n.,;:!?()[]\"'");return value if len(value)<128 and re.fullmatch(r"[a-z0-9][a-z0-9 '.-]*",value) else ''
 def part(value):
  value=value.lower()
  for mark,pos in [('adv','adverb'),('prep','preposition'),('conj','conjunction'),('interj','interjection'),('pron','pronoun'),('aux','verb'),('imp','verb'),('v.','verb'),('p. p.','verb'),('n.','noun'),('a.','adjective')]:
   if mark in value:return pos
  return 'other'
 records={}
 for name,data in sorted(files.items()):
  if not re.fullmatch(r'gcide-0\.54/CIDE\.[A-Z]',name):continue
  try:corpus=data.decode('utf8')
  except UnicodeDecodeError:corpus=data.decode('latin1')
  for entry in re.finditer(r'<ent>(.*?)</ent>(.*?)(?=<ent>|\Z)',corpus,re.S):
   head=text(entry[1]);word=key(head)
   if not word:continue
   body=entry[2];positions=list(re.finditer(r'<def>(.*?)</def>',body,re.S));senses=[]
   for index,definition in enumerate(positions):
    gloss=text(definition[1]);previous=list(re.finditer(r'<pos>(.*?)</pos>',body[:definition.start()],re.S));pos=part(text(previous[-1][1])) if previous else 'other'
    following=body[definition.end():positions[index+1].start() if index+1<len(positions) else len(body)];mark=re.match(r'\s*<mark>(.*?)</mark>',following,re.S)
    if mark:gloss=text(mark[1])+' '+gloss
    if not gloss.strip() or len(gloss.encode('utf8'))>2048:continue
    sense={'pos':pos,'definition':gloss}
    if sense not in senses:senses.append(sense)
   if not senses:continue
   record=records.setdefault(word,{'word':word,'headword':head[:127],'source':'gnu-gcide-0.54','senses':[]})
   for sense in senses:
    if sense in record['senses']:continue
    record['senses'].append(sense)
    if len(record['senses'])>128 or len(compact(record))>32768:record['senses'].pop();break
 assert 50000<len(records)<=200000
 keys=sorted(records);payloads=[compact(records[word]) for word in keys];offset=32+172*len(keys);size=offset+sum(map(len,payloads));assert size<=128*1024**2
 target=output/'gcide.ibd'
 with target.open('xb') as stream:
  os.chmod(target,0o600);stream.write(struct.pack('<8sIIQQ',b'IBWN3V1\n',len(keys),172,offset,size))
  for word,payload in zip(keys,payloads):stream.write(struct.pack('<128sQI32s',word.encode('ascii'),offset,len(payload),hashlib.sha256(payload).digest()));offset+=len(payload)
  for payload in payloads:stream.write(payload)
  stream.flush();os.fsync(stream.fileno())
 (output/'LICENSE.gcide').write_bytes(files['gcide-0.54/COPYING'])
 manifest={'version':1,'provider':'gnu-gcide-0.54','datasetVersion':'0.54','sourceUrl':'https://ftp.gnu.org/gnu/gcide/gcide-0.54.tar.gz','sourceSha256':SOURCE_SHA,'sha256':hashlib.sha256(target.read_bytes()).hexdigest(),'bytes':size,'entries':len(keys),'format':'ibwn3-v1 bounded positional index','license':'GPL-3.0-or-later; LICENSE.gcide','pronunciation':'not exported','unknownSymbolDefinitionsOmitted':omitted,'origin':'GNU GCIDE, Webster1913 with contributed/WordNet additions; historical/obsolete labels preserved','converterSha256':hashlib.sha256(Path(__file__).read_bytes()).hexdigest()};(output/'gcide-manifest.json').write_bytes(compact(manifest));return manifest
if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('archive',type=Path);parser.add_argument('output',type=Path);args=parser.parse_args();print(json.dumps(convert(args.archive,args.output),sort_keys=True))
