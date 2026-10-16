"""Final read-only snapshot consistency check; JSONL manifest arrives on stdin."""
from pathlib import Path, PurePosixPath
import json, os, stat, sys
assert Path('/etc/machine-id').read_text().strip()=='622e1d96c4974ce09dc3c5aca09f88e2'
home=Path('/home/zayway.guest');assert Path('/home/zayway.linux').is_symlink() and Path('/home/zayway.linux').resolve()==home
header=None;expected=set();files=0;summary=None
def absolute(name):
    p=PurePosixPath(name);assert not p.is_absolute() and '..' not in p.parts and p.parts[0] in {'home','tmp'}
    return (home if p.parts[0]=='home' else Path('/tmp')).joinpath(*p.parts[1:])
def kind(s):
    for name,predicate in [('dir',stat.S_ISDIR),('file',stat.S_ISREG),('symlink',stat.S_ISLNK),('fifo',stat.S_ISFIFO),('character',stat.S_ISCHR),('block',stat.S_ISBLK)]:
        if predicate(s.st_mode):return name
    return 'socket' if stat.S_ISSOCK(s.st_mode) else 'unknown'
for line in sys.stdin:
    e=json.loads(line)
    if e['type']=='header':header=e
    elif e['type']=='summary':summary=e
    elif e['type']=='entry':
        assert e['path'] not in expected;expected.add(e['path']);p=absolute(e['path']);s=p.lstat()
        fields={'mode':stat.S_IMODE(s.st_mode),'uid':s.st_uid,'gid':s.st_gid,'mtime_ns':s.st_mtime_ns,'ctime_ns':s.st_ctime_ns,'dev':s.st_dev,'ino':s.st_ino,'nlink':s.st_nlink,'kind':kind(s)}
        assert all(e[k]==v for k,v in fields.items()), 'Source metadata changed: '+e['path']
        if e['kind']=='file':assert s.st_size==e['size'];files+=1
        if e['kind']=='symlink':assert os.readlink(p)==e['target']
assert header and summary and summary['complete'] and files==summary['files']
seen=set()
def walk(p,name):
    if p.name in {'.ssh','.git-credentials'}:return
    s=p.lstat()
    if stat.S_ISSOCK(s.st_mode):return
    seen.add(name)
    if stat.S_ISDIR(s.st_mode):
        for child in p.iterdir():walk(child,name+'/'+child.name)
for name,path in header['roots'].items():assert str(absolute(name))==path;walk(Path(path),name)
assert seen==expected, 'Added or removed source entries after snapshot'
current={'home'}
for p in Path('/tmp').iterdir():
    s=p.lstat();runtime=p.name.startswith(('ssh-','systemd-','.X11','.ICE','.font-','.XIM','snap-private-','inkie-migration-'))
    project=s.st_uid==501 or p.name.startswith(('reader-','inkbound-','interaction20','tom20','e2-','e2tools','rd','structured-selection'))
    if project and not runtime and not stat.S_ISSOCK(s.st_mode):current.add('tmp/'+p.name)
assert current==set(header['roots']), 'New project temporary data exists outside snapshot'
assert {p.name for p in Path('/home').iterdir()}=={'zayway.guest','zayway.linux'}
print(json.dumps({'source_entries':len(expected),'regular_files':files,'unchanged':True,'no_new_project_roots':True,'home_alias_verified':True}))
