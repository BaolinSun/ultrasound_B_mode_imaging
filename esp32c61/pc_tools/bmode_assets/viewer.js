'use strict';
const el=id=>document.getElementById(id);
let frozen=false, sequence=0, revision=0, objectUrl=null, imagePending=false, statusPending=false;
let available=false, offline=false, actionPending=false;
const states={idle:'未接收',starting:'正在启动',running:'正在接收',stopping:'正在停止',error:'接收异常'};
async function api(path,body){
 const response=await fetch(path,{cache:'no-store',...(body?{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}:{})});
 const data=await response.json();if(!response.ok)throw Error(data.error||'请求失败');return data;
}
function clearPicture(){
 revision++;sequence=0;el('image').hidden=true;el('empty').hidden=false;
 el('image').removeAttribute('src');if(objectUrl){URL.revokeObjectURL(objectUrl);objectUrl=null}
}
function render(s){
 offline=false;available=!!s.image_available;
 el('state').textContent=states[s.state]||'状态未知';
 el('start').disabled=actionPending||['starting','running','stopping'].includes(s.state);
 el('stop').disabled=actionPending||!['starting','running'].includes(s.state);
 el('connection').textContent=`ESP32 ${s.device_ip} · 数据端口 ${s.port} · 协议 ${(s.transport||'待检测').toUpperCase()}`;
 el('error').textContent=s.error||s.processing_error||'';
 for(const [id,key,suffix] of [['receive-fps','receive_fps',' FPS'],['display-fps','display_fps',' FPS'],['throughput','payload_mbit_s',' Mbit/s']])el(id).textContent=Number(s[key]||0).toFixed(2)+suffix;
 el('coverage').textContent=`${s.lines||0} / 64`;
 const source=s.source_status||{};
 el('pipeline').textContent=`缓存状态采样：PL 完整帧发布 ${Number(s.source_fps||0).toFixed(2)} FPS；SDIO 接收 ${Number(s.sdio_fps||0).toFixed(2)} FPS；源端丢弃 ${source.source_drops||0} / 错误 ${source.source_errors||0}；最近 SDIO ${((source.last_receive_us||0)/1000).toFixed(1)} ms`;
 el('frame-format').textContent=`当前：${s.sample_rate_mhz??25} MHz · ${s.samples_per_line??6144} 点 × 64 线 · 抽取 2^${s.decimation||0} · Q2.14 · 双线性扫描转换 · TGC 开启 · DR 45 dB · 亮度 +3 dB · 参考灰度 LUT`;
 el('counters').textContent=`有效载荷 ${s.received_frames||0} · 网络坏帧 ${s.bad_frames||0} · 协议错误 ${s.protocol_errors||0} · 图像整帧格式错误 ${s.format_errors||0} · 处理丢弃 ${s.queue_drops||0} · 本地 UDP 队列丢包 ${s.local_queue_drops||0} · 非法数据报 ${s.invalid_datagrams||0}`;
 const age=s.image_age_s;
 const stale=s.state!=='running'||s.lines!==64||age===null||age>5;
 el('image').classList.toggle('stale',stale);
 el('image-note').textContent=frozen?'画面已冻结；后台继续接收，继续显示时获取最新图像':!available?`等待合法图像线（${s.lines||0}/64）；格式错误时不会强行绘图`:`${stale?'画面非实时 / 行数据过期；':'完整帧图像；'}最近发布 ${Number(age).toFixed(1)} 秒前 · 会话 ${s.session??'—'} · 载荷 ${s.image_frame??'—'}`;
 if(!available){clearPicture();el('empty').textContent=`等待完整的 64 条合法图像线（${s.lines||0}/64）`}
}
async function pollStatus(){
 if(statusPending)return;statusPending=true;
 try{render(await api('/api/status'))}catch(error){offline=true;el('state').textContent='服务连接断开';el('error').textContent=error.message;el('start').disabled=true;el('stop').disabled=true;el('image').classList.toggle('stale',true);el('image-note').textContent='服务已断开，保留的画面不是实时图像'}finally{statusPending=false}
}
async function pollImage(){
 if(imagePending||frozen||offline||!available)return;
 imagePending=true;const requestRevision=revision;
 try{
  const response=await fetch('/api/image?after='+sequence,{cache:'no-store'});
  if(response.status===204)return;
  if(!response.ok)throw Error('图像读取失败');
  const next=Number(response.headers.get('X-Image-Sequence'));
  const blob=await response.blob();
  if(frozen||requestRevision!==revision||!available||!Number.isSafeInteger(next)||next<=sequence)return;
  const old=objectUrl;objectUrl=URL.createObjectURL(blob);sequence=next;
  el('image').src=objectUrl;el('image').hidden=false;el('empty').hidden=true;
  if(old)URL.revokeObjectURL(old);
 }catch(error){el('error').textContent=error.message}finally{imagePending=false}
}
async function action(name){
 if(actionPending)return;actionPending=true;el('start').disabled=true;el('stop').disabled=true;
 try{await api('/api/viewer/'+name,{});if(name==='start'){clearPicture();frozen=false;el('freeze').textContent='冻结画面'}}catch(error){el('error').textContent=error.message}finally{actionPending=false;await pollStatus()}
}
el('start').addEventListener('click',()=>action('start'));
el('stop').addEventListener('click',()=>action('stop'));
el('freeze').addEventListener('click',()=>{frozen=!frozen;revision++;el('freeze').textContent=frozen?'继续显示':'冻结画面';if(!frozen)pollImage();pollStatus()});
async function imageLoop(){try{await pollImage()}finally{setTimeout(imageLoop,100)}}
pollStatus();setInterval(pollStatus,1000);imageLoop();
