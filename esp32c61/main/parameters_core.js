/* Pure builders and bounded transaction client; no device I/O at module load. */
(function(root,factory){const value=factory();if(typeof module==='object'&&module.exports)module.exports=value;else root.ParameterConfig=value})(typeof globalThis==='object'?globalThis:this,function(){
'use strict';
const sections={fdemod_sin:8192,fdemod_cos:8192,log_table:1024,x_element:64,hamming:64,dfilter:64,t_timing:10,sin_beta:64,delay_profile:16384};
const names={afe5832:'AFE5832',tx7332:'TX7332',delay_profile:'发射延时',fdemod_sin:'解调正弦',fdemod_cos:'解调余弦',log_table:'对数表',x_element:'阵元位置',hamming:'加窗',dfilter:'滤波',t_timing:'扫描时序',sin_beta:'扫描角度'};
const timingNames=['原始点数 rtc_dot','周期 rtc_period','t1','t2','t3','t4','t5','t6','采集点数 t7','抽取参数 rtc_down'];
const HPF={100:0,110:1,120:2,130:3,140:4,150:5,160:6,170:7,20:8,30:9,40:10,50:11,60:12,70:13,80:14,90:15,260:16,270:17,290:18,300:19,310:20,180:24,190:25,200:26,210:27,220:28,230:29,240:30,250:31};
const copy=value=>JSON.parse(JSON.stringify(value));
function object(v,label){if(!v||Array.isArray(v)||typeof v!=='object')throw Error(label+' 必须是对象');return v}
function keys(v,allowed,label){object(v,label);for(const key of Object.keys(v))if(!allowed.includes(key))throw Error(label+' 未知配置字段：'+key)}
function uint(v,bits,label='数值'){if(typeof v==='string'&&/^(?:0x[0-9a-f]+|[0-9]+)$/i.test(v.trim()))v=Number(v.trim());if(typeof v!=='number'||!Number.isSafeInteger(v)||v<0||v>2**bits-1)throw Error(label+' 必须为 '+bits+' 位无符号整数');return v}
function boolean(v,label){if(typeof v!=='boolean')throw Error(label+' 必须为 true/false');return v}
function hex(v,width=8){return '0x'+v.toString(16).toUpperCase().padStart(width,'0')}
function list(v,label){if(!Array.isArray(v))throw Error(label+' 必须为数组');return v}
function validateTiming(words,offset=0){words.forEach((v,i)=>{const n=i+offset;if((n===0||n===8)&&v!==6144)throw Error('t_timing['+n+'] 必须为 6144；仅通过第 9 项抽取');if(n===9&&v>4)throw Error('抽取参数必须为 0～4')})}
function reg(out,map,addr,data){if(!['GLOBAL','ADC','VCA','DTGC'].includes(map))throw Error('AFE map 无效');out.push('AFE WR '+map+' '+hex(uint(addr,8,'AFE 地址'),2)+' '+hex(uint(data,16,'AFE 数据'),4))}
function raw(out,rows,map){for(const item of list(rows,'寄存器表')){keys(item,map?['addr','data']:['map','addr','data'],'寄存器');reg(out,map||String(item.map).toUpperCase(),item.addr,item.data)}}
function afeCommands(afe){
 keys(afe,['input_impedance_ohm','vca','dhpf','dtgc','raw_registers'],'afe5832');const out=[];
 if(afe.raw_registers!==undefined)list(afe.raw_registers,'原始寄存器');
 if(afe.vca!==undefined){const v=afe.vca;keys(v,['lna_gain_db','pga_gain_db','lpf_mhz','lna_hpf_khz','power_mode'],'vca');
  const lna=uint(v.lna_gain_db??18,8),pga=uint(v.pga_gain_db??24,8),lpf=uint(v.lpf_mhz??20,8),hpf=uint(v.lna_hpf_khz??100,16),mode=v.power_mode??'low_noise';
  const L={18:0,21:1,15:3},P={24:0,27:1,21:2},F={20:0,25:1,10:2,15:3};
  if(!(lna in L)||!(pga in P)||!(lpf in F)||!(hpf in HPF)||!['low_noise','low_power'].includes(mode))throw Error('VCA 档位无效');
  if(mode==='low_power'&&lpf===25)throw Error('低功耗模式不支持 25 MHz LPF');
  reg(out,'VCA',0xc7,((mode==='low_power'?0:F[lpf])<<14)|(HPF[hpf]<<9)|(F[lpf]<<5)|(P[pga]<<2)|L[lna]);reg(out,'VCA',0xc8,mode==='low_power'?1:0);
 }
 if(afe.dhpf!==undefined){keys(afe.dhpf,['groups'],'dhpf');if(afe.dhpf.groups!==undefined)list(afe.dhpf.groups,'DHPF');const seen=new Set();for(const g of list(afe.dhpf.groups??[],'DHPF')){
  keys(g,['group','enable','corner_code','round_enable'],'DHPF');const index=uint(g.group,2);if(seen.has(index))throw Error('重复的 DHPF 组');seen.add(index);
  reg(out,'ADC',[0x15,0x21,0x2d,0x39][index],((boolean(g.round_enable??false,'round_enable')?1:0)<<5)|(uint(g.corner_code??3,4)<<1)|(boolean(g.enable??true,'enable')?1:0));
 }}
 if(afe.dtgc!==undefined){const d=afe.dtgc;object(d,'DTGC');if(Object.keys(d).some(k=>!['profiles','raw_registers'].includes(k)))throw Error('参数化 DTGC 不支持直接导入；请先转换为寄存器表');
  for(const key of ['profiles','raw_registers'])if(d[key]!==undefined)list(d[key],'DTGC '+key);
  for(const p of list(d.profiles??[],'DTGC profiles')){
   keys(p,['profile','start_gain','stop_gain','pos_step','pos_step_feq','neg_step','neg_step_feq','start_index','stop_index','start_gain_time','hold_gain_time'],'DTGC profile');
   const base=0xa1+5*uint(p.profile,2);reg(out,'DTGC',base,(uint(p.start_gain??0,8)<<8)|uint(p.stop_gain??0x90,8));
   reg(out,'DTGC',base+1,(uint(p.pos_step??0,5)<<11)|(uint(p.pos_step_feq??0,3)<<8)|(uint(p.neg_step??31,5)<<3)|uint(p.neg_step_feq??7,3));
   if(['start_index','stop_index','start_gain_time','hold_gain_time'].some(k=>k in p)){
    reg(out,'DTGC',base+2,(uint(p.start_index??0,8)<<8)|uint(p.stop_index??0x90,8));reg(out,'DTGC',base+3,uint(p.start_gain_time??0,16));reg(out,'DTGC',base+4,uint(p.hold_gain_time??0,16));
   }
  }raw(out,d.raw_registers??[],'DTGC');
 }
 raw(out,afe.raw_registers??[]);return out;
}
function tableCommands(kind,section,values,count){const words=list(values,section).map((v,i)=>uint(v,32,section+'['+i+']'));if(words.length!==count)throw Error(section+' 必须有 '+count+' 字');if(section==='t_timing')validateTiming(words);
 const out=[];for(let offset=0;offset<words.length;offset+=8){const chunk=words.slice(offset,offset+8);out.push(kind+' WR '+section+' '+offset+' '+chunk.length+' '+chunk.map(v=>hex(v)).join(' '))}return out;
}
function validate(config){
 keys(config,['description','notes','metadata','format_version','afe5832','tx7332','bram','viewer'],'配置');
 if(config.format_version!==undefined&&config.format_version!==1)throw Error('不支持的配置版本');
 if(config.afe5832!==undefined)afeCommands(config.afe5832);
 if(config.tx7332!==undefined){keys(config.tx7332,['tx7332_config_data'],'tx7332');if(config.tx7332.tx7332_config_data!==undefined)tableCommands('TX7332','tx7332_config_data',config.tx7332.tx7332_config_data,128)}
 if(config.bram!==undefined){keys(config.bram,Object.keys(sections),'bram');for(const [k,v]of Object.entries(config.bram))tableCommands('BRAM',k,v,sections[k])}
 if(config.viewer!==undefined){const v=config.viewer;keys(v,['version','dynamic_range_db','brightness_db','contrast','noise_floor_db','tgc_enabled','tgc_points','lut','depth_mm','angles_deg'],'viewer');
  if(v.version!==undefined&&v.version!==1)throw Error('不支持的显示配置版本');
  for(const [k,lo,hi]of [['dynamic_range_db',20,70],['brightness_db',-10,20],['contrast',.5,2],['noise_floor_db',-60,-20],['depth_mm',10,188]])if(v[k]!==undefined&&(typeof v[k]!=='number'||!Number.isFinite(v[k])||v[k]<lo||v[k]>hi))throw Error('显示字段范围错误：'+k);
  if(v.tgc_enabled!==undefined)boolean(v.tgc_enabled,'tgc_enabled');
  if(v.lut!==undefined){const lut=list(v.lut,'LUT');if(lut.length!==256)throw Error('LUT 必须有 256 项');lut.forEach(n=>{if(typeof n!=='number')throw Error('LUT 必须为整数');uint(n,8)})}
  if(v.tgc_points!==undefined){const points=list(v.tgc_points,'TGC');if(points.length<2||points.length>128)throw Error('TGC 控制点数量无效');let last=-1;for(const p of points){if(!Array.isArray(p)||p.length!==2||p.some(x=>typeof x!=='number'||!Number.isFinite(x))||p[0]<0||p[0]>6143||p[0]<=last||p[1]<-24||p[1]>24)throw Error('TGC 控制点无效');last=p[0]}if(points[0][0]!==0||points.at(-1)[0]!==6143)throw Error('TGC 需覆盖 0～6143')}
  if(v.angles_deg!==undefined){const a=list(v.angles_deg,'角度');if(a.length!==64||a.some((x,i)=>typeof x!=='number'||!Number.isFinite(x)||x< -89||x>89||(i&&x<=a[i-1])))throw Error('64 条线的角度必须有效且严格递增')}
 }return config;
}
function targets(config){return ['afe5832','tx7332',...Object.keys(sections)].filter(k=>k==='afe5832'?!!config.afe5832:k==='tx7332'?!!config.tx7332?.tx7332_config_data:config.bram?.[k]!==undefined)}
function build(config,selected=targets(config)){
 validate(config);if(!selected.length)throw Error('未选择配置目标');const out=[];
 for(const target of selected){if(!targets(config).includes(target))throw Error('目标尚未加载：'+target);
  out.push(...(target==='afe5832'?afeCommands(config.afe5832):target==='tx7332'?tableCommands('TX7332','tx7332_config_data',config.tx7332.tx7332_config_data,128):tableCommands('BRAM',target,config.bram[target],sections[target])));
 }if(!out.length)throw Error('没有可执行的参数');if(out.some(s=>s.length>159))throw Error('命令超过 159 字节');return out;
}
function warnings(commands){const seen=new Set(),repeated=new Set();for(const c of commands){if(!c.startsWith('AFE WR '))continue;const key=c.split(' ').slice(2,4).join(' ');if(seen.has(key))repeated.add(key);seen.add(key)}return [...repeated].map(k=>'重复地址 '+k+'；按预览顺序执行，后写覆盖前写')}
function normalize(config){const result=copy(validate(config));if(result.afe5832?.dtgc){const rows=afeCommands({dtgc:result.afe5832.dtgc}).map(c=>{const a=c.split(' ');return {map:a[2],addr:a[3],data:a[4]}});delete result.afe5832.dtgc;result.afe5832.raw_registers=[...rows,...(result.afe5832.raw_registers??[])]}if(result.afe5832?.raw_registers)result.afe5832.raw_registers=result.afe5832.raw_registers.map(r=>({map:r.map.toUpperCase(),addr:hex(uint(r.addr,8),2),data:hex(uint(r.data,16),4)}));return result}
function statusFields(response){if(!response.startsWith('@STATUS '))throw Error('缺少 PS STATUS');return Object.fromEntries([...response.matchAll(/(\w+)=([^\s]+)/g)].map(m=>[m[1],m[2]]))}
class OutcomeError extends Error{constructor(message,unknown=false){super(message);this.unknown=unknown}}
class Transaction {
 constructor(request,{delay=ms=>new Promise(r=>setTimeout(r,ms)),random=()=>crypto.getRandomValues(new Uint32Array(1))[0],now=()=>Date.now(),progress=()=>{}}={}){
  this.request=request;this.delay=delay;this.now=now;this.progress=progress;this.client=hex(random()).slice(2).toLowerCase();this.counter=random()&0x7fffffff;this.seq=(random()&0x7fffffff)||1;this.busy=false;this.cancelled=false;this.owned=false;this.uncertain=false;
 }
 cancel(){this.cancelled=true}
 async batch(commands){
  const id=(++this.counter).toString(16).padStart(8,'0');let accepted;
  try{accepted=await this.request('/api/zynq/commands',{client_id:this.client,request_id:id,commands})}catch(e){throw new OutcomeError('提交失败，执行结果未知：'+e.message,true)}
  if(!/^[0-9a-f]{8}$/i.test(accepted.job_id??''))throw new OutcomeError('任务回执无效，执行结果未知',true);
  const deadline=this.now()+300000;
  for(;;){let job;try{job=await this.request('/api/zynq/jobs/'+accepted.job_id)}catch(e){throw new OutcomeError('查询中断，执行结果未知：'+e.message,true)}
   if(!job.busy){const results=job.results??[];if(job.state==='unknown')throw new OutcomeError('PS 执行结果未知：'+job.error,true);
    if(job.state!=='succeeded')throw new OutcomeError('批次失败：'+(job.error||job.state));
    if(job.count!==commands.length||job.completed!==commands.length||results.length!==commands.length||results.some((v,i)=>v.command!==commands[i]||!/^@(ACK|RD|STATUS) /.test(v.response??'')))throw new OutcomeError('批次回执不完整或不匹配',true);
    return results;
   }if(this.now()>=deadline)throw new OutcomeError('等待命令超时，执行结果未知',true);await this.delay(150);
  }
 }
 async run(commands,{down=null}={}){
  if(this.busy||this.uncertain)throw Error('控制忙碌或上次结果未知；请先查询设备状态');
  if(!commands.length||commands.some(c=>!(/^(AFE (WR|RD) |TX7332 (WR|RD) |BRAM WR )/.test(c))||c.length>159||!/^[\x20-\x7e]+$/.test(c)))throw Error('不是有效的参数命令');
  const snapshot=commands.slice();this.busy=true;this.cancelled=false;const responses=[];
  try{
   // Mark possible ownership before POST: a lost reply must never trigger replay.
   this.owned=true;responses.push(...await this.batch(['CFG BEGIN '+this.seq]));
   for(let n=0;n<snapshot.length;n+=8){if(this.cancelled)throw new OutcomeError('用户取消');const part=snapshot.slice(n,n+8);responses.push(...await this.batch(part));this.progress(Math.min(n+8,snapshot.length),snapshot.length, responses.slice(-8))}
   if(this.cancelled)throw new OutcomeError('用户取消');
   responses.push(...await this.batch(['CFG END','STATUS']));this.owned=false;
   const state=statusFields(responses.at(-1).response);
   if(state.cfg!=='0'||state.run!=='0'||state.hv!=='0'||(down!==null&&state.down!==String(down)))throw new OutcomeError('最终状态不匹配，请查询 PS；不会自动恢复');
   return responses;
  }catch(e){
   if(e.unknown){this.uncertain=true;throw e}
   if(this.owned){try{await this.batch(['CFG ABORT']);this.owned=false}catch(cleanup){this.uncertain=true;throw new OutcomeError(e.message+'；中止未确认，请查询设备；事务由看门狗清理',true)}}throw e;
  }finally{this.busy=false;this.seq=(this.seq+1)&0x7fffffff;if(!this.seq)this.seq=1}
 }
}
const frameLengths=[787456,394240,197632,99328,50176];
function quickFields(text){if(typeof text!=='string'||!text.startsWith('@STATUS '))return null;const fields={};for(const item of text.slice(8).split(' ')){const m=/^([a-z_][a-z0-9_]*)=([a-z0-9]+)$/.exec(item);if(!m||Object.hasOwn(fields,m[1]))return null;fields[m[1]]=m[2]}return fields.v==='1'?fields:null}
function quickState(snapshot,localBusy=false){const c=snapshot?.zynq_control??{},fields=c.synced?quickFields(c.ps_status):null;
 const allowed=!localBusy&&c.ready&&c.synced&&!c.busy&&!c.config_owned&&fields?.cfg==='0'&&c.afe_state==='ready'&&c.tx_state==='ready';
 const down=fields&&/^[0-4]$/.test(fields.down)?Number(fields.down):null,received=frameLengths.indexOf(snapshot?.sdio?.last_received_bytes);
 return {gainAllowed:!!allowed,downAllowed:!!allowed&&down!==null,down,received,fields};
}
function downDescription(d){return `${1<<d} 倍 · ${6144>>d} 点/线 · ${frameLengths[d]} B/帧`}
async function quickDown(t,value,snapshot){if(!/^[0-4]$/.test(String(value)))throw Error('下采样参数必须为 0～4');if(!quickState(snapshot).downAllowed||t.busy||t.uncertain)throw Error('控制忙碌、状态未知或芯片尚未就绪，请查询状态');const d=Number(value),seq=t.seq;
 const results=await t.run(['BRAM WR t_timing 9 1 '+d],{down:d});const expected=['CFG BEGIN '+seq,'BRAM WR t_timing 9 1 '+d,'CFG END','STATUS'];
 if(results.length!==4||results.some((r,i)=>r.command!==expected[i])||results.slice(0,3).some((r,i)=>r.response!==`@ACK ${seq} ${['CFG_BEGIN','BRAM_WR','CFG_END'][i]}`))throw Error('配置确认不匹配，请查询；不会自动重发');
 const f=quickFields(results[3].response);if(!f||f.seq!==String(seq)||f.down!==String(d)||f.cfg!=='0'||f.run!=='0'||f.hv!=='0')throw Error('最终 PS 状态不匹配，请查询');return results;
}
async function quickGain(t,value,snapshot){if(!/^[0-8]$/.test(String(value)))throw Error('数字增益必须为 0～8 的整数');if(!quickState(snapshot).gainAllowed||t.busy||t.uncertain)throw Error('控制忙碌、状态未知或芯片尚未就绪，请查询状态');const r=await t.batch(['GAIN SET '+value]);if(r.length!==1||!new RegExp('^@ACK \\d+ GAIN_SET '+value+'$').test(r[0].response))throw Error('数字增益回执未确认，请查询；不会自动重发');return r;
}
return {sections,names,timingNames,HPF,uint,hex,validateTiming,afeCommands,tableCommands,validate,build,targets,warnings,normalize,statusFields,Transaction,OutcomeError,quickFields,quickState,quickDown,quickGain,downDescription};
});
