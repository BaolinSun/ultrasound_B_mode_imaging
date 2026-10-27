"""Real IDF NVS images and simulated ROM-loader I/O; never opens a serial port."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import types
import sys
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location('provision_device', ROOT/'esp32c61/pc_tools/provision_device.py')
P = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(P)


class Device:
    def __init__(self, table, image, mac='102030405060'):
        self.table, self.image, self.mac = table, image, mac
        self.writes, self.reads = [], []
        self.corrupt_readback = False

    def read(self, address, length):
        self.reads.append((address, length))
        if address == 0x8000:
            assert length == 0x1000
            return self.table
        assert address == P.OFFSET and length == P.SIZE
        if self.corrupt_readback and self.writes:
            return bytes(P.SIZE)
        return self.image

    def write_identity(self, image):
        assert len(image) == P.SIZE
        self.writes.append((P.OFFSET, image))
        self.image = image


class ProvisioningToolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not P.IDF_DEFAULT.is_dir():
            raise unittest.SkipTest('ESP-IDF 6.1 is required for real NVS fixtures')
        try:
            import qrcode  # noqa
        except ImportError:
            raise unittest.SkipTest('Install pc_tools/requirements-provisioning.txt for QR fixtures')
        cls.temp = tempfile.TemporaryDirectory(prefix='provision_test_', dir=ROOT/'build')
        cls.base = Path(cls.temp.name)
        cls.identity = P.parse_record(P.record('10:20:30:40:50:60', 'Ultrasound-C61-405060', 'ABCDEFGH12345678'))
        cls.output = P.save_bundle(cls.identity, cls.base/'bundle', P.IDF_DEFAULT)
        cls.image = (cls.output/'device_nvs.bin').read_bytes()
        cls.bundle = {**cls.identity, 'image': cls.output/'device_nvs.bin',
                      'sha256': (cls.output/'image.sha256').read_text().strip()}
        module = P.load_module(P.IDF_DEFAULT/'components/partition_table/gen_esp32part.py', '_test_parts')
        module.offset_part_table = 0x8000
        cls.table = module.PartitionTable.from_csv((ROOT/'esp32c61/partitions.csv').read_text()).to_binary().ljust(0x1000, b'\xff')

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def device(self, image=None):
        return Device(self.table, self.image if image is None else image)

    def test_actual_image_roundtrip_qr_png_and_no_console_password(self):
        self.assertEqual(P.decode_nvs(self.image, P.IDF_DEFAULT), self.identity)
        self.assertEqual(len(self.image), 0x6000)
        import qrcode
        from PIL import Image
        # Compare the actual PNG to the QR generated from the documented Wi-Fi payload.
        expected = qrcode.make(P.wifi_qr(self.identity)).convert('1')
        with Image.open(self.output/'wifi_qr.png') as actual:
            self.assertEqual(actual.size, expected.size)
            self.assertEqual(actual.convert('1').tobytes(), expected.tobytes())
        self.assertEqual(P.wifi_qr(self.identity), 'WIFI:T:WPA;S:Ultrasound-C61-405060;P:ABCDEFGH12345678;;')
        self.assertIn('http://192.168.4.1/', (self.output/'label.txt').read_text())
        out = io.StringIO()
        with contextlib.redirect_stdout(out):
            self.assertEqual(P.main(['generate', '--mac', self.identity['mac'], '--output-dir', str(self.base/'cli')]), 0)
        generated = json.loads((self.base/'cli/identity.json').read_text())
        self.assertRegex(generated['password'], r'^[A-Za-z0-9]{16}$')
        self.assertNotIn(generated['password'], out.getvalue())
        with self.assertRaises(FileExistsError):
            P.save_bundle(self.identity, self.output, P.IDF_DEFAULT)

    def test_mac_and_record_validation(self):
        for mac in ('', '000000000000', '112233445566', 'xyz', '1020304050600'):
            with self.assertRaises(ValueError):
                P.normalize_mac(mac)
        for args in [('102030405060', '', 'ABCDEFGH12345678'),
                     ('102030405060', 'a;b', 'ABCDEFGH12345678'),
                     ('102030405060', 'x', 'short'), ('102030405060', 'x', 'A'*15+';')]:
            with self.assertRaises(ValueError):
                P.record(*args)
        for text in ('x', 'DVC2|102030405060|x|ABCDEFGH12345678', 'DVC1|10203040506a|x|ABCDEFGH12345678'):
            with self.assertRaises(ValueError):
                P.parse_record(text)

    def test_checks_before_write_and_exact_partition_readback(self):
        d = self.device(b'\xff'*P.SIZE)
        self.assertIn('verified', P.on_device('write', d, P.IDF_DEFAULT, self.bundle))
        self.assertEqual(d.writes, [(P.OFFSET, self.image)])
        self.assertEqual(d.reads, [(0x8000, 0x1000), (P.OFFSET, P.SIZE), (P.OFFSET, P.SIZE)])
        d = self.device()
        self.assertIn('no write', P.on_device('write', d, P.IDF_DEFAULT, self.bundle))
        self.assertEqual(d.writes, [])
        d = self.device(b'\xff'*P.SIZE)
        d.corrupt_readback = True
        with self.assertRaisesRegex(ValueError, 'read-back'):
            P.on_device('write', d, P.IDF_DEFAULT, self.bundle)

    def test_bad_mac_bundle_hash_and_table_never_write(self):
        for field, value in [('mac', '202030405060'), ('password', 'HGFEDCBA87654321'), ('sha256', '0'*64)]:
            d = self.device(b'\xff'*P.SIZE)
            with self.assertRaises(ValueError):
                P.on_device('write', d, P.IDF_DEFAULT, {**self.bundle, field:value})
            self.assertFalse(d.writes)
        d = self.device()
        d.mac = '202030405060'
        with self.assertRaises(ValueError):
            P.on_device('write', d, P.IDF_DEFAULT, self.bundle)
        self.assertFalse(d.writes)
        d = self.device(b'\xff'*P.SIZE)
        d.table = b'\xff'*0x1000
        with self.assertRaises(ValueError):
            P.on_device('write', d, P.IDF_DEFAULT, self.bundle)
        self.assertFalse(d.writes)

    def test_different_existing_credentials_refused_and_export_preserves_label(self):
        other = P.save_bundle({**self.identity, 'password':'HGFEDCBA87654321'}, self.base/'other', P.IDF_DEFAULT)
        d = self.device((other/'device_nvs.bin').read_bytes())
        with self.assertRaisesRegex(ValueError, 'Different credentials'):
            P.on_device('write', d, P.IDF_DEFAULT, self.bundle)
        self.assertFalse(d.writes)
        output = P.on_device('export', self.device(), P.IDF_DEFAULT, output=self.base/'export')
        self.assertEqual(json.loads((output/'identity.json').read_text()), self.identity)
        self.assertEqual((output/'wifi_qr.png').read_bytes(), (self.output/'wifi_qr.png').read_bytes())
        with self.assertRaises(ValueError):
            P.on_device('export', self.device(b'\xff'*P.SIZE), P.IDF_DEFAULT, output=self.base/'empty')

    def test_crc_and_length_corruption_refused(self):
        for offset in (4, 28, 72, 130, P.SIZE-1):
            corrupted = bytearray(self.image)
            corrupted[offset] ^= 1
            with self.assertRaises(ValueError):
                P.decode_nvs(corrupted, P.IDF_DEFAULT)
        with self.assertRaises(ValueError):
            P.decode_nvs(b'x', P.IDF_DEFAULT)

    def test_local_bundle_error_never_opens_serial_or_prints_secrets(self):
        with patch.object(P, 'Device') as device, contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(P.main(['write', '--port', 'NOT_A_PORT', '--bundle', str(self.base/'missing')]), 1)
        device.assert_not_called()
        self.assertNotIn(self.identity['password'], err.getvalue())

    def test_esptool_adapter_checks_model_mac_size_and_closes_without_real_port(self):
        rom=types.SimpleNamespace(CHIP_NAME='ESP32-C61',read_mac=lambda:bytes.fromhex('102030405060'))
        calls=[]
        rom._port=types.SimpleNamespace(close=lambda:calls.append('close'))
        rom.hard_reset=lambda:calls.append('reset')
        rom.run_stub=lambda:rom
        module=types.SimpleNamespace(detect_chip=lambda port:rom,detect_flash_size=lambda esp:'4MB',
                flash_size_bytes=lambda size:0x400000,read_flash=lambda *args,**kwargs:self.image,
                write_flash=lambda esp,data,**kwargs:calls.append(data),
                run_stub=lambda esp:(calls.append('stub') or esp),
                attach_flash=lambda esp:calls.append('attach'))
        with patch.dict('sys.modules',{'esptool':module}):
            d=P.Device('FAKE_PORT');self.assertEqual(d.mac,'102030405060')
            self.assertEqual(calls[:2],['stub','attach'])
            self.assertEqual(d.read(P.OFFSET,P.SIZE),self.image)
            d.write_identity(self.image);self.assertEqual(calls[-1],[(P.OFFSET,self.image)])
            with self.assertRaises(ValueError):d.write_identity(b'x')
            d.close();self.assertEqual(calls[-2:],['reset','close'])
            rom.CHIP_NAME='ESP32-C6'
            with self.assertRaises(ValueError):P.Device('FAKE_PORT')
            self.assertEqual(calls[-1],'close');rom.CHIP_NAME='ESP32-C61'
            module.flash_size_bytes=lambda size:0x200000
            with self.assertRaises(ValueError):P.Device('FAKE_PORT')
            self.assertEqual(calls[-1],'close')

    def test_real_esptool_flash_initialization_without_physical_serial(self):
        import esptool
        calls=[]
        class Rom:
            CHIP_NAME='ESP32-C61'
            IS_STUB=False
            secure_download_mode=False
            _port=types.SimpleNamespace(close=lambda:calls.append('close'))
            attached=False
            def read_mac(self):return bytes.fromhex('102030405060')
            def flash_spi_attach(self,value):
                self.attached=True;calls.append('attach')
            def flash_id(self,cache=True):
                if not self.attached:raise RuntimeError('SPI must be attached first')
                return 0x1640EF
            def read_spiflash_sfdp(self,*args):return 0
            def run_spiflash_command(self,command):calls.append(('flash_command',command))
            def flash_set_parameters(self,size):assert self.attached and size==0x400000
            def read_flash(self,address,size,progress_fn):
                assert self.attached;calls.append(('read',address,size));return b'x'*size
            def hard_reset(self):calls.append('reset')
        rom=Rom()
        # Real vendor attach/detect/read functions; only hardware transport is replaced.
        with patch.object(esptool,'detect_chip',return_value=rom),patch.object(esptool,'run_stub',return_value=rom),contextlib.redirect_stdout(io.StringIO()):
            try:
                d=P.Device('SIMULATED_PORT')
            except P.StageError as error:
                raise error.cause  # This transport is synthetic, not a real device.
            self.assertEqual(d.read(0x8000,32),b'x'*32)
            d.close()
        self.assertLess(calls.index('attach'),calls.index(('read',0x8000,32)))
        self.assertLess(calls.index(('flash_command',0x99)),calls.index(('read',0x8000,32)))
        self.assertEqual(calls[-2:],['reset','close'])

    def test_dependency_and_existing_output_errors_happen_before_connect(self):
        with patch.object(P,'Device') as device,contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(P.main(['export','--port','NEVER_OPEN','--output-dir',str(self.output)]),1)
        device.assert_not_called();self.assertIn('already exists',err.getvalue());self.assertIn('stage=preflight',err.getvalue())
        with patch.object(P.importlib.util,'find_spec',return_value=None),patch.object(P,'Device') as device,contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(P.main(['export','--port','NEVER_OPEN']),1)
        device.assert_not_called();self.assertIn('esp-idf-nvs-partition-gen',err.getvalue())

    def test_errors_record_stage_but_not_raw_secret_messages(self):
        secret='UNPRINTABLE_SECRET_MARKER'
        e=OSError(13,secret)
        with patch.object(P,'Device',side_effect=P.StageError('connect',e)),contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(P.main(['export','--port','NEVER_OPEN']),1)
        self.assertIn('stage=connect',err.getvalue());self.assertIn('type=PermissionError',err.getvalue())
        self.assertNotIn(secret,err.getvalue())
        path=Path(err.getvalue().split('Diagnostic: ')[1].strip())
        diagnostic=json.loads(path.read_text(encoding='utf-8'))
        self.assertEqual(diagnostic['python'],sys.executable);self.assertEqual(diagnostic['errno'],13)
        self.assertNotIn(secret,path.read_text(encoding='utf-8'))

    def test_cleanup_failure_does_not_mask_primary_read_error(self):
        class Broken:
            mac='102030405060'
            def read(self,*args):raise ValueError('SENSITIVE_FLASH_BYTES')
            def close(self):P.stage('reset');raise OSError('secondary reset failure')
        with patch.object(P,'Device',return_value=Broken()),contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(P.main(['export','--port','NEVER_OPEN']),1)
        self.assertIn('stage=read_partition_table',err.getvalue());self.assertIn('type=ValueError',err.getvalue())
        self.assertNotIn('SENSITIVE_FLASH_BYTES',err.getvalue())

    def test_generator_uses_active_conda_python_by_default(self):
        self.assertEqual(P.idf_python(),sys.executable)
        self.assertEqual(P.idf_python('explicit_python'),'explicit_python')

    def test_interrupt_still_resets_and_closes_without_writing_flash(self):
        calls=[]
        class Interrupted:
            mac='102030405060'
            def read(self,*args):raise KeyboardInterrupt
            def close(self):calls.append('closed')
        with patch.object(P,'Device',return_value=Interrupted()),self.assertRaises(KeyboardInterrupt):
            P.main(['export','--port','NEVER_OPEN'])
        self.assertEqual(calls,['closed'])

    def test_monitor_port_error_is_specific_without_echoing_vendor_text(self):
        raw=ValueError('Could not open COM3: SECRET_VALUE')
        with patch.object(P,'Device',side_effect=P.StageError('connect',raw)),contextlib.redirect_stderr(io.StringIO()) as err:
            self.assertEqual(P.main(['export','--port','NEVER_OPEN']),1)
        self.assertIn('Ctrl+]',err.getvalue());self.assertNotIn('SECRET_VALUE',err.getvalue())


class ProvisioningContracts(unittest.TestCase):
    def test_no_password_apis_or_console_dependency(self):
        main = ROOT/'esp32c61/main'
        wifi = (main/'wifi_manager.c').read_text(encoding='utf-8')
        self.assertNotIn('password=%s', wifi)
        self.assertNotIn('15000', wifi)
        store = (main/'config_store.c').read_text(encoding='utf-8')
        self.assertNotIn('esp_fill_random', store)
        self.assertNotIn('nvs_flash_erase', store)
        for text in (wifi, store):
            self.assertNotIn('usb_serial_jtag_read', text)
        web = (main/'web_server.c').read_text(encoding='utf-8')
        self.assertIn('"provisioning_state",device_store_state()', web)
        self.assertNotIn('identity.password', web)
        self.assertIn('/provisioning/', (ROOT/'esp32c61/.gitignore').read_text())
        self.assertNotIn('device_nvs.bin', (ROOT/'esp32c61/build.ps1').read_text(encoding='utf-8'))


if __name__ == '__main__':
    unittest.main()
