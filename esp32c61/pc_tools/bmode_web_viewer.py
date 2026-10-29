#!/usr/bin/env python3
"""PC B-mode viewer. Never enables HV, starts scanning or downloads parameters."""
import argparse
from datetime import datetime
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import ipaddress
import json
from pathlib import Path
import secrets
import threading
from urllib.parse import parse_qs, urlsplit

try:
    from bmode_viewer_service import ViewerService
except ModuleNotFoundError as exc:
    raise SystemExit('缺少显示依赖，请执行: python -m pip install -r pc_tools/requirements-viewer.txt\n'+str(exc)) from None

ASSETS = Path(__file__).with_name('bmode_assets')
LOGS = Path(__file__).resolve().parents[1] / 'logs'


def make_server(service, address=('127.0.0.1', 8080)):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = 'HTTP/1.1'

        def log_message(self, *args):
            pass  # No per-image/packet console traffic.

        def reply(self, code, body=b'', mime='application/json; charset=utf-8', headers=None):
            self.send_response(code)
            self.send_header('Content-Type', mime)
            self.send_header('Content-Length', str(len(body)))
            self.send_header('Cache-Control', 'no-store')
            self.send_header('X-Content-Type-Options', 'nosniff')
            self.send_header('Content-Security-Policy', "default-src 'self'; img-src 'self' blob:; style-src 'self' 'unsafe-inline'; script-src 'self'")
            for key, value in (headers or {}).items():
                self.send_header(key, str(value))
            self.end_headers()
            if body:
                try:
                    self.wfile.write(body)
                except (BrokenPipeError, ConnectionResetError, TimeoutError):
                    pass  # Slow/closed browsers never interrupt the receiver.

        def js(self, code, value):
            self.reply(code, json.dumps(value, ensure_ascii=False, allow_nan=False).encode('utf-8'))

        def same_host(self):
            try:
                host = urlsplit('http://'+self.headers.get('Host', ''))
                return (host.hostname in ('localhost', self.connection.getsockname()[0]) and
                        host.port == self.server.server_port)
            except ValueError:
                return False

        def do_GET(self):
            if not self.same_host():
                self.js(403, {'error': 'Host 不允许'})
                return
            url = urlsplit(self.path)
            if url.path in ('/', '/viewer.js', '/viewer_settings.js') and not url.query:
                filename, mime = ('viewer.html', 'text/html; charset=utf-8') if url.path == '/' else (url.path[1:], 'text/javascript; charset=utf-8')
                self.reply(200, (ASSETS/filename).read_bytes(), mime)
            elif url.path == '/api/viewer/settings' and not url.query:
                self.js(200, service.imaging.get_settings())
            elif url.path == '/api/status' and not url.query:
                self.js(200, service.snapshot())
            elif url.path == '/api/image':
                query = parse_qs(url.query, keep_blank_values=True)
                try:
                    if set(query)-{'after'} or len(query.get('after', ['0'])) != 1:
                        raise ValueError()
                    after = int(query.get('after', ['0'])[0])
                    if not 0 <= after <= 2**63-1:
                        raise ValueError()
                except ValueError:
                    self.js(400, {'error': '无效图像序号'})
                    return
                sequence, png, metadata = service.imaging.latest_with_metadata(after)
                self.reply(200 if png else 204, png or b'', 'image/png', {
                    'X-Image-Sequence': sequence,
                    'X-Image-Display': json.dumps(metadata, separators=(',', ':'), allow_nan=False)})
            else:
                self.js(404, {'error': '接口不存在'})

        def do_POST(self):
            # Local same-origin UI only; no CORS, USB token or arbitrary device proxy.
            origin = self.headers.get('Origin')
            if not self.same_host() or (origin and origin != 'http://'+self.headers.get('Host')):
                self.close_connection = True
                self.js(403, {'error': '仅允许同源网页操作'})
                return
            if self.path not in ('/api/viewer/start', '/api/viewer/stop'):
                self.close_connection = True
                self.js(404, {'error': '接口不存在'})
                return
            try:
                length = int(self.headers.get('Content-Length', '-1'))
                if not 0 < length <= 256 or self.headers.get_content_type() != 'application/json':
                    raise ValueError()
                body = self.rfile.read(length)
                if len(body) != length or json.loads(body) != {}:
                    raise ValueError()
            except (ValueError, UnicodeDecodeError):
                self.close_connection = True
                self.js(400, {'error': '需要空 JSON 对象 {}，不接受配置或控制命令'})
                return
            try:
                service.start() if self.path.endswith('/start') else service.stop()
                self.js(202, {'accepted': True})
            except RuntimeError as exc:
                self.js(409, {'error': str(exc)})

        def do_PUT(self):
            origin = self.headers.get('Origin')
            if not self.same_host() or (origin and origin != 'http://'+self.headers.get('Host')):
                self.close_connection = True
                self.js(403, {'error': '仅允许同源网页操作'})
                return
            if self.path != '/api/viewer/settings':
                self.close_connection = True
                self.js(404, {'error': '接口不存在'})
                return
            try:
                length = int(self.headers.get('Content-Length', '-1'))
                if not 0 < length <= 32768 or self.headers.get_content_type() != 'application/json' or self.headers.get('Transfer-Encoding'):
                    raise ValueError('仅接受不超过 32 KiB 的 JSON 显示设置')
                raw = self.rfile.read(length)
                if len(raw) != length:
                    raise ValueError('请求不完整')
                body = json.loads(raw)
                if not isinstance(body, dict):
                    raise ValueError('需要 JSON 对象')
                if set(body) == {'revision', 'settings'}:
                    result = service.set_display_settings(body['settings'], body['revision'])
                elif set(body) == {'revision', 'profile', 'confirm_geometry'} and body['confirm_geometry'] is True:
                    result = service.set_display_settings(body['profile'], body['revision'], profile=True)
                else:
                    raise ValueError('仅接受版本匹配的显示设置或已确认的显示配置导入')
                self.js(202, result)
            except (ValueError, UnicodeDecodeError, TypeError, OverflowError) as exc:
                self.close_connection = True
                self.js(400, {'error': str(exc)})
            except RuntimeError as exc:
                self.js(409, {'error': str(exc)})

        def setup(self):
            super().setup()
            self.connection.settimeout(2.)

    server = ThreadingHTTPServer(address, Handler)
    server.daemon_threads = True
    return server


