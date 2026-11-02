'use strict';
let displayConfig=null,displayVersion=0,displayBusy=false;
let displayDirty=0,displaySent=0,displayTimer=null,displayBlocked=false;
let displayInputsLocked=false;
const depthPresets=[40,80,120,160,180];
const displayFields={dr:'dynamic_range_db',brightness:'brightness_db',contrast:'contrast',noise:'noise_floor_db'};
function displayMessage(text,error=false){el('display-message').textContent=text;el('display-message').classList.toggle('error',error)}
function paintDepth(){const depth=Number(el('display-depth').value),valid=Number.isFinite(depth)&&depth>=10&&depth<=188,locked=displayInputsLocked||!displayConfig;
 el('value-depth').textContent=valid?String(depth):'—';el('depth-cm').textContent=valid?(depth/10).toFixed(1)+' cm · 显示范围':'请选择显示深度';
 el('display-depth').setAttribute('aria-valuetext',valid?depth+' 毫米':'未设置');
 el('depth-less').disabled=locked||!valid||depth<=10;el('depth-more').disabled=locked||!valid||depth>=188;
 for(const value of depthPresets){const button=el('depth-preset-'+value);button.disabled=locked;button.setAttribute('aria-pressed',String(valid&&depth===value))}
}
function chooseDepth(value){if(displayInputsLocked||!displayConfig)return;const depth=Math.max(10,Math.min(188,value));if(depth===Number(el('display-depth').value))return;el('display-depth').value=String(depth);paintDepth();editDisplay()}
function displayLock(value,inputs=false){displayBusy=value;for(const id of ['display-import','display-reload','display-export'])el(id).disabled=value;if(inputs||!value){displayInputsLocked=value;for(const id of ['dr','brightness','contrast','noise','tgc','depth','points','lut'])el('display-'+id).disabled=value}paintDepth();paintTgcLock()}
async function settingsApi(body){const r=await fetch('/api/viewer/settings',{cache:'no-store',...(body?{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}:{})});const d=await r.json();if(!r.ok)throw Error(d.error||'显示设置请求失败');return d}
function showDisplaySettings(result){displayConfig=result.settings;displayVersion=result.revision;tgcUndo=null;for(const[id,key]of Object.entries(displayFields)){el('display-'+id).value=displayConfig[key];el('value-'+id).textContent=displayConfig[key]}
 el('display-tgc').checked=displayConfig.tgc_enabled;el('display-depth').value=displayConfig.depth_mm;paintDepth();el('display-points').value=JSON.stringify(displayConfig.tgc_points);el('display-lut').value=JSON.stringify(displayConfig.lut);plotTgc();syncTgc();
}
function editedDisplay(){if(!displayConfig)throw Error('请先读取显示设置');const s=JSON.parse(JSON.stringify(displayConfig));for(const[id,key]of Object.entries(displayFields))s[key]=Number(el('display-'+id).value);s.tgc_enabled=el('display-tgc').checked;s.depth_mm=Number(el('display-depth').value);s.tgc_points=JSON.parse(el('display-points').value);s.lut=JSON.parse(el('display-lut').value);return s}
function plotTgc(){try{const pts=JSON.parse(el('display-points').value);if(!Array.isArray(pts))return;const svg=el('tgc-plot');svg.replaceChildren();const line=document.createElementNS('http://www.w3.org/2000/svg','polyline');line.setAttribute('points',pts.map(p=>`${Number(p[0])/6143*600},${60-Number(p[1])*2.3}`).join(' '));line.setAttribute('fill','none');line.setAttribute('stroke','#74d7de');line.setAttribute('stroke-width','2');svg.append(line)}catch{displayMessage('TGC 需要合法的 JSON 控制点数组',true)}}
function validateDisplayDraft(s){
 for(const [key,lo,hi] of [['dynamic_range_db',20,70],['brightness_db',-10,20],['contrast',.5,2],['noise_floor_db',-60,-20],['depth_mm',10,188]])if(!Number.isFinite(s[key])||s[key]<lo||s[key]>hi)throw Error(key+' 超出允许范围，保留上一组有效图像');
 if(!Array.isArray(s.lut)||s.lut.length!==256||s.lut.some(v=>!Number.isInteger(v)||v<0||v>255))throw Error('LUT 需要 256 个 0～255 整数');
 const p=s.tgc_points;if(!Array.isArray(p)||p.length<2||p.length>128||p.some((v,i)=>!Array.isArray(v)||v.length!==2||v.some(x=>typeof x!=='number'||!Number.isFinite(x))||v[0]<0||v[0]>6143||v[1]<-24||v[1]>24||(i&&v[0]<=p[i-1][0]))||p[0][0]!==0||p.at(-1)[0]!==6143)throw Error('TGC 控制点需递增、覆盖 0～6143，增益为 −24～24 dB');
 return s;
}
function queueDisplay(){
 if(displayTimer!==null)clearTimeout(displayTimer);
 if(!displayConfig||displayBlocked)return;
 displayTimer=setTimeout(()=>{displayTimer=null;applyDisplay()},200);
}
function editDisplay(){displayDirty++;if(displayBlocked){displayMessage('自动更新已暂停，请重新读取设置后调整。',true);return}displayMessage('正在调整…有效参数将自动更新图像');queueDisplay()}
async function reloadDisplay(){if(displayBusy)return;if(displayTimer!==null){clearTimeout(displayTimer);displayTimer=null}displayLock(true,true);try{showDisplaySettings(await settingsApi());displayDirty=displaySent=0;displayBlocked=false;displayMessage('自动更新已启用；调整参数即可生效，不改变硬件或接收状态')}catch(e){displayBlocked=true;displayMessage(e.message,true)}finally{displayLock(false)}}
async function applyDisplay(){
 if(displayBusy||displayBlocked||!displayConfig||displayDirty===displaySent)return;
 let settings;try{settings=validateDisplayDraft(editedDisplay())}catch(e){displayMessage('参数尚未完整或无效：'+e.message,true);return}
 const sent=displayDirty;displayLock(true);
 try{const result=await settingsApi({revision:displayVersion,settings});displayConfig=result.settings;displayVersion=result.revision;displaySent=sent;displayMessage(displayDirty===sent?'已自动更新，等待图像线程发布；接收持续运行':'正在应用最新调整…')}
 catch(e){displayBlocked=true;displayMessage(e.message+'；自动更新已暂停，请重新读取设置后调整。',true)}
 finally{displayLock(false);if(!displayBlocked&&displayDirty!==displaySent)queueDisplay()}
}
function renderDisplayStatus(s){
 const actualRevision=typeof imageDisplayRevision!=='undefined'&&imageDisplayRevision?imageDisplayRevision:s.image_settings_revision;
 el('display-revision').textContent=`设置版本 ${s.settings_revision??'—'} · 图像线程已应用 ${s.applied_settings_revision??'—'} · 当前图像版本 ${actualRevision||'尚无'}。抽取以实际帧为准。`;
 if(typeof frozen!=='undefined'&&frozen)return;
 const d=(typeof imageDisplaySettings!=='undefined'&&imageDisplaySettings)||s.image_display_settings||s.display_settings;if(!d)return;
 el('geometry-title').textContent=`B-mode · ${Number(d.angles_deg[0]).toFixed(1)}°～${Number(d.angles_deg[63]).toFixed(1)}° / ${d.depth_mm} mm`;
 for(const fraction of [25,50,75,100])el('depth'+fraction).textContent=(d.depth_mm*fraction/100).toFixed(1);
 const left=Math.min(0,Math.sin(d.angles_deg[0]*Math.PI/180))*d.depth_mm,right=Math.max(0,Math.sin(d.angles_deg[63]*Math.PI/180))*d.depth_mm;
 el('width-left').textContent=left.toFixed(1)+' mm';el('width-right').textContent=right.toFixed(1)+' mm';el('viewport').style.aspectRatio=String((right-left)/d.depth_mm);


}
for(const id of Object.keys(displayFields))el('display-'+id).addEventListener('input',()=>{el('value-'+id).textContent=el('display-'+id).value;editDisplay()});
el('display-points').addEventListener('input',()=>{plotTgc();syncTgc();editDisplay()});
el('display-depth').addEventListener('input',()=>{paintDepth();editDisplay()});
el('depth-less').addEventListener('click',()=>chooseDepth(Number(el('display-depth').value)-5));el('depth-more').addEventListener('click',()=>chooseDepth(Number(el('display-depth').value)+5));
for(const value of depthPresets)el('depth-preset-'+value).addEventListener('click',()=>chooseDepth(value));
el('display-lut').addEventListener('input',editDisplay);el('display-tgc').addEventListener('change',()=>{syncTgc();editDisplay()});el('display-reload').addEventListener('click',reloadDisplay);
el('display-import').addEventListener('change',async()=>{if(displayBusy)return;displayLock(true,true);try{const file=el('display-import').files[0];if(!file)return;if(file.size>2*1024*1024)throw Error('配置文件上限 2 MiB');if(!displayConfig)throw Error('请先读取显示设置');const source=JSON.parse(await file.text()),profile={};
 if(source.viewer!==undefined)profile.viewer=source.viewer;else if(source.version===1&&source.dynamic_range_db!==undefined)profile.viewer=source;
 if(source.bram?.sin_beta!==undefined)profile.bram={sin_beta:source.bram.sin_beta};if(!profile.viewer&&!profile.bram)throw Error('文件没有 viewer 设置或 sin_beta 角度表');
 if(!confirm('仅将此文件的显示设置和角度映射应用至 PC。请确认它与已下发的硬件配置对应；不会写入硬件。只有角度映射变更需要停止接收。'))return;
 const result=await settingsApi({revision:displayVersion,profile,confirm_geometry:true});showDisplaySettings(result);displayDirty=displaySent=0;displayBlocked=false;displayMessage('已导入显示配置；文件与硬件对应关系由用户确认，抽取继续按实际帧判断。')
 }catch(e){displayMessage(e.message+'；未自动重发。',true)}finally{displayLock(false);el('display-import').value=''}});
