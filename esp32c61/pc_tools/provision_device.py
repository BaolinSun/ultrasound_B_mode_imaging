"""Per-device Wi-Fi labels and isolated NVS preparation; never uses monitor.

generate is offline. export/write enter the ESP ROM loader and reset the ESP;
they are explicit operator actions, never invoked by build.ps1/idf.py flash.
"""
from __future__ import annotations
import argparse
import csv
import hashlib
import importlib.util
import importlib.metadata
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import re
import secrets
import subprocess
import sys
import traceback

PROJECT = Path(__file__).resolve().parents[1]
OFFSET, SIZE = 0x3F0000, 0x6000
IDF_DEFAULT = Path(os.environ.get('IDF_PATH', r'D:\Program Files\esp\v6.1\esp-idf'))
ALPHABET = '23456789ABCDEFGHJKLMNPQRSTUVWXYZabcdefghijkmnopqrstuvwxyz'
STAGE = 'preflight'


class ProvisioningError(ValueError):
    """Only fixed diagnostic messages; never include credential values."""


class StageError(ProvisioningError):
    def __init__(self, stage, cause):
        self.stage, self.cause = stage, cause
        super().__init__('Device operation failed')


def stage(value):
    global STAGE
    STAGE = value


def package_version(name):
    try:
        return importlib.metadata.version(name)
    except importlib.metadata.PackageNotFoundError:
        return None


def preflight(action, idf, python=None, output=None):
    stage('preflight')
    for name, package in (('qrcode', 'qrcode[pil]'), ('PIL.Image', 'Pillow')):
        try:
            importlib.import_module(name)
        except ImportError:
            raise ProvisioningError(f'Missing/unusable {package}; run python -m pip install -r .\\pc_tools\\requirements-provisioning.txt') from None
    load_module(idf/'components/nvs_flash/nvs_partition_tool/nvs_parser.py', '_ub_preflight_nvs')
    if python is None or Path(python).resolve() == Path(sys.executable).resolve():
        if importlib.util.find_spec('esp_idf_nvs_partition_gen') is None:
            raise ProvisioningError('Missing esp-idf-nvs-partition-gen in current Python; install requirements-provisioning.txt')
    else:
        result = subprocess.run([python, '-c', 'import esp_idf_nvs_partition_gen'], capture_output=True)
        if result.returncode:
            raise ProvisioningError('Selected --idf-python lacks esp-idf-nvs-partition-gen')
    if output is not None and Path(output).exists():
        raise ProvisioningError('Output directory already exists; use a new --output-dir or keep the existing label files')
    if action != 'generate':
        import esptool
        if package_version('esptool') != '5.4.0':
            raise ProvisioningError('esptool 5.4.0 is required; install requirements-provisioning.txt in this Python environment')
        for name in ('detect_chip','run_stub','attach_flash','detect_flash_size','flash_size_bytes','read_flash','write_flash'):
            if not callable(getattr(esptool,name,None)):
                raise ProvisioningError('esptool API is incomplete; reinstall requirements-provisioning.txt')


