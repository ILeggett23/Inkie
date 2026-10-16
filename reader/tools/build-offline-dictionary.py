#!/usr/bin/env python3
"""Offline-only, bounded WordNet 3.0 converter. Never executes archive contents.
Usage: import-wordnet.py OFFICIAL_ARCHIVE OUTPUT_DIRECTORY [--expected-sha256 HASH]
Input archive: https://wordnetcode.princeton.edu/3.0/WordNet-3.0.tar.gz
Format: https://wordnet.princeton.edu/documentation/wndb5wn
License: https://wordnet.princeton.edu/license-and-commercial-use
"""
import argparse, hashlib, json, os, pathlib, struct, tarfile, tempfile
MAGIC=b'IBWN3V1\n'; RECORD=172; HEADER=32
OFFICIAL_SHA256='640db279c949a88f61f851dd54ebbb22d003f8b90b85267042ef85a3781d3a52'
def need(ok,reason):
    if not ok: raise ValueError(reason)
def compact(value): return json.dumps(value,ensure_ascii=True,separators=(',',':')).encode('ascii')
def convert(archive,output,expected=OFFICIAL_SHA256):
    need(archive.is_file() and not archive.is_symlink() and archive.stat().st_size<=16_000_000,'archive-size-or-type')
    digest=hashlib.sha256(archive.read_bytes()).hexdigest();need(digest==expected,'archive-checksum')
    need(not output.exists(),'output-already-exists-preserve-installed-dictionary')
    sources={}; allowed={'WordNet-3.0/LICENSE'}
    for pos in ('noun','verb','adj','adv'):
        allowed.update('WordNet-3.0/dict/'+name+'.'+pos for name in ('data','index'))
        allowed.add('WordNet-3.0/dict/'+pos+'.exc')
    with tarfile.open(archive,'r:gz') as tar:
        expanded=0;count=0
        for member in tar:
            count+=1;expanded+=member.size
            need(count<=1024 and expanded<=100_000_000,'archive-expansion-budget')
            need(not member.issym() and not member.islnk() and '..' not in pathlib.PurePosixPath(member.name).parts and not member.name.startswith('/'),'unsafe-archive-member')
            if member.name not in allowed: continue
            need(member.isfile() and member.size<=25_000_000 and member.name not in sources,'invalid-or-duplicate-data-member')
            raw=tar.extractfile(member).read(member.size+1);need(len(raw)==member.size,'truncated-member');sources[member.name]=raw
    need(set(sources)==allowed,'required-data-or-license-missing')
    records={};exceptions={};maxgloss=0
    for pos,description in (('noun','noun'),('verb','verb'),('adj','adjective'),('adv','adverb')):
        data=sources['WordNet-3.0/dict/data.'+pos];index=sources['WordNet-3.0/dict/index.'+pos]
        for line in index.splitlines():
            if line.startswith(b'  '): continue
            fields=line.decode('ascii').split();need(len(fields)>=6,'index-record')
            lemma=fields[0].replace('_',' ');sense_count=int(fields[2]);pointers=int(fields[3]);offsets=fields[6+pointers:]
            need(0<sense_count<=128 and len(offsets)==sense_count,'index-senses')
            entry=records.setdefault(lemma,{'word':lemma,'headword':lemma,'senses':[]})
            for offset in offsets:
                at=int(offset);need(0<=at<len(data),'synset-offset')
                end=data.find(b'\n',at);need(end>=at and end-at<=16384,'synset-line-limit');synset=data[at:end].decode('ascii')
                need(synset.startswith(offset+' ') and '|' in synset,'synset-identity');gloss=synset.split('|',1)[1].strip();need(gloss and len(gloss)<=8192,'gloss-size');maxgloss=max(maxgloss,len(gloss))
                entry['senses'].append({'pos':description,'definition':gloss})
        for line in sources['WordNet-3.0/dict/'+pos+'.exc'].decode('ascii').splitlines():
            parts=[v.replace('_',' ') for v in line.split()]
            if len(parts)>=2: exceptions.setdefault(parts[0],[]).extend(parts[1:])
    for word,bases in exceptions.items():
        if word in records: continue
        available=[base for base in bases if base in records]
        if available:
            headword=available[0];records[word]={'word':word,'headword':headword,'senses':records[headword]['senses'],'morphology':'WordNet exception list'}
    need(100000<=len(records)<=200000,'word-count');keys=sorted(records);payloads=[]
    for word in keys:
        need(0<len(word.encode('ascii'))<128 and '\x00' not in word,'index-key-limit')
        payload=compact(records[word]);need(0<len(payload)<=32768 and len(records[word]['senses'])<=128,'record-size-limit');payloads.append(payload)
    offset=HEADER+RECORD*len(keys);size=offset+sum(map(len,payloads));need(size<=128*1024*1024,'dictionary-size-limit')
    output.parent.mkdir(parents=True,exist_ok=True)
    stage=pathlib.Path(tempfile.mkdtemp(prefix='wordnet-staging-',dir=output.parent));os.chmod(stage,0o700)
    try:
        with open(stage/'wordnet.ibd','xb') as stream:
            os.chmod(stream.name,0o600);stream.write(struct.pack('<8sIIQQ',MAGIC,len(keys),RECORD,offset,size))
            for key,payload in zip(keys,payloads):
                encoded=key.encode('ascii');stream.write(struct.pack('<128sQI32s',encoded,offset,len(payload),hashlib.sha256(payload).digest()));offset+=len(payload)
            for payload in payloads:stream.write(payload)
            stream.flush();os.fsync(stream.fileno())
        dictionary_digest=hashlib.sha256((stage/'wordnet.ibd').read_bytes()).hexdigest()
        manifest={'version':1,'provider':'princeton-wordnet-3.0','datasetVersion':'3.0','entries':len(keys),'bytes':size,'sha256':dictionary_digest,'sourceSha256':digest,'sourceUrl':'https://wordnetcode.princeton.edu/3.0/WordNet-3.0.tar.gz','license':'LICENSE.wordnet','format':'ibwn3-v1','pronunciation':'not provided by WordNet','maxGlossCharacters':maxgloss}
        for name,content in [('LICENSE.wordnet',sources['WordNet-3.0/LICENSE']),('manifest.json',compact(manifest))]:
            with open(stage/name,'xb') as stream:os.chmod(stream.name,0o600);stream.write(content);stream.flush();os.fsync(stream.fileno())
        directory_fd=os.open(stage,os.O_RDONLY);os.fsync(directory_fd);os.close(directory_fd);os.rename(stage,output);directory_fd=os.open(output.parent,os.O_RDONLY);os.fsync(directory_fd);os.close(directory_fd)
        return manifest
    except BaseException:
        for child in stage.iterdir():child.unlink()
        stage.rmdir();raise
if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('archive',type=pathlib.Path);parser.add_argument('output',type=pathlib.Path);parser.add_argument('--expected-sha256',default=OFFICIAL_SHA256);args=parser.parse_args()
    print(json.dumps(convert(args.archive,args.output,args.expected_sha256),sort_keys=True))
