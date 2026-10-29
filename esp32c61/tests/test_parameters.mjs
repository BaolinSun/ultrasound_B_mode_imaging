import test from 'node:test';
import assert from 'node:assert/strict';
import {createRequire} from 'node:module';
const require=createRequire(import.meta.url),P=require('../main/parameters_core.js');
const timing=down=>[6144,7059,10,125,3,403,6656,915,6144,down];
test('all table depths/chunks/offsets and five frame configurations',()=>{
 const bram=Object.fromEntries(Object.entries(P.sections).map(([k,n])=>[k,k==='t_timing'?timing(0):Array(n).fill(0xffffffff)]));
 const config={bram,tx7332:{tx7332_config_data:Array(128).fill(0)}};
 for(let d=0;d<5;d++){config.bram.t_timing=timing(d);const commands=P.build(config);assert.ok(commands.every(s=>s.length<=159));assert.equal(commands.filter(c=>c.startsWith('BRAM WR delay_profile ')).length,2048);assert.ok(commands.includes('BRAM WR t_timing 8 2 0x00001800 '+P.hex(d)))}
 for(const k of [0,8,9]){const bad={bram:{t_timing:timing(0)}};bad.bram.t_timing[k]=k===9?5:2048;assert.throws(()=>P.build(bad))}
 assert.throws(()=>P.build({bram:{dfilter:[1]}}));assert.throws(()=>P.build({bram:{unknown:[1]}}));assert.throws(()=>P.build({command:'HV ENABLE'}));assert.throws(()=>P.uint(true,32));assert.throws(()=>P.uint('-1',32));
});
test('AFE encoding, DTGC normalization, ordered overrides and no silent fields',()=>{
 const cfg={afe5832:{vca:{lna_gain_db:21,pga_gain_db:27,lpf_mhz:20,lna_hpf_khz:310,power_mode:'low_noise'},dhpf:{groups:[{group:2,enable:true,corner_code:3,round_enable:true}]},dtgc:{profiles:[{profile:0,start_gain:1,stop_gain:2}]},raw_registers:[{map:'VCA',addr:'0xC7',data:0}]}};
 const commands=P.build(cfg);assert.equal(commands[0],'AFE WR VCA 0xC7 0x2805');assert.equal(commands[2],'AFE WR ADC 0x2D 0x0027');assert.equal(commands.at(-1),'AFE WR VCA 0xC7 0x0000');assert.equal(P.warnings(commands).length,1);assert.deepEqual(P.build(P.normalize(cfg)),commands);
 assert.throws(()=>P.build({afe5832:{dtgc:{adc_clk_hz:50000000}}}),/参数化 DTGC/);
 assert.throws(()=>P.build({afe5832:{raw_registers:null}}));assert.throws(()=>P.build({afe5832:{dhpf:{groups:null}}}));assert.throws(()=>P.build({afe5832:{dtgc:{profiles:null}}}));
 assert.equal(P.normalize({afe5832:{raw_registers:[{map:'adc',addr:1,data:2}]}}).afe5832.raw_registers[0].map,'ADC');
 assert.throws(()=>P.build({afe5832:{vca:{power_mode:'low_power',lpf_mhz:25}}}));assert.throws(()=>P.build({afe5832:{vca:{typo:1}}}));assert.throws(()=>P.build({afe5832:{raw_registers:[{map:'ADC',addr:256,data:1}]}}));
});
function fake({fail=0,unknown=0,lost=0,truncate=false,onBatch=()=>{},badStatus=false}={}){
 const posts=[];let current=[];return {posts,async request(path,body){
  if(body){posts.push(body);current=body.commands;onBatch(posts.length);if(posts.length===lost)throw Error('network');assert.ok(body.commands.length<=8);assert.ok(JSON.stringify(body).length<2048);return {job_id:posts.length.toString(16).padStart(8,'0')}}
  const n=Number.parseInt(path.split('/').at(-1),16);return {busy:false,state:n===unknown?'unknown':n===fail?'failed':'succeeded',error:'test error',count:current.length,completed:current.length,
   results:current.slice(0,truncate?-1:undefined).map(command=>({command,response:command==='STATUS'?'@STATUS cfg=0 run=0 hv='+(badStatus?1:0)+' down=4':'@ACK 12 OK'}))};
 }};
}
function tx(f){return new P.Transaction(f.request,{random:()=>123,delay:async()=>{}})}
test('transaction begins once, batches eight, ends and verifies stopped state',async()=>{
 const f=fake(),t=tx(f);await t.run(P.build({bram:{dfilter:Array(64).fill(1),t_timing:timing(4)}}),{down:4});assert.equal(f.posts[0].commands[0],'CFG BEGIN 123');assert.deepEqual(f.posts.at(-1).commands,['CFG END','STATUS']);assert.equal(new Set(f.posts.map(p=>p.client_id)).size,1);assert.equal(new Set(f.posts.map(p=>p.request_id)).size,f.posts.length);assert.equal(t.owned,false);assert.ok(!f.posts.flatMap(p=>p.commands).some(c=>/HV ENABLE|BEAM START|sdio/.test(c)));
});
test('NACK stops writes and attempts own abort; cancellation finishes current batch',async()=>{
 const commands=P.build({bram:{dfilter:Array(64).fill(1),hamming:Array(64).fill(2)}}),f=fake({fail:2}),t=tx(f);
 await assert.rejects(t.run(commands),/批次失败/);assert.deepEqual(f.posts.at(-1).commands,['CFG ABORT']);assert.equal(f.posts.length,3);
 let c;const g=fake({onBatch:n=>{if(n===2)c.cancel()}});c=tx(g);await assert.rejects(c.run(commands),/用户取消/);assert.deepEqual(g.posts.at(-1).commands,['CFG ABORT']);assert.equal(g.posts.length,3);
});
test('unknown/lost/truncated replies never replay or send subsequent commands',async()=>{
 for(const option of [{unknown:2},{lost:2},{truncate:true}]){const f=fake(option),t=tx(f);await assert.rejects(t.run(['AFE WR ADC 0x01 0x00']),e=>e.unknown);assert.equal(t.uncertain,true);const n=f.posts.length;await assert.rejects(t.run(['AFE RD ADC 1']));assert.equal(f.posts.length,n);assert.ok(!f.posts.some(b=>b.commands.includes('CFG ABORT')))}
});
test('result state mismatch rejects, illegal commands cannot open transaction',async()=>{
 const f=fake({badStatus:true});await assert.rejects(tx(f).run(['AFE RD ADC 1']),/最终状态/);const g=fake();await assert.rejects(tx(g).run(['HV ENABLE']));assert.equal(g.posts.length,0);
});
test('all AFE modes, defaults, full DTGC registers, strict malformed imports',()=>{
 for(const lna of [15,18,21])for(const pga of [21,24,27])for(const lpf of [10,15,20,25])for(const mode of ['low_noise','low_power']){
  const cfg={afe5832:{vca:{lna_gain_db:lna,pga_gain_db:pga,lpf_mhz:lpf,power_mode:mode}}};
  if(mode==='low_power'&&lpf===25)assert.throws(()=>P.build(cfg));else assert.equal(P.build(cfg).length,2);
 }
 assert.equal(P.afeCommands({vca:{},dhpf:{groups:[{group:0},{group:1,enable:false,round_enable:false,corner_code:0}]},dtgc:{profiles:[{profile:3,start_gain:5,stop_gain:10,pos_step:3,pos_step_feq:4,neg_step:2,neg_step_feq:1,start_index:1,stop_index:2,start_gain_time:100,hold_gain_time:50}],raw_registers:[{addr:255,data:65535}]}}).length,10);
 assert.equal(P.afeCommands({dhpf:{},dtgc:{}}).length,0);
 for(const bad of [null,[],{afe5832:0},{bram:0},{tx7332:0},{format_version:2},{afe5832:{vca:{lna_gain_db:20}}},{afe5832:{raw_registers:[{map:'BAD',addr:0,data:0}]}},{afe5832:{dhpf:{groups:[{group:0},{group:0}]}}},{afe5832:{dhpf:{groups:[{group:1,enable:1}]}}}])assert.throws(()=>P.validate(bad));
 assert.throws(()=>P.build({}));assert.throws(()=>P.build({bram:{hamming:Array(64).fill(1)}},['dfilter']));assert.throws(()=>P.tableCommands('BRAM','hamming',true,64));assert.throws(()=>P.uint(1.1,16));assert.throws(()=>P.statusFields('@ACK 1 OK'));
});
test('display metadata validation permits partial import but rejects incompatible curves',()=>{
 const viewer={version:1,dynamic_range_db:45,brightness_db:3,contrast:1,noise_floor_db:-45,tgc_enabled:true,depth_mm:120,lut:Array.from({length:256},(_,i)=>i),angles_deg:Array.from({length:64},(_,i)=>-45+90*i/63),tgc_points:[[0,0],[6143,24]]};
 P.validate({viewer});P.validate({viewer:{}});
 for(const [key,value]of [['version',2],['depth_mm',189],['tgc_enabled',1],['lut',[1]],['lut',Array(256).fill('1')],['tgc_points',[]],['tgc_points',[[0,0],[0,1],[6143,0]]],['tgc_points',[[1,0],[6143,0]]],['angles_deg',Array(64).fill(0)],['unknown',1]])assert.throws(()=>P.validate({viewer:{...viewer,[key]:value}}));
});
test('polling handles busy, deadline, malformed job and failed cleanup without replay',async()=>{
 const f=fake();let poll=0;const request=async(path,body)=>{if(!body&&poll++===0)return {busy:true};return f.request(path,body)};
 const t=new P.Transaction(request);await t.run(['AFE RD ADC 1']);assert.ok(poll>=2);assert.equal(t.owned,false);
 let now=0;const timed=new P.Transaction(async(p,b)=>b?{job_id:'12345678'}:{busy:true},{random:()=>1,now:()=>now+=300001,delay:async()=>{}});
 await assert.rejects(timed.run(['AFE RD ADC 1']),e=>e.unknown&&/超时/.test(e.message));
 const broken=new P.Transaction(async()=>({job_id:'bad'}),{random:()=>1});await assert.rejects(broken.run(['AFE RD ADC 1']),e=>e.unknown);
 const g=fake({fail:2}),h=new P.Transaction(async(p,b)=>{if(b?.commands[0]==='CFG ABORT')throw Error('cleanup lost');return g.request(p,b)},{random:()=>1});await assert.rejects(h.run(['AFE RD ADC 1']),e=>e.unknown&&/中止未确认/.test(e.message));
 const c=fake({onBatch:n=>{if(n===1)cancel.cancel()}}),cancel=tx(c);await assert.rejects(cancel.run(['AFE RD ADC 1']),/用户取消/);assert.deepEqual(c.posts.at(-1).commands,['CFG ABORT']);
});
