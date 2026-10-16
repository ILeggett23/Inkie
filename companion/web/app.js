'use strict';
const $ = (id) => document.getElementById(id);
const state = {me:null, devices:[], jobs:[], uploads:new Map(), conversation:crypto.randomUUID(), turns:[], active:null, events:null, ink:false,sending:false,refreshing:null,refreshAccount:null,deliveryJobs:null,uploadingBatch:false};
let toastTimer;
function notify(message, error=false) {
  $('message').textContent=message; $('message').classList.toggle('error',error); $('message').hidden=false;
  clearTimeout(toastTimer); toastTimer=setTimeout(()=>{$('message').hidden=true;},error?12000:6000);
}
async function api(path, {method='GET', data, raw, headers={}, signal}={}) {
  if(signal?.aborted)throw new DOMException('Canceled','AbortError');
  if(method!=='GET') headers['X-CSRF-Token']=state.me?.csrf||'';
  if(data!==undefined) headers['Content-Type']='application/json';
  const controller=new AbortController();let timedOut=false;const cancel=()=>controller.abort();signal?.addEventListener('abort',cancel,{once:true});const timer=setTimeout(()=>{timedOut=true;controller.abort();},35000);
  try{const response=await fetch(path,{method,headers,body:raw??(data!==undefined?JSON.stringify(data):undefined),signal:controller.signal,credentials:'same-origin'});const result=await response.json();if(!response.ok){const error=new Error(result.error||'Request failed');error.status=response.status;error.code=result.code;throw error;}return result;
  }catch(e){if(timedOut)throw new Error('Request timed out. Your draft is kept; recover the reply or retry the upload when connected.');throw e;}finally{clearTimeout(timer);signal?.removeEventListener('abort',cancel);}
}

