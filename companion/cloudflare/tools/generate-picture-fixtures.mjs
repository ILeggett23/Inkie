/* Regenerate the reader's parent-bound v4 picture transport fixtures from companion contract code. Usage: node tools/generate-picture-fixtures.mjs <isolated-reader-root> --check|--write. Never touches the authoritative reader-integration tree. */
import fs from 'node:fs';import path from 'node:path';
import {buildPictureFixtures,companionInputs} from '../tests/picture_contract_fixtures.mjs';
const [root,mode]=process.argv.slice(2);
if(!root||!['--check','--write'].includes(mode))throw Error('Usage: generate-picture-fixtures.mjs <isolated-reader-root> --check|--write');
if(path.resolve(root).includes('reader-integration'))throw Error('Refusing the authoritative reader tree');
const dir=path.join(root,'fixtures/visual/contracts');if(!fs.statSync(dir).isDirectory())throw Error('No fixture directory');
const {spec,bean}=companionInputs(),built=buildPictureFixtures(spec,bean);let differ=0;
for(const [name,bytes] of Object.entries(built)){const file=path.join(dir,name),old=fs.existsSync(file)?fs.readFileSync(file):null,same=old!==null&&old.equals(bytes);
 if(mode==='--write'&&!same)fs.writeFileSync(file,bytes);if(!same)differ++;console.log(`${same?'IDENTICAL':mode==='--write'?'WRITTEN':'DIFFERS'} ${name} ${bytes.length}`);}
console.log(mode==='--check'?(differ?`PICTURE_FIXTURES_DIFFER count=${differ}`:'PICTURE_FIXTURES_REPRODUCED_BYTE_IDENTICAL'):`PICTURE_FIXTURES_WRITTEN changed=${differ}`);
if(mode==='--check'&&differ)process.exitCode=1;
