'use strict';
const P=ParameterConfig, by=id=>document.getElementById(id), PAGE=64, LIMIT=2*1024*1024;
let config={format_version:1}, category='afe5832', page=0, locked=false;
const selections=new Set();
function message(text,error=false){by('message').textContent=text;by('message').classList.toggle('error',error)}
async function request(path,body){const controller=new AbortController(),timer=setTimeout(()=>controller.abort(),8000);try{const r=await fetch(path,{cache:'no-store',signal:controller.signal,...(body?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}:{})});const d=await r.json();if(!r.ok)throw Error(d.message||'HTTP '+r.status);return d}finally{clearTimeout(timer)}}
const transaction=new P.Transaction(request,{progress(n,total,results){by('progress').max=total;by('progress').value=n;message(`已确认 ${n}/${total} 条参数命令`);by('results').textContent=results.map(x=>x.command+'\n'+x.response).join('\n')}});
function lock(value){locked=value;by('editor').disabled=value;by('files').disabled=value;for(const id of ['apply','validate','query'])by(id).disabled=value;by('cancel').disabled=!value;for(const c of by('targets').querySelectorAll('input'))c.disabled=value}
function values(){return category==='afe5832'?config.afe5832?.raw_registers:category==='tx7332'?config.tx7332?.tx7332_config_data:config.bram?.[category]}
function put(value){if(category==='afe5832')config.afe5832=value;else if(category==='tx7332')config.tx7332={tx7332_config_data:value};else{config.bram??={};config.bram[category]=value}}
function categoryValue(){return category==='afe5832'?config.afe5832:values()}
function checkedTargets(){return [...selections].filter(k=>P.targets(config).includes(k))}
function targets(){by('targets').replaceChildren();for(const key of P.targets(config)){const l=document.createElement('label'),c=document.createElement('input');c.type='checkbox';c.checked=selections.has(key);c.disabled=locked;c.onchange=()=>{c.checked?selections.add(key):selections.delete(key)};l.append(c,document.createTextNode(P.names[key]));by('targets').append(l)}}
function input(value,change){const i=document.createElement('input');i.value=String(value);i.onchange=()=>{try{change(i.value);i.setCustomValidity('');syncJson();plot()}catch(e){i.setCustomValidity(e.message);i.reportValidity();message(e.message,true)}};return i}
function td(row,value){const c=document.createElement('td');if(value instanceof Node)c.append(value);else c.textContent=value;row.append(c)}
function drawAfeForm(){const afe=config.afe5832??{},v=afe.vca??{};by('use-vca').checked=!!afe.vca;for(const [id,key,def]of [['lna','lna_gain_db',18],['pga','pga_gain_db',24],['lpf','lpf_mhz',20],['hpf','lna_hpf_khz',100],['power','power_mode','low_noise']])by(id).value=v[key]??def;
 by('use-dhpf').checked=!!afe.dhpf;for(let n=0;n<4;n++){const g=afe.dhpf?.groups?.find(x=>Number(x.group)===n)??{};by('dhpf-enable-'+n).checked=g.enable??true;by('dhpf-corner-'+n).value=g.corner_code??3;by('dhpf-round-'+n).checked=g.round_enable??false}}
function updateAfe(){if(locked)return;const a=JSON.parse(JSON.stringify(config.afe5832??{}));
 if(by('use-vca').checked)a.vca={lna_gain_db:Number(by('lna').value),pga_gain_db:Number(by('pga').value),lpf_mhz:Number(by('lpf').value),lna_hpf_khz:Number(by('hpf').value),power_mode:by('power').value};else delete a.vca;
 if(by('use-dhpf').checked)a.dhpf={groups:Array.from({length:4},(_,n)=>({group:n,enable:by('dhpf-enable-'+n).checked,corner_code:P.uint(by('dhpf-corner-'+n).value,4),round_enable:by('dhpf-round-'+n).checked}))};else delete a.dhpf;
 config.afe5832=a;selections.add('afe5832');syncJson();targets();try{P.afeCommands(a);message('已修改浏览器草稿，尚未下发')}catch(e){message(e.message,true)}}
