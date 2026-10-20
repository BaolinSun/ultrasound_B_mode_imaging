// Runs the actual embedded page script with a DOM/API test double.
// This verifies client interactions, not browser rendering or ESP radio/NVS hardware.
import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import {readFileSync} from 'node:fs';

const html = readFileSync(new URL('../main/index.html', import.meta.url), 'utf8');
const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
function setup() {
  const elements = {};
  for (const match of html.matchAll(/<[^>]*\bid="([^"]+)"[^>]*>/g)) {
    const classes = new Set();
    elements[match[1]] = {
      value: match[0].match(/\bvalue="([^"]*)"/)?.[1] || '', textContent: '', checked: false,
      disabled: match[0].includes(' disabled'), listeners: {},
      classList: {toggle(name, enable) {if(enable)classes.add(name);else classes.delete(name)},
                  contains(name) {return classes.has(name)}},
      addEventListener(name, handler) {this.listeners[name] = handler},
      setAttribute(name, value) {this[name] = String(value)},
      getAttribute(name) {return this[name] ?? null},
      removeAttribute(name) {delete this[name]},
    };
  }
  const backend = {
    config: {ssid: '', has_password: false, target_ip: '', target_port: 50001},
    status: {connected: false, upload_ready: false, ap_active: true, reconfiguring: false,
      sta_ip: '', ap_ssid: 'Ultrasound-C61-123456', psram_total: 2097152, psram_free: 1209696,
      psram_largest: 1200000, selftest_bytes: 787456, max_bytes: 1048576, busy: false,
      result: 'not_ready', total_bytes: 0, sent_bytes: 0, packets: 0, retries: 0,
      socket_errno: 0, active_ip: '', active_port: 0},
    offline: false, failSave: false, calls: [], now:1700000000000,
  };
  backend.confirmed=true;
  const context = vm.createContext({crypto:{getRandomValues(a){a[0]=123;return a}},confirm(){return backend.confirmed},setTimeout,
  document: {getElementById(id) {
    assert.ok(elements[id], `page referenced nonexistent element ${id}`); return elements[id];
  }}, setInterval() {}, console, Date:class extends Date{static now(){return backend.now}},
  async fetch(path, options) {
    backend.calls.push({path, options});
    if(backend.offline)throw Error('network unavailable');
    let result, ok = true;
    if(path === '/api/config' && options.method === 'PUT') {
      const candidate = JSON.parse(options.body);
      if(backend.failSave){ok=false;result={message:'NVS 保存失败，当前配置未改变'}}
      else {
        const wifiChanged = candidate.ssid !== backend.config.ssid || 'password' in candidate;
        backend.config={ssid:candidate.ssid,has_password:'password' in candidate ? !!candidate.password : backend.config.has_password,
          target_ip:candidate.target_ip,target_port:candidate.target_port};
        if(wifiChanged){backend.status.reconfiguring=true;backend.status.upload_ready=false}
        result={message:'配置已保存'};
      }
    } else if(path === '/api/config')result={...backend.config};
    else if(path === '/api/status')result={...backend.status};
    else if(path === '/api/upload') {
      backend.status.busy=true;backend.status.result='busy';backend.status.total_bytes=787456;
      backend.status.active_ip=backend.config.target_ip;backend.status.active_port=backend.config.target_port;
      result={message:'已接受单次 PSRAM 自测上传'};
    } else if(path === '/api/sdio/start') {
      if(!backend.status.sdio?.host_sdio){ok=false;result={message:'先选择 OUTPUT SDIO'}}
      else {backend.status.sdio.running=true;result={message:'SDIO 测试已启动'}}
    } else if(path === '/api/stop' || path === '/api/sdio/stop') {
      if(backend.status.sdio)backend.status.sdio.running=false;
      backend.status.busy=false;backend.status.result='cancelled';result={message:'停止请求已提交'};
    } else if(path === '/api/zynq/commands'){
      backend.commandBody=JSON.parse(options.body);
      if(backend.commandHook)await backend.commandHook();
      if(backend.commandConflict){ok=false;result={message:'控制任务忙碌或配置被其他客户端占用'}}
      else result={job_id:'00000001'};
    }
    else if(path === '/api/zynq/jobs/00000001'){
      if(backend.jobHook)await backend.jobHook();
      result=backend.job||{busy:false,state:'succeeded',count:1,completed:1,
        results:[{command:'STATUS',response:'@STATUS v=1 cfg=0 run=0 hv=0'}]};
    }
    else throw Error(`unexpected endpoint ${path}`);
    return {ok,json:async()=>result};
  }});
  vm.runInContext(script, context);
  return {elements,backend,context,settle:()=>new Promise(resolve=>setImmediate(resolve))};
}
const submit = env => env.elements['config-form'].listeners.submit({preventDefault(){}});

