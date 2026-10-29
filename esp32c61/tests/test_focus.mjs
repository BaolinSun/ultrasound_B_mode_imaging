import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
import {readFileSync} from 'node:fs';
import {createHash} from 'node:crypto';
const require=createRequire(import.meta.url),P=require('../main/parameters_core.js'),F=require('../main/focus.js');
const html=readFileSync(new URL('../main/parameters.html',import.meta.url),'utf8');
function fixture(){const profiles=[],tables={};for(const depth of F.depths){
 const source=JSON.parse(readFileSync(new URL(`../configs/delay_profile_focus_${depth}mm.json`,import.meta.url)));
 const words=source.bram.delay_profile.map(Number),raw=Buffer.alloc(65536);words.forEach((w,i)=>raw.writeUInt32LE(w,i*4));
 const sha256=createHash('sha256').update(raw).digest('hex'),entry={focus_mm:depth,words:16384,source:`delay_profile_focus_${depth}mm.json`,crc32:F.crcWords(words),sha256,url:`/focus/${depth}-${sha256.slice(0,12)}.json`};profiles.push(entry);tables[entry.url]={format_version:1,bram:{delay_profile:words}};
 }return {catalog:{version:1,profiles},tables};}
test('all sixteen presets preserve exact input order and generate only delay writes',()=>{
 const f=fixture(),catalog=F.validateCatalog(f.catalog);
 for(const d of F.depths){const entry=catalog.get(d),profile=f.tables[entry.url],commands=F.prepare(P,entry,profile);
  assert.equal(commands.length,2048);assert.equal(commands.at(-1).split(' ')[3],'16376');assert.ok(commands.every(c=>c.startsWith('BRAM WR delay_profile ')&&c.length<=159));
  assert.deepEqual(commands.flatMap(c=>c.split(' ').slice(5).map(Number)),profile.bram.delay_profile);
 }
 assert.equal(F.crcWords([]),'00000000');assert.equal(F.crcWords([0]),'2144df1c');
});
test('invalid catalogs, corrupt/wrong-length/mixed tables are rejected before transaction',()=>{
 const f=fixture();for(const mutate of [c=>c.version=2,c=>c.profiles.pop(),c=>c.profiles[0].url='https://other/profile.json',c=>c.profiles[0].words=3,c=>c.profiles[0].focus_mm=80,c=>c.profiles[0].crc32='bad']){const c=structuredClone(f.catalog);mutate(c);assert.throws(()=>F.validateCatalog(c))}
 const e=f.catalog.profiles[0],original=f.tables[e.url];
 for(const mutate of [p=>p.bram.delay_profile.pop(),p=>p.bram.delay_profile[1]^=1,p=>p.bram.t_timing=[6144,0,0,0,0,0,0,0,6144,0],p=>p.afe5832={vca:{}},p=>p.commands=['HV ENABLE']]){const p=structuredClone(original);mutate(p);assert.throws(()=>F.prepare(P,e,p))}
});
function ui(){
 const f=fixture(),nodes={};for(const m of html.matchAll(/id="([^"]+)"/g))nodes[m[1]]={value:m[1]==='focus-depth'?'80':'',textContent:'',disabled:false,listeners:{},attrs:{},setAttribute(k,v){this.attrs[k]=v},classList:{toggle(){}},addEventListener(k,v){this.listeners[k]=v}};
 const b={ready:true,loading:false,paths:[],applications:[],drafts:[],hold:null,httpError:false,corrupt:false,success:true};
 const panel=new F.Panel({el:id=>nodes[id],parameters:P,canApply:()=>b.ready&&!b.loading,
  setLoading:value=>{b.loading=value},onDraft:p=>b.drafts.push(p),apply:async(c,d)=>{b.applications.push({c,d});if(b.hold)await b.hold;return b.success},
  fetch:async path=>{b.paths.push(path);if(b.httpError)return {ok:false,status:500};if(b.hold&&path!=='/focus/catalog.json')await b.hold;const p=structuredClone(path==='/focus/catalog.json'?f.catalog:f.tables[path]);if(b.corrupt&&path!=='/focus/catalog.json')p.bram.delay_profile[0]^=1;return {ok:true,text:async()=>JSON.stringify(p)}}});
 return {p:panel,e:nodes,b,f};
}
test('selection is read-only; range, bounds, aliases and unavailable gates stay explicit',async()=>{
 const t=ui();await t.p.start();assert.deepEqual(t.b.paths,['/focus/catalog.json']);assert.equal(t.e['focus-value'].textContent,'80');assert.equal(t.e['focus-apply'].disabled,false);
 t.p.select(60);assert.match(t.e['focus-duplicate'].textContent,/70 mm/);t.p.select(100);assert.match(t.e['focus-duplicate'].textContent,/110 mm/);
 t.p.select(20);assert.equal(t.e['focus-less'].disabled,true);t.e['focus-more'].listeners.click();assert.equal(t.e['focus-value'].textContent,'30');
 t.p.select(170);assert.equal(t.e['focus-more'].disabled,true);assert.equal(t.b.applications.length,0);assert.equal(t.b.paths.length,1);
 t.b.ready=false;t.p.render();assert.equal(t.e['focus-apply'].disabled,true);await t.p.perform('apply');assert.equal(t.b.paths.length,1);
 assert.match(html,/id="focus-depth" type="range" min="20" max="170" step="10"/);
});
test('apply snapshots selection, blocks duplicates and records success only on confirmation',async()=>{
 const t=ui();await t.p.start();let release;t.b.hold=new Promise(r=>release=r);const first=t.p.perform('apply');
 assert.equal(t.b.loading,true);assert.equal(t.e['focus-apply'].disabled,true);t.p.select(170);assert.equal(t.e['focus-depth'].value,'80');await t.p.perform('apply');assert.equal(t.b.paths.length,2);
 t.b.hold=null;release();await first;assert.equal(t.b.applications.length,1);assert.equal(t.b.applications[0].d,80);assert.equal(t.b.applications[0].c.length,2048);assert.match(t.e['focus-applied'].textContent,/80 mm.*ACK/);
 t.b.success=false;t.p.select(120);await t.p.perform('apply');assert.match(t.e['focus-message'].textContent,/未确认/);assert.match(t.e['focus-applied'].textContent,/80 mm/);
});
test('HTTP/corruption/cancellation cannot start writes; loading failures unlock page',async()=>{
 for(const kind of ['http','crc','cancel']){const t=ui();await t.p.start();if(kind==='http')t.b.httpError=true;if(kind==='crc')t.b.corrupt=true;
  let release;if(kind==='cancel')t.b.hold=new Promise(r=>release=r);const task=t.p.perform('apply');if(kind==='cancel'){t.p.cancelPending();t.b.hold=null;release()}await task;
  assert.equal(t.b.applications.length,0);assert.equal(t.b.loading,false);assert.equal(t.p.busy,false);assert.match(t.e['focus-message'].textContent,/失败|CRC|取消/);
 }
 const t=ui();t.b.httpError=true;await t.p.start();assert.equal(t.e['focus-apply'].disabled,true);t.b.httpError=false;await t.p.start();assert.equal(t.e['focus-apply'].disabled,false);
});
test('draft loading never applies hardware and complete protected transaction has no restart',async()=>{
 const t=ui();await t.p.start();await t.p.perform('draft');assert.equal(t.b.drafts.length,1);assert.equal(t.b.applications.length,0);
 const posts=[];let current;const tx=new P.Transaction(async(path,body)=>{
  if(body){posts.push(body);current=body.commands;return {job_id:'12345678'}}
  return {busy:false,state:'succeeded',count:current.length,completed:current.length,results:current.map(command=>({command,response:command==='STATUS'?'@STATUS cfg=0 run=0 hv=0':'@ACK 12 OK'}))};
 },{random:()=>12,delay:async()=>{}});
 const e=t.f.catalog.profiles[0];await tx.run(F.prepare(P,e,t.f.tables[e.url]));
 assert.deepEqual(posts[0].commands,['CFG BEGIN 12']);assert.deepEqual(posts.at(-1).commands,['CFG END','STATUS']);assert.equal(posts.length,258);
 assert.ok(posts.every(p=>p.commands.length<=8&&JSON.stringify(p).length<2048));assert.ok(!posts.flatMap(p=>p.commands).some(c=>/HV ENABLE|BEAM START|t_timing|GAIN SET/.test(c)));
});