function syncJson(){by('category-json').value=JSON.stringify(categoryValue()??null,null,2)}
function draw(){
 const rows=values()??[],count=category==='tx7332'?Math.floor(rows.length/2):rows.length;page=Math.max(0,Math.min(page,Math.ceil(count/PAGE)-1));
 by('afe-form').hidden=category!=='afe5832';if(category==='afe5832')drawAfeForm();
 by('summary').textContent=category==='afe5832'?'表单生成命令在前，原始寄存器在后；同址后写会覆盖。':`${P.names[category]} · ${rows.length}/${category==='tx7332'?128:P.sections[category]} 个 32 位传输字；偏移单位为字。`;
 by('thead').innerHTML=category==='afe5832'?'<tr><th>序号</th><th>映射</th><th>地址</th><th>数据</th><th>操作</th></tr>':category==='tx7332'?'<tr><th>序号</th><th>地址字</th><th>数据字</th><th>操作</th></tr>':'<tr><th>字偏移</th><th>名称</th><th>32 位传输值</th></tr>';
 by('rows').replaceChildren();
 for(let n=page*PAGE;n<Math.min(count,(page+1)*PAGE);n++){
  const r=document.createElement('tr');td(r,n);
  if(category==='afe5832'){
   const v=rows[n],s=document.createElement('select');for(const m of ['GLOBAL','ADC','VCA','DTGC'])s.add(new Option(m,m));s.value=v.map;s.onchange=()=>{v.map=s.value;syncJson()};td(r,s);
   td(r,input(v.addr,x=>v.addr=P.hex(P.uint(x,8),2)));td(r,input(v.data,x=>v.data=P.hex(P.uint(x,16),4)));
   const actions=document.createElement('div'),rd=document.createElement('button'),del=document.createElement('button');rd.textContent='回读';rd.onclick=()=>readRegister('AFE RD '+v.map+' '+P.hex(P.uint(v.addr,8),2));del.textContent='删除';del.onclick=()=>{rows.splice(n,1);draw()};actions.append(rd,del);td(r,actions);
  }else if(category==='tx7332'){
   td(r,input(rows[n*2],x=>rows[n*2]=P.hex(P.uint(x,32))));td(r,input(rows[n*2+1],x=>rows[n*2+1]=P.hex(P.uint(x,32))));const b=document.createElement('button');b.textContent='回读';b.onclick=()=>{try{const address=P.uint(rows[n*2],32);if(address>0x3ff)throw Error('此地址字不能直接作为 10 位 TX 回读地址');readRegister('TX7332 RD '+P.hex(address))}catch(e){message(e.message,true)}};td(r,b);
  }else{td(r,category==='t_timing'?P.timingNames[n]:'—');td(r,input(rows[n],x=>{const v=P.uint(x,32);if(category==='t_timing')P.validateTiming([v],n);rows[n]=P.hex(v)}))}
  by('rows').append(r);
 }
 by('page').textContent=count?`${page+1}/${Math.ceil(count/PAGE)} 页 · ${count} 行`:'未加载此类别';by('previous').disabled=!page;by('next').disabled=(page+1)*PAGE>=count;syncJson();plot();targets();
}
function plot(){
 const raw=values()??[],v=category==='afe5832'?raw.map(x=>Number(x.data)):raw.map(Number),svg=by('plot');svg.replaceChildren();if(!v.length)return;
 const lo=Math.min(...v.slice(0,32768)),hi=Math.max(...v.slice(0,32768)),span=hi-lo||1;
 function shape(tag,attrs){const node=document.createElementNS('http://www.w3.org/2000/svg',tag);for(const[k,x]of Object.entries(attrs))node.setAttribute(k,x);svg.append(node)}
 if(by('plot-mode').value==='heat'){const columns=128,rows=Math.ceil(v.length/columns);for(let y=0;y<rows;y++)for(let x=0;x<columns&&y*columns+x<v.length;x++){const a=(v[y*columns+x]-lo)/span;shape('rect',{x:x*1000/columns,y:y*180/rows,width:1000/columns+.2,height:180/rows+.2,fill:`hsl(${210-170*a} 70% ${25+45*a}%)`})}}
 else{const step=Math.max(1,Math.ceil(v.length/1000));const pts=[];for(let n=0;n<v.length;n+=step)pts.push(`${n*990/Math.max(1,v.length-1)+5},${170-160*(v[n]-lo)/span}`);shape('polyline',{points:pts.join(' '),fill:'none',stroke:'#7ed8de','stroke-width':1.5})}
}
function validEditor(){for(const field of by('editor').querySelectorAll('input'))if(!field.reportValidity())throw Error('请先修正编辑框中的无效值；未发送任何参数')}
function preview(){validEditor();const commands=P.build(config,checkedTargets()),notes=P.warnings(commands);by('preview').textContent=notes.join('\n')+'\n'+`共 ${commands.length} 条参数命令；展示前 40 条：\n`+commands.slice(0,40).join('\n');return commands}
async function applyCommands(commands,down=null){
 if(locked)return;if(!confirm('将排空上传、关闭高压并停止扫描。完成后保持停止。确认继续？'))return;
 lock(true);by('progress').value=0;message('正在排空并进入配置事务…');try{const results=await transaction.run(commands,{down});by('results').textContent=results.slice(-16).map(x=>x.command+'\n'+x.response).join('\n');message('配置/读取已确认完成；扫描、高压及上传保持停止，请手动恢复。')}catch(e){message(e.message+'；不会自动重发或恢复成像。',true)}finally{lock(false)}
}
async function readRegister(command){try{validEditor();P.validate(config);await applyCommands([command])}catch(e){message(e.message,true)}}
function saveBlob(value,name){const url=URL.createObjectURL(new Blob([JSON.stringify(value,null,2)],{type:'application/json'})),a=document.createElement('a');a.href=url;a.download=name;a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)}
function setConfig(value){config=P.normalize(value);for(const t of P.targets(config))selections.add(t);page=0;draw()}
for(const[k,label]of Object.entries(P.names))by('category').add(new Option(label,k));
for(const n of Object.keys(P.HPF).map(Number).sort((a,b)=>a-b))by('hpf').add(new Option(n,n));
for(let n=0;n<4;n++){const d=document.createElement('div');d.innerHTML=`<strong>组 ${n}</strong> <label><input id="dhpf-enable-${n}" type="checkbox">启用</label><label>截止代码 <input id="dhpf-corner-${n}" type="number" min="0" max="15" style="width:65px"></label><label><input id="dhpf-round-${n}" type="checkbox">舍入</label>`;by('dhpf').append(d)}
for(const i of by('afe-form').querySelectorAll('input,select'))i.addEventListener('change',()=>{try{updateAfe()}catch(e){message(e.message,true)}});
by('category').onchange=()=>{category=by('category').value;page=0;draw()};by('previous').onclick=()=>{page--;draw()};by('next').onclick=()=>{page++;draw()};by('plot-mode').onchange=plot;
by('create').onclick=()=>{if(categoryValue()!==undefined&&!confirm('替换此类别的草稿？'))return;put(category==='afe5832'?{vca:{lna_gain_db:18,pga_gain_db:24,lpf_mhz:20,lna_hpf_khz:100,power_mode:'low_noise'},raw_registers:[]}:category==='t_timing'?[6144,7059,10,125,3,403,6656,915,6144,0]:Array(category==='tx7332'?128:P.sections[category]).fill('0x00000000'));selections.add(category);page=0;draw();message('仅创建编辑模板；零系数模板不代表可用成像配置，请导入/编辑正确数值后再下发。')};
by('add-reg').onclick=()=>{config.afe5832??={};config.afe5832.raw_registers??=[];config.afe5832.raw_registers.push({map:'ADC',addr:'0x00',data:'0x0000'});selections.add('afe5832');page=Math.floor((config.afe5832.raw_registers.length-1)/PAGE);draw()};
by('apply-json').onclick=()=>{const before=JSON.stringify(config);try{put(JSON.parse(by('category-json').value));setConfig(config);message('类别草稿已更新，尚未发送')}catch(e){config=JSON.parse(before);message(e.message,true)}};
by('import').onchange=async()=>{if(locked)return;lock(true);try{let next=JSON.parse(JSON.stringify(config));if(P.targets(next).length&&!confirm('合并导入配置，同名类别将由文件替换；继续？'))return;
 for(const f of by('import').files){if(f.size>LIMIT)throw Error('单个配置文件上限 2 MiB');const v=P.normalize(JSON.parse(await f.text()));next={...next,...v,bram:{...(next.bram??{}),...(v.bram??{})}}}if(JSON.stringify(next).length>LIMIT)throw Error('合并配置超过 2 MiB');setConfig(next);message('已导入浏览器草稿，尚未下发')}catch(e){message('导入失败，未发送参数：'+e.message,true)}finally{by('import').value='';lock(false)}};
