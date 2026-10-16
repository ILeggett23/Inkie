"""Upload/verify encrypted project packs. NEVER stops or deletes the VM."""
from pathlib import Path, PurePosixPath
import hashlib, json, os, subprocess, tarfile, threading, gzip, shutil, time, io, stat

R=Path(__file__).resolve().parent
GH=R/'tools/gh'; AGE=R/'tools/age'
LIMA=Path('/Users/zayway/Documents/Codex/2026-09-04/files-pasted-by-the-user-you/work/tools/lima-2.2.0/bin/limactl')
ENV=dict(os.environ,GH_CONFIG_DIR='/private/tmp/inkie-gh-config-20261015',LIMA_HOME='/Users/zayway/Documents/Codex/inkbound-lima',GH_PROMPT_DISABLED='1')
REPO='ILeggett23/Inkie'; TAG='inkbound-builder-backup-2026-10-15'
PUBLIC='/Users/zayway/.ssh/inkbound_tablet_ed25519.pub'
IDENTITY='/Users/zayway/.ssh/inkbound_tablet_ed25519'
LIMIT=512*1024*1024
R.joinpath('staging').mkdir(mode=0o700,exist_ok=True)
R.joinpath('recovery/chunks').mkdir(mode=0o700,parents=True,exist_ok=True)