class StatusLog:
    """Once/second snapshots; disk latency is isolated from all data/ACK threads."""
    def __init__(self, service):
        LOGS.mkdir(parents=True, exist_ok=True)
        self.path = LOGS/(datetime.now().strftime('bmode-viewer-%Y%m%d-%H%M%S-')+secrets.token_hex(4)+'.jsonl')
        self.file = self.path.open('x', encoding='utf-8')
        self.stop = threading.Event()
        self.error = ''
        self.thread = threading.Thread(target=self._run, args=(service,), name='bmode-log', daemon=True)
        self.thread.start()

    def _run(self, service):
        try:
            while True:
                self.file.write(json.dumps({'time': datetime.now().astimezone().isoformat(), **service.snapshot()}, ensure_ascii=False)+'\n')
                self.file.flush()
                if self.stop.wait(1):
                    return
        except OSError as exc:
            self.error = str(exc)
            print('显示日志写入失败（接收仍继续）: '+str(exc), flush=True)
        finally:
            self.file.close()

    def close(self):
        self.stop.set()
        self.thread.join(2)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--device-ip', required=True, type=ipaddress.IPv4Address)
    parser.add_argument('--port', type=int, default=50001, help='ESP32 target data port')
    parser.add_argument('--data-bind', type=ipaddress.IPv4Address, default=ipaddress.IPv4Address('0.0.0.0'))
    parser.add_argument('--http-port', type=int, default=8080)
    # HTTP is deliberately loopback-only; no remote control exposure for this viewer.
    args = parser.parse_args(argv)
    if not 1 <= args.port <= 65535 or not 1 <= args.http_port <= 65535:
        parser.error('port must be 1..65535')
    service = server = log = None
    try:
        service = ViewerService(str(args.device_ip), args.port, str(args.data_bind))
        server = make_server(service, ('127.0.0.1', args.http_port))
        log = StatusLog(service)
        print(f'浏览器打开 http://127.0.0.1:{args.http_port}/ （启动程序不会自动上传）', flush=True)
        print('仅接收/显示：不会启动扫描、开启高压或下载参数。日志: '+str(log.path), flush=True)
        server.serve_forever(poll_interval=.2)
    except KeyboardInterrupt:
        print('正在停止本工具启动的 SDIO 上传…', flush=True)
    except (OSError, ValueError) as exc:
        print('显示服务启动失败: '+str(exc), flush=True)
        return 1
    finally:
        if service:
            service.close()
            if service.snapshot()['state'] == 'error':
                print('退出状态: '+service.snapshot()['error'], flush=True)
        if log:
            log.close()
        if server:
            server.server_close()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