function element(tag,text,className) {const el=document.createElement(tag); if(text!==undefined) el.textContent=text; if(className) el.className=className; return el;}
function action(text,run,className='quiet') {const b=element('button',text,className);b.type='button';b.addEventListener('click',()=>Promise.resolve(run()).catch(e=>notify(e.message,true)));return b;}
function bytes(n){return n<1024?`${n} bytes`:n<1048576?`${Math.round(n/1024)} KiB`:`${(n/1048576).toFixed(1)} MiB`;}
function ago(t){if(!t)return 'Not seen yet';const s=(Date.now()/1000)-t;return s<90?'Online recently':s<3600?`${Math.floor(s/60)} min ago`:`${Math.floor(s/3600)} h ago`;}
function page(name) {document.querySelectorAll('.page').forEach(el=>el.hidden=el.id!==name);document.querySelectorAll('[data-page]').forEach(el=>el.setAttribute('aria-current',el.dataset.page===name?'page':'false'));if(name==='tom'&&!state.active&&!state.sending&&state.latest&&!state.latestShown)recoverLatest().catch(e=>notify(e.message,true));}
document.querySelectorAll('[data-page]').forEach(button=>button.addEventListener('click',()=>page(button.dataset.page)));
async function enter(){
  state.me=await api('/v1/me');$('signin').hidden=true;$('workspace').hidden=false;$('signout').hidden=false;$('account-label').textContent=state.me.email;
  $('model').replaceChildren(...state.me.tom_models.map(model=>{const opt=element('option',model);opt.value=model;return opt;}));
  const provider=state.me.tom_provider?.label||'the configured AI provider';
  $('tom-config').textContent=state.me.tom_configured?`Tom uses ${provider}. Up to ${state.me.tom_daily_limit} requests per account per day; responses are capped at ${state.me.tom_max_output_tokens||2048} tokens. Handwriting interpretation also uses a request.`:'Tom is not configured. Your administrator must configure a server-side AI provider and authorize usage. Drafts remain available; no request will be sent.';
  $('tom-consent-text').textContent=`For this send, share this question and any handwriting above with ${provider}. Usage may incur charges. No document context is attached here.`;
  $('tom-provider-info').textContent=state.me.tom_provider?.id==='workers-ai'?'Cloudflare Workers AI runs the allowed model through the private service binding. Usage belongs to the service Cloudflare account. No OpenAI API key is required.':`Provider: ${provider}. Provider credentials and authorized usage are configured only on the server.`;
  $('tom-send').disabled=!state.me?.tom_configured;$('transcribe').disabled=!state.me?.tom_configured;
  $('service-config').replaceChildren(element('p',`Signed in as ${state.me.email}`),element('p',`Protocol v${state.me.protocol} · Max file ${bytes(state.me.max_upload_bytes)}`),element('p',`Tom: ${state.me.tom_configured?'configured':'unconfigured'} · ${state.me.tom_daily_limit} requests/day`));
  $('question').value=localStorage.getItem('inkbound-draft:'+state.me.email)||'';loadLatest();
  await refresh();
}
$('login-form').addEventListener('submit',async event=>{event.preventDefault();const button=event.submitter;button.disabled=true;try{await api('/v1/auth/login',{method:'POST',data:{email:$('email').value,password:$('password').value}});$('password').value='';await enter();}catch(e){notify(e.message,true);}finally{button.disabled=false;}});
$('signout').addEventListener('click',async()=>{try{if(state.sending||state.recovering||state.active){notify('Stop the active Tom request before signing out.',true);return;}await api('/v1/auth/logout',{method:'POST',data:{}});state.me=null;state.latest=null;state.latestShown=false;state.devices=[];state.jobs=[];state.deliveryJobs=null;state.turns=[];state.conversation=crypto.randomUUID();state.events?.close();$('messages').replaceChildren();$('local-uploads').replaceChildren();$('workspace').hidden=true;$('signin').hidden=false;$('signout').hidden=true;$('account-label').textContent='Your books, within reach.';}catch(e){notify(e.message,true);}});
function renderDevices(){
  // Most recently seen reader first and selected by default: readers can share a name (a re-paired tablet), and
  // a remembered choice must not keep sending books to a reader that has stopped connecting.
  const active=state.devices.filter(d=>!d.revoked&&d.expires>Date.now()/1000).sort((a,b)=>(b.last_seen||0)-(a.last_seen||0));
  const remembered=$('device-select').value||localStorage.getItem('inkbound-selected:'+state.me.email)||'';
  const stale=d=>!d||(active[0]&&(active[0].last_seen||0)-(d.last_seen||0)>3600);
  const selected=stale(active.find(d=>d.id===remembered))?(active[0]?.id||''):remembered;
  const duplicate=name=>active.filter(d=>d.name===name).length>1;
  $('device-select').replaceChildren(...active.map(d=>{const o=element('option',duplicate(d.name)?`${d.name} · ${ago(d.last_seen)}`:d.name);o.value=d.id;return o;}));
  if(!active.length){const o=element('option','Pair a reader first');o.value='';$('device-select').append(o);}
  if(active.some(d=>d.id===selected))$('device-select').value=selected;
  $('files').disabled=!active.length||state.uploadingBatch;
  showSelectedDevice();
  $('device-list').replaceChildren();
  if(!state.devices.length)$('device-list').append(element('p','No readers paired yet.','empty'));
  for(const d of state.devices){const card=element('div',undefined,'device');card.append(element('strong',d.name),element('p',d.revoked?'Revoked':`Last connection: ${ago(d.last_seen)} · Credential expires ${new Date(d.expires*1000).toLocaleDateString()}`));if(!d.revoked)card.append(action('Revoke reader',async()=>{if(!confirm(`Revoke ${d.name}? Pending uploads will be canceled. Imported documents stay on the reader.`))return;await api('/v1/devices/'+d.id,{method:'DELETE'});await refresh();},'quiet danger'));$('device-list').append(card);}
}
function showSelectedDevice(){const d=state.devices.find(d=>d.id===$('device-select').value);$('device-status').textContent=d?`${d.name} · ${ago(d.last_seen)}. Delivery waits until your reader is awake and connected.`:'A reader must be paired before uploading.';}
$('device-select').addEventListener('change',()=>{localStorage.setItem('inkbound-selected:'+state.me.email,$('device-select').value);showSelectedDevice();});
$('pair-form').addEventListener('submit',async event=>{event.preventDefault();try{const result=await api('/v1/pair/claim',{method:'POST',data:{code:$('pair-code').value.replace(/\s/g,'')}});$('pair-result').textContent=`${result.name}: approve ${state.me.email} on your reader now. This page will update after approval.`;$('pair-code').value='';}catch(e){notify(e.message,true);}});
function renderJobs(){
  const jobs=state.deliveryJobs?state.jobs.filter(j=>state.deliveryJobs.has(j.job_id)):state.jobs.slice(0,1);
  $('jobs').replaceChildren();$('queue-empty').hidden=jobs.length>0;
  for(const job of jobs){const card=element('div',undefined,'job');const body=element('div');const device=state.devices.find(d=>d.id===job.device_id);body.append(element('strong',job.filename),element('span',`${device?.name||'Reader'} · ${bytes(job.source_size)}${job.converted?' · Converted to EPUB':''}`,'meta'));
    const progress=element('progress');progress.max=job.source_size;progress.value=job.received;progress.setAttribute('aria-label',`Upload progress for ${job.filename}`);if(job.status==='uploading')body.append(progress);
    body.append(element('span',`${job.status}${job.error?' · '+job.error:''}`,`status ${job.status==='failed'?'failed':''}`));card.append(body);
    if(!['imported','canceled','expired','deleted'].includes(job.status))card.append(action('Cancel',async()=>{if(!confirm('Cancel this pending relay upload? This does not delete imported reader content.'))return;state.uploads.get(job.job_id)?.controller.abort();await api('/v1/jobs/'+job.job_id,{method:'DELETE'});await refresh();}));
    if(job.status==='failed')body.append(action('Retry from original file',()=>{notify('Choose the same file again to retry or resume securely.');$('files').click();}));
    $('jobs').append(card);
  }
}
async function currentDeliveries(){const ids=state.deliveryJobs?[...state.deliveryJobs]:null;if(ids&&ids.length===0)return {jobs:[]};if(!ids)return api('/v1/jobs?limit=1');const pages=[];for(let i=0;i<ids.length;i+=32)pages.push(await api('/v1/jobs?ids='+encodeURIComponent(ids.slice(i,i+32).join(','))));return {jobs:pages.flatMap(p=>p.jobs)};}
async function refresh(){const account=state.me;if(!account)return;if(state.refreshing&&state.refreshAccount===account)return state.refreshing;
  const operation=(async()=>{try{const [devices,jobs]=await Promise.all([api('/v1/devices'),currentDeliveries()]);if(state.me!==account)return;const changedDevices=state.renderedAccount!==account||JSON.stringify(state.devices)!==JSON.stringify(devices.devices),changedJobs=state.renderedAccount!==account||JSON.stringify(state.jobs)!==JSON.stringify(jobs.jobs);state.renderedAccount=account;state.devices=devices.devices;state.jobs=jobs.jobs;if(changedDevices)renderDevices();if(changedJobs)renderJobs();$('connection').textContent='Connected';$('connection').classList.remove('offline');}catch(e){if(state.me===account){$('connection').textContent='Offline';$('connection').classList.add('offline');}throw e;}})();state.refreshing=operation;state.refreshAccount=account;try{return await operation;}finally{if(state.refreshing===operation){state.refreshing=null;state.refreshAccount=null;}}}