def sha_file(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as src:
        for b in iter(lambda:src.read(1024*1024),b''):h.update(b)
    return h.hexdigest()

def save_json(p,v):
    temp=Path(str(p)+'.new')
    with temp.open('w') as out:json.dump(v,out,indent=2);out.write('\n');out.flush();os.fsync(out.fileno())
    temp.chmod(0o600);temp.replace(p)

def gh(args,check=True):
    p=subprocess.run([str(GH),*args],env=ENV,capture_output=True,timeout=1800)
    if check and p.returncode:
        (R/'github-command-failure.log').write_bytes(p.stderr)
        raise RuntimeError('GitHub command failed: '+args[0]+'; retained diagnostic log')
    return p

def api(path):return json.loads(gh(['api',path]).stdout)
assert api('user')['login']=='ILeggett23'
metadata=api('repos/'+REPO);assert metadata['private'] and metadata['owner']['login']=='ILeggett23'

critical_names=set()
source_manifest=R.parent/'release21b/SOURCE-release-r11.sha256'
for line in source_manifest.read_text().splitlines():critical_names.add('home/release21b-gate-r11/'+line.split('  ',1)[1])
app_manifest=R.parent/'release21b/install-r11/candidate-MANIFEST.sha256'
for line in app_manifest.read_text().splitlines():critical_names.add('home/release21b-gate-r10/app-device/'+line.split('  ',1)[1])
critical_names.add('home/release21b-gate-r10/app-device/MANIFEST.sha256')
chunks=[];critical=[];required=set();header=None;summary=None
manifest=R/'vm-manifest.jsonl'; manifest_sha=sha_file(manifest)
with manifest.open() as source:
    for line in source:
        e=json.loads(line)
        if e['type']=='header':header=e
        elif e['type']=='chunk':chunks.append(e)
        elif e['type']=='entry' and e['path'] in critical_names:
            assert e['kind']=='file';critical.append(e);required.update(digest for digest,size in e['chunks'])
        elif e['type']=='summary':summary=e
assert header and summary and summary['complete'] and len(critical)==len(critical_names)==875
assert len(chunks)==summary['unique_chunks'] and len({c['sha256'] for c in chunks})==len(chunks)
groups=[];group=[];used=0;first=0
for ordinal,c in enumerate(chunks):
    footprint=512+((c['size']+511)//512)*512
    if group and used+footprint>LIMIT:
        groups.append((first,ordinal-1,group));group=[];used=0;first=ordinal
    name=c['path'];root=max((key for key in header['roots'] if name==key or name.startswith(key+'/')),key=len)
    p=Path(header['roots'][root]).joinpath(*PurePosixPath(name).parts[len(PurePosixPath(root).parts):])
    group.append({**c,'source':str(p)});used+=footprint
if group:groups.append((first,len(chunks)-1,group))
assert len(groups)<990
state_path=R/'backup-state.json'
if state_path.exists():state=json.loads(state_path.read_text());assert state['manifest_sha256']==manifest_sha
else:
    state={'repo':REPO,'tag':TAG,'manifest_sha256':manifest_sha,'summary':summary,'pack_count':len(groups),'parts':{},'complete':False,'vm_deleted':False}
    save_json(state_path,state)
def find_release():
    pages=json.loads(gh(['api','repos/'+REPO+'/releases?per_page=100','--paginate','--slurp']).stdout)
    found=[v for page in pages for v in page if v['tag_name']==TAG]
    assert len(found)<=1, 'Ambiguous backup drafts; no original data changed'
    return found[0] if found else None
release=find_release()
if release is None:
    notes=R/'release-notes-in-progress.md'
    notes.write_text('Encrypted backup of the dedicated Inkbound builder. This draft is incomplete until snapshot-index.json reports complete=true. The original VM remains intact during upload and verification. Current source is on main. SSH identities are excluded.\n')
    gh(['release','create',TAG,'--repo',REPO,'--target','main','--draft','--title','Inkbound builder backup — 2026-10-15','--notes-file',str(notes)])
    release=find_release()
assert release and release['draft'] is True
release_id=release['id'];state['release_id']=release_id;save_json(state_path,state)

def assets():
    pages=json.loads(gh(['api','repos/'+REPO+'/releases/'+str(release_id)+'/assets?per_page=100','--paginate','--slurp']).stdout)
    result={}
    for page in pages:
        for a in page:assert a['name'] not in result;result[a['name']]=a
    return result

def verify_cipher(path,spec,remote_name=None,save_required=False):
    expected={c['sha256']:c['size'] for c in spec};seen=set();cipher_hash=hashlib.sha256();count=[0];errors=[]
    download=None;source=None
    if remote_name:
        download=subprocess.Popen([str(GH),'release','download',TAG,'--repo',REPO,'--pattern',remote_name,'--output','-'],env=ENV,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        source=download.stdout
    else:source=Path(path).open('rb')
    log=(R/'staging/age-verify.log').open('wb')
    decrypt=subprocess.Popen([str(AGE),'--decrypt','-i',IDENTITY],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log)
    def feed():
        try:
            while True:
                b=source.read(1024*1024)
                if not b:break
                cipher_hash.update(b);count[0]+=len(b);decrypt.stdin.write(b)
        except BaseException as e:errors.append(repr(e))
        finally:
            try:decrypt.stdin.close()
            except OSError:pass
            source.close()
    thread=threading.Thread(target=feed);thread.start()
    try:
        with tarfile.open(fileobj=decrypt.stdout,mode='r|gz') as archive:
            for m in archive:
                assert m.isfile() and m.name.startswith('chunks/')
                digest=m.name.removeprefix('chunks/');assert digest in expected and digest not in seen and m.size==expected[digest]
                data=archive.extractfile(m).read();assert len(data)==m.size and hashlib.sha256(data).hexdigest()==digest;seen.add(digest)
                if save_required and digest in required:
                    target=R/'recovery/chunks'/digest
                    if target.exists():assert sha_file(target)==digest
                    else:
                        with target.open('xb') as out:out.write(data)
        for b in iter(lambda:decrypt.stdout.read(1024*1024),b''):pass
        thread.join(30);assert not thread.is_alive() and not errors
        assert decrypt.wait(timeout=30)==0 and seen==set(expected)
        if download:assert download.wait(timeout=30)==0,download.stderr.read().decode(errors='replace')
    except BaseException:
        decrypt.terminate()
        if download:download.terminate()
        raise
    finally:
        decrypt.stdout.close();log.close()
    return cipher_hash.hexdigest(),count[0]

def upload_checked(path,name,digest,size):
    present=assets().get(name)
    if not present:
        gh(['release','upload',TAG,str(path),'--repo',REPO])
        present=assets().get(name)
    assert present and present['state']=='uploaded' and present['size']==size
    assert present.get('digest')=='sha256:'+digest, 'GitHub server SHA256 mismatch: '+name
    return {'asset_id':present['id'],'github_digest':present['digest'],'size':size,'name':name}

for number,(first,last,spec) in enumerate(groups,1):
    name=f'vm-{number:04d}.tar.gz.age';path=R/'staging'/name;key=str(number);record=state['parts'].get(key)
    need_remote=bool(required.intersection(c['sha256'] for c in spec))
    if record and record.get('verified'):
        a=assets().get(name);assert a and a['id']==record['asset_id'] and a['digest']=='sha256:'+record['sha256'] and a['size']==record['size']
        if need_remote and any(not (R/'recovery/chunks'/c['sha256']).exists() for c in spec if c['sha256'] in required):
            h,n=verify_cipher(None,spec,name,True);assert h==record['sha256'] and n==record['size']
        print('ALREADY_VERIFIED',number,len(groups),flush=True);continue
    started=time.monotonic()
    if record and path.exists():assert sha_file(path)==record['sha256']
    else:
        assert not path.exists()
        log=(R/'staging'/f'pack-{number:04d}.log').open('wb')
        command=[str(LIMA),'shell','inkbound-builder','--','sudo','-n','python3','-c',(R/'vm_pack.py').read_text()]
        pack=subprocess.Popen(command,env=ENV,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=log)
        encrypt=subprocess.Popen([str(AGE),'-R',PUBLIC,'-o',str(path)],stdin=pack.stdout,stderr=log);pack.stdout.close()
        pack.stdin.write(json.dumps(spec).encode());pack.stdin.close()
        assert pack.wait(timeout=1800)==0 and encrypt.wait(timeout=1800)==0;log.close()
        digest,size=verify_cipher(path,spec)
        record={'name':name,'first_chunk':first,'last_chunk':last,'chunk_count':len(spec),'sha256':digest,'size':size,'local_recovery_verified':True,'verified':False}
        state['parts'][key]=record;save_json(state_path,state)
    record.update(upload_checked(path,name,record['sha256'],record['size']))
    if need_remote:
        h,n=verify_cipher(None,spec,name,True);assert h==record['sha256'] and n==record['size'];record['remote_recovery_readback']=True
    record['verified']=True;save_json(state_path,state)
    path.unlink()  # New temporary upload copy only; original VM files remain untouched.
    print('PACK_VERIFIED',number,len(groups),'encrypted_mib',round(record['size']/1024**2,1),'seconds',round(time.monotonic()-started),flush=True)

assert len(state['parts'])==len(groups) and all(p['verified'] for p in state['parts'].values())
recovered=R/'recovery/restored';recovered.mkdir(mode=0o700,exist_ok=True)
for e in critical:
    path=recovered.joinpath(*PurePosixPath(e['path']).parts);path.parent.mkdir(mode=0o700,parents=True,exist_ok=True)
    if not path.exists():
        with path.open('xb') as out:
            for digest,size in e['chunks']:
                blob=R/'recovery/chunks'/digest;assert sha_file(blob)==digest and blob.stat().st_size==size
                with blob.open('rb') as src:shutil.copyfileobj(src,out)
    assert path.stat().st_size==e['size'] and sha_file(path)==e['sha256'];path.chmod(e['mode'])
state['critical_remote_restore']={'files':len(critical),'reader_source_files':845,'app_payload_and_manifest_files':30,'all_file_sha256_match':True}
save_json(state_path,state)
print('CRITICAL_REMOTE_RESTORE_PASS',len(critical),flush=True)
state['content_packs_complete']=True;save_json(state_path,state)
print('CONTENT_PACKS_COMPLETE_NO_VM_DELETION',flush=True)
