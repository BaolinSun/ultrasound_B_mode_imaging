import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFileSync} from 'node:fs';
const html=readFileSync(new URL('../pc_tools/bmode_assets/viewer.html',import.meta.url),'utf8');
const js=readFileSync(new URL('../pc_tools/bmode_assets/viewer_settings.js',import.meta.url),'utf8');
const defaults=()=>({version:1,dynamic_range_db:45,brightness_db:3,contrast:1,noise_floor_db:-45,tgc_enabled:true,depth_mm:120,tgc_points:[[0,0],[6143,24]],lut:Array.from({length:256},(_,i)=>i),angles_deg:Array.from({length:64},(_,i)=>-45+90*i/63)});
function setup(){
 const nodes={};for(const m of html.matchAll(/id="([^"]+)"/g))nodes[m[1]]={value:'',checked:false,disabled:false,textContent:'',listeners:{},style:{},attributes:{},setAttribute(n,v){this.attributes[n]=v},classList:{toggle(){}},addEventListener(n,f){this.listeners[n]=f},replaceChildren(){},append(){}};
 const timers=new Map();let timer=0;
 const b={settings:defaults(),revision:1,puts:[],hold:null,fail:false,clearCount:0};
 const c=vm.createContext({el:id=>{assert.ok(nodes[id],id);return nodes[id]},frozen:false,confirm:()=>true,
  setTimeout(fn){timers.set(++timer,fn);return timer},clearTimeout(id){timers.delete(id)},
  clearPicture(){b.clearCount++},document:{createElementNS(){return {setAttribute(){}}}},
  async fetch(path,opts){assert.equal(path,'/api/viewer/settings');
   if(opts.method==='PUT'){
    const body=JSON.parse(opts.body);b.puts.push(body);assert.equal(body.revision,b.revision);
    if(b.fail)return {ok:false,json:async()=>({error:'revision conflict'})};
    b.settings=body.settings;b.revision++;const result=structuredClone({settings:b.settings,revision:b.revision});if(b.hold)await b.hold;
    return {ok:true,json:async()=>result};
   }return {ok:true,json:async()=>structuredClone({settings:b.settings,revision:b.revision})};
  }});
 vm.runInContext(js,c);
 const settle=()=>new Promise(r=>setImmediate(r));
 return {c,e:nodes,b,timers,settle,async tick(){const jobs=[...timers.values()];timers.clear();for(const job of jobs)job();await settle()}};
}
test('all requested display parameters auto-apply without upload/stop or hardware requests',async()=>{
 const t=setup();await t.settle();assert.equal(t.e['display-apply'],undefined);
 for(const [id,value,key]of [['dr','50','dynamic_range_db'],['brightness','8','brightness_db'],['contrast','1.5','contrast'],['noise','-50','noise_floor_db'],['depth','160','depth_mm']]){
  t.e['display-'+id].value=value;t.e['display-'+id].listeners.input();await t.tick();assert.equal(t.b.settings[key],Number(value));
 }
 t.e['display-tgc'].checked=false;t.e['display-tgc'].listeners.change();await t.tick();assert.equal(t.b.settings.tgc_enabled,false);
 t.e['display-points'].value='[[0,-2],[6143,16]]';t.e['display-points'].listeners.input();await t.tick();assert.deepEqual(t.b.settings.tgc_points,[[0,-2],[6143,16]]);
 t.e['display-lut'].value=JSON.stringify(Array(256).fill(32));t.e['display-lut'].listeners.input();await t.tick();assert.equal(t.b.settings.lut[255],32);
 assert.equal(t.b.clearCount,0);assert.equal(t.b.puts.length,8);
});

