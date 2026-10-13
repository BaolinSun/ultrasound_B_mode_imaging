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
    offline: false, failSave: false, calls: [],
  };
  const context = vm.createContext({document: {getElementById(id) {
    assert.ok(elements[id], `page referenced nonexistent element ${id}`); return elements[id];
  }}, setInterval() {}, console,
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
    } else throw Error(`unexpected endpoint ${path}`);
    return {ok,json:async()=>result};
  }});
  vm.runInContext(script, context);
  return {elements,backend,context,settle:()=>new Promise(resolve=>setImmediate(resolve))};
}
const submit = env => env.elements['config-form'].listeners.submit({preventDefault(){}});

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
