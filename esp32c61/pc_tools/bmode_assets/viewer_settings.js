'use strict';
let displayConfig=null,displayVersion=0,displayBusy=false;
const displayFields={dr:'dynamic_range_db',brightness:'brightness_db',contrast:'contrast',noise:'noise_floor_db'};
function displayMessage(text,error=false){el('display-message').textContent=text;el('display-message').classList.toggle('error',error)}
function displayLock(value){displayBusy=value;for(const id of ['display-apply','display-import','display-reload','display-export'])el(id).disabled=value}
async function settingsApi(body){const r=await fetch('/api/viewer/settings',{cache:'no-store',...(body?{method:'PUT',headers:{'Content-Type':'application/json'},body:JSON.stringify(body)}:{})});const d=await r.json();if(!r.ok)throw Error(d.error||'显示设置请求失败');return d}
function showDisplaySettings(result){displayConfig=result.settings;displayVersion=result.revision;for(const[id,key]of Object.entries(displayFields)){el('display-'+id).value=displayConfig[key];el('value-'+id).textContent=displayConfig[key]}
 el('display-tgc').checked=displayConfig.tgc_enabled;el('display-depth').value=displayConfig.depth_mm;el('display-points').value=JSON.stringify(displayConfig.tgc_points);el('display-lut').value=JSON.stringify(displayConfig.lut);plotTgc();
}
function editedDisplay(){if(!displayConfig)throw Error('请先读取显示设置');const s=JSON.parse(JSON.stringify(displayConfig));for(const[id,key]of Object.entries(displayFields))s[key]=Number(el('display-'+id).value);s.tgc_enabled=el('display-tgc').checked;s.depth_mm=Number(el('display-depth').value);s.tgc_points=JSON.parse(el('display-points').value);s.lut=JSON.parse(el('display-lut').value);return s}
function plotTgc(){try{const pts=JSON.parse(el('display-points').value);if(!Array.isArray(pts))return;const svg=el('tgc-plot');svg.replaceChildren();const line=document.createElementNS('http://www.w3.org/2000/svg','polyline');line.setAttribute('points',pts.map(p=>`${Number(p[0])/6143*600},${60-Number(p[1])*2.3}`).join(' '));line.setAttribute('fill','none');line.setAttribute('stroke','#74d7de');line.setAttribute('stroke-width','2');svg.append(line)}catch{displayMessage('TGC 需要合法的 JSON 控制点数组',true)}}
async function reloadDisplay(){if(displayBusy)return;displayLock(true);try{showDisplaySettings(await settingsApi());displayMessage('已读取 PC 显示配置；不代表硬件参数已匹配')}catch(e){displayMessage(e.message,true)}finally{displayLock(false)}}
async function applyDisplay(){if(displayBusy)return;displayLock(true);try{const result=await settingsApi({revision:displayVersion,settings:editedDisplay()});showDisplaySettings(result);clearPicture();displayMessage('已接受显示设置，等待图像线程应用；不下发硬件')}catch(e){displayMessage(e.message+'；未自动重发，请重新读取后确认。',true)}finally{displayLock(false)}}
function renderDisplayStatus(s){
 const d=s.image_display_settings||s.display_settings;if(!d)return;
 el('geometry-title').textContent=`B-mode · ${Number(d.angles_deg[0]).toFixed(1)}°～${Number(d.angles_deg[63]).toFixed(1)}° / ${d.depth_mm} mm`;
 for(const fraction of [25,50,75,100])el('depth'+fraction).textContent=(d.depth_mm*fraction/100).toFixed(1);
 const left=Math.min(0,Math.sin(d.angles_deg[0]*Math.PI/180))*d.depth_mm,right=Math.max(0,Math.sin(d.angles_deg[63]*Math.PI/180))*d.depth_mm;
 el('width-left').textContent=left.toFixed(1)+' mm';el('width-right').textContent=right.toFixed(1)+' mm';el('viewport').style.aspectRatio=String((right-left)/d.depth_mm);
 el('display-depth').disabled=['starting','running','stopping'].includes(s.state);
 el('display-revision').textContent=`设置版本 ${s.settings_revision??'—'} · 图像线程已应用 ${s.applied_settings_revision??'—'} · 当前图像版本 ${s.image_settings_revision||'尚无'}。抽取以实际帧为准。`;
}
for(const id of Object.keys(displayFields))el('display-'+id).addEventListener('input',()=>{el('value-'+id).textContent=el('display-'+id).value});
el('display-points').addEventListener('change',plotTgc);el('display-apply').addEventListener('click',applyDisplay);el('display-reload').addEventListener('click',reloadDisplay);
el('display-import').addEventListener('change',async()=>{if(displayBusy)return;displayLock(true);try{const file=el('display-import').files[0];if(!file)return;if(file.size>2*1024*1024)throw Error('配置文件上限 2 MiB');if(!displayConfig)throw Error('请先读取显示设置');const source=JSON.parse(await file.text()),profile={};
 if(source.viewer!==undefined)profile.viewer=source.viewer;else if(source.version===1&&source.dynamic_range_db!==undefined)profile.viewer=source;
 if(source.bram?.sin_beta!==undefined)profile.bram={sin_beta:source.bram.sin_beta};if(!profile.viewer&&!profile.bram)throw Error('文件没有 viewer 设置或 sin_beta 角度表');
 if(!confirm('仅将此文件的显示设置和角度映射应用至 PC。请确认它与已下发的硬件配置对应；不会写入硬件。几何变更前需停止接收。'))return;
 const result=await settingsApi({revision:displayVersion,profile,confirm_geometry:true});showDisplaySettings(result);clearPicture();displayMessage('已导入显示配置；文件与硬件对应关系由用户确认，抽取继续按实际帧判断。')
 }catch(e){displayMessage(e.message+'；未自动重发。',true)}finally{displayLock(false);el('display-import').value=''}});
el('display-export').addEventListener('click',()=>{try{const text=JSON.stringify({format_version:1,viewer:editedDisplay()},null,2),url=URL.createObjectURL(new Blob([text],{type:'application/json'})),a=document.createElement('a');a.href=url;a.download='ultrasound-display.json';a.click();setTimeout(()=>URL.revokeObjectURL(url),1000)}catch(e){displayMessage(e.message,true)}});
el('lut-file').addEventListener('change',async()=>{try{const file=el('lut-file').files[0];if(!file)return;if(file.size>16384)throw Error('LUT 文件上限 16 KiB');const text=await file.text(),start=text.indexOf('['),end=text.lastIndexOf(']'),values=start>=0?JSON.parse(text.slice(start,end+1)):text.trim().split(/[\s,]+/).map(Number);if(!Array.isArray(values)||values.length!==256||values.some(v=>!Number.isInteger(v)||v<0||v>255))throw Error('LUT 需要 256 个 0～255 整数');el('display-lut').value=JSON.stringify(values);displayMessage('LUT 已载入草稿，点击应用显示设置后生效')}catch(e){displayMessage(e.message,true)}finally{el('lut-file').value=''}});
reloadDisplay();
