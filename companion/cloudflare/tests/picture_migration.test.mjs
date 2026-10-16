import {test} from 'node:test';import assert from 'node:assert/strict';import {readFileSync} from 'node:fs';import {spawnSync} from 'node:child_process';
test('additive picture-parent migration preserves legacy rows and leaves their lineage null',()=>{const sql=['0001_initial.sql','0002_tom_deletion.sql','0003_tom_provider.sql','0004_tom_illustrations.sql','0005_picture_parent.sql'].map(n=>readFileSync(new URL('../migrations/'+n,import.meta.url),'utf8'));const code=`import sys,json,sqlite3
sql=json.load(sys.stdin);c=sqlite3.connect(':memory:');c.row_factory=sqlite3.Row
for s in sql[:4]:c.executescript(s)
c.execute('INSERT INTO users VALUES(?,?,?)',('fixture','migration@example.invalid','synthetic-fixture-hash'))
c.execute("INSERT INTO tom_illustrations(id,user_id,fingerprint,prompt,provider,model,preset,status,created,updated,expires,object_key) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)",('legacy','fixture','frozen','original prompt','openai','configured-model','quick','completed',1,2,999,'preserved-object'))
before=dict(c.execute('SELECT * FROM tom_illustrations').fetchone());c.executescript(sql[4]);after=dict(c.execute('SELECT * FROM tom_illustrations').fetchone());assert after.pop('parent_request_id') is None;assert before==after
print('LEGACY_ROWS_PRESERVED')
`;const r=spawnSync('python3',['-c',code],{input:JSON.stringify(sql),encoding:'utf8'});assert.equal(r.status,0,r.stderr);assert.ok(r.stdout.includes('LEGACY_ROWS_PRESERVED'));});