test('ESP page links to the local viewer without starting receive, scan or HV',async()=>{
  const link=html.match(/<a\b[^>]*\bid="image-viewer-link"[^>]*>/)?.[0];
  assert.ok(link);
  assert.match(link,/href="http:\/\/127\.0\.0\.1:8080\/"/);
  assert.match(link,/target="_blank"/);
  assert.match(link,/rel="noopener noreferrer"/);
  assert.doesNotMatch(link,/onclick|disabled/);
  assert.ok(html.indexOf('id="viewer-title"')<html.indexOf('连接与目标地址'));
  assert.match(html,/同一台电脑/);
  assert.match(html,/bmode_web_viewer\.py --device-ip 192\.168\.3\.27/);
  const e=setup();await e.settle();
  assert.ok(e.backend.calls.every(call=>!call.options.method||call.options.method==='GET'));
  assert.equal(e.elements['image-viewer-link'].listeners.click,undefined);
  assert.equal(e.elements['image-viewer-link'].disabled,false);
});

test('temperature card is independent of upload and shows errors, zero, negative, stale and disconnect',async()=>{
  const e=setup();await e.settle();
  assert.match(e.elements['temp-zynq'].textContent,/等待/);
  e.backend.status.temperatures={seq:0,age_ms:0,period_ms:10000,
    zynq:{status:'ok',celsius:35.123},power:{status:'ok',celsius:0},afe:{status:'ok',celsius:-10.125}};
  await e.context.refresh();
  assert.equal(e.elements['temp-zynq'].textContent,'35.12 °C');
  assert.equal(e.elements['temp-power'].textContent,'0.00 °C');
  assert.equal(e.elements['temp-afe'].textContent,'-10.13 °C');
  assert.equal(e.elements.upload.disabled,true);
  assert.match(e.elements['temp-age'].textContent,/序号 0/);
  e.backend.status.temperatures.power={status:'read_error',celsius:null};
  await e.context.refresh();assert.match(e.elements['temp-power'].textContent,/读取失败/);
  assert.equal(e.elements['temp-zynq'].textContent,'35.12 °C');
  e.backend.status.temperatures.age_ms=30001;
  await e.context.refresh();assert.match(e.elements['temp-zynq'].textContent,/过期/);
  e.backend.offline=true;await e.context.refresh();
  assert.match(e.elements['temp-afe'].textContent,/断开/);
  e.backend.offline=false;e.backend.status.temperatures=undefined;await e.context.refresh();
  assert.match(e.elements['temp-zynq'].textContent,/等待/);
  assert.ok(html.indexOf('温度监测')>html.indexOf('连接与目标地址'));
  assert.ok(html.indexOf('温度监测')<html.indexOf('设备与传输状态'));
});

const temperatureSample=(seq,zynq=40,power=30,afe=28)=>({seq,age_ms:0,period_ms:10000,
 zynq:{status:'ok',celsius:zynq},power:{status:'ok',celsius:power},afe:{status:'ok',celsius:afe}});
