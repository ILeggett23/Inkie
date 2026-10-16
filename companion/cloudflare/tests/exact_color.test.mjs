import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {validateSceneReply,pictureReplyFormat,pictureReplyFormatV5,colorValue} from '../src/visual_scene.mjs';
const bar=JSON.parse(readFileSync(new URL('./fixtures/visual/bar.json',import.meta.url),'utf8'));
const reply=scene=>JSON.stringify({mode:'visual',response:'',visual:scene});
test('scene version 5 adds exact #rrggbb colors to native drawings; version 4 stays names-only and picture plans are unchanged',()=>{
 assert.ok(!JSON.stringify(pictureReplyFormat).includes('#rrggbb')&&JSON.stringify(pictureReplyFormatV5).includes('#rrggbb'));
 assert.equal(JSON.stringify(pictureReplyFormatV5.schema.properties.visual.anyOf[2]),JSON.stringify(pictureReplyFormat.schema.properties.visual.anyOf[2])); /* picture plans: same schema in both versions */
 for(const c of ['black','pink','#3366cc','#3366CC'])assert.ok(colorValue(c),c);for(const c of ['white','#36c','#3366cz','3366cc','#3366cc0',12])assert.ok(!colorValue(c),String(c));
 const palette=structuredClone(bar.scene);palette.palette[0].color='#3366cc';
 assert.throws(()=>validateSceneReply(reply(palette)));assert.throws(()=>validateSceneReply(reply(palette),{},{version:4}));
 assert.equal(validateSceneReply(reply(palette),{},{version:5}).visual.palette[0].color,'#3366cc');
 const style=structuredClone(bar.scene);style.objects[0].style.color='#AA3311';assert.equal(validateSceneReply(reply(style),{},{version:5}).visual.objects[0].style.color,'#AA3311');assert.throws(()=>validateSceneReply(reply(style)));
 for(const bad of ['#3366cz','#36c','white']){const s=structuredClone(bar.scene);s.palette[0].color=bad;assert.throws(()=>validateSceneReply(reply(s),{},{version:5}),bad);}
 assert.deepEqual(validateSceneReply(reply(bar.scene),{},{version:5}).visual,validateSceneReply(reply(bar.scene)).visual); /* named colors read the same in both versions */
});

test('mixed scene prose uses an explicit capability and preserves all visual checks',()=>{const raw=JSON.stringify({mode:'visual',response:'These bars compare the supplied values.',visual:bar.scene});assert.throws(()=>validateSceneReply(raw));const checked=validateSceneReply(raw,{},{allowMixed:true});assert.equal(checked.response,'These bars compare the supplied values.');assert.deepEqual(checked.visual,bar.scene);const broken=structuredClone(bar.scene);broken.objects[0].bounds=[-1,0,10,10];assert.throws(()=>validateSceneReply(JSON.stringify({mode:'visual',response:'Explanation',visual:broken}),{},{allowMixed:true}));});
