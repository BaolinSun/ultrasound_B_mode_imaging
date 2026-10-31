"""Fixed viewer actions over the existing protected Zynq command service."""
import hashlib
from pathlib import Path
import re
import secrets
import struct
import zlib

from build_focus_assets import DEPTHS, load_profile
from zynq_wifi_control import Client, ControlFailure


def fields(response):
    if not response.startswith('@STATUS '):
        raise RuntimeError('缺少 PS 状态回执')
    result = {}
    for token in response[8:].split():
        pair = re.fullmatch(r'([a-z_][a-z0-9_]*)=([a-zA-Z0-9]+)', token)
        if not pair or pair[1] in result:
            raise RuntimeError('PS 状态格式错误')
        result[pair[1]] = pair[2]
    if result.get('v') != '1':
        raise RuntimeError('PS 状态版本不匹配')
    return result


class FocusLibrary:
    def __init__(self, root=None):
        self.root = root or Path(__file__).resolve().parents[1] / 'configs'
        self.entries = []
        self.error = ''
        try:
            for depth in DEPTHS:
                words = load_profile(self.root / f'delay_profile_focus_{depth}mm.json')
                raw = struct.pack('<16384I', *words)
                self.entries.append({'focus_mm': depth, 'words': len(words),
                                     'crc32': f'{zlib.crc32(raw):08x}', 'sha256': hashlib.sha256(raw).hexdigest()})
        except (ValueError, OSError) as exc:
            self.entries = []
            self.error = '焦点文件不可用：'+str(exc)

    def catalog(self):
        return {'version': 1, 'profiles': self.entries, 'error': self.error}

    def commands(self, depth):
        if type(depth) is not int or depth not in DEPTHS or not self.entries:
            raise ValueError(self.error or '焦点必须为 20～170 mm，每档 10 mm')
        words = load_profile(self.root / f'delay_profile_focus_{depth}mm.json')
        raw = struct.pack('<16384I', *words)
        entry = next(p for p in self.entries if p['focus_mm'] == depth)
        if hashlib.sha256(raw).hexdigest() != entry['sha256']:
            raise ValueError('延时文件已变化，请重启 PC 服务以重新校验目录')
        return [f'FOCUS SET {depth} 0x{entry["crc32"]}']


class ViewerControl:
    def __init__(self, device_ip, api):
        self.device_ip, self.api = device_ip, api
        self.client = self._client()
        self.sequence = 0
        self.last_status = {}

    def _client(self):
        client = Client(self.device_ip, timeout=100)
        client.api = self.api
        return client

    def batch(self, commands, client=None):
        client = client or self.client
        state = self.last_status
        try:
            job = client.batch(commands, allow_hv='HV ENABLE' in commands)
            replies = job['results']
            for item in replies:
                command, response = item['command'], item['response']
                if command == 'FOCUS STATUS':
                    if not re.fullmatch(r'@FOCUS v=1 ready=1 depth=[0-9]+ crc=0x[0-9a-fA-F]{8} words=16384',response):
                        raise ControlFailure('FOCUS 表库未就绪或 PS 固件不匹配，请更新 Zynq 应用')
                elif command.startswith('FOCUS SET '):
                    _,_,depth,crc=command.split()
                    expected=f'@ACK {self.sequence} FOCUS_SET {depth} 0x{int(crc,0):08x}'
                    if response.lower()!=expected.lower(): raise ControlFailure('FOCUS 确认不匹配，执行结果未知',unknown=True)
                elif command == 'STATUS':
                    state = fields(response)
                    if client is self.client:
                        self.last_status = state
                else:
                    tag = {'HV ENABLE': 'HV_ENABLE', 'HV DISABLE': 'HV_DISABLE',
                           'BEAM START': 'BEAM_START', 'CFG END': 'CFG_END', 'CFG ABORT': 'CFG_ABORT'}.get(command)
                    tag = 'CFG_BEGIN' if command.startswith('CFG BEGIN ') else 'BRAM_WR' if command.startswith('BRAM WR ') else tag
                    match = re.fullmatch(r'@ACK ([0-9]+) '+str(tag), response)
                    if not match or (command.startswith(('CFG ', 'BRAM WR ')) and int(match[1]) != self.sequence):
                        raise ControlFailure('PS 确认不匹配，执行结果未知', unknown=True)
            return state
        finally:
            client.history.clear()  # Do not retain every large table/result across runs.

    def verify(self, *, run, hv, config=True):
        state = self.batch(['STATUS'])
        if state.get('run') != str(run) or state.get('hv') != str(hv) or (config and state.get('cfg') != '0'):
            raise RuntimeError('PS 扫描/高压/配置状态与请求不一致，未确认操作成功')
        return state

    @staticmethod
    def active(cancel):
        if cancel.is_set():
            raise RuntimeError('操作已取消')

    def start(self, cancel):
        state = self.batch(['STATUS'])
        if state.get('cfg') != '0' or state.get('afe') != 'ready' or state.get('tx') != 'ready':
            raise RuntimeError('PS 配置占用或 AFE/TX 未就绪')
        self.active(cancel)
        self.batch(['HV ENABLE'])
        self.active(cancel)
        self.batch(['BEAM START'])
        self.active(cancel)
        self.verify(run=1, hv=1)

    def off(self, urgent=False):
        client = self._client() if urgent else self.client
        # A single HV DISABLE uses the firmware's priority shutdown path.
        self.batch(['HV DISABLE'], client)
        state = self.batch(['STATUS'], client)
        if state.get('run') != '0' or state.get('hv') != '0':
            raise RuntimeError('关闭扫描/高压未确认')

    def freeze(self, cancel):
        self.batch(['HV DISABLE'])  # Existing PS also stops scan.
        self.active(cancel)
        self.batch(['BEAM START'])  # Preserve background reception with HV OFF.
        self.active(cancel)
        self.verify(run=1, hv=0)

    def resume(self, cancel):
        self.verify(run=1, hv=0)
        self.active(cancel)
        self.batch(['HV ENABLE'])
        self.active(cancel)
        self.verify(run=1, hv=1)

    def focus(self, commands, cancel, progress):
        if len(commands)!=1 or not commands[0].startswith('FOCUS SET '): raise ValueError('预设焦点必须为单条 FOCUS SET')
        self.batch(['FOCUS STATUS'])
        self.sequence = secrets.randbits(32) or 1
        owned = False
        try:
            self.active(cancel)
            owned = True  # Lost submission ACK may still mean CFG was accepted.
            self.batch(['CFG BEGIN '+str(self.sequence)+' FOCUS'])
            for offset in range(0, len(commands), 8):
                self.active(cancel)
                self.batch(commands[offset:offset+8])
                progress(min(offset+8, len(commands)), len(commands))
            self.active(cancel)
            self.batch(['CFG END'])
            owned = False
            state = self.verify(run=0, hv=0)
            check=self.client.batch(['FOCUS STATUS'])
            try:
                reply=check['results'][0]['response']
                _,_,depth,crc=commands[0].split()
                expected=f'@FOCUS v=1 ready=1 depth={depth} crc=0x{int(crc,0):08x} words=16384'
                if reply.lower()!=expected.lower(): raise RuntimeError('焦点最终状态未确认')
            finally: self.client.history.clear()
            if state.get('seq') != str(self.sequence):
                raise RuntimeError('焦点配置事务编号不匹配')
        except Exception as exc:
            if owned and not getattr(exc, 'unknown', False):
                try:
                    self.batch(['CFG ABORT'])
                except Exception as cleanup:
                    raise RuntimeError(str(exc)+'；中止未确认: '+str(cleanup)) from None
            raise
