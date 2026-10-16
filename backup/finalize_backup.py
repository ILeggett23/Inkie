"""Seal the verified cloud backup. Does not stop/delete the VM."""
from pathlib import Path, PurePosixPath
import hashlib, json, os, subprocess, gzip, tarfile, importlib.util, shutil, io
R=Path(__file__).resolve().parent;GH=R/'tools/gh';AGE=R/'tools/age'
ENV=dict(os.environ,GH_CONFIG_DIR='/private/tmp/inkie-gh-config-20261015',LIMA_HOME='/Users/zayway/Documents/Codex/inkbound-lima')
LIMA='/Users/zayway/Documents/Codex/2026-09-04/files-pasted-by-the-user-you/work/tools/lima-2.2.0/bin/limactl'
REPO='ILeggett23/Inkie';TAG='inkbound-builder-backup-2026-10-15';IDENTITY='/Users/zayway/.ssh/inkbound_tablet_ed25519';PUBLIC=IDENTITY+'.pub'
def sha(p):
    h=hashlib.sha256()
    with Path(p).open('rb') as source:
        for b in iter(lambda:source.read(1024*1024),b''):h.update(b)
    return h.hexdigest()
def gh(args):
    p=subprocess.run([str(GH),*args],env=ENV,capture_output=True,timeout=1800)
    if p.returncode:(R/'finalize-github-error.log').write_bytes(p.stderr);raise RuntimeError('GitHub finalization failed; original VM retained')
    return p.stdout
def api(p):return json.loads(gh(['api',p]))
state=json.loads((R/'backup-state.json').read_text());assert state.get('content_packs_complete') and state['critical_remote_restore']['files']==875
assert state['critical_remote_restore']['all_file_sha256_match'] and all(v['verified'] for v in state['parts'].values())
assert api('repos/'+REPO)['private'] is True
release_id=state['release_id']
def assets():return {a['name']:a for page in json.loads(gh(['api','repos/'+REPO+'/releases/'+str(release_id)+'/assets?per_page=100','--paginate','--slurp'])) for a in page}
present=assets()
for p in state['parts'].values():
    a=present[p['name']];assert a['id']==p['asset_id'] and a['digest']=='sha256:'+p['sha256'] and a['size']==p['size'] and a['state']=='uploaded'
manifest=R/'vm-manifest.jsonl';assert sha(manifest)==state['manifest_sha256']
with manifest.open('rb') as source:
    p=subprocess.run([LIMA,'shell','inkbound-builder','--','sudo','-n','python3','-c',(R/'vm_verify_metadata.py').read_text()],stdin=source,capture_output=True,env=ENV,timeout=300)
(R/'source-final-consistency.log').write_bytes(p.stdout+p.stderr);assert p.returncode==0,p.stderr.decode(errors='replace')
consistency=json.loads(p.stdout);assert consistency['unchanged'] and consistency['regular_files']==382673
print('ALL_ORIGINAL_PROJECT_FILES_UNCHANGED',flush=True)

name='vm-manifest.jsonl.gz.age';encrypted=R/'staging'/name
if not encrypted.exists():
    with (R/'staging/manifest-encrypt.log').open('wb') as log:
        age=subprocess.Popen([str(AGE),'-R',PUBLIC,'-o',str(encrypted)],stdin=subprocess.PIPE,stderr=log)
        with gzip.GzipFile(fileobj=age.stdin,mode='wb',compresslevel=3,mtime=0) as compressed,manifest.open('rb') as source:shutil.copyfileobj(source,compressed)
        age.stdin.close();assert age.wait(timeout=120)==0
