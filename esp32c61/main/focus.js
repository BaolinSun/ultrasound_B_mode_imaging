/* Focus presets select prepared tables; no calculation or device I/O at load. */
(function(root,factory){const value=factory();if(typeof module==='object'&&module.exports)module.exports=value;else root.FocusConfig=value})(typeof globalThis==='object'?globalThis:this,function(){
'use strict';
const depths=Array.from({length:16},(_,i)=>20+10*i),presets=[20,50,80,110,140,170];
function validateCatalog(value){
 if(value?.version!==1||!Array.isArray(value.profiles)||value.profiles.length!==16)throw Error('焦点目录版本或档位不完整，请更新 ESP32 固件');
 const result=new Map();for(const p of value.profiles){
  if(!depths.includes(p.focus_mm)||result.has(p.focus_mm)||p.words!==16384||!/^[0-9a-f]{8}$/.test(p.crc32)||!/^[0-9a-f]{64}$/.test(p.sha256)||p.url!==`/focus/${p.focus_mm}-${p.sha256.slice(0,12)}.json`||p.source!==`delay_profile_focus_${p.focus_mm}mm.json`)throw Error('焦点目录格式非法');
  result.set(p.focus_mm,{...p});
 }return result;
}
function crcWords(words){let crc=0xffffffff;for(const word of words){for(let byte=0;byte<4;byte++){crc^=(word>>>(byte*8))&255;for(let bit=0;bit<8;bit++)crc=(crc>>>1)^((crc&1)?0xedb88320:0)}}return ((crc^0xffffffff)>>>0).toString(16).padStart(8,'0')}
function prepare(P,entry,profile){
 P.validate(profile);if(Object.keys(profile).some(k=>!['format_version','bram'].includes(k))||!profile.bram||Object.keys(profile.bram).length!==1||!Array.isArray(profile.bram.delay_profile))throw Error('焦点文件包含非延时表配置');
 const words=profile.bram.delay_profile.map(v=>P.uint(v,32));if(words.length!==16384||crcWords(words)!==entry.crc32)throw Error('焦点延时表长度或 CRC 不匹配，未发送参数');
 return P.build({format_version:1,bram:{delay_profile:words}},['delay_profile']);
}
class Panel{
 constructor({el,parameters,fetch,canApply,apply,setLoading,onDraft}){
  Object.assign(this,{el,parameters,fetch,canApply,apply,setLoading,onDraft});this.catalog=null;this.busy=false;this.applying=false;this.generation=0;
  this.el('focus-depth').addEventListener('input',()=>this.render());
  for(const [id,delta]of [['focus-less',-10],['focus-more',10]])this.el(id).addEventListener('click',()=>this.select(Number(this.el('focus-depth').value)+delta));
  for(const d of presets)this.el('focus-preset-'+d).addEventListener('click',()=>this.select(d));
  this.el('focus-apply').addEventListener('click',()=>this.perform('apply'));
  this.el('focus-load').addEventListener('click',()=>this.perform('draft'));
  this.el('focus-reload').addEventListener('click',()=>this.start());this.render();
 }
 message(text,error=false){this.el('focus-message').textContent=text;this.el('focus-message').classList.toggle('error',error)}
 select(depth){if(this.busy||!this.catalog)return;this.el('focus-depth').value=String(Math.max(20,Math.min(170,depth)));this.render()}
 render(){
  const depth=Number(this.el('focus-depth').value),entry=this.catalog?.get(depth),disabled=this.busy||!this.catalog;
  this.el('focus-value').textContent=entry?String(depth):'—';this.el('focus-cm').textContent=entry?(depth/10).toFixed(1)+' cm':'正在读取可用焦点';
  this.el('focus-depth').disabled=disabled;this.el('focus-depth').setAttribute('aria-valuetext',depth+' 毫米，待设置焦点');
  this.el('focus-less').disabled=disabled||depth<=20;this.el('focus-more').disabled=disabled||depth>=170;
  for(const d of presets){const b=this.el('focus-preset-'+d);b.disabled=disabled;b.setAttribute('aria-pressed',String(depth===d&&!!entry))}
  this.el('focus-apply').disabled=disabled||!entry||!this.canApply();this.el('focus-load').disabled=disabled||!entry||!this.canApply();this.el('focus-reload').disabled=this.busy;
  this.el('focus-source').textContent=entry?entry.source+' · 16384 字 · DDR 预存表快速切换':'尚未加载内置延时文件';
  const same=entry?[...this.catalog.values()].filter(p=>p.sha256===entry.sha256&&p.focus_mm!==depth).map(p=>p.focus_mm):[];
  this.el('focus-duplicate').textContent=same.length?'注意：本档延时表与 '+same.join(' / ')+' mm 文件完全相同；按原文件下发。':'';
  this.el('focus-admission').textContent=this.canApply()?'控制服务就绪，可应用焦点；拖动或选档不会下发':'应用需 PS 已握手、AFE/TX 就绪、无配置事务且控制服务空闲';
 }
 async json(url,maxBytes){const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),10000);try{
  const r=await this.fetch(url,{cache:'no-store',signal:controller.signal});if(!r.ok)throw Error('焦点文件读取失败（HTTP '+r.status+'），请重新读取或更新固件');
  const text=await r.text();if(text.length>maxBytes)throw Error('焦点资源过大');return JSON.parse(text);
 }finally{clearTimeout(timer)}}
 async start(){if(this.busy)return;this.busy=true;this.render();this.message('正在读取内置焦点目录…');try{this.catalog=validateCatalog(await this.json('/focus/catalog.json',16000));this.message('16 档焦点已就绪；选择后点击“停扫并应用焦点”')}catch(e){this.catalog=null;this.message(e.message,true)}finally{this.busy=false;this.render()}}
 cancelPending(){if(this.busy&&!this.applying){this.generation++;this.message('已取消焦点文件加载；未下发参数')}}
 progress(n,total){if(this.applying)this.message(`正在切换 DDR 预存焦点：${n}/${total} 条；请勿关闭页面`)}
 async perform(action){
  if(this.busy||!this.canApply())return;const entry=this.catalog?.get(Number(this.el('focus-depth').value));if(!entry)return;
  const generation=++this.generation;this.busy=true;let loading=true;this.setLoading(true);this.render();this.message('正在加载并校验 '+entry.focus_mm+' mm 延时表…');
  try{
   let commands;
   if(action==='draft'){
    const profile=await this.json(entry.url,400000);prepare(this.parameters,entry,profile);
    if(generation!==this.generation)return;
    this.onDraft(profile);this.message(entry.focus_mm+' mm 延时表已载入编辑区，尚未发送硬件');return;
   }
   commands=[`FOCUS SET ${entry.focus_mm} 0x${entry.crc32}`];
   this.setLoading(false);loading=false;if(!this.canApply())throw Error('控制状态已变化；未切换焦点');
   this.applying=true;this.message('正在停扫、关闭高压并下发焦点，请勿关闭页面…');
   const succeeded=await this.apply(commands,entry.focus_mm);
   if(succeeded){this.el('focus-applied').textContent=`本页最近成功下发：${entry.focus_mm} mm · ${new Date().toLocaleTimeString()}（ACK 确认，非硬件焦点回读）`;this.message('焦点参数已确认下发；扫描、高压及上传保持停止，请手动恢复。')}
   else this.message('本次未确认焦点更新；请查看下方回执，不会自动重发。',true);
  }catch(e){this.message(e.message+'；未自动重发。',true)}finally{if(loading)this.setLoading(false);this.busy=false;this.applying=false;this.render()}
 }
}
return {depths,presets,validateCatalog,crcWords,prepare,Panel};
});
