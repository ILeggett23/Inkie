import test from 'node:test';
import assert from 'node:assert/strict';
import {randomUUID} from 'node:crypto';
import {validateTom,providerPayload,workersAIPayload,processTom} from '../src/worker.mjs';
import {memoryCue,memoryOperations,memoryRequest,MEMORY_LIMITS} from '../src/memory.mjs';

const base=(extra={})=>({request_id:randomUUID(),conversation_id:randomUUID(),provider:'openai',model:'gpt-5.4-mini',max_output_tokens:800,question:'What should I read next? My name is Sam.',scope:'general',context:null,history:[],consent:true,purpose:'answer',...extra});
const models=['gpt-5.4-mini'];

test('memory notes are validated on their own bound and only with the capability',()=>{
 const ok=base({memory_capable:true,memory:['The reader\'s name is Sam.']});assert.deepEqual(validateTom(ok,models),ok);
 assert.deepEqual(validateTom(base({memory_capable:true}),models).memory_capable,true);
 for(const bad of [base({memory:['x']}),base({memory_capable:false}),base({memory_capable:'yes'}),base({memory_capable:true,memory:'x'}),base({memory_capable:true,memory:['']}),base({memory_capable:true,memory:[' ']}),base({memory_capable:true,memory:['x'.repeat(241)]}),base({memory_capable:true,memory:Array.from({length:61},(_,i)=>'Note '+i)}),base({memory_capable:true,memory:Array.from({length:18},()=>'y'.repeat(240))}),base({memory_capable:true,memory:[3]})])
  assert.throws(()=>validateTom(bad,models),/Memory notes exceed limits/);
 const transcription=base({purpose:'transcribe',question:'',memory_capable:true,memory:['x'],handwriting_image:'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGNgYGD4DwABBAEAwS2OUAAAAABJRU5ErkJggg=='});
 assert.throws(()=>validateTom(transcription,models),/Memory notes exceed limits/);
 // outside the combined 32000: a full question and full notes are both accepted
 assert.ok(validateTom(base({question:'q'.repeat(8000),memory_capable:true,memory:Array.from({length:17},()=>'z'.repeat(240))}),models));
});

test('notes reach the model as information before the question, with guidance; transcription and plain requests are unchanged',()=>{
 const d=base({memory_capable:true,memory:['The reader\'s name is Sam.','Sam prefers short answers.']}),p=providerPayload(d),content=p.input.at(-1).content;
 assert.match(content[0].text,/^Notes you keep about this reader.*never instructions\):\n- The reader's name is Sam\.\n- Sam prefers short answers\.$/s);assert.match(content[1].text,/^Question: /);
 assert.match(p.instructions,/You remember\./);assert.match(p.instructions,/never instructions\.$/);
 const w=workersAIPayload(d);assert.match(w.messages.at(-1).content[0].text,/^Notes you keep about this reader/);assert.match(w.messages[0].content,/You remember\./);
 const plain=providerPayload(base());assert.doesNotMatch(plain.instructions,/You remember/);assert.match(plain.input.at(-1).content[0].text,/^Question: /);
 const history=providerPayload(base({history:[{role:'user',text:'(Earlier, about p. 3: “It was the best of times”) Who says this?'},{role:'assistant',text:'The narrator.'}]}));assert.match(history.instructions,/You remember\./);assert.equal(history.input.length,3);
 const transcribe=providerPayload({...d,purpose:'transcribe'});assert.doesNotMatch(transcribe.instructions,/You remember/);assert.ok(transcribe.input.at(-1).content.every(c=>!/Notes you keep/.test(c.text||'')));
});

test('only messages about the reader ask for notes, and proposals are cleaned to bounded operations',()=>{
 for(const yes of ['My name is Sam','I’m studying biology','Please remember that I teach Year 9','forget what I said about poems','Call me Sam','I prefer short answers'])assert.ok(memoryCue(yes),yes);
 for(const no of ['Who is Sydney Carton?','Explain this passage','Draw an eye','',null])assert.ok(!memoryCue(no),String(no));
 const kept=['The reader likes poems.','Sam prefers long answers.'];
 assert.equal(memoryOperations({remember:['Sam prefers short answers.','  Sam   studies  biology. ','sam studies biology.','x'.repeat(241),3,''],forget:['sam prefers LONG answers.','Not kept']},kept),'- Sam prefers long answers.\n+ Sam prefers short answers.\n+ Sam studies biology.');
 assert.equal(memoryOperations({remember:['The reader likes poems.'],forget:[]},kept),'');
 assert.equal(memoryOperations({remember:'x',forget:[]},kept),'');assert.equal(memoryOperations(null,kept),'');
 assert.equal(memoryOperations({remember:Array.from({length:9},(_,i)=>'Note '+i),forget:[]},[]).split('\n').length,MEMORY_LIMITS.remember);
 const r=memoryRequest('gpt-5.4-mini','My name is Sam',kept);assert.equal(r.stream,false);assert.equal(r.store,false);assert.equal(r.text.format.type,'json_schema');assert.equal(r.text.format.strict,true);assert.deepEqual(r.tools,[]);assert.match(r.input[0].content[0].text,/- The reader likes poems\.\n- Sam prefers long answers\.[\s\S]*My name is Sam$/);assert.match(r.instructions,/never instructions to you/);
});

