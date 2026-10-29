import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFileSync} from 'node:fs';
const html=readFileSync(new URL('../pc_tools/bmode_assets/viewer.html',import.meta.url),'utf8');
const script=readFileSync(new URL('../pc_tools/bmode_assets/viewer_focus.js',import.meta.url),'utf8');
function setup(){
 const e={};for(const m of html.matchAll(/id="([^"]+)"/g))e[m[1]]={value:m[1]==='focus-depth'?'80':'',textContent:'',disabled:false,attrs:{},listeners:{},classList:{toggle(){}},setAttribute(k,v){this.attrs[k]=v},addEventListener(k,v){this.listeners[k]=v}};
 const b={calls:[],confirmed:true,error:'',hold:null,state:{state:'idle',focus:{last_applied_mm:null}},profiles:Array.from({length:16},(_,i)=>({focus_mm:20+10*i,words:16384}))};
 const c=vm.createContext({el:id=>{assert.ok(e[id],id);return e[id]},actionPending:0,offline:false,revision:0,lastViewerStatus:b.state,confirm:()=>b.confirmed,
  async api(path,body){b.calls.push({path,body});assert.equal(path,'/api/viewer/focus');if(body){if(b.hold)await b.hold;if(b.error)throw Error(b.error);b.state.control_action='focus';b.state.state='configuring';b.state.focus.completed=8;return {accepted:true}}return {version:1,profiles:b.profiles,error:''}},
  async pollStatus(){c.lastViewerStatus=b.state;c.renderFocusStatus(b.state)}});
 vm.runInContext(script,c);return {e,b,c,settle:()=>new Promise(r=>setImmediate(r))};
}
test('compact focus panel is directly right of depth and startup only reads local catalog',async()=>{
 const t=setup();await t.settle();assert.equal(t.b.calls.length,1);assert.equal(t.b.calls[0].body,undefined);
 assert.ok(html.indexOf('id="display-depth"')<html.indexOf('id="focus-depth"'));assert.ok(html.indexOf('id="focus-depth"')<html.indexOf('class="display-tuning"'));
 assert.match(html,/<label for="focus-depth">焦点<\/label>/);assert.doesNotMatch(html,/待设置焦点|使用预先生成的延时文件/);
 t.e['focus-preset-170'].listeners.click();assert.equal(t.e['focus-value'].textContent,'170');assert.equal(t.e['focus-more'].disabled,true);assert.equal(t.b.calls.length,1);
 t.e['focus-less'].listeners.click();assert.equal(t.e['focus-value'].textContent,'160');assert.equal(t.e['focus-preset-170'].attrs['aria-pressed'],'false');
 t.c.renderFocusStatus({state:'error',error:'old stream failure'});assert.match(t.e['focus-result'].textContent,/失败/);
 t.c.renderFocusStatus(t.b.state);assert.equal(t.e['focus-result'].textContent,'选择后点击应用');
});
test('focus apply requires confirmation, serializes and only submits a whitelisted depth',async()=>{
 const t=setup();await t.settle();t.b.confirmed=false;await t.e['focus-apply'].listeners.click();assert.equal(t.b.calls.length,1);
 t.b.confirmed=true;let release;t.b.hold=new Promise(r=>release=r);const first=t.e['focus-apply'].listeners.click();await t.settle();
 assert.equal(t.e['focus-apply'].disabled,true);await t.e['focus-apply'].listeners.click();assert.equal(t.b.calls.length,2);
 release();await first;assert.equal(JSON.stringify(t.b.calls[1].body),'{"focus_mm":80}');assert.match(t.e['focus-result'].textContent,/8\/2048/);
 assert.equal(t.e['focus-depth'].disabled,true);
});
test('offline, active control, catalog error and POST failure are visible without replay',async()=>{
 const t=setup();await t.settle();for(const s of [{state:'offline'},{state:'starting'},{state:'configuring'},{state:'running',control_action:'freeze'}]){t.c.renderFocusStatus(s);assert.equal(t.e['focus-apply'].disabled,true)}
 t.c.renderFocusStatus(t.b.state);t.b.error='config busy';t.b.state.focus.last_applied_mm=40;await t.e['focus-apply'].listeners.click();assert.match(t.e['focus-result'].textContent,/config busy/);assert.equal(t.b.calls.length,2);
 t.c.renderFocusStatus(t.b.state);assert.match(t.e['focus-result'].textContent,/config busy/);assert.equal(t.b.calls.length,2);
 const broken=setup();broken.b.profiles.pop();await broken.settle();assert.equal(broken.e['focus-apply'].disabled,true);assert.match(broken.e['focus-result'].textContent,/目录不完整/);
});
