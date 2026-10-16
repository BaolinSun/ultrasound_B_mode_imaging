import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import vm from 'node:vm';

const html=readFileSync(new URL('../pc_tools/bmode_assets/viewer.html',import.meta.url),'utf8');
const script=readFileSync(new URL('../pc_tools/bmode_assets/viewer.js',import.meta.url),'utf8');
function setup(){
 const elements={};
 for(const match of html.matchAll(/\bid="([^"]+)"/g)){
  const classes=new Set();elements[match[1]]={hidden:false,disabled:false,textContent:'',listeners:{},
   classList:{toggle(name,on){if(on)classes.add(name);else classes.delete(name)},contains(name){return classes.has(name)}},
   addEventListener(name,fn){this.listeners[name]=fn},removeAttribute(name){delete this[name]}};
 }
 const backend={calls:[],revoked:[],seq:1,imageCalls:0,pending:null,offline:false,state:{state:'idle',device_ip:'192.168.3.27',port:50001,
  transport:null,image_available:false,image_age_s:null,lines:0}};
 const context=vm.createContext({document:{getElementById(id){assert.ok(elements[id],id);return elements[id]}},
  setTimeout(){},setInterval(){},URL:{createObjectURL(){return 'blob:test'},revokeObjectURL(url){backend.revoked.push(url)}},
  async fetch(path,options){
   backend.calls.push({path,options});if(backend.offline)throw Error('offline');
   if(path==='/api/status')return {ok:true,json:async()=>({...backend.state})};
   if(path==='/api/viewer/start'){backend.state.state='starting';return {ok:true,json:async()=>({accepted:true})}};
   if(path==='/api/viewer/stop'){backend.state.state='stopping';return {ok:true,json:async()=>({accepted:true})}};
   if(path.startsWith('/api/image')){
    backend.imageCalls++;
    return {ok:true,status:200,headers:{get(){return String(backend.seq)}},async blob(){if(backend.pending)await backend.pending;return {}}};
   }
   throw Error('Unexpected API '+path);
  }});
 vm.runInContext(script,context);
 return {context,e:elements,b:backend,settle:()=>new Promise(resolve=>setImmediate(resolve))};
}

test('page startup only reads cache; buttons only control upload, never scan/HV/config',async()=>{
 const t=setup();await t.settle();
 assert.ok(t.b.calls.every(call=>call.path==='/api/status'));
 await t.e.start.listeners.click();await t.settle();
 assert.equal(t.b.calls.filter(c=>c.path==='/api/viewer/start').length,1);
 const post=t.b.calls.find(c=>c.path==='/api/viewer/start');assert.equal(post.options.body,'{}');
 assert.equal(t.e.start.disabled,true);
 t.b.state.state='running';await t.context.pollStatus();
 await t.e.stop.listeners.click();await t.settle();
 assert.equal(t.b.calls.filter(c=>c.path==='/api/viewer/stop').length,1);
 assert.ok(!t.b.calls.some(c=>/zynq|config|beam|hv/i.test(c.path)));
});

test('freeze does not stop receive, unfreeze fetches latest not queued frames',async()=>{
 const t=setup();await t.settle();Object.assign(t.b.state,{state:'running',image_available:true,image_age_s:.1,lines:64});
 await t.context.pollStatus();await t.context.pollImage();assert.equal(t.e.image.hidden,false);
 t.e.freeze.listeners.click();await t.settle();const before=t.b.imageCalls;
 t.b.seq=9;await t.context.pollImage();assert.equal(t.b.imageCalls,before);
 assert.equal(t.b.calls.filter(c=>c.options.method==='POST').length,0);
 t.e.freeze.listeners.click();await t.settle();assert.equal(t.b.imageCalls,before+1);
 assert.ok(t.b.revoked.length>0);
});

test('new session clears picture, stale/partial/disconnected pictures are labelled',async()=>{
 const t=setup();await t.settle();Object.assign(t.b.state,{state:'running',image_available:true,image_age_s:.1,lines:64});
 await t.context.pollStatus();await t.context.pollImage();
 t.b.state.lines=63;t.b.state.image_age_s=6;await t.context.pollStatus();
 assert.equal(t.e.image.classList.contains('stale'),true);assert.match(t.e['image-note'].textContent,/过期/);
 t.b.state.image_available=false;await t.context.pollStatus();assert.equal(t.e.image.hidden,true);
 assert.equal(t.e.empty.hidden,false);
 t.b.offline=true;await t.context.pollStatus();assert.match(t.e['image-note'].textContent,/不是实时/);
 assert.equal(t.e.start.disabled,true);
 t.b.offline=false;await t.context.pollStatus();assert.equal(t.e.stop.disabled,false);
});

test('single in-flight image request and a freeze discards its late result',async()=>{
 const t=setup();await t.settle();Object.assign(t.b.state,{state:'running',image_available:true,image_age_s:.1,lines:64});
 await t.context.pollStatus();let release;t.b.pending=new Promise(resolve=>release=resolve);
 const request=t.context.pollImage();await t.settle();await t.context.pollImage();assert.equal(t.b.imageCalls,1);
 t.e.freeze.listeners.click();release();await request;
 assert.notEqual(t.e.image.hidden,false);
});

test('backend status is text, not markup; no parameter widgets',async()=>{
 const t=setup();await t.settle();t.b.state.error='<img src=x onerror=alert(1)>';
 await t.context.pollStatus();assert.equal(t.e.error.textContent,t.b.state.error);
 assert.doesNotMatch(html,/<(?:input|select|textarea)\b/i);
 assert.match(html,/完整帧冻结保证/);
});