def report_error(error, args):
    current_stage = error.stage if isinstance(error, StageError) else STAGE
    cause = error.cause if isinstance(error, StageError) else error
    hints = {'connect':'Cannot connect to serial port/ROM loader; check port, cable and port ownership',
             'load_stub':'Cannot start the flasher stub; check esptool installation and download mode',
             'attach_flash':'Cannot attach/initialize SPI Flash',
             'detect_flash':'Cannot detect Flash capacity',
             'read_partition_table':'Cannot read partition table',
             'read_identity':'Cannot read device credential partition',
             'reset':'ESP reset failed; reset ESP manually; exported files may already be complete'}
    if isinstance(cause, ProvisioningError):
        detail = str(cause)
    elif isinstance(cause, FileExistsError):
        detail = 'Output directory already exists; choose a new --output-dir'
    elif isinstance(cause, ImportError):
        detail = 'Python dependency import failed; install requirements-provisioning.txt in the displayed environment'
    else:
        detail = hints.get(current_stage, 'Operation failed; check the stage and exception type below')
        if current_stage=='connect' and 'could not open' in str(cause).lower():
            detail='Serial port could not be opened (busy or unavailable); exit ESP-IDF monitor with Ctrl+] and check the assigned COM port'
    diagnostic = {'utc':datetime.now(timezone.utc).isoformat(), 'action':args.action, 'stage':current_stage,
                  'error_type':type(cause).__name__, 'errno':getattr(cause,'errno',None),
                  'winerror':getattr(cause,'winerror',None), 'hint':detail,
                  'python':sys.executable, 'python_version':sys.version.split()[0],
                  'conda_environment':Path(sys.prefix).name,
                  'shell_conda_environment':os.environ.get('CONDA_DEFAULT_ENV'),
                  'esptool_version':package_version('esptool'),
                  'frames':[{'file':Path(f.filename).name,'line':f.lineno,'function':f.name}
                            for f in traceback.extract_tb(cause.__traceback__)]}
    print(f"Provisioning failed [stage={current_stage}, type={type(cause).__name__}]: {detail}.", file=sys.stderr)
    print(f'Python: {sys.executable}. No credential values are printed.', file=sys.stderr)
    try:
        folder=PROJECT/'logs';folder.mkdir(exist_ok=True)
        path=folder/('provision_device_'+datetime.now(timezone.utc).strftime('%Y%m%d_%H%M%S_%f')+'.diagnostic.json')
        path.write_text(json.dumps(diagnostic,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
        print(f'Diagnostic: {path}',file=sys.stderr)
    except OSError:
        pass  # Diagnostics must not mask the original failure.


def normalize_mac(value):
    raw = value.replace(':', '').replace('-', '').upper()
    if not re.fullmatch(r'[0-9A-F]{12}', raw) or int(raw[:2], 16) & 1 or int(raw, 16) == 0:
        raise ProvisioningError('Invalid unicast factory MAC')
    return raw


def record(mac, ssid, password):
    mac = normalize_mac(mac)
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,32}', ssid):
        raise ProvisioningError('Invalid device SSID')
    if not re.fullmatch(r'[A-Za-z0-9]{16}', password):
        raise ProvisioningError('Device password must contain 16 ASCII letters/digits')
    return f'DVC1|{mac}|{ssid}|{password}'


def parse_record(text):
    fields = text.split('|')
    if len(fields) != 4 or fields[0] != 'DVC1' or record(*fields[1:]) != text:
        raise ProvisioningError('Invalid device identity record')
    return {'version': 1, 'mac': fields[1], 'ssid': fields[2], 'password': fields[3]}


def wifi_qr(identity):
    # Identity alphabet intentionally excludes Wi-Fi QR delimiters.
    return f"WIFI:T:WPA;S:{identity['ssid']};P:{identity['password']};;"


def load_module(path, name):
    if not path.is_file():
        raise ProvisioningError('ESP-IDF 6.1 source path is missing required tools')
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def decode_nvs(data, idf):
    """Use the vendor parser, but reject bad CRCs/ambiguous live records."""
    if len(data) != SIZE:
        raise ProvisioningError('Wrong device NVS size')
    parser = load_module(idf / 'components/nvs_flash/nvs_partition_tool/nvs_parser.py', '_ub_nvs')
    nvs = parser.NVS_Partition('device_nvs', bytearray(data))
    namespaces, identities = set(), []
    entries = []
    for page in nvs.pages:
        if page.is_empty:
            if data[page.start_address:page.start_address+4096] != b'\xff'*4096:
                raise ProvisioningError('Incomplete/corrupt device NVS erased page')
            continue
        h = page.header
        if h['status'] not in ('Active', 'Full') or h['crc']['original'] != h['crc']['computed']:
            raise ProvisioningError('Device NVS page integrity error')
        entries.extend(e for e in page.entries if e.state == 'Written')
    for e in entries:
        m = e.metadata
        if m['crc']['original'] != m['crc']['computed']:
            raise ProvisioningError('Device NVS entry integrity error')
        if m['namespace'] == 0 and e.key == 'device' and m['type'] == 'uint8_t':
            namespaces.add(e.data['value'])
    for e in entries:
        m = e.metadata
        if m['namespace'] == 0 and e.key == 'device':
            continue
        if m['namespace'] not in namespaces or e.key != 'identity' or m['type'] != 'string':
            raise ProvisioningError('Unexpected live data in device credential partition')
        if m['crc']['data_original'] != m['crc']['data_computed'] or any(c.state != 'Written' for c in e.children):
            raise ProvisioningError('Device NVS string integrity error')
        raw = b''.join(bytes(c.raw) for c in e.children)[:e.data['size']]
        if not raw.endswith(b'\0') or b'\0' in raw[:-1]:
            raise ProvisioningError('Invalid device NVS string')
        try:
            identities.append(parse_record(raw[:-1].decode('ascii')))
        except (UnicodeError, ValueError):
            raise ProvisioningError('Invalid device identity record') from None
    if not identities:
        return None
    if any(i != identities[0] for i in identities):
        raise ProvisioningError('Conflicting live device identities')
    return identities[0]


def check_table(data, idf):
    module = load_module(idf / 'components/partition_table/gen_esp32part.py', '_ub_parts')
    module.offset_part_table = 0x8000
    table = module.PartitionTable.from_binary(data)
    table.verify()
    table.verify_size_fits(0x400000)
    expected = {'nvs': (1, 2, 0x9000, 0x6000), 'phy_init': (1, 1, 0xF000, 0x1000),
                'factory': (0, 0, 0x10000, 1500 * 1024), 'device_nvs': (1, 2, OFFSET, SIZE)}
    for name, fields in expected.items():
        p = table.find_by_name(name)
        if p is None or (p.type, p.subtype, p.offset, p.size) != fields or p.encrypted or p.readonly:
            raise ProvisioningError('Incompatible partition table; flash matching ESP firmware/table first')
    if len(table) != len(expected):
        raise ProvisioningError('Unexpected partition layout')


def idf_python(explicit=None):
    return explicit or sys.executable


def save_bundle(identity, output, idf, python=None):
    stage('export_bundle')
    import qrcode
    text = record(identity['mac'], identity['ssid'], identity['password'])
    identity = parse_record(text)
    # Exclusive directory creation protects all labels and restore records together.
    output = Path(output)
    output.mkdir(parents=True, exist_ok=False)
    (output / 'identity.json').write_text(json.dumps(identity, indent=2) + '\n', encoding='utf-8')
    with (output / 'device.csv').open('x', encoding='utf-8', newline='') as f:
        writer = csv.writer(f)
        writer.writerows([['key', 'type', 'encoding', 'value'], ['device', 'namespace', '', ''],
                         ['identity', 'data', 'string', text]])
    generator = idf / 'components/nvs_flash/nvs_partition_generator/nvs_partition_gen.py'
    result = subprocess.run([idf_python(python), str(generator), 'generate', str(output / 'device.csv'),
                             str(output / 'device_nvs.bin'), hex(SIZE)], capture_output=True)
    if result.returncode:
        raise ProvisioningError('NVS image generation failed; incomplete bundle retained for inspection')
    if decode_nvs((output / 'device_nvs.bin').read_bytes(), idf) != identity:
        raise ProvisioningError('Generated identity verification failed')
    qrcode.make(wifi_qr(identity)).save(output / 'wifi_qr.png')
    label = (f"ESP32-C61 {identity['mac']}\nWi-Fi: {identity['ssid']}\nPassword: {identity['password']}\n"
             'Connect using wifi_qr.png, then open http://192.168.4.1/ manually.\n'
             'Scan connects to the DEVICE hotspot; enter router credentials on the page.\n')
    (output / 'label.txt').write_text(label, encoding='utf-8')
    digest = hashlib.sha256((output / 'device_nvs.bin').read_bytes()).hexdigest()
    (output / 'image.sha256').write_text(digest + '\n', encoding='ascii')
    return output


class Device:
    """One exclusive ROM-loader session; only write_identity mutates flash."""
    def __init__(self, port):
        import esptool
        self.tool = esptool
        self.esp = None
        try:
            stage('connect')
            self.esp = esptool.detect_chip(port)
            stage('check_chip')
            if self.esp.CHIP_NAME != 'ESP32-C61':
                raise ProvisioningError('Only ESP32-C61 is supported')
            stage('read_mac')
            self.mac = normalize_mac(bytes(self.esp.read_mac()).hex())
            stage('load_stub')
            self.esp = esptool.run_stub(self.esp)
            stage('attach_flash')
            esptool.attach_flash(self.esp)
            stage('detect_flash')
            flash_size = esptool.detect_flash_size(self.esp)
            if not flash_size or esptool.flash_size_bytes(flash_size) < 0x400000:
                raise ProvisioningError('At least 4 MiB of detected Flash is required')
        except BaseException as error:
            failed_stage=STAGE
            try:
                self.close()
            except Exception:
                pass
            if not isinstance(error,Exception):
                raise
            raise StageError(failed_stage,error) from None

    def read(self, offset, size):
        return bytes(self.tool.read_flash(self.esp, offset, size, no_progress=True))

    def write_identity(self, image):
        if len(image) != SIZE:
            raise ProvisioningError('Wrong image length')
        self.tool.write_flash(self.esp, [(OFFSET, image)], flash_size='keep')

    def close(self):
        if self.esp is None:
            return
        stage('reset')
        try:
            self.esp.hard_reset()
        finally:
            self.esp._port.close()


def on_device(action, device, idf, bundle=None, output=None, python=None):
    stage('read_partition_table')
    check_table(device.read(0x8000, 0x1000), idf)
    stage('read_identity')
    existing = decode_nvs(device.read(OFFSET, SIZE), idf)
    if existing and existing['mac'] != device.mac:
        raise ProvisioningError('Stored identity belongs to another MAC; refusing operation')
    if action == 'export':
        if not existing:
            raise ProvisioningError('Device is not provisioned; generate/write a bundle first')
        return save_bundle(existing, output, idf, python)
    identity = parse_record(record(**{k: bundle[k] for k in ('mac', 'ssid', 'password')}))
    if identity['mac'] != device.mac:
        raise ProvisioningError('Bundle belongs to another device')
    image = Path(bundle['image']).read_bytes()
    if hashlib.sha256(image).hexdigest() != bundle['sha256'] or decode_nvs(image, idf) != identity:
        raise ProvisioningError('Bundle image/identity/hash mismatch')
    if existing:
        if existing != identity:
            raise ProvisioningError('Different credentials already exist; refusing overwrite')
        return 'already provisioned; no write'
    stage('write_identity')
    device.write_identity(image)
    stage('readback')
    if device.read(OFFSET, SIZE) != image:
        raise ProvisioningError('Flash read-back verification failed; do not use label yet')
    return 'identity written and read-back verified'


def main(argv=None):
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--idf-path', type=Path, default=IDF_DEFAULT)
    p.add_argument('--idf-python')
    sub = p.add_subparsers(dest='action', required=True)
    gen = sub.add_parser('generate', help='offline creation; never connects to hardware')
    gen.add_argument('--mac', required=True)
    gen.add_argument('--output-dir', type=Path)
    export = sub.add_parser('export', help='read existing credentials; resets ESP into loader')
    export.add_argument('--port', required=True)
    export.add_argument('--output-dir', type=Path)
    write = sub.add_parser('write', help='write only the MAC-bound device_nvs partition')
    write.add_argument('--port', required=True)
    write.add_argument('--bundle', type=Path, required=True)
    args = p.parse_args(argv)
    stage('preflight')
    try:
        preflight(args.action,args.idf_path,args.idf_python,getattr(args,'output_dir',None))
        if args.action == 'generate':
            mac = normalize_mac(args.mac)
            identity = parse_record(record(mac, 'Ultrasound-C61-' + mac[-6:], ''.join(secrets.choice(ALPHABET) for _ in range(16))))
            out = save_bundle(identity, args.output_dir or PROJECT / 'provisioning' / mac, args.idf_path, args.idf_python)
            print(f'Prepared bundle: {out} (credentials are in the label files, not console)')
        else:
            stage('validate_local_bundle')
            # Check local bundle before entering the ROM loader.
            bundle = None
            if args.action == 'write':
                bundle = json.loads((args.bundle / 'identity.json').read_text(encoding='utf-8'))
                bundle['image'] = args.bundle / 'device_nvs.bin'
                bundle['sha256'] = (args.bundle / 'image.sha256').read_text(encoding='ascii').strip()
                parse_record(record(**{k: bundle[k] for k in ('mac', 'ssid', 'password')}))
                image = bundle['image'].read_bytes()
                if hashlib.sha256(image).hexdigest() != bundle['sha256'] or decode_nvs(image, args.idf_path) != {k:bundle[k] for k in ('version','mac','ssid','password')}:
                    raise ProvisioningError('Invalid local bundle')
            device = Device(args.port)
            try:
                out = on_device(args.action, device, args.idf_path, bundle,
                                args.output_dir or PROJECT / 'provisioning' / device.mac, args.idf_python)
                print(out)
            except BaseException as error:
                failed_stage=STAGE
                try:
                    device.close()
                except Exception:
                    pass
                if not isinstance(error,Exception):
                    raise
                raise StageError(failed_stage,error) from None
            else:
                device.close()
        return 0
    except Exception as error:
        report_error(error,args)
        return 1


if __name__ == '__main__':
    raise SystemExit(main())