$('refresh').addEventListener('click',()=>refresh().catch(e=>notify(e.message,true)));
function checksumFile(file,signal,onProgress){return new Promise((resolve,reject)=>{const worker=new Worker('/hash-worker.js',{type:'module'});let finished=false;const done=(error,sha)=>{if(finished)return;finished=true;signal.removeEventListener('abort',abort);worker.terminate();error?reject(error):resolve(sha);};const abort=()=>done(new DOMException('Canceled','AbortError'));signal.addEventListener('abort',abort,{once:true});worker.onerror=()=>done(new Error('Could not checksum this file'));worker.onmessage=({data})=>{if(typeof data?.sha256==='string'&&/^[a-f0-9]{64}$/.test(data.sha256))done(null,data.sha256);else if(data?.error)done(new Error('Could not checksum this file'));else if(Number.isSafeInteger(data?.bytes)&&data.bytes>=0&&data.bytes<=file.size)onProgress(data.bytes);};if(signal.aborted)abort();else worker.postMessage({file});});}
async function upload(file){
  const device=$('device-select').value; if(!device)throw new Error('Pair and select a reader first.');
  const maxBytes=state.me.max_upload_bytes;
  if(!Number.isSafeInteger(file.size)||file.size<=0||file.size>maxBytes)throw new Error(`${file.name}: file exceeds this service’s supported size or is empty.`);
  const row=element('div',undefined,'local-upload');const title=element('strong',file.name);const status=element('p','Preparing checksum…','hint');const bar=element('progress');bar.max=file.size;bar.value=0;bar.setAttribute('aria-label',file.name+' upload progress');const controller=new AbortController();let jobId;
  const cancel=action('Cancel',async()=>{controller.abort();if(jobId)await api('/v1/jobs/'+jobId,{method:'DELETE'});status.textContent='Canceled';await refresh();});row.append(title,bar,status,cancel);$('local-uploads').append(row);
  try{
    const hash=await checksumFile(file,controller.signal,done=>{status.textContent=`Preparing checksum ${Math.round(done/file.size*100)}%`;bar.value=done;});bar.value=0;
    if(controller.signal.aborted)throw new DOMException('Canceled','AbortError');
    let job=await api('/v1/jobs',{method:'POST',data:{upload_id:crypto.randomUUID(),device_id:device,filename:file.name,size:file.size,sha256:hash,new_import:$('new-import').checked},signal:controller.signal});jobId=job.job_id;state.deliveryJobs?.add(jobId);state.uploads.set(jobId,{controller});
    if(job.status==='failed'){await api(`/v1/jobs/${jobId}/retry`,{method:'POST',data:{}});job={...job,status:'uploading',received:0};}
    if(job.status!=='uploading'){status.textContent=`Already ${job.status}; duplicate avoided.`;bar.value=file.size;return;}
    let offset=job.received;bar.value=offset;
    while(offset<file.size){const end=Math.min(offset+state.me.chunk_bytes,file.size);status.textContent=`Uploading ${Math.round(offset/file.size*100)}%${offset?' · resumable':''}`;
      const result=await api(`/v1/jobs/${jobId}/chunk`,{method:'PUT',raw:file.slice(offset,end),headers:{'Content-Type':'application/octet-stream','Content-Range':`bytes ${offset}-${end-1}/${file.size}`},signal:controller.signal});offset=result.received;bar.value=offset;
    }
    await api(`/v1/jobs/${jobId}/complete`,{method:'POST',data:{},signal:controller.signal});status.textContent='Queued for validation. Delivery is confirmed only after the reader saves it.';
  }catch(e){status.textContent=e.name==='AbortError'?'Canceled or paused; check Deliveries.':`${e.message}. Select the same file to resume; nothing will be sent automatically.`;if(e.name!=='AbortError')notify(file.name+': '+e.message,true);}
  finally{cancel.disabled=true;if(jobId)state.uploads.delete(jobId);await refresh().catch(()=>{});}
}
async function uploadFiles(files){if(state.uploadingBatch){notify('Wait for the current upload to finish.',true);return;}state.uploadingBatch=true;state.deliveryJobs=new Set();renderJobs();try{for(const file of Array.from(files))await upload(file).catch(e=>notify(e.message,true));}finally{state.uploadingBatch=false;$('files').value='';renderDevices();}}
$('files').addEventListener('change',e=>uploadFiles(e.target.files));
for(const event of ['dragenter','dragover'])$('drop-zone').addEventListener(event,e=>{e.preventDefault();$('drop-zone').classList.add('drag');});
for(const event of ['dragleave','drop'])$('drop-zone').addEventListener(event,e=>{e.preventDefault();$('drop-zone').classList.remove('drag');});
$('drop-zone').addEventListener('drop',e=>uploadFiles(e.dataTransfer.files));
// Handwritten input is a browser fallback. The reader has its own preserved pen path.
const canvas=$('handwriting'),ctx=canvas.getContext('2d');let drawing=false,last;
function point(e){const r=canvas.getBoundingClientRect();return [(e.clientX-r.left)*canvas.width/r.width,(e.clientY-r.top)*canvas.height/r.height];}
canvas.addEventListener('pointerdown',e=>{drawing=true;last=point(e);canvas.setPointerCapture(e.pointerId);});
canvas.addEventListener('pointermove',e=>{if(!drawing)return;const next=point(e);ctx.strokeStyle='#222922';ctx.lineWidth=2.5;ctx.lineCap='round';ctx.beginPath();ctx.moveTo(...last);ctx.lineTo(...next);ctx.stroke();last=next;state.ink=true;});
for(const event of ['pointerup','pointercancel'])canvas.addEventListener(event,()=>drawing=false);
$('clear-handwriting').addEventListener('click',()=>{ctx.clearRect(0,0,canvas.width,canvas.height);state.ink=false;});
$('question').addEventListener('input',()=>{if(state.me)localStorage.setItem('inkbound-draft:'+state.me.email,$('question').value);});
function addMessage(role,text,ink){const item=element('article',undefined,`message ${role==='Tom'?'answer':''}`);item.append(element('header',role));if(ink){const img=element('img');img.src=ink;img.alt='Original handwritten question';item.append(img);}const content=element('div',text);item.append(content);const status=element('span','','state');item.append(status);$('messages').append(item);return {item,content,status};}
function stopWatching(){state.events?.close();state.events=null;state.active=null;$('tom-stop').disabled=true;$('tom-send').disabled=!state.me?.tom_configured;$('transcribe').disabled=!state.me?.tom_configured;}
function watch(requestId,answer,purpose,payload){const account=state.me,current=()=>state.me===account&&state.active===requestId;let last=0;state.active=requestId;$('tom-stop').disabled=false;$('tom-send').disabled=true;$('transcribe').disabled=true;
  const source=new EventSource(`/v1/tom/requests/${requestId}/events?after=0`);state.events=source;
  source.addEventListener('delta',e=>{if(!current())return;const seq=Number(e.lastEventId);if(seq<=last)return;last=seq;answer.content.textContent+=JSON.parse(e.data).text;$('messages').scrollTop=$('messages').scrollHeight;});
  source.addEventListener('status',async e=>{if(!current())return;const seq=Number(e.lastEventId);if(seq<=last)return;last=seq;const status=JSON.parse(e.data).status;answer.status.textContent=status;if(!['queued','streaming'].includes(status)){stopWatching();state.turns=[{payload,text:answer.content.textContent,status,request_id:requestId}];if(purpose==='transcribe'&&status==='completed'){$('question').value=answer.content.textContent;$('question').dispatchEvent(new Event('input'));notify('Review the interpretation, correct any unclear words, then Send to ask Tom.');}}});
  source.onerror=async()=>{source.close();if(!current())return;try{const result=await api('/v1/tom/requests/'+requestId);if(!current())return;answer.status.textContent=result.status+(result.error?' · '+result.error:'');if(['queued','streaming'].includes(result.status)){// Keep displayed text at its event cursor; applying a newer snapshot here would duplicate replayed deltas.
        const resumed=new EventSource(`/v1/tom/requests/${requestId}/events?after=${last}`);state.events=resumed;
        resumed.addEventListener('delta',e=>{if(!current())return;if(Number(e.lastEventId)>last){last=Number(e.lastEventId);answer.content.textContent+=JSON.parse(e.data).text;}});
        resumed.addEventListener('status',e=>{if(!current())return;last=Math.max(last,Number(e.lastEventId));const value=JSON.parse(e.data).status;answer.status.textContent=value;if(!['queued','streaming'].includes(value))stopWatching();});
        resumed.onerror=()=>{resumed.close();if(!current())return;answer.status.textContent='Connection interrupted. Recover this reply when connected; nothing was resubmitted.';stopWatching();offerRecovery(answer);};
      }else {answer.content.textContent=result.text;stopWatching();}}catch{if(!current())return;answer.status.textContent='Offline. Partial output is retained. Recover this reply when connected; nothing was resubmitted.';stopWatching();offerRecovery(answer);}};
}
async function sendTom(purpose='answer'){
  if(state.sending||state.recovering||state.active)return;state.sending=true;$('tom-send').disabled=true;$('transcribe').disabled=true;try{
  if(!navigator.onLine)throw new Error('Offline. Your draft remains here; reconnect and press Send deliberately.');
  if(!$('consent').checked)throw new Error('Review the disclosure and give consent for this send.');
  if(!state.me?.tom_configured)throw new Error('Tom is not configured on this server.');
  if(purpose==='transcribe'&&!state.ink)throw new Error('Handwrite a question first.');
  const history=[];
  const payload={request_id:crypto.randomUUID(),conversation_id:state.conversation,model:$('model').value,provider:state.me.tom_provider.id,max_output_tokens:state.me.tom_max_output_tokens,question:$('question').value,scope:'general',context:null,history,consent:true,purpose};
  if(state.ink)payload.handwriting_image=canvas.toDataURL('image/png');
  rememberLatest(payload);const result=await api('/v1/tom/requests',{method:'POST',data:payload});state.latestShown=true;
  $('messages').replaceChildren();addMessage('You',payload.question,payload.handwriting_image);const answer=addMessage('Tom','');answer.status.textContent=result.status;watch(result.request_id,answer,purpose,payload);$('consent').checked=false;
  if(purpose==='answer'){$('question').value='';localStorage.removeItem('inkbound-draft:'+state.me.email);ctx.clearRect(0,0,canvas.width,canvas.height);state.ink=false;}
  }finally{state.sending=false;if(!state.active){$('tom-send').disabled=!state.me?.tom_configured;$('transcribe').disabled=!state.me?.tom_configured;}}
}
$('tom-form').addEventListener('submit',e=>{e.preventDefault();sendTom().catch(e=>notify(e.message,true));});
$('transcribe').addEventListener('click',()=>sendTom('transcribe').catch(e=>notify(e.message,true)));
$('tom-stop').addEventListener('click',async()=>{try{if(state.active)await api(`/v1/tom/requests/${state.active}/cancel`,{method:'POST',data:{}});notify('Stop requested. Any partial answer is retained. Provider charges already incurred may still apply.');}catch(e){notify(e.message,true);}});
$('new-conversation').addEventListener('click',()=>{if(state.active||state.sending||state.recovering){notify('Stop the active request first.',true);return;}clearLatest();state.conversation=crypto.randomUUID();state.turns=[];$('messages').replaceChildren(element('p','A fresh page for your next question.','empty'));});
$('delete-conversation').addEventListener('click',async()=>{if(state.active||state.sending||state.recovering){notify('Stop the active request first.',true);return;}if(!confirm('Permanently delete this companion conversation and its stored questions/answers?'))return;try{await api('/v1/tom/conversations/'+state.conversation,{method:'DELETE'});$('new-conversation').click();}catch(e){notify(e.message,true);}});
window.addEventListener('offline',()=>{$('connection').textContent='Offline';$('connection').classList.add('offline');});
window.addEventListener('online',()=>{notify('Connection restored. Drafts and failed uploads require an explicit send or retry.');refresh().catch(()=>{});});
setInterval(()=>{if(state.me&&document.visibilityState==='visible')refresh().catch(()=>{});},5000);
const configuredService=window.INKBOUND_SERVICE_ORIGIN||'';
const staticHosting=location.hostname.endsWith('.github.io')||(configuredService&&configuredService!==location.origin);
if(staticHosting){
  $('signin').hidden=true;$('service-needed').hidden=false;
  $('service-needed-text').textContent='This GitHub Pages interface is published, but private uploads, sign-in, delivery, conversions, and Tom require a separately deployed companion service. GitHub Pages cannot run these APIs or store provider secrets.';
  try{const url=new URL(configuredService);if(url.protocol==='https:'&&url.origin===configuredService){$('service-link').href=url.origin;$('service-link').hidden=false;$('service-needed-text').textContent+=' Open the secure service to sign in on its own origin. This keeps sessions reliable on phones without storing access tokens in browser storage.';}}catch{}
}else enter().catch(e=>{if(e.status!==401)notify('Companion unavailable: '+e.message,true);});