test('depth uses a vertical range with readout, presets and bounded 5 mm steps',async()=>{
 const t=setup();await t.settle();
 assert.match(html, /id="display-depth" type="range"/);assert.match(html,/aria-orientation="vertical"/);
 assert.equal(t.e['value-depth'].textContent,'120');assert.match(t.e['depth-cm'].textContent,/12.0 cm/);
 assert.equal(t.e['depth-preset-120'].attributes['aria-pressed'],'true');
 t.e['depth-more'].listeners.click();await t.tick();assert.equal(t.b.settings.depth_mm,125);
 t.e['depth-less'].listeners.click();await t.tick();assert.equal(t.b.settings.depth_mm,120);
 t.e['depth-preset-40'].listeners.click();await t.tick();assert.equal(t.b.settings.depth_mm,40);
 assert.equal(t.e['depth-preset-40'].attributes['aria-pressed'],'true');assert.equal(t.e['depth-preset-120'].attributes['aria-pressed'],'false');
 for(const [depth,button]of [[11,'depth-less'],[187,'depth-more']]){
  t.e['display-depth'].value=String(depth);t.e['display-depth'].listeners.input();await t.tick();
  t.e[button].listeners.click();await t.tick();assert.equal(t.b.settings.depth_mm,depth===11?10:188);assert.equal(t.e[button].disabled,true);
  const count=t.b.puts.length;t.e[button].listeners.click();await t.tick();assert.equal(t.b.puts.length,count);
 }
 assert.equal(t.b.clearCount,0);
});

test('depth remains editable during auto updates and locks during explicit reload',async()=>{
 const t=setup();await t.settle();let release;t.b.hold=new Promise(r=>release=r);
 t.e['depth-preset-80'].listeners.click();await t.tick();assert.equal(t.e['depth-more'].disabled,false);
 t.e['depth-preset-180'].listeners.click();await t.tick();assert.equal(t.b.puts.length,1);assert.equal(t.e['value-depth'].textContent,'180');
 t.b.hold=null;release();await t.settle();await t.tick();assert.equal(t.b.puts.length,2);assert.equal(t.b.settings.depth_mm,180);
 const loading=t.e['display-reload'].listeners.click();assert.equal(t.e['depth-less'].disabled,true);assert.equal(t.e['depth-preset-40'].disabled,true);
 t.e['depth-preset-40'].listeners.click();assert.equal(t.e['value-depth'].textContent,'180');await loading;assert.equal(t.e['depth-less'].disabled,false);
});
test('rapid dragging coalesces, serializes and preserves edits made while a request is in flight',async()=>{
 const t=setup();await t.settle();for(let i=0;i<10;i++){t.e['display-brightness'].value=String(i);t.e['display-brightness'].listeners.input()}
 assert.equal(t.timers.size,1);let release;t.b.hold=new Promise(r=>release=r);await t.tick();assert.equal(t.b.puts.length,1);
 t.e['display-brightness'].value='17';t.e['display-brightness'].listeners.input();await t.tick();assert.equal(t.b.puts.length,1);
 t.b.hold=null;release();await t.settle();assert.equal(t.e['display-brightness'].value,'17');await t.tick();assert.equal(t.b.puts.length,2);assert.equal(t.b.settings.brightness_db,17);
});
test('incomplete/invalid draft retains old image, valid correction resumes automatically',async()=>{
 const t=setup();await t.settle();
 for(const [id,value]of [['depth',''],['lut','[1]'],['points','[[0,0],']]){
  const old=t.e['display-'+id].value;t.e['display-'+id].value=value;t.e['display-'+id].listeners.input();await t.tick();assert.equal(t.b.puts.length,0);assert.match(t.e['display-message'].textContent,/无效/);t.e['display-'+id].value=old;
 }
 t.e['display-depth'].value='140';t.e['display-depth'].listeners.input();await t.tick();assert.equal(t.b.settings.depth_mm,140);assert.equal(t.b.clearCount,0);
});
test('conflict suspends auto submissions until explicit reload; freeze retains axes',async()=>{
 const t=setup();await t.settle();t.b.fail=true;t.e['display-dr'].value='60';t.e['display-dr'].listeners.input();await t.tick();assert.match(t.e['display-message'].textContent,/已暂停/);
 t.e['display-dr'].value='65';t.e['display-dr'].listeners.input();await t.tick();assert.equal(t.b.puts.length,1);
 t.b.fail=false;await t.e['display-reload'].listeners.click();t.e['display-dr'].value='66';t.e['display-dr'].listeners.input();await t.tick();assert.equal(t.b.puts.length,2);
 t.c.frozen=true;t.e['geometry-title'].textContent='frozen geometry';t.c.renderDisplayStatus({image_display_settings:{...defaults(),depth_mm:180},settings_revision:3});assert.equal(t.e['geometry-title'].textContent,'frozen geometry');
});


