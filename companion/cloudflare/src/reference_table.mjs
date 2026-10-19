/* Compile registered factual tables from their immutable records, never from generated replacement rows. */
import {requiredReferences} from './visual_references.mjs';
import catalog from './visual_reference_data.mjs';
const escape=s=>s.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');
const normalized=question=>String(question||'').replace(/[\u2018\u2019\u02bc]/g,"'").trim();
const plainColor=/\b(?:plain|black[\s-]+and[\s-]+white|monochrome|uncolou?red|without\s+colou?rs?|no\s+colou?rs?)\b/i;
const excluded=q=>/\b(?:not|never|don't|explain|why|how|subset|only|first|except|omit|without)\b|\d/i.test(q.replace(plainColor,''));
/* Scene version 5 readers get the table colored by element family (exact colors); a plain request keeps it black. */
const colored=(q,options)=>options.version===5&&!plainColor.test(q);
// The registered table most recently drawn or offered in this conversation: newest assistant turns first, at most three.
function historyReference(history){
 const said=(Array.isArray(history)?history:[]).filter(h=>h?.role==='assistant'&&typeof h.text==='string').slice(-3).reverse();
 for(const h of said){const t=h.text.toLowerCase().replace(/-/g,' ');for(const ref of catalog)if(ref.matrix&&(t.includes(ref.id.replace(/-/g,' '))||t.includes(ref.title.toLowerCase())||ref.aliases.some(a=>t.includes(a))))return ref;}
 return null;
}
// A short request to color (or recolor) the drawing just made, in words that name nothing else: "Add color", "Can you color
// it by family?", "Make it colorful". A confirmation ("yes", "go ahead") counts only when Inkie's last turn itself asked about
// coloring a registered table, so a "yes" to an unrelated question never redraws one.
const followUpWords=new Set('please can could would will you add use apply put give make it its color colors colour colours colored coloured colorful colourful colorize colourise color-coded colour-coded in with some the a to this that them table periodic elements element chart by family families category categories type types ok okay yes yeah sure great thanks thank now and again redraw draw show do also full'.split(' '));
const affirmation=/^(?:yes|yeah|yep|sure|ok|okay|please|please\s+do|do\s+it|go\s+ahead|yes\s+please|sounds\s+good|that\s+works|perfect)(?:\s*,?\s*(?:please|thanks|thank\s+you|do\s+it|go\s+ahead))?[.!\s]*$/i;
const colorWord=/\bcolou?r/i;
function colorFollowUp(q,history){
 const said=(Array.isArray(history)?history:[]).filter(h=>h?.role==='assistant'&&typeof h.text==='string');const last=said.length?said[said.length-1].text:'';
 if(affirmation.test(q))return colorWord.test(last)&&last.includes('?')&&historyReference([{role:'assistant',text:last}])!==null;
 const words=q.toLowerCase().replace(/[.,!?]/g,' ').split(/\s+/).filter(Boolean);
 return colorWord.test(q)&&words.length<=12&&!/^(?:what|which|why|how|who|when|where|is|are|does|was|were)\b/i.test(q)&&words.every(w=>followUpWords.has(w));
}
export function referenceTableReply(question,options={}){
 const q=normalized(question),refs=requiredReferences(q);
 if(!refs.length&&colored(q,options)&&colorFollowUp(q,options.history)){const ref=historyReference(options.history);if(ref)return {mode:'visual',response:'',visual:referenceGrid(ref,{colored:true})};}
 if(refs.length!==1||!refs[0].matrix||excluded(q))return null;
 const ref=refs[0],aliases=ref.aliases.map(escape).join('|');
 // Match the whole request so quoted actions, translations and capability questions
 // stay with the planner. Conversation prefixes are deliberately bounded.
 const greeting='(?:(?:hi|hello|hey)(?:\\s+tom)?[,.!]\\s*)?';
 const acknowledgment="(?:(?:(?:i'm|i\\s+am|in)\\s+(?:great|good|fine)|thanks|thank\\s+you)[.!]\\s*)?";
 const request="(?:please\\s+)?(?:(?:(?:can|could|would|will)\\s+you|(?:i\\s+)?wanted\\s+to\\s+see\\s+if\\s+you\\s+(?:can|could)|(?:i\\s+would|i'd)\\s+like\\s+you\\s+to|i\\s+want\\s+you\\s+to)\\s+(?:please\\s+)?)?";
 // "draw me out" and "draw out" both occur in handwriting; a whole-set suffix names the complete table, not a subset or styling.
 const verb='(?:draw|sketch|illustrate|show|display|render|make|create|generate|produce|give)(?:\\s+(?:me|us))?(?:\\s+out)?(?:\\s+(?:me|us))?';
 const whole='(?:\\s+of\\s+(?:all\\s+)?(?:the\\s+)?(?:chemical\\s+)?elements)?(?:\\s+(?:with|showing|including)\\s+all(?:\\s+of)?(?:\\s+the)?\\s+(?:chemical\\s+)?elements)?';
 // Color wording chooses the table's coloring, never its contents: "a colored periodic table", "... in color", "... in black and white".
 const shade='(?:(?:colou?red|colou?rful|colou?r[\\s-]coded|plain|black[\\s-]+and[\\s-]+white|monochrome|uncolou?red)\\s+)?';
 const family='(?:\\s+by\\s+(?:element\\s+)?(?:family|families|category|categories|type|types))?';
 const tint='(?:\\s*,?\\s+(?:in\\s+(?:full\\s+)?colou?rs?|with\\s+colou?rs?|colou?r(?:ed|[\\s-]coded)'+family+'|by\\s+(?:element\\s+)?(?:family|families|category|categories)|in\\s+black[\\s-]+and[\\s-]+white|without\\s+colou?rs?|with\\s+no\\s+colou?rs?))?';
 const v5=options.version===5,action=verb+'\\s+(?:(?:a|an|the)\\s+)?(?:(?:full|complete|standard|whole|entire)\\s+)?'+(v5?shade:'')+'(?:'+aliases+')'+whole+(v5?tint:'');
 const courtesy='(?:\\s+for\\s+me)?(?:\\s*,?\\s*(?:please|thanks|thank\\s+you))?[.!?\\s]*';
 if(!new RegExp('^'+greeting+acknowledgment+request+action+courtesy+'$','i').test(q))return null;
 return {mode:'visual',response:'',visual:referenceGrid(ref,{colored:colored(q,options)})};
}
// A planned reply that binds a dataset to every record of a registered table the reader's own question names, but cannot
// place them (the validator rejected it), asked for that whole table: the catalog supplies its facts and positions. The
// rejected reply is discarded, never edited; partial, negated or subset requests and unregistered data stay rejected.
// Scene version 5 also accepts the table drawn or offered in the last assistant turns ("Add color", "Yes" to an offer), so a
// follow-up that redraws the whole table is rescued the same way; it is colored unless asked plain.
export function referenceFallbackReply(raw,question,options={}){
 let reply;try{reply=JSON.parse(raw);}catch{return null;}
 const q=normalized(question);if(reply?.mode!=='visual'||!Array.isArray(reply.visual?.datasets)||excluded(q))return null;
 const refs=requiredReferences(q),recent=options.version===5?historyReference(options.history):null;if(recent&&!refs.includes(recent))refs.push(recent);
 for(const ref of refs){if(!ref.matrix)continue;const n=ref.records.length,keys=[new Set(ref.records.map(f=>String(f[ref.bindings.id]))),new Set(ref.records.map(f=>String(f[ref.bindings.label])))].filter(k=>k.size===n);
  // Every record exactly once, named by its registered id or its unique label (models use either: 1..118 or H..Og).
  const complete=rows=>rows.length===n&&['id','label'].some(field=>{const seen=new Set(rows.map(r=>String(r?.[field])));return seen.size===n&&keys.some(k=>[...seen].every(v=>k.has(v)));});
  for(const d of reply.visual.datasets){if(d?.source==='reference:'+ref.id&&Array.isArray(d.rows)&&complete(d.rows))return {mode:'visual',response:'',visual:referenceGrid(ref,{colored:colored(q,options)})};}}
 return null;
}
// Element families use IUPAC's collective names (Nomenclature of Inorganic Chemistry 2005, IR-3.5), worked out only from each
// record's own group, period and number: hydrogen is not an alkali metal, groups 3-12 are the transition elements, and the
// null-group bands are the lanthanoids (period 6) and actinoids (period 7). Groups 13 and 14 have no IUPAC collective name.
// Distinct mid-saturation colors, as the color e-ink panel shows them muted; they are drawn as highlighter fills under black text.
const elementFamilies=[['hydrogen','#bab0ac'],['alkali metals','#e15759'],['alkaline earth metals','#f28e2b'],['transition elements','#edc948'],['lanthanoids','#ff9da7'],['actinoids','#b07aa1'],['group 13','#9c755f'],['group 14','#b6992d'],['pnictogens','#59a14f'],['chalcogens','#499894'],['halogens','#4e79a7'],['noble gases','#a0cbe8']];
const elementFamily=f=>f.number===1?'hydrogen':f.group===null?(f.period===6?'lanthanoids':'actinoids'):f.group===1?'alkali metals':f.group===2?'alkaline earth metals':f.group<=12?'transition elements':['group 13','group 14','pnictogens','chalcogens','halogens','noble gases'][f.group-13];
const families={'chemical-elements-2022':{of:elementFamily,colors:elementFamilies}};
export function referenceGrid(ref,options={}){
 const coloring=options.colored?families[ref.id]:null;
 const m=ref.matrix,main=ref.records.filter(r=>r[m.column]!==null),bands=new Map();
 const columns=Math.max(...main.map(r=>r[m.column]))-m.origin+1,mainRows=Math.max(...main.map(r=>r[m.row]))-m.origin+1;
 for(const fact of ref.records)if(fact[m.column]===null){const band=bands.get(fact[m.row])||[];band.push(fact);bands.set(fact[m.row],band);}
 const position=new Map();let bandRow=mainRows+1;
 for(const key of [...bands.keys()].sort((a,b)=>a-b)){const band=bands.get(key).slice().sort((a,b)=>a[m.order]-b[m.order]);const start=Math.floor((columns-band.length)/2);band.forEach((f,i)=>position.set(String(f[ref.bindings.id]),[bandRow,start+i]));++bandRow;}
 const rows=ref.records.map(fact=>{const id=String(fact[ref.bindings.id]),p=fact[m.column]===null?position.get(id):[fact[m.row]-m.origin,fact[m.column]-m.origin];return {id,label:fact[ref.bindings.label],x:ref.bindings.x?fact[ref.bindings.x]:null,y:ref.bindings.y?fact[ref.bindings.y]:null,row:p[0],column:p[1],category:coloring?coloring.of(fact):''};});
 const rowCount=bands.size?bandRow:mainRows,width=columns*80,height=rowCount*90,id='reference-grid',style={tool:'fineliner',width:'medium',color:'black'};
 // The colored table carries its own key below the grid: a highlighter swatch per family with its name beside it, 3 per row.
 const used=coloring?coloring.colors.filter(([name])=>rows.some(r=>r.category===name)):[],legend=[],perRow=3,slot=width/perRow,top=height+30,line=64;
 used.forEach(([name,color],i)=>{const x=(i%perRow)*slot+16,y=top+Math.floor(i/perRow)*line,swatch='family-'+(i+1);
  legend.push({id:swatch,parent:id,kind:'path',bounds:[x,y,44,44],style:{tool:'highlighter',width:'thick',color},content:{points:[[x,y],[x+44,y],[x+44,y+44],[x,y+44],[x,y]],filled:true,holes:[]}});
  legend.push({id:swatch+'-name',parent:swatch,kind:'label',bounds:[x+58,y-4,slot-90,52],style,content:{text:name,size:30}});});
 const title=coloring?ref.title+', colored by element family':ref.title,canvas=[width,legend.length?top+Math.ceil(used.length/perRow)*line:height];
 return {version:2,category:'table',strategy:'native',title,canvas,palette:used.map(([category,color])=>({category,color})),objects:[{id,parent:'',kind:'grid',bounds:[0,0,width,height],style,content:{data:ref.id,rows:rowCount,columns,fields:[ref.bindings.x?'x':'id','label']}},...legend],relations:[],datasets:[{id:ref.id,source:'reference:'+ref.id,rows}],required:[id],image:null,plan:{subject:ref.title,detail:'standard',style:'precise native reference grid',components:[{id,name:ref.title,role:'part',features:['Complete registered records in their declared ordinal dimensions'],layer:1,depth:'midground',target:null,colors:[]}],spatial:[],mapKind:null,context:[],rubric:['Preserve every registered record and dimension','Legible unclipped native table',...(coloring?['Element families from IUPAC collective names with a legend']:[])]}};
}