const chartHistory=e=>JSON.parse(vm.runInContext('JSON.stringify(temperatureHistory.points)',e.context));
async function sample(e,t,advance=10000){e.backend.now+=advance;e.backend.status.temperatures=t;await e.context.refresh()}

test('temperature chart adds one point per sequence, preserves zero/negative and uses no new API',async()=>{
 const e=setup();await e.settle();
 await sample(e,temperatureSample(0,0,-5,27));
 assert.deepEqual(chartHistory(e)[0].values,[0,-5,27]);
 assert.equal(e.elements['temp-dot-zynq'].r,'2');
 assert.match(e.elements['temp-path-power'].d,/^M/);
 await e.context.refresh();await e.context.refresh();
 assert.equal(chartHistory(e).length,1);
 await sample(e,temperatureSample(1,1,-4,28));
 assert.match(e.elements['temp-path-zynq'].d,/L/);
 assert.equal(e.elements['temp-dot-zynq'].r,'0');
 assert.equal(chartHistory(e).length,2);
 assert.ok(e.backend.calls.every(c=>c.path==='/api/config'||c.path==='/api/status'));
 assert.doesNotMatch(html,/<script[^>]+src=|<link[^>]+https?:/);
 assert.ok(Number(e.elements['temp-y-4'].textContent)<-5);
});

