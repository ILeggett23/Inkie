#!/usr/bin/env python3
"""Preserve bounded test evidence, then remove this successful run's own fixture tree."""
from pathlib import Path
import hashlib,json,os,shutil,sys
run,source,config=Path(sys.argv[1]),Path(sys.argv[2]),sys.argv[3]
assert config in ('release','asan','ubsan')
assert run.parent==Path('/tmp') and run.name.startswith('reader-interaction3-validation.')
assert run.is_dir() and not run.is_symlink() and run.stat().st_uid==os.getuid()
out=source/'evidence'/'successful-runs'/(config+'-'+run.name.rsplit('.',1)[1]);out.mkdir(parents=True,exist_ok=False)
files={}
for relative in ('library-route/library-grid.png','notes-route/reader-definition.png','notes-route/reader-notes.png','notes-route/reader-layers.png','reveal-route/reader-tools-effects.png','reveal-route/reader-definition-effects.png','v1-panel-effects/v1-notes-effects.png','v1-panel-effects/v1-tom-effects.png','v1-panel-effects/v1-tom-history-effects.png','v1-panel-effects/v1-general-tom-effects.png',*[f'visual-previews/{name}.png' for name in ('periodic','eye','person','landscape','cell','bar','math','map','flow','phone')]):
    path=run/relative
    if not path.exists():continue
    assert path.is_file() and not path.is_symlink() and path.stat().st_size<=8*1024*1024
    target=out/path.name;shutil.copyfile(path,target);files[path.name]=hashlib.sha256(target.read_bytes()).hexdigest()
stamp=source/('build-'+config)/'.compile-inputs.json'
assert stamp.is_file();shutil.copyfile(stamp,out/'compile-inputs.json')
(out/'run.json').write_text(json.dumps({'config':config,'all_required_tests_passed':True,'synthetic_data_removed':str(run),'retained_previews':files},indent=2)+'\n')
shutil.rmtree(run)
print('SUCCESSFUL_SYNTHETIC_DATA_RECLAIMED='+str(run))