by('export').onclick=()=>{try{P.validate(config);saveBlob(config,'ultrasound-parameters.json')}catch(e){message(e.message,true)}};
by('save-draft').onclick=()=>{try{P.validate(config);const text=JSON.stringify(config);if(text.length>LIMIT)throw Error('草稿超过 2 MiB');localStorage.setItem('ultrasound-parameters-v1',text);message('草稿已保存至本浏览器，不代表已应用硬件')}catch(e){message('草稿保存失败：'+e.message,true)}};
by('load-draft').onclick=()=>{try{const text=localStorage.getItem('ultrasound-parameters-v1');if(!text)throw Error('无已保存草稿');setConfig(JSON.parse(text));message('草稿已恢复，尚未下发')}catch(e){message(e.message,true)}};
by('clear').onclick=()=>{if(confirm('清空当前编辑内容？已保存草稿仍保留。')){config={format_version:1};selections.clear();draw()}};
by('validate').onclick=()=>{try{preview();message('全部配置校验通过；预览不是硬件应用结果')}catch(e){message(e.message,true)}};
by('apply').onclick=()=>{try{const commands=preview(),down=checkedTargets().includes('t_timing')?P.uint(config.bram.t_timing[9],3):null;applyCommands(commands,down)}catch(e){message(e.message,true)}};
by('cancel').onclick=()=>{transaction.cancel();message('已请求取消：当前批次结束后中止事务，已写入的参数不会回滚')};
by('safe-stop').onclick=async()=>{if(by('safe-stop').disabled)return;by('safe-stop').disabled=true;transaction.cancel();try{const r=await transaction.batch(['HV DISABLE']);message('安全停止已确认：'+r[0].response+'；不恢复扫描或上传')}catch(e){message('安全停止未确认：'+e.message+'；请检查设备，不会自动重发',true)}finally{by('safe-stop').disabled=false}};
by('query').onclick=async()=>{lock(true);try{const r=await transaction.batch(['STATUS']);const f=P.statusFields(r[0].response);by('device').textContent=r[0].response+'（刚刚查询；PS 软件记录）';if(f.cfg==='0')transaction.uncertain=false;message('状态查询完成；不会恢复扫描或高压')}catch(e){message(e.message,true)}finally{lock(false)}};
window.addEventListener('beforeunload',e=>{if(locked){e.preventDefault();e.returnValue='配置仍在执行；关闭页面不会回滚已写入参数'}});
draw();
