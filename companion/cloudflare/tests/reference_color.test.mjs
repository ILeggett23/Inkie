import test from 'node:test';
import assert from 'node:assert/strict';
import {referenceTableReply,referenceFallbackReply,referenceGrid} from '../src/reference_table.mjs';
import {validateSceneReply} from '../src/visual_scene.mjs';
import {validateVisualSources} from '../src/visual_sources.mjs';
import catalog from '../src/visual_reference_data.mjs';
const elements=catalog.find(r=>r.id==='chemical-elements-2022');
const drawn=[{role:'user',text:'Draw the periodic table'},{role:'assistant',text:'Chemical elements, IUPAC 4 May 2022\nchemical-elements-2022: 118 records'}];
// The real 2026-10-13 conversation: Tom asked how to color instead of coloring, then asked again after "Yes".
const offered=[...drawn,{role:'user',text:'Add color'},{role:'assistant',text:'Do you want me to color the periodic table by element category, such as alkali metals, noble gases, and so on? If so, I can do that, but I need the coloring rule you want.'}];
const askedAgain=[...offered,{role:'user',text:'Yes'},{role:'assistant',text:'Yes to coloring by category? If so, should I use standard periodic-table groups like alkali metals, alkaline earth metals, transition metals, halogens, noble gases, lanthanides, actinides, and so on?'}];
const isColored=r=>r?.visual?.palette?.length===12;
test('scene version 5 draws the registered periodic table colored by IUPAC element family, with a legend, from the catalog alone',()=>{
 const reply=referenceTableReply('Draw the periodic table',{version:5});assert.ok(isColored(reply));const v=reply.visual;
 validateSceneReply(JSON.stringify(reply),{},{version:5});validateVisualSources(v,'Draw the periodic table');
 assert.throws(()=>validateSceneReply(JSON.stringify(reply))); /* exact colors never reach a version 4 reader */
 const family=Object.fromEntries(v.datasets[0].rows.map(r=>[r.label,r.category]));
 for(const [symbol,name] of [['H','hydrogen'],['Li','alkali metals'],['Fr','alkali metals'],['Be','alkaline earth metals'],['Sc','transition elements'],['Zn','transition elements'],['Rf','transition elements'],['La','lanthanoids'],['Lu','lanthanoids'],['Ac','actinoids'],['Lr','actinoids'],['B','group 13'],['C','group 14'],['N','pnictogens'],['O','chalcogens'],['F','halogens'],['Ts','halogens'],['He','noble gases'],['Og','noble gases']])assert.equal(family[symbol],name,symbol);
 const count=name=>v.datasets[0].rows.filter(r=>r.category===name).length;assert.equal(count('transition elements'),38);assert.equal(count('lanthanoids'),15);assert.equal(count('actinoids'),15);assert.equal(count('noble gases'),7);
 assert.deepEqual(v.datasets[0].rows.map(({category,...rest})=>rest),referenceGrid(elements).datasets[0].rows.map(({category,...rest})=>rest)); /* facts and positions are the catalog's, unchanged */
 const swatches=v.objects.filter(o=>o.kind==='path'),names=v.objects.filter(o=>o.kind==='label');assert.equal(swatches.length,12);assert.equal(names.length,12);
 for(const s of swatches){assert.equal(s.style.tool,'highlighter');assert.ok(v.palette.some(p=>p.color===s.style.color));assert.ok(s.bounds[1]>=v.objects[0].bounds[3]);}
 assert.deepEqual(names.map(n=>n.content.text),v.palette.map(p=>p.category));for(const n of names)assert.equal(n.style.color,'black');
 assert.ok(v.canvas[1]>=Math.max(...v.objects.map(o=>o.bounds[1]+o.bounds[3])));assert.match(v.title,/colored by element family/);
});
test('version 4 readers keep the exact uncolored table, and plain wording keeps it black on version 5',()=>{
 for(const options of [{},{version:4}]){const reply=referenceTableReply('Draw the periodic table',options);assert.equal(JSON.stringify(reply.visual),JSON.stringify(referenceGrid(elements)));assert.deepEqual(reply.visual.palette,[]);assert.equal(reply.visual.objects.length,1);}
 for(const q of ['Draw the periodic table in black and white','Draw the periodic table without colors','Draw a plain periodic table','Draw a black and white periodic table'])assert.deepEqual(referenceTableReply(q,{version:5})?.visual.palette,[],q);
 for(const q of ['Draw the periodic table in color','Draw a colored periodic table','Draw me out a periodic table in color.','Show the periodic table color-coded by family'])assert.ok(isColored(referenceTableReply(q,{version:5})),q);
 for(const q of ['Draw me out a periodic table in color.','Draw a colored periodic table'])assert.equal(referenceTableReply(q,{version:4}),null,q); /* version 4 keeps sending styled requests to the planner */
});
test('a short color follow-up or a confirmation of Tom\'s offer redraws the table just drawn, in color, without the planner',()=>{
 for(const q of ['Add color','Add colors','Can you color it by family?','Make it colorful','Color it please','Yes, add color','Use colors'])assert.ok(isColored(referenceTableReply(q,{history:drawn,version:5})),q);
 assert.ok(isColored(referenceTableReply('Yes',{history:offered,version:5})));assert.ok(isColored(referenceTableReply('Yes',{history:askedAgain,version:5})));assert.ok(isColored(referenceTableReply('Go ahead',{history:offered,version:5})));
 const unrelated=[...drawn,{role:'user',text:'Why is the sky blue?'},{role:'assistant',text:'Sunlight scatters off air molecules, and blue light scatters most. Want to know why sunsets look so colorful?'}];
 const leftToPlanner=[['Yes',unrelated],['Add color to the heart',drawn],['What color is gold?',drawn],['Color it by group',drawn],['Add color',[]],['Yes',drawn],['Yes',[]],['Draw a heart',drawn],['Add color',[{role:'user',text:'Draw a heart'},{role:'assistant',text:'heart'}]]];
 for(const [q,history] of leftToPlanner)assert.equal(referenceTableReply(q,{history,version:5}),null,q);
 assert.equal(referenceTableReply('Add color',{history:drawn,version:4}),null);assert.equal(referenceTableReply('Yes',{history:offered,version:4}),null);
});
test('a rejected follow-up plan binding the whole table drawn earlier is rescued in color on version 5 only',()=>{
 const rows=elements.records.map(f=>({id:String(f.number),label:f.symbol,x:f.number,y:f.period,row:null,column:null,category:'metal'}));
 const raw=JSON.stringify({mode:'visual',response:'',visual:{version:2,category:'table',datasets:[{id:'elements',source:'reference:chemical-elements-2022',rows}]}});
 assert.ok(isColored(referenceFallbackReply(raw,'Yes please',{history:offered,version:5})));
 assert.equal(referenceFallbackReply(raw,'Yes please',{history:offered,version:4}),null); /* version 4: only a table its own question names */
 assert.equal(referenceFallbackReply(raw,'Yes please',{history:[],version:5}),null);
 assert.equal(referenceFallbackReply(JSON.stringify({mode:'visual',response:'',visual:{version:2,category:'table',datasets:[{id:'elements',source:'reference:chemical-elements-2022',rows:rows.slice(0,20)}]}}),'Yes please',{history:offered,version:5}),null); /* partial plans stay rejected */
 assert.deepEqual(referenceFallbackReply(raw,'Draw the periodic table in black and white',{version:5}).visual.palette,[]);
});
