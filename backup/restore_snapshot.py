"""Restore an Inkie content snapshot into a NEW empty directory.

First decrypt vm-manifest.jsonl.gz.age with age and gzip.
Use --repo/--release/--identity to stream content packs directly from GitHub,
or --packs for an existing directory of decrypted .tar.gz content packs.
This utility never writes outside --destination and never follows symlinks.
Run in a recreated Linux VM to retain Linux UID/GID ownership.
"""
import argparse, hashlib, json, os, subprocess, tarfile, re
from pathlib import Path, PurePosixPath

def safe_name(name):
    p = PurePosixPath(name)
    if p.is_absolute() or '..' in p.parts or not p.parts or p.parts[0] not in {'home','tmp'}:
        raise ValueError('Unsafe manifest path: ' + name)
    return p

def restore(manifest, packs, destination, only_prefix=None, repo=None, release=None, identity=None):
    entries = []; required = {}; chunks = set(); summary = None; chunk_order = []
    with Path(manifest).open() as source:
        for line in source:
            record = json.loads(line)
            if record['type'] == 'entry':
                safe_name(record['path'])
                if only_prefix is None or record['path'] == only_prefix or record['path'].startswith(only_prefix.rstrip('/')+'/'):
                    entries.append(record)
                    for digest, size in record.get('chunks', []):
                        if digest in required: assert required[digest] == size
                        required[digest] = size
            elif record['type'] == 'chunk': chunk_order.append(record['sha256'])
            elif record['type'] == 'summary': summary = record
    assert summary and summary['complete'] is True
    assert entries, 'Requested source prefix was not found'
    destination = Path(destination).resolve()
    destination.mkdir(mode=0o700, parents=True, exist_ok=False)
    hardlinks = {}; regular_count=0; writes={}; pending=[]
    def target(name):
        rel=safe_name(name); result=destination.joinpath(*rel.parts)
        parent=destination
        for component in rel.parts[:-1]:
            parent=parent/component
            if parent.exists() or parent.is_symlink(): assert parent.is_dir() and not parent.is_symlink()
            else: parent.mkdir(mode=0o700)
        assert not result.exists() and not result.is_symlink(), 'Duplicate output path'
        return result
    for entry in sorted((e for e in entries if e['kind']=='dir'),key=lambda e:len(PurePosixPath(e['path']).parts)):
        p=destination.joinpath(*safe_name(entry['path']).parts)
        p.mkdir(mode=0o700,parents=True,exist_ok=True); assert not p.is_symlink()
    for entry in (e for e in entries if e['kind']!='dir'):
        p=target(entry['path']); kind=entry['kind']
        if kind=='file':
            group=(entry['dev'],entry['ino'])
            if entry['nlink']>1 and group in hardlinks: os.link(hardlinks[group],p)
            else:
                with p.open('xb') as out: out.truncate(entry['size'])
                offset=0
                for digest,size in entry['chunks']:
                    writes.setdefault(digest,[]).append((p,offset,size));offset+=size
                assert offset==entry['size']
                if entry['nlink']>1: hardlinks[group]=p
            regular_count+=1
        elif kind in {'symlink','fifo'}: pending.append((entry,p))
        else: raise RuntimeError('Device nodes require a separate reviewed restore: '+entry['path'])
    def consume(stream):
        with tarfile.open(fileobj=stream,mode='r|gz') as archive:
            for member in archive:
                assert member.isfile() and member.name.startswith('chunks/') and 0<member.size<=4*1024*1024
                digest=member.name.removeprefix('chunks/')
                assert len(digest)==64 and all(c in '0123456789abcdef' for c in digest)
                data=archive.extractfile(member).read();assert len(data)==member.size and hashlib.sha256(data).hexdigest()==digest
                if digest not in required: continue
                assert digest not in chunks and len(data)==required[digest]
                if any(data):
                    for p,offset,size in writes[digest]:
                        fd=os.open(p,os.O_RDWR|os.O_NOFOLLOW)
                        with os.fdopen(fd,'r+b',buffering=0) as out:out.seek(offset);out.write(data)
                chunks.add(digest)
    if repo:
        assert re.fullmatch(r'[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+',repo) and release and identity
        raw=subprocess.run(['gh','release','download',release,'--repo',repo,'--pattern','snapshot-index.json','--output','-'],capture_output=True,check=True).stdout
        index=json.loads(raw);assert index['complete'] and index['manifest_sha256']==hashlib.sha256(Path(manifest).read_bytes()).hexdigest()
        for pack in index['packs']:
            if not any(h in required for h in chunk_order[pack['first_chunk']:pack['last_chunk']+1]):continue
            download=subprocess.Popen(['gh','release','download',release,'--repo',repo,'--pattern',pack['name'],'--output','-'],stdout=subprocess.PIPE)
            decrypt=subprocess.Popen(['age','--decrypt','-i',identity],stdin=download.stdout,stdout=subprocess.PIPE);download.stdout.close()
            try:consume(decrypt.stdout)
            finally:decrypt.stdout.close()
            assert decrypt.wait()==0 and download.wait()==0
    else:
        assert packs
        for pack in sorted(Path(packs).glob('vm-*.tar.gz')):
            with pack.open('rb') as stream:consume(stream)
    assert set(required)==chunks, 'Missing snapshot chunks'
    for entry in (e for e in entries if e['kind']=='file'):
        p=destination.joinpath(*safe_name(entry['path']).parts);h=hashlib.sha256()
        with p.open('rb') as source:
            for block in iter(lambda:source.read(1024*1024),b''):h.update(block)
        assert p.stat().st_size==entry['size'] and h.hexdigest()==entry['sha256'],entry['path']
    for entry,p in pending:
        if entry['kind']=='symlink':os.symlink(entry['target'],p)
        else:os.mkfifo(p,entry['mode'])
    for entry in (e for e in entries if e['kind']!='dir'):
        p=destination.joinpath(*safe_name(entry['path']).parts);kind=entry['kind']
        if kind!='symlink':
            os.chmod(p,entry['mode'])
            if os.geteuid()==0: os.chown(p,entry['uid'],entry['gid'])
            os.utime(p,ns=(entry['mtime_ns'],entry['mtime_ns']))
    for entry in sorted((e for e in entries if e['kind']=='dir'),key=lambda e:len(PurePosixPath(e['path']).parts),reverse=True):
        p=destination.joinpath(*safe_name(entry['path']).parts);os.chmod(p,entry['mode'])
        if os.geteuid()==0: os.chown(p,entry['uid'],entry['gid'])
        os.utime(p,ns=(entry['mtime_ns'],entry['mtime_ns']))
    return {'restored_regular_files':regular_count,'destination':str(destination),'full_snapshot':only_prefix is None}

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--manifest',required=True);p.add_argument('--packs');p.add_argument('--repo');p.add_argument('--release');p.add_argument('--identity');p.add_argument('--destination',required=True);p.add_argument('--only-prefix')
    a=p.parse_args();print(json.dumps(restore(a.manifest,a.packs,a.destination,a.only_prefix,a.repo,a.release,a.identity),indent=2))