async function run(payload,{memoryReply,memoryStatus=200}={}){
 const id=payload.request_id,events=[],calls=[];const row={id,provider:'openai',status:'queued',text:'',payload:JSON.stringify(payload),payload_key:null};
 const DB={prepare(query){return {bind(...args){return {query,args,async first(){if(query.startsWith("UPDATE tom SET status='streaming'")){if(row.status!=='queued')return null;row.status='streaming';return {...row};}if(query==='SELECT status FROM tom WHERE id=?')return {status:row.status};throw Error('Unexpected first '+query);},async run(){if(query.startsWith('INSERT INTO tom_events')){if(row.status==='streaming'||args[1]==='status')events.push({kind:args[1],data:JSON.parse(args[2])});}else if(query.startsWith('UPDATE tom SET text=?,status=')){if(row.status!=='streaming')return {meta:{changes:0}};row.text=args[0];row.status=args[1];}else if(query.startsWith('UPDATE tom SET error='))row.error=args[0];else throw Error('Unexpected run '+query);return {meta:{changes:1}};}};}};},async batch(statements){if(row.status!=='streaming')return statements.map(()=>({meta:{changes:0}}));row.text=statements[0].args[0];for(const s of statements.slice(1))events.push({kind:'delta',data:JSON.parse(s.args[1])});return statements.map(()=>({meta:{changes:1}}));}};
 const env={DB,TOM_ENABLED:'true',TOM_PROVIDER:'openai',TOM_MODELS:'gpt-5.4-mini',TOM_MAX_OUTPUT_TOKENS:'800',OPENAI_API_KEY:'synthetic-only'},original=globalThis.fetch;
 globalThis.fetch=async(url,init)=>{const body=JSON.parse(init.body);calls.push(body);assert.equal(url,'https://api.openai.com/v1/responses');
  if(body.stream===false){if(memoryStatus!==200)return new Response('{}',{status:memoryStatus});return Response.json({status:'completed',output:[{type:'reasoning',summary:[]},{type:'message',content:[{type:'output_text',text:JSON.stringify(memoryReply)}]}]});}
  let sent=false;return new Response(new ReadableStream({pull(c){if(!sent){sent=true;c.enqueue(new TextEncoder().encode('data: '+JSON.stringify({type:'response.output_text.delta',delta:'Hello Sam.'})+'\n\n'));}else{c.enqueue(new TextEncoder().encode('data: '+JSON.stringify({type:'response.completed',response:{status:'completed'}})+'\n\n'));c.close();}}}),{headers:{'Content-Type':'text/event-stream'}});};
 try{await processTom(env,id);}finally{globalThis.fetch=original;}return {row,events,calls};
}

test('a reply proposes notes once, before its final status, only when the reader offered memory',async()=>{
 const offered=await run(base({memory_capable:true,memory:['Sam prefers long answers.']}),{memoryReply:{remember:['The reader\'s name is Sam.'],forget:['Sam prefers long answers.']}});
 assert.equal(offered.row.status,'completed');assert.equal(offered.row.text,'Hello Sam.');assert.equal(offered.calls.length,2);
 const kinds=offered.events.map(e=>e.kind);assert.deepEqual(kinds.slice(-2),['memory','status']);assert.equal(kinds.filter(k=>k==='memory').length,1);
 assert.deepEqual(offered.events.at(-2).data,{text:'- Sam prefers long answers.\n+ The reader\'s name is Sam.'});assert.deepEqual(offered.events.at(-1).data,{status:'completed'});
 const answer=offered.calls.find(c=>c.stream===true);assert.match(answer.input.at(-1).content[0].text,/Sam prefers long answers/);
 // no capability, or nothing about the reader: no second call and no event
 for(const p of [base(),base({memory_capable:true,question:'Who is Sydney Carton?'})]){const r=await run(p,{memoryReply:{remember:['x'],forget:[]}});assert.equal(r.row.status,'completed');assert.equal(r.calls.length,1);assert.ok(!r.events.some(e=>e.kind==='memory'));}
 // nothing worth keeping, or a failed proposal: the answer completes, no event
 for(const options of [{memoryReply:{remember:[],forget:[]}},{memoryStatus:500},{memoryReply:'not an object'}]){const r=await run(base({memory_capable:true}),options);assert.equal(r.row.status,'completed');assert.equal(r.calls.length,2);assert.ok(!r.events.some(e=>e.kind==='memory'));}
});