function latestKey(){return 'inkbound-latest-request:'+state.me.email;}
function validLatest(value){const uuid=/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/;return value&&uuid.test(value.request_id)&&uuid.test(value.conversation_id)&&typeof value.question==='string'&&value.question.length<=8000&&['answer','transcribe'].includes(value.purpose);}
function loadLatest(){state.latest=null;state.latestShown=false;try{const value=JSON.parse(localStorage.getItem(latestKey()));if(validLatest(value))state.latest=value;}catch{}}
function rememberLatest(payload){state.latest={request_id:payload.request_id,conversation_id:payload.conversation_id,question:payload.question,purpose:payload.purpose};state.latestShown=false;try{localStorage.setItem(latestKey(),JSON.stringify(state.latest));}catch{}}
function clearLatest(){state.latest=null;state.latestShown=false;try{localStorage.removeItem(latestKey());}catch{}}
function offerRecovery(answer){if(!answer.item.querySelector('.recover-reply'))answer.item.append(action('Recover reply',()=>recoverLatest(),'quiet recover-reply'));}
async function recoverLatest(){if(!state.me||!validLatest(state.latest)||state.active||state.sending||state.recovering)return;const account=state.me,latest=state.latest;state.recovering=true;state.latestShown=true;$('tom-send').disabled=true;$('transcribe').disabled=true;try{const result=await api('/v1/tom/requests/'+latest.request_id);if(state.me!==account||state.latest!==latest)return;state.conversation=latest.conversation_id;$('messages').replaceChildren();addMessage('You',latest.question);const answer=addMessage('Tom',result.text||'');answer.status.textContent=result.status+(result.error?' · '+result.error:'');if(['queued','streaming'].includes(result.status)){answer.content.textContent='';watch(latest.request_id,answer,latest.purpose,{...latest,scope:'general'});}}catch(e){state.latestShown=false;throw e;}finally{state.recovering=false;if(state.me===account&&!state.active){$('tom-send').disabled=!state.me?.tom_configured;$('transcribe').disabled=!state.me?.tom_configured;}}}