test('nine TGC presets auto-apply bounded original-sample curves with exact default restoration',async()=>{
 const t=setup();await t.settle();
 const original=[[0,0],[512,0],[1024,0],[2048,0],[3072,0],[3247,0],[4096,8],[4608,13],[5120,17],[5632,21],[6143,24]];
 for(const key of ['default','flat','deep','near','gentle','middle','edges','reduce','boost']){
  t.e['tgc-preset-'+key].listeners.click();await t.tick();const p=t.b.settings.tgc_points;
  assert.equal(p[0][0],0);assert.equal(p.at(-1)[0],6143);assert.ok(p.every((v,i)=>v[1]>=-24&&v[1]<=24&&(!i||v[0]>p[i-1][0])));
  assert.equal(t.e['tgc-preset-'+key].attributes['aria-pressed'],'true');
  if(key==='default')assert.deepEqual(p,original);
  if(key==='flat')assert.ok(p.every(v=>v[1]===0));
  if(key==='deep')assert.ok(p[0][1]<p.at(-1)[1]);
 }
 assert.equal(t.b.clearCount,0);assert.equal(t.b.puts.length,9);
});

test('TGC bands keep physical depths when display is cropped; undo and disabled guard work',async()=>{
 const t=setup();await t.settle();const initial=structuredClone(t.b.settings.tgc_points);
 const labels=Array.from({length:8},(_,i)=>t.e['tgc-depth-'+i].textContent);
 assert.equal(labels[0],'0.0');assert.equal(labels[7],'189.2');
 t.e['tgc-band-3'].value='-12.5';t.e['tgc-band-3'].listeners.input();await t.tick();
 assert.equal(t.b.settings.tgc_points.length,8);assert.equal(t.b.settings.tgc_points[3][1],-12.5);
 assert.equal(t.e['tgc-value-3'].textContent,'-12.5 dB');assert.match(t.e['tgc-selected'].textContent,/自定义/);
 t.e['tgc-undo'].listeners.click();await t.tick();assert.deepEqual(t.b.settings.tgc_points,initial);
 t.e['depth-preset-40'].listeners.click();await t.tick();assert.deepEqual(Array.from({length:8},(_,i)=>t.e['tgc-depth-'+i].textContent),labels);
 t.e['display-tgc'].checked=false;t.e['display-tgc'].listeners.change();await t.tick();assert.equal(t.e['tgc-band-0'].disabled,true);
 const n=t.b.puts.length;t.e['tgc-preset-deep'].listeners.click();await t.tick();assert.equal(t.b.puts.length,n);
 t.e['display-tgc'].checked=true;t.e['display-tgc'].listeners.change();await t.tick();assert.equal(t.e['tgc-band-0'].disabled,false);
});

test('TGC preserves imported custom points until a band is edited and queues latest drag during PUT',async()=>{
 const t=setup();await t.settle();const custom=[[0,-2],[1000,4],[6143,13]];
 t.e['display-points'].value=JSON.stringify(custom);t.e['display-points'].listeners.input();await t.tick();assert.deepEqual(t.b.settings.tgc_points,custom);
 let release;t.b.hold=new Promise(r=>release=r);t.e['tgc-band-7'].value='20';t.e['tgc-band-7'].listeners.input();await t.tick();
 t.e['tgc-band-7'].value='24';t.e['tgc-band-7'].listeners.input();await t.tick();
 t.b.hold=null;release();await t.settle();await t.tick();assert.equal(t.b.settings.tgc_points.at(-1)[1],24);
});
