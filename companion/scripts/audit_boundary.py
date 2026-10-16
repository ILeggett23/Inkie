"""Fail closed on tracked repository content outside the companion allowlist."""
import argparse
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT=Path(__file__).resolve().parent.parent
ROOT_FILES={'.gitignore','.dockerignore','.env.example','README.md','WORKLOG.md','SECURITY.md','NOTICE.md','requirements.txt','Dockerfile'}
DIRS={'companion','web','tests','protocol','docs','deploy','scripts','.github','cloudflare'}
EXTENSIONS={'.py','.js','.cjs','.mjs','.ts','.json','.jsonc','.md','.txt','.html','.css','.svg','.webmanifest','.yaml','.yml','.toml','.sql','.lock','.example'}
PATTERNS=[r'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',r'\bsk-[A-Za-z0-9_-]{25,}',r'\bgh[pousr]_[A-Za-z0-9]{30,}',r'\bgithub_pat_[A-Za-z0-9_]{30,}',r'/Users/',r'libqsgepaper',r'EVIOCGRAB',r'QQuickPaintedItem',r'Xochitl',r'inkbound-companion-cross-fixture\.json']

def audit(paths, root=None):
    root=root or ROOT
    failures=[]
    if not paths:
        return ['No source files selected; an empty index is not a publication audit']
    for name in paths:
        path=root/name;parts=Path(name).parts
        if name not in ROOT_FILES and (not parts or parts[0] not in DIRS):failures.append(name+': outside explicit companion scope');continue
        if path.is_symlink() or any((root.joinpath(*parts[:i])).is_symlink() for i in range(1,len(parts))):failures.append(name+': symlink or symlink ancestor forbidden');continue
        if not path.is_file():failures.append(name+': missing regular file');continue
        if any(p in ('node_modules','private','runtime','work','outputs','__pycache__','.wrangler') for p in parts):failures.append(name+': runtime or build directory forbidden');continue
        if path.stat().st_size>1024*1024:failures.append(name+': file over1MiB requires boundary review');continue
        if len(parts)>1 and path.suffix not in EXTENSIONS and path.name not in ('.gitignore','Dockerfile','.dockerignore'):failures.append(name+': file type not allowed');continue
        try:text=path.read_text()
        except UnicodeDecodeError:failures.append(name+': binary forbidden');continue
        if name=='scripts/audit_boundary.py':continue
        for pattern in PATTERNS:
            if re.search(pattern,text):failures.append(name+': sensitive/local-only content pattern');break
    return failures

def history_audit(root, paths):
    failures=[]
    commits=subprocess.run(['git','rev-list','--all'],cwd=root,check=True,capture_output=True,text=True).stdout.splitlines()
    if not commits:
        return ['No commits exist; historical publication audit cannot pass']
    for commit in commits:
        entries=subprocess.run(['git','ls-tree','-rz','--full-tree',commit],cwd=root,check=True,capture_output=True).stdout.split(b'\0')
        for entry in filter(None,entries):
            meta,name_bytes=entry.split(b'\t',1)
            mode,kind,object_id=meta.decode().split()
            try:name=name_bytes.decode('utf-8')
            except UnicodeDecodeError:
                failures.append(commit+': undecodable historical path');continue
            if mode not in ('100644','100755') or kind!='blob':
                failures.append(commit+': historical symlink/submodule/nonregular entry '+name);continue
            if name not in paths:
                failures.append(commit+': historical file outside audited tracked set '+name)
            raw=subprocess.run(['git','cat-file','blob',object_id],cwd=root,check=True,capture_output=True).stdout
            if len(raw)>1024*1024:
                failures.append(commit+': historical file over1MiB '+name);continue
            try:text=raw.decode('utf-8')
            except UnicodeDecodeError:
                failures.append(commit+': binary history '+name);continue
            if name!='scripts/audit_boundary.py' and any(re.search(p,text) for p in PATTERNS):
                failures.append(commit+': sensitive history pattern '+name)
    return failures

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--untracked-source',action='store_true');parser.add_argument('--history',action='store_true');args=parser.parse_args()
    if args.untracked_source:
        paths=[str(p.relative_to(ROOT)) for p in ROOT.rglob('*') if p.is_file() and not any(x in p.relative_to(ROOT).parts for x in ('work','outputs','node_modules','.git','__pycache__','.venv','.wrangler','.dev.vars','.env'))]
    else:
        result=subprocess.run(['git','ls-files','-z'],cwd=ROOT,check=True,capture_output=True);paths=[x for x in result.stdout.decode().split('\0') if x]
    failures=audit(paths)
    if args.history:
        failures.extend(history_audit(ROOT,paths))
    print(json.dumps({'files_checked':len(paths),'pass':not failures,'failures':failures},indent=2))
    return bool(failures)

if __name__=='__main__':sys.exit(main())
