import test from 'node:test';
import assert from 'node:assert/strict';
import {randomUUID} from 'node:crypto';
import {processTom,providerPayload} from '../src/worker.mjs';

// A provider that fails before streaming is retried twice (nothing was produced yet); a used-up quota and other client
// errors are not; a stream that breaks after output is never retried (its partial text is kept, as before).
async function run(replies){
 const id=randomUUID(),events=[],calls=[];const payload={request_id:id,conversation_id:randomUUID(),provider:'openai',model:'gpt-5.4-mini',max_output_tokens:800,question:'Synthetic reliability fixture',scope:'general',context:null,history:[],consent:true,purpose:'answer'};
 const row={id,provider:'openai',status:'queued',text:'',payload:JSON.stringify(payload),payload_key:null};
 const DB={prepare(query){return {bind(...args){return {query,args,async first(){if(query.startsWith("UPDATE tom SET status='streaming'")){if(row.status!=='queued')return null;row.status='streaming';return {...row};}if(query==='SELECT status FROM tom WHERE id=?')return {status:row.status};throw Error('Unexpected first '+query);},async run(){if(query.startsWith('INSERT INTO tom_events'))events.push({kind:args[1],data:JSON.parse(args[2])});else if(query.startsWith('UPDATE tom SET text=?,status=')){if(row.status!=='streaming')return {meta:{changes:0}};row.text=args[0];row.status=args[1];}else if(query.startsWith('UPDATE tom SET error='))row.error=args[0];else throw Error('Unexpected run '+query);return {meta:{changes:1}};}};}};},async batch(statements){if(row.status!=='streaming')return statements.map(()=>({meta:{changes:0}}));row.text=statements[0].args[0];for(const s of statements.slice(1))events.push({kind:'delta',data:JSON.parse(s.args[1])});return statements.map(()=>({meta:{changes:1}}));}};
 const env={DB,TOM_ENABLED:'true',TOM_PROVIDER:'openai',TOM_MODELS:'gpt-5.4-mini',TOM_MAX_OUTPUT_TOKENS:'800',OPENAI_API_KEY:'synthetic-only'},original=globalThis.fetch;
 const stream=()=>{let sent=false;return new Response(new ReadableStream({pull(c){if(!sent){sent=true;c.enqueue(new TextEncoder().encode('data: '+JSON.stringify({type:'response.output_text.delta',delta:'Answer.'})+'\n\n'));}else{c.enqueue(new TextEncoder().encode('data: '+JSON.stringify({type:'response.completed',response:{status:'completed'}})+'\n\n'));c.close();}}}),{headers:{'Content-Type':'text/event-stream'}});};
 globalThis.fetch=async(url,init)=>{calls.push(JSON.parse(init.body));const reply=replies[Math.min(calls.length-1,replies.length-1)];if(reply==='stream')return stream();if(reply==='network')throw new TypeError('fetch failed');return Response.json({error:{code:reply.code,message:'synthetic provider message'}},{status:reply.status,headers:{'Retry-After':'0.01'}});};
 try{await processTom(env,id);}finally{globalThis.fetch=original;}return {row,events,calls};
}

test('busy or briefly failing providers are retried before anything streams',async()=>{
 for(const first of [{status:503,code:'server_error'},{status:429,code:'rate_limit_exceeded'},{status:500,code:''},'network']){
  const r=await run([first,'stream']);assert.equal(r.row.status,'completed',JSON.stringify(first));assert.equal(r.row.text,'Answer.');assert.equal(r.calls.length,2);assert.deepEqual(r.calls[0],r.calls[1]);
 }
 const twice=await run([{status:502,code:''},{status:503,code:''},'stream']);assert.equal(twice.row.status,'completed');assert.equal(twice.calls.length,3);
});

test('retries stop after two, and quota or client errors are not retried; the provider message is never kept',async()=>{
 const always=await run([{status:503,code:'server_error'}]);assert.equal(always.row.status,'interrupted');assert.equal(always.calls.length,3);assert.match(always.row.error,/HTTP 503 server_error/);assert.doesNotMatch(always.row.error,/synthetic provider message/);
 for(const [reply,calls] of [[{status:429,code:'insufficient_quota'},1],[{status:400,code:'invalid_request_error'},1],[{status:401,code:'invalid_api_key'},1]]){const r=await run([reply,'stream']);assert.equal(r.row.status,'interrupted');assert.equal(r.calls.length,calls);assert.match(r.row.error,new RegExp('HTTP '+reply.status));}
});

test('reading handwriting asks for no reasoning; answers keep their payload',()=>{
 const png='data:image/png;base64,iVBORw0KGgo=',read=providerPayload({purpose:'transcribe',model:'gpt-5.4-mini',question:'',history:[],handwriting_image:png,max_output_tokens:800});assert.deepEqual(read.reasoning,{effort:'none'});
 assert.deepEqual(providerPayload({purpose:'transcribe',model:'gpt-5-mini',question:'',history:[],handwriting_image:png,max_output_tokens:800}).reasoning,{effort:'minimal'});
 assert.equal(providerPayload({purpose:'transcribe',model:'gpt-4.1-mini',question:'',history:[],handwriting_image:png,max_output_tokens:800}).reasoning,undefined);
 assert.equal(providerPayload({purpose:'answer',model:'gpt-5.4-mini',question:'Q',history:[],max_output_tokens:800}).reasoning,undefined);
});
