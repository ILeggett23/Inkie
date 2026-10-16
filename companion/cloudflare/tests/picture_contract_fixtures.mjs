/* Authored parent-bound v4 transport controls, rebuilt only from the companion's own contract code. Never model output or drawing-quality evidence; the bean stays the known-negative control. */
import {readFileSync} from 'node:fs';
import {lowerPicturePlan,picturePrompt,pictureHash,pictureImageId} from '../src/picture_plan.mjs';
import {picturePng,validatePictureAdvice} from '../src/picture_review.mjs';
import {transportPng} from './picture_fixture.mjs';
export const FIXTURE_PARENT='22222222-2222-4222-8222-222222222222',FIXTURE_REVIEW_MODEL='gpt-5.4-mini',BEAN_SHA256='77afae8c962401eb9c59f2448f6ce4e45dd23dfed946f36377ec308d8ab02a37';
const evidence='Synthetic colored transport region, not subject-quality evidence';
const region=(id,color,bounds,target,points)=>({id,visible:true,confidence:.9,bounds,target,colors:[color],geometry:{kind:'area',points,holes:[]},evidence});
/* Regions of transportPng(): red rectangle, blue ellipse (32 points), black triangle. */
export function regionAdvice(){const ellipse=[];for(let i=0;i<32;i++){const t=2*Math.PI*i/32;ellipse.push([.5+.11*Math.cos(t),.5+.22*Math.sin(t)]);}return {status:'advisory',summary:'Synthetic transport bindings, not quality acceptance',issues:[],components:[region('alpha','red',[.08,.28,.22,.44],[.19,.5],[[.08,.28],[.3,.28],[.3,.72],[.08,.72]]),region('beta','blue',[.39,.28,.22,.44],[.5,.5],ellipse),region('gamma','black',[.7,.28,.22,.44],[.7+.22/2,.5],[[.7,.72],[.92,.72],[.81,.28]])]};}
/* Three vertical thirds over the bean image: transport bindings only; the reader's known-negative pixel check must still reject it. */
export function thirdsAdvice(){const third=k=>{const x=k/3,w=1/3;return region(['alpha','beta','gamma'][k],['red','blue','black'][k],[x,0,w,1],[(2*k+1)/6,.5],[[x,0],[x+w,0],[x+w,1],[x,1]]);};return {status:'advisory',summary:'Synthetic transport bindings, not quality acceptance',issues:[],components:[0,1,2].map(third)};}
export const unavailable=summary=>({status:'unavailable',summary,issues:[],components:[]});
function basePictureFixtures(spec,bean){
 if(pictureHash(bean)!==BEAN_SHA256)throw Error('Known-negative bean control changed');
 const canonical=v=>Array.isArray(v)?v.map(canonical):v&&typeof v==='object'?Object.fromEntries(Object.keys(v).sort().map(k=>[k,canonical(v[k])])):v; /* the reader sends Qt QJsonDocument bytes, whose object keys are sorted */
 const scene=lowerPicturePlan(spec),plan_json=JSON.stringify(canonical(scene)),plan_sha256=pictureHash(plan_json),request_id=pictureImageId(FIXTURE_PARENT),prompt=picturePrompt(scene);
 const request={visual_plan:scene,parent_request_id:FIXTURE_PARENT,request_id,plan_sha256,review_model:FIXTURE_REVIEW_MODEL};
 const advice=regionAdvice(),{status,...bare}=advice;validatePictureAdvice(bare,scene);
 const regions=transportPng(),json=(v,indent)=>Buffer.from(JSON.stringify(v,null,indent)+'\n');
 return {
  'picture-plan-v4.json':json(spec,2),
  'picture-parity.json':json({spec,scene,prompt,plan_json,plan_sha256,request_id,review_model:FIXTURE_REVIEW_MODEL,parent_request_id:FIXTURE_PARENT},2),
  'picture-panel-identity.json':json({parent:FIXTURE_PARENT,child:request_id}),
  'picture-advice.json':json(advice,2),
  'picture-regions.png':regions,
  'picture-bound.png':picturePng(regions,request,advice),
  'picture-panel.png':picturePng(regions,request,unavailable('Synthetic UI transport control')),
  'picture-unavailable.png':picturePng(regions,request,unavailable('Synthetic transport control: no quality review')),
  'picture-bean-negative.png':picturePng(bean,request,thirdsAdvice()),
  'picture-unlocated.png':picturePng(regions,request,{...advice,summary:'Synthetic unlocated advisory transport control (sealing fallback shape)',components:advice.components.map(g=>({...g,bounds:null,target:null,geometry:null}))})};
}
export function buildPictureFixtures(spec,bean){const files=basePictureFixtures(spec,bean),plain=structuredClone(spec);for(const c of plain.components)c.label=null;const unlabelled=basePictureFixtures(plain,bean);return {...files,'picture-no-labels.png':unlabelled['picture-unavailable.png'],'picture-no-labels-parity.json':unlabelled['picture-parity.json']};}
export const companionInputs=()=>({spec:JSON.parse(readFileSync(new URL('./fixtures/picture-plan-v4.json',import.meta.url),'utf8')),bean:readFileSync(new URL('./fixtures/visual/negative/bean-eye.png',import.meta.url))});
