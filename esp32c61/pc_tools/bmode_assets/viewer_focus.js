'use strict';
let viewerFocusCatalog=null,viewerFocusPending=false,viewerFocusError='';
const viewerFocusPresets=[20,50,80,110,140,170];
function focusMessage(text,error=false){el('focus-result').textContent=text;el('focus-result').classList.toggle('error',error)}
function renderFocusStatus(s=lastViewerStatus){
 const depth=Number(el('focus-depth').value),profile=viewerFocusCatalog?.profiles.find(p=>p.focus_mm===depth);
 const blocked=viewerFocusPending||!!actionPending||!!s.control_action||['starting','stopping','configuring','offline'].includes(s.state)||offline||!viewerFocusCatalog;
 el('focus-value').textContent=profile?String(depth):'—';el('focus-cm').textContent=profile?(depth/10).toFixed(1)+' cm':'读取中';
 el('focus-depth').disabled=blocked;el('focus-depth').setAttribute('aria-valuetext',depth+' 毫米，待应用焦点');
 el('focus-less').disabled=blocked||depth<=20;el('focus-more').disabled=blocked||depth>=170;el('focus-apply').disabled=blocked||!profile;
 el('focus-apply').setAttribute('aria-busy',String(viewerFocusPending||s.control_action==='focus'));
 el('focus-apply').setAttribute('title',`下发 ${depth} mm 焦点配置（停扫并关闭高压）`);
 for(const d of viewerFocusPresets){el('focus-preset-'+d).disabled=blocked;el('focus-preset-'+d).setAttribute('aria-pressed',String(d===depth&&!!profile))}
 if(s.control_action==='focus')focusMessage(s.state==='configuring'?`焦点切换 ${s.focus?.completed||0}/1`:'焦点：正在停扫并排空上传…');
 else if(s.error)focusMessage('操作失败，请查看页面提示',true);
 else if(viewerFocusError)focusMessage(viewerFocusError,true);
 else if(s.focus?.last_applied_mm)focusMessage('最近下发 '+s.focus.last_applied_mm+' mm');
 else focusMessage(viewerFocusPending?'正在提交焦点配置…':'焦点：选择后点击右上角图标下发');
}
function chooseViewerFocus(depth){if(el('focus-depth').disabled)return;viewerFocusError='';el('focus-depth').value=String(Math.min(170,Math.max(20,depth)));renderFocusStatus()}
async function applyViewerFocus(){
 if(el('focus-apply').disabled)return;
 if(!confirm('应用焦点会停止接收、关闭高压并停止扫描，完成后不自动恢复。确认下发所选延时表？'))return;
 viewerFocusError='';viewerFocusPending=true;renderFocusStatus();focusMessage('正在提交焦点配置…');
 try{await api('/api/viewer/focus',{focus_mm:Number(el('focus-depth').value)});revision++;await pollStatus()}
 catch(e){viewerFocusError=e.message;focusMessage(e.message,true)}finally{viewerFocusPending=false;renderFocusStatus()}
}
el('focus-depth').addEventListener('input',()=>{viewerFocusError='';renderFocusStatus()});
el('focus-less').addEventListener('click',()=>chooseViewerFocus(Number(el('focus-depth').value)-10));
el('focus-more').addEventListener('click',()=>chooseViewerFocus(Number(el('focus-depth').value)+10));
for(const d of viewerFocusPresets)el('focus-preset-'+d).addEventListener('click',()=>chooseViewerFocus(d));
el('focus-apply').addEventListener('click',applyViewerFocus);
(async()=>{try{
 const result=await api('/api/viewer/focus');
 if(result.version!==1||!Array.isArray(result.profiles)||result.profiles.length!==16||new Set(result.profiles.map(p=>p.focus_mm)).size!==16||result.profiles.some(p=>!Number.isInteger(p.focus_mm)||p.focus_mm<20||p.focus_mm>170||p.focus_mm%10||p.words!==16384))throw Error(result.error||'焦点目录不完整，请检查 PC configs 文件');
 viewerFocusCatalog=result;focusMessage('焦点：选择后点击右上角图标下发');
 }catch(e){viewerFocusError=e.message;focusMessage(e.message,true)}finally{renderFocusStatus()}})();
