/* The authored reader fixtures are rebuilt only from contract code (tools/generate-picture-fixtures.mjs). Transport identity, not drawing quality. */
import {test} from 'node:test';import assert from 'node:assert/strict';
import {buildPictureFixtures,companionInputs,FIXTURE_PARENT,BEAN_SHA256} from './picture_contract_fixtures.mjs';
import {pictureImageId,pictureHash} from '../src/picture_plan.mjs';
const envelope=bytes=>{let at=8;while(at+12<=bytes.length){const n=bytes.readUInt32BE(at),k=bytes.subarray(at+4,at+8).toString();if(k==='tEXt'){const t=bytes.subarray(at+8,at+8+n).toString();if(t.startsWith('InkboundScene\0'))return JSON.parse(t.slice(14));}at+=12+n;}return null;};
test('authored picture fixtures are parent-bound, hash-consistent and deterministic',()=>{
 const {spec,bean}=companionInputs(),a=buildPictureFixtures(spec,bean),b=buildPictureFixtures(spec,bean);for(const k of Object.keys(a))assert.ok(a[k].equals(b[k]),k);
 const parity=JSON.parse(a['picture-parity.json']);assert.equal(parity.parent_request_id,FIXTURE_PARENT);assert.equal(parity.request_id,pictureImageId(FIXTURE_PARENT));assert.equal(pictureHash(parity.plan_json),parity.plan_sha256);assert.deepEqual(JSON.parse(a['picture-panel-identity.json']),{parent:FIXTURE_PARENT,child:parity.request_id});
 for(const name of ['picture-bound.png','picture-panel.png','picture-unavailable.png','picture-bean-negative.png','picture-unlocated.png']){const m=envelope(a[name]);assert.equal(m.version,4);assert.equal(m.parent_request_id,FIXTURE_PARENT);assert.equal(m.request_id,parity.request_id);assert.equal(m.plan_sha256,parity.plan_sha256);assert.equal(m.quality_status,'unverified');}
 assert.equal(envelope(a['picture-bean-negative.png']).source_sha256,BEAN_SHA256,'the bean stays the original known-negative pixels');assert.equal(envelope(a['picture-regions.png']),null);const u=envelope(a['picture-unlocated.png']);assert.equal(u.advice.status,'advisory');assert.equal(u.grounding_status,'unbound');assert.ok(u.advice.components.length===3&&u.advice.components.every(g=>g.bounds===null&&g.target===null&&g.geometry===null));
 assert.throws(()=>buildPictureFixtures(spec,Buffer.concat([bean,Buffer.from([0])])),/Known-negative/);
 const plain=JSON.parse(a['picture-no-labels-parity.json']),pm=envelope(a['picture-no-labels.png']);assert.ok(plain.spec.components.every(c=>c.label===null));assert.equal(pm.plan_sha256,plain.plan_sha256);assert.equal(pm.request_id,plain.request_id);assert.equal(pm.advice.status,'unavailable');assert.notEqual(plain.plan_sha256,parity.plan_sha256);
});