test('temperature time axis appends left to right with local clock labels and thin dark styling',async()=>{
 const e=setup();await e.settle();
 assert.equal(e.elements['temp-x-0'].textContent,'—');
 await sample(e,temperatureSample(0));
 const first=e.backend.now;
 assert.match(e.elements['temp-path-zynq'].d,/^M64\.00 /);
 const clock=at=>{const d=new Date(at);return [d.getHours(),d.getMinutes(),d.getSeconds()].map(v=>String(v).padStart(2,'0')).join(':')};
 assert.equal(e.elements['temp-x-0'].textContent,clock(first));
 assert.equal(e.elements['temp-x-3'].textContent,clock(first+120000));
 await sample(e,temperatureSample(1));
 const xs=[...e.elements['temp-path-zynq'].d.matchAll(/[ML]([\d.]+) /g)].map(m=>Number(m[1]));
 assert.equal(xs[0],64);assert.ok(xs[1]>xs[0]&&xs[1]<680);
 assert.equal(e.elements['temp-x-0'].textContent,clock(first));
 for(let i=0;i<4;i++)assert.match(e.elements['temp-x-'+i].textContent,/^\d{2}:\d{2}:\d{2}$/);
 assert.match(html,/background:#202129/);assert.match(html,/stroke-width:1\.25/);
 assert.match(html,/本地时间 \(HH:MM:SS\) →/);
});

test('temperature time axis handles midnight and rolls bounded history without reversing samples',async()=>{
 const e=setup();await e.settle();
 e.backend.now=new Date(2026,9,20,23,59,50).getTime();
 await sample(e,temperatureSample(0),0);
 assert.equal(e.elements['temp-x-0'].textContent,'23:59:50');
 assert.equal(e.elements['temp-x-3'].textContent,'00:01:50');
 for(let seq=1;seq<200;seq++)await sample(e,temperatureSample(seq));
 const points=chartHistory(e),xs=[...e.elements['temp-path-zynq'].d.matchAll(/[ML]([\d.]+) /g)].map(m=>Number(m[1]));
 assert.equal(points.length,180);assert.equal(xs.length,180);
 assert.ok(xs.every((x,i)=>x>=64&&x<=680&&(!i||x>xs[i-1])));
 assert.ok(xs.at(-1)<680);assert.notEqual(e.elements['temp-x-0'].textContent,'23:59:50');
});

test('temperature chart gaps only failed channel and never records missing values as zero',async()=>{
 const e=setup();await e.settle();await sample(e,temperatureSample(1));
 const bad=temperatureSample(2);bad.power={status:'read_error',celsius:null};
 await sample(e,bad);await sample(e,temperatureSample(3));
 assert.equal(chartHistory(e)[1].values[1],null);
 assert.equal((e.elements['temp-path-power'].d.match(/M/g)||[]).length,2);
 assert.doesNotMatch(e.elements['temp-path-power'].d,/L/);
 assert.equal((e.elements['temp-path-zynq'].d.match(/L/g)||[]).length,2);
 const invalid=temperatureSample(4);invalid.zynq.celsius=Infinity;
 await sample(e,invalid);assert.equal(chartHistory(e).at(-1).values[0],null);
 assert.equal(e.elements['temp-dot-zynq'].r,'0');
});

test('temperature chart keeps history but breaks lines after stale/disconnect/skipped samples',async()=>{
 const e=setup();await e.settle();await sample(e,temperatureSample(1));
 e.backend.offline=true;await e.context.refresh();
 assert.equal(chartHistory(e).length,1);assert.equal(e.elements['temp-dot-zynq'].r,'0');
 e.backend.offline=false;await sample(e,temperatureSample(2));
 assert.equal(chartHistory(e).at(-1).gap,true);
 const old=temperatureSample(2);old.age_ms=30001;await sample(e,old,31000);
 assert.equal(chartHistory(e).length,2);
 await sample(e,temperatureSample(3));assert.equal(chartHistory(e).at(-1).gap,true);
 await sample(e,temperatureSample(5));assert.equal(chartHistory(e).at(-1).gap,true);
 assert.equal((e.elements['temp-path-zynq'].d.match(/M/g)||[]).length,4);
});

test('temperature chart clears old session, accepts uint32 wrap and handles clock changes',async()=>{
 const e=setup();await e.settle();await sample(e,temperatureSample(99));
 await sample(e,temperatureSample(0));assert.equal(chartHistory(e).length,1);
 e.backend.status.temperatures=undefined;await e.context.refresh();assert.equal(chartHistory(e).length,0);
 await sample(e,temperatureSample(0xffffffff));await sample(e,temperatureSample(0));
 assert.equal(chartHistory(e).length,2);assert.equal(chartHistory(e)[1].gap,false);
 e.backend.now-=60000;await sample(e,temperatureSample(1));
 assert.equal(chartHistory(e).length,1);assert.equal(chartHistory(e)[0].gap,true);
});

test('realistic 1.2 s polling draws continuous lines for 10 s source, but not stale 40 s source',async()=>{
 for(const period of [10000,40000]){
  const e=setup();await e.settle();const base=e.backend.now;
  for(let elapsed=0;elapsed<=180000;elapsed+=1200){
   e.backend.now=base+elapsed;
   const t=temperatureSample(Math.floor(elapsed/period));t.age_ms=elapsed%period;
   if(t.age_ms>30000)for(const n of ['zynq','power','afe'])t[n]={status:'stale',celsius:null};
   e.backend.status.temperatures=t;await e.context.refresh();
  }
  const segments=(e.elements['temp-path-zynq'].d.match(/L/g)||[]).length;
  assert.equal(segments,period===10000?chartHistory(e).length-1:0);
  assert.equal(chartHistory(e).length,period===10000?19:5);
 }
});

test('temperature chart storage is bounded to 180 groups and thirty minutes',async()=>{
 const e=setup();await e.settle();
 for(let seq=0;seq<200;seq++)await sample(e,temperatureSample(seq));
 assert.equal(chartHistory(e).length,180);
 assert.match(e.elements['temp-chart-status'].textContent,/180/);
 e.backend.now+=1800001;e.backend.status.temperatures.age_ms=1800001;await e.context.refresh();
 assert.equal(chartHistory(e).length,0);assert.equal(e.elements['temp-path-zynq'].d,'');
 assert.equal(e.elements['temp-dot-zynq'].r,'0');
});

test('TCP verified ACK differs from UDP local completion and exposes protocol',async()=>{
  const e=setup();await e.settle();
  Object.assign(e.backend.status,{sdio_transport:'tcp',active_transport:'tcp',result:'received_verified',
    tcp_connected:true,tcp_confirmed_frames:12,tcp_chunks:137,tcp_waits:4,tcp_ack_us:12000,socket_errno:12,
    tcp_phase:'payload',tcp_send_buffer:28800,tcp_send_buffer_actual:28800,tcp_default_send_buffer:5760,tcp_stage_bytes:5760,tcp_metric_frame:9,
    tcp_metric_result:'sent_local',tcp_payload_us:500000,tcp_write_wait_us:420000,tcp_yield_us:22000});
  await vm.runInContext('refresh()',e.context);
  assert.match(e.elements.result.textContent,/PC 完整收帧且 CRC 校验通过/);
  assert.doesNotMatch(e.elements.result.textContent,/errno/);
  assert.match(e.elements['sdio-timing'].textContent,/编译模式 TCP/);
  assert.match(e.elements['sdio-timing'].textContent,/PC 确认 12/);
  assert.match(e.elements.packets.textContent,/不是 TCP 分段数/);
  assert.match(e.elements['tcp-timing'].textContent,/发送载荷/);
  assert.match(e.elements['tcp-timing'].textContent,/配置 28800 B/);
  assert.match(e.elements['tcp-timing'].textContent,/实际 28800 B/);
  assert.match(e.elements['tcp-timing'].textContent,/其他 TCP 默认 5760 B/);
  assert.match(e.elements['tcp-timing'].textContent,/500.0 ms/);
  assert.match(e.elements['tcp-timing'].textContent,/不能相加/);
  Object.assign(e.backend.status,{active_transport:'udp',result:'sent_local'});
  await vm.runInContext('refresh()',e.context);
  assert.match(e.elements.result.textContent,/本地发送完成/);
  assert.doesNotMatch(e.elements.result.textContent,/CRC 校验通过/);
});

test('SDIO starts only after explicit host selection and shares stop control',async()=>{
  const e=setup();await e.settle();
  Object.assign(e.backend.status,{connected:true,upload_ready:true,
    sdio:{ready:true,host_sdio:false,running:false,raw_busy:false,free_slots:2}});
  e.elements['target-ip'].value='192.168.3.31';await submit(e);
  assert.equal(e.elements['sdio-once'].disabled,true);
  assert.match(e.elements['sdio-gate'].textContent,/Zynq SDIO 初始化\/握手尚未完成/);
  e.backend.status.sdio.host_sdio=true;await vm.runInContext('refresh()',e.context);
  assert.equal(e.elements['sdio-once'].disabled,false);
  assert.match(e.elements['sdio-gate'].textContent,/可启动/);
  await e.elements['sdio-once'].listeners.click();
  assert.equal(e.backend.status.sdio.running,true);
  assert.equal(e.elements.upload.disabled,true);
  assert.equal(e.elements['sdio-stream'].disabled,true);
  await e.elements['sdio-stop'].listeners.click();
  assert.equal(e.backend.status.sdio.running,false);
  await e.elements['sdio-stream'].listeners.click();
  const call=e.backend.calls.filter(c=>c.path==='/api/sdio/start').at(-1);
  assert.equal(JSON.parse(call.options.body).frames,0);
});

test('first provisioning: no target/no network disables upload and displays PSRAM', async()=>{
  const e=setup();await e.settle();
  assert.equal(e.elements.upload.disabled,true);
  assert.match(e.elements.ap.textContent,/192\.168\.4\.1/);
  assert.match(e.elements.memory.textContent,/2097152/);
  assert.match(e.elements['test-size'].textContent,/787456/);
  assert.notEqual(e.elements.message.textContent,'正在读取设备配置…');
});
test('save live target, upload once, stop and display connection IP',async()=>{
  const e=setup();await e.settle();
  Object.assign(e.backend.status,{connected:true,upload_ready:true,sta_ip:'192.168.1.61'});
  e.elements['target-ip'].value='192.168.1.100';
  await submit(e);
  assert.equal(e.elements.upload.disabled,false);
  assert.equal(e.elements['sta-link'].href,'http://192.168.1.61/');
  await e.elements.upload.listeners.click();
  assert.equal(e.elements.upload.disabled,true);assert.equal(e.elements.stop.disabled,false);
  assert.equal(e.elements.destination.textContent,'192.168.1.100:50001');
  await e.elements.stop.listeners.click();
  assert.match(e.elements.result.textContent,/已停止/);
  assert.equal(e.elements.upload.disabled,false);
});
test('NVS failure is shown and saved target stays unchanged',async()=>{
  const e=setup();await e.settle();e.backend.failSave=true;
  e.elements['target-ip'].value='192.168.1.200';await submit(e);
  assert.equal(e.backend.config.target_ip,'');assert.equal(e.elements.upload.disabled,true);
  assert.match(e.elements.message.textContent,/NVS 保存失败/);
  assert.equal(e.elements.message.classList.contains('error'),true);
});
test('password omission preserves secret, explicit open Wi-Fi clears it',async()=>{
  const e=setup();await e.settle();e.elements.ssid.value='router';await submit(e);
  let put=e.backend.calls.filter(c=>c.options.method==='PUT').at(-1);
  assert.equal('password' in JSON.parse(put.options.body),false);
  e.elements.password.value='secret123';await submit(e);
  assert.equal(e.elements.password.value,'');
  e.elements['open-network'].checked=true;await submit(e);
  put=e.backend.calls.filter(c=>c.options.method==='PUT').at(-1);
  assert.equal(JSON.parse(put.options.body).password,'');
  assert.equal(e.elements['open-network'].checked,false);
  assert.equal(e.elements.upload.disabled,true);
});
test('device disconnect disables actions; recovery restores usable state',async()=>{
  const e=setup();await e.settle();e.backend.offline=true;await e.context.refresh();
  assert.equal(e.elements.upload.disabled,true);assert.match(e.elements.message.textContent,/无法访问设备/);
  e.backend.offline=false;await e.context.refresh();
  assert.match(e.elements.message.textContent,/连接已恢复/);
  assert.equal(e.elements.message.classList.contains('error'),false);
});
test('status uses text nodes, removes stale IP and reports send errors',async()=>{
  const e=setup();await e.settle();
  Object.assign(e.backend.status,{ap_ssid:'<img src=x onerror=alert(1)>',result:'send_error',socket_errno:12});
  await e.context.refresh();
  assert.match(e.elements.ap.textContent,/<img/);
  assert.match(e.elements.result.textContent,/errno 12/);
  assert.equal(e.elements['sta-link'].href,undefined);
});

test('Wi-Fi controls need no token; HV confirmation remains and polling only reads cache',async()=>{
 const e=setup();await e.settle();
 e.backend.status.zynq_control={ready:true,busy:false,config_owned:false,synced:true,ps_status:'@STATUS v=1 cfg=0 run=0 hv=0'};
 await e.context.refresh();
 await e.context.sendZynq(['STATUS']);
 assert.equal(e.backend.calls.filter(c=>c.path==='/api/zynq/commands').length,1);
 assert.equal(e.elements['control-token'],undefined);
 e.backend.confirmed=false;await e.context.sendZynq(['HV ENABLE']);
 assert.equal(e.backend.calls.filter(c=>c.path==='/api/zynq/commands').length,1);
 e.backend.confirmed=true;await e.context.sendZynq(['HV ENABLE']);
 const post=e.backend.calls.filter(c=>c.path==='/api/zynq/commands').at(-1);
 assert.equal(post.options.headers['X-Control-Token'],undefined);
 assert.equal(JSON.parse(post.options.body).allow_hv,true);
 const before=e.backend.calls.filter(c=>c.path==='/api/zynq/commands').length;
 await e.context.refresh();await e.context.refresh();
 assert.equal(e.backend.calls.filter(c=>c.path==='/api/zynq/commands').length,before);
});

test('configuration gate disables image submissions and safe stop sends HV DISABLE directly',async()=>{
 const e=setup();await e.settle();
 e.backend.status.zynq_control={ready:true,busy:false,config_owned:true};
 e.backend.status.upload_ready=true;e.backend.config.target_ip='192.168.1.2';
 await e.context.refresh();assert.equal(e.elements.upload.disabled,true);assert.equal(e.elements['sdio-once'].disabled,true);
 await e.elements['zynq-safe-stop'].listeners.click();await e.settle();
 const post=e.backend.calls.find(c=>c.path==='/api/zynq/commands');
 assert.deepEqual(JSON.parse(post.options.body).commands,['HV DISABLE']);
 assert.equal('localStorage' in e.context,false);
});

async function downsampleEnvironment(d=0){
 const e=setup();await e.settle();
 e.backend.status.zynq_control={ready:true,busy:false,config_owned:false,synced:true,
  ps_status:`@STATUS v=1 cfg=0 owner=2 seq=0 run=1 hv=1 run_src=ps spi=1 down=${d}`};
 await e.context.refresh();return e;
}
function successfulDownsampleJob(e,d,override={}){
 const seq=123; // Deterministic getRandomValues in this test environment.
 const commands=[`CFG BEGIN ${seq}`,`BRAM WR t_timing 9 1 ${d}`,'CFG END','STATUS'];
 e.backend.job={busy:false,state:'succeeded',count:4,completed:4,results:commands.map((command,i)=>({command,
  response:i<3?`@ACK ${seq} ${['CFG_BEGIN','BRAM_WR','CFG_END'][i]}`:
   `@STATUS v=1 cfg=0 owner=2 seq=${seq} run=0 hv=0 run_src=ps spi=0 down=${d}`})),...override};
 return commands;
}
test('downsampling offers all five sizes, sends one safe transaction and never restarts imaging or upload',async()=>{
 for(const [d,n] of [787456,394240,197632,99328,50176].entries()){
  const e=await downsampleEnvironment();const commands=successfulDownsampleJob(e,d);
  e.elements['zynq-down'].value=String(d);e.elements['zynq-down'].listeners.change();
  assert.match(e.elements['down-preview'].textContent,new RegExp(`${6144>>d} 点/线 · ${n} B/帧`));
  assert.match(e.elements['down-configured'].textContent,/1 倍/); // Selection is not applied state.
  await e.elements['zynq-down-set'].listeners.click();
  assert.deepEqual(e.backend.commandBody.commands,commands);assert.equal(e.backend.commandBody.allow_hv,false);
  assert.match(e.elements['down-result'].textContent,/配置成功/);
  assert.equal(e.backend.calls.filter(c=>c.path==='/api/zynq/commands').length,1);
  assert.ok(e.backend.calls.filter(c=>c.options.method==='POST').every(c=>c.path==='/api/zynq/commands'));
  assert.doesNotMatch(e.backend.commandBody.commands.join('\n'),/BEAM START|HV ENABLE/);
  assert.equal(e.elements['zynq-start'].disabled,false);assert.equal(e.elements['zynq-hv-on'].disabled,false);
 }
});
test('downsampling distinguishes PS shadow, selection and last CRC-passed frame, not current BEGIN or source snapshot',async()=>{
 const e=await downsampleEnvironment(3);
 e.backend.status.sdio={last_received_bytes:394240,last_received_frame:57,frame_bytes:50176,source_down:4};
 await e.context.refresh();
 assert.match(e.elements['down-configured'].textContent,/8 倍/);
 assert.match(e.elements['down-received'].textContent,/2 倍.*394240.*57/);
 assert.match(e.elements['down-preview'].textContent,/1 倍/);
 e.backend.status.sdio.last_received_bytes=0;await e.context.refresh();
 assert.match(e.elements['down-received'].textContent,/等待/);
 const posts=e.backend.calls.filter(c=>c.options.method==='POST').length;
 await e.context.refresh();assert.equal(e.backend.calls.filter(c=>c.options.method==='POST').length,posts);
});
test('downsampling rejects invalid selection, old or unknown PS and occupied control, preserving safe stop',async()=>{
 for(const invalid of ['', '-1','5','1.5','NaN',' 1','01']){
  const e=await downsampleEnvironment();e.elements['zynq-down'].value=invalid;
  await e.elements['zynq-down-set'].listeners.click();assert.equal(e.backend.commandBody,undefined);
 }
 for(const change of [{synced:false},{busy:true},{config_owned:true},{ready:false},
  {ps_status:'@STATUS v=1 cfg=1 run=0 hv=0 down=2'},
  {ps_status:'@STATUS v=1 cfg=0 run=0 hv=0'}, {ps_status:'@STATUS v=1 cfg=0 run=0 hv=0 down=5'}]){
  const e=await downsampleEnvironment();Object.assign(e.backend.status.zynq_control,change);await e.context.refresh();
  assert.equal(e.elements['zynq-down-set'].disabled,true);
  await e.elements['zynq-down-set'].listeners.click();assert.equal(e.backend.commandBody,undefined);
  if(change.ready!==false)assert.equal(e.elements['zynq-safe-stop'].disabled,false);
 }
 const e=await downsampleEnvironment();e.backend.offline=true;await e.context.refresh();
 assert.equal(e.elements['zynq-down-set'].disabled,true);assert.match(e.elements['down-configured'].textContent,/未知/);
});
test('downsampling checks exact ACKs, command order, final down/run/HV/transaction state and never retries failures',async()=>{
 for(const fault of ['down','cfg','run','hv','seq','old','duplicate','ack','order','short','nack','unknown','conflict','lost']){
  const e=await downsampleEnvironment();e.elements['zynq-down'].value='2';successfulDownsampleJob(e,2);
  const job=e.backend.job;
  if(['down','cfg','run','hv','seq'].includes(fault))job.results[3].response=job.results[3].response.replace(`${fault}=${fault==='down'?'2':fault==='seq'?'123':'0'}`,`${fault}=1`);
  if(fault==='old')job.results[3].response='@STATUS v=1 cfg=0 run=0 hv=0';
  if(fault==='duplicate')job.results[3].response+=' down=2';
  if(fault==='ack')job.results[1].response='@ACK 999 BRAM_WR';
  if(fault==='order')job.results[0].command='STATUS';
  if(fault==='short'){job.completed=3;job.results.pop()}
  if(fault==='nack'||fault==='unknown'){job.state=fault==='nack'?'failed':'unknown';job.error='injected failure';job.completed=1;job.results=job.results.slice(0,1)}
  if(fault==='conflict')e.backend.commandConflict=true;
  if(fault==='lost')e.backend.jobHook=async()=>{throw Error('network lost')};
  await e.elements['zynq-down-set'].listeners.click();
  assert.doesNotMatch(e.elements['down-result'].textContent,/配置成功/);
  assert.match(e.elements['down-result'].textContent,/失败或结果未确认/);
  assert.equal(e.backend.calls.filter(c=>c.path==='/api/zynq/commands').length,1,fault);
 }
});
test('downsampling prevents double submit and keeps safety stop available during async work',async()=>{
 const e=await downsampleEnvironment();successfulDownsampleJob(e,0);
 let release,entered;const waiting=new Promise(resolve=>{release=resolve}),started=new Promise(resolve=>{entered=resolve});
 e.backend.commandHook=async()=>{entered();await waiting};
 const first=e.elements['zynq-down-set'].listeners.click();await started;
 assert.equal(e.elements['zynq-down-set'].disabled,true);assert.equal(e.elements['zynq-down'].disabled,true);
 assert.equal(e.elements['zynq-safe-stop'].disabled,false);
 await e.elements['zynq-down-set'].listeners.click();
 release();await first;
 assert.equal(e.backend.calls.filter(c=>c.path==='/api/zynq/commands').length,1);
});