digest=sha(encrypted);size=encrypted.stat().st_size
existing=assets().get(name)
if existing is None:gh(['release','upload',TAG,str(encrypted),'--repo',REPO]);existing=assets()[name]
assert existing['digest']=='sha256:'+digest and existing['size']==size
download=subprocess.Popen([str(GH),'release','download',TAG,'--repo',REPO,'--pattern',name,'--output','-'],env=ENV,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
with (R/'staging/manifest-decrypt.log').open('wb') as log:
    decrypt=subprocess.Popen([str(AGE),'--decrypt','-i',IDENTITY],stdin=download.stdout,stdout=subprocess.PIPE,stderr=log);download.stdout.close()
    with gzip.GzipFile(fileobj=decrypt.stdout,mode='rb') as source,(R/'recovery/remote-vm-manifest.jsonl').open('wb') as output:shutil.copyfileobj(source,output)
    decrypt.stdout.close();assert decrypt.wait(timeout=120)==0 and download.wait(timeout=120)==0
assert sha(R/'recovery/remote-vm-manifest.jsonl')==state['manifest_sha256']
manifest_asset={'name':name,'sha256':digest,'size':size,'asset_id':existing['id'],'remote_decrypt_sha256_match':True}
print('ENCRYPTED_MANIFEST_REMOTE_RECOVERY_PASS',flush=True)

# Exercise the published restore utility on actual remotely recovered source bytes.
prefix='home/release21b-gate-r11/src';needed=set();source_count=0
with manifest.open() as source:
    for line in source:
        e=json.loads(line)
        if e['type']=='entry' and e['kind']=='file' and e['path'].startswith(prefix+'/'):
            source_count+=1;needed.update(d for d,n in e['chunks'])
packs=R/'recovery/restore-tool-test-packs';packs.mkdir(exist_ok=True)
testpack=packs/'vm-0000.tar.gz'
if not testpack.exists():
    with tarfile.open(testpack,'w:gz') as archive:
        for digest in sorted(needed):
            p=R/'recovery/chunks'/digest;assert sha(p)==digest
            item=tarfile.TarInfo('chunks/'+digest);item.size=p.stat().st_size;item.mode=0o600
            with p.open('rb') as source:archive.addfile(item,source)
spec=importlib.util.spec_from_file_location('inkie_restore',R/'restore_snapshot.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
result=module.restore(R/'recovery/remote-vm-manifest.jsonl',packs,R/'recovery/restore-tool-test',prefix)
expected_source_count=sum(line.split('  ',1)[1].startswith('src/') for line in (R.parent/'release21b/SOURCE-release-r11.sha256').read_text().splitlines())
assert result['restored_regular_files']==source_count==expected_source_count and source_count>0
(R/'restore-tool-result.json').write_text(json.dumps(result,indent=2)+'\n')
print('PUBLISHED_RESTORE_TOOL_ACTUAL_SOURCE_PASS',source_count,flush=True)

# Independent current app artifact, without user data or credentials.
app=R.parent/'release21b/qualification-r11/app-device.tar.gz';app_name='Inkbound-21b-candidate-app-device.tar.gz'
app_stage=R/'staging'/app_name
if not app_stage.exists():shutil.copyfile(app,app_stage)
app_sha=sha(app_stage);assert app_sha=='0e2c5308b68fbfbf05383780d40e9924130d6114cc9ea3e661c22f1cdbfed7d1'
a=assets().get(app_name)
if a is None:gh(['release','upload',TAG,str(app_stage),'--repo',REPO]);a=assets()[app_name]
assert a['digest']=='sha256:'+app_sha and a['size']==app_stage.stat().st_size
index={'format_version':1,'complete':True,'repository':REPO,'private':True,'release_tag':TAG,'machine_id':'622e1d96c4974ce09dc3c5aca09f88e2','manifest_sha256':state['manifest_sha256'],'manifest_asset':manifest_asset,'snapshot':state['summary'],'packs':[state['parts'][str(i)] for i in range(1,state['pack_count']+1)],'verification':{'all_chunks_locally_decrypted_and_sha256_verified':True,'all_uploaded_assets_server_sha256_verified':True,'critical_source_and_app_remotely_restored':state['critical_remote_restore'],'restore_utility_actual_source_files':source_count,'original_source_consistency':consistency},'current_app_candidate':{'name':app_name,'sha256':app_sha,'app_manifest_sha256':'471b5d004fdbf02c0f008ac29593717594bcdb128f18a8615852aa83198112d0','physical_acceptance':'NOT_COMPLETED'},'recovery_identity_on_mac':IDENTITY,'private_key_uploaded':False,'os_identity_directories_excluded':True}
index_path=R/'snapshot-index.json';index_path.write_text(json.dumps(index,indent=2)+'\n');index_sha=sha(index_path)
old=assets().get(index_path.name)
if old is None:gh(['release','upload',TAG,str(index_path),'--repo',REPO]);old=assets()[index_path.name]
assert old['digest']=='sha256:'+index_sha
remote=gh(['release','download',TAG,'--repo',REPO,'--pattern',index_path.name,'--output','-']);assert hashlib.sha256(remote).hexdigest()==index_sha and json.loads(remote)['complete']
proof={'backup_verified':True,'repository':REPO,'private':True,'release_id':release_id,'release_tag':TAG,'snapshot_index_sha256':index_sha,'manifest_sha256':state['manifest_sha256'],'regular_files':382673,'packs':state['pack_count'],'source_unchanged':True,'critical_remote_restore_files':875,'restore_utility_source_files':source_count,'vm_deleted':False,'source_tree_originals_modified':False}
(R/'BACKUP-VERIFIED.json').write_text(json.dumps(proof,indent=2)+'\n')
notes=R/'release-notes-verified.md';notes.write_text('Verified private builder archive: 382,673 project files. Every encrypted pack was locally decrypted and checked against source chunk hashes; every uploaded asset matches GitHub server SHA256. The current 845-file source and 30 app payload/manifest files were recovered from downloaded packs. The encrypted manifest and published restore utility passed recovery checks. Original project files remained unchanged. See snapshot-index.json. SSH identities are excluded; the recovery key remains on the Mac. The app is a software-qualified candidate; physical acceptance is unfinished.\n')
gh(['release','edit',TAG,'--repo',REPO,'--draft=false','--notes-file',str(notes)])
assert api('repos/'+REPO+'/releases/'+str(release_id))['draft'] is False
state['complete']=True;state['cloud_backup_verified']=True;state['vm_deleted']=False
(R/'backup-state.json').write_text(json.dumps(state,indent=2)+'\n')
print('CLOUD_BACKUP_VERIFIED_READY_FOR_SEPARATE_VM_RETIREMENT',flush=True)