el('display-export').addEventListener('click',()=>{try{const text=JSON.stringify({format_version:1,viewer:editedDisplay()},null,2),url=URL.createObjectURL(new Blob([text],{type:'application/json'})),a=document.createElement('a');a.href=url;a.download='ultrasound-display.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)}catch(e){displayMessage(e.message,true)}});
el('lut-file').addEventListener('change',async()=>{try{const file=el('lut-file').files[0];if(!file)return;if(file.size>16384)throw Error('LUT 文件上限 16 KiB');const text=await file.text(),start=text.indexOf('['),end=text.lastIndexOf(']'),values=start>=0?JSON.parse(text.slice(start,end+1)):text.trim().split(/[\s,]+/).map(Number);if(!Array.isArray(values)||values.length!==256||values.some(v=>!Number.isInteger(v)||v<0||v>255))throw Error('LUT 需要 256 个 0～255 整数');el('display-lut').value=JSON.stringify(values);editDisplay()}catch(e){displayMessage(e.message,true)}finally{el('lut-file').value=''}});

// Positions are ORIGINAL 25 MHz samples: clipping depth and decimation must not move TGC.
const tgcPositions=Array.from({length:8},(_,i)=>Math.round(i*6143/7));
const tgcOriginal=[[0,0],[512,0],[1024,0],[2048,0],[3072,0],[3247,0],[4096,8],[4608,13],[5120,17],[5632,21],[6143,24]];
const tgcProfiles={
 default:{name:'原默认',points:tgcOriginal},flat:{name:'平直',gains:[0,0,0,0,0,0,0,0]},
 deep:{name:'深部增强',gains:[-6,-3,0,3,6,10,15,20]},near:{name:'浅部增强',gains:[20,15,10,6,3,0,-3,-6]},
 gentle:{name:'渐进增强',gains:[0,2,4,6,8,10,12,14]},middle:{name:'中段增强',gains:[0,3,8,13,13,8,3,0]},
 edges:{name:'两端增强',gains:[12,8,3,0,0,3,8,12]},reduce:{name:'整体减弱',gains:Array(8).fill(-8)},boost:{name:'整体增强',gains:Array(8).fill(8)}
};
let tgcUndo=null;
function tgcPoints(profile){return profile.points||tgcPositions.map((x,i)=>[x,profile.gains[i]])}
function tgcAt(points,x){let i=1;while(i<points.length-1&&points[i][0]<x)i++;const a=points[i-1],b=points[i];return a[1]+(b[1]-a[1])*(x-a[0])/(b[0]-a[0])}
function readTgc(){const p=JSON.parse(el('display-points').value);validateDisplayDraft({...editedDisplay(),tgc_points:p});return p}
function paintTgcLock(){const locked=displayInputsLocked||!displayConfig,off=!el('display-tgc').checked;
 for(let i=0;i<8;i++)el('tgc-band-'+i).disabled=locked||off;
 for(const key of Object.keys(tgcProfiles))el('tgc-preset-'+key).disabled=locked||off;
 el('tgc-undo').disabled=locked||off||!tgcUndo;el('tgc-bank').setAttribute('data-enabled',String(!off));
}
function syncTgc(){try{const points=readTgc();let selected='自定义曲线';
 for(let i=0;i<8;i++){const gain=tgcAt(points,tgcPositions[i]);el('tgc-band-'+i).value=String(Math.round(gain*2)/2);el('tgc-depth-'+i).textContent=(tgcPositions[i]*1540/25000000/2*1000).toFixed(1);el('tgc-value-'+i).textContent=(gain>0?'+':'')+Number(gain.toFixed(1))+' dB';el('tgc-band-'+i).setAttribute('aria-valuetext',gain.toFixed(1)+' dB')}
 for(const [key,profile] of Object.entries(tgcProfiles)){const ref=tgcPoints(profile),match=points.length===ref.length&&points.every((p,i)=>p[0]===ref[i][0]&&Math.abs(p[1]-ref[i][1])<.000001);el('tgc-preset-'+key).setAttribute('aria-pressed',String(match));if(match)selected=profile.name}
 el('tgc-selected').textContent='当前：'+selected+(el('display-tgc').checked?'':' · 已关闭');
 }catch{el('tgc-selected').textContent='控制点无效，请修正 JSON 或重新读取'}paintTgcLock()}
function setTgc(points,remember=true){if(displayInputsLocked||!displayConfig||!el('display-tgc').checked)return;
 if(remember){try{tgcUndo=JSON.parse(el('display-points').value)}catch{tgcUndo=null}}
 el('display-points').value=JSON.stringify(points);plotTgc();syncTgc();editDisplay();
}
function drawTgcIcon(key,profile){const svg=el('tgc-icon-'+key);svg.replaceChildren();for(let i=0;i<8;i++){const y=5+i*7,gain=tgcAt(tgcPoints(profile),tgcPositions[i]),x=10+(gain+24)/48*80;
 for(const [cls,x1,x2] of [['rail',8,92],['handle',x-3,x+3]]){const line=document.createElementNS('http://www.w3.org/2000/svg','line');for(const [attr,value]of Object.entries({x1,x2,y1:y,y2:y,class:cls}))line.setAttribute(attr,String(value));svg.append(line)}}}
for(const [key,profile]of Object.entries(tgcProfiles)){drawTgcIcon(key,profile);el('tgc-preset-'+key).addEventListener('click',()=>setTgc(tgcPoints(profile)))}
for(let i=0;i<8;i++)el('tgc-band-'+i).addEventListener('input',()=>{const points=tgcPositions.map((x,j)=>[x,Number(el('tgc-band-'+j).value)]);setTgc(points)});
el('tgc-undo').addEventListener('click',()=>{if(tgcUndo){const points=tgcUndo;tgcUndo=null;setTgc(points,false)}});

reloadDisplay();
