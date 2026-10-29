import gzip
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('build_focus_assets', ROOT / 'pc_tools/build_focus_assets.py')
assets = importlib.util.module_from_spec(spec)
spec.loader.exec_module(assets)


class FocusAssetTests(unittest.TestCase):
    def test_all_profiles_round_trip_without_changing_a_word(self):
        originals = {p.name: p.read_bytes() for p in (ROOT / 'configs').glob('delay_profile_focus_*mm.json')}
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder)
            catalog = assets.build_assets(ROOT / 'configs', output)
            blob = (output / 'focus_profiles.bin').read_bytes()
            header = (output / 'focus_assets.h').read_text()
            offset = 0
            self.assertEqual([p['focus_mm'] for p in catalog['profiles']], list(range(20, 171, 10)))
            for entry in catalog['profiles']:
                # gzip member boundaries are independently recovered with zlib.
                decoder = zlib.decompressobj(16 + zlib.MAX_WBITS)
                decoded = decoder.decompress(blob[offset:])
                length = len(blob) - offset - len(decoder.unused_data)
                self.assertTrue(decoder.eof)
                words = json.loads(decoded)['bram']['delay_profile']
                expected = assets.load_profile(ROOT / 'configs' / entry['source'])
                self.assertEqual(words, expected)
                raw = struct.pack('<16384I', *words)
                self.assertEqual(hashlib.sha256(raw).hexdigest(), entry['sha256'])
                self.assertEqual(f'{zlib.crc32(raw):08x}', entry['crc32'])
                self.assertIn(f'{{"{entry["url"]}", {offset}u, {length}u}}', header)
                offset += length
            self.assertEqual(offset, len(blob))
            self.assertLess(len(blob), 200000)
            before = {p.name: p.read_bytes() for p in output.iterdir()}
            assets.build_assets(ROOT / 'configs', output)
            self.assertEqual(before, {p.name: p.read_bytes() for p in output.iterdir()})
        self.assertEqual(originals, {p.name: p.read_bytes() for p in (ROOT / 'configs').glob('delay_profile_focus_*mm.json')})

    def test_identical_source_tables_are_exposed_not_rewritten(self):
        with tempfile.TemporaryDirectory() as folder:
            catalog = assets.build_assets(ROOT / 'configs', Path(folder))
            entries = {p['focus_mm']: p for p in catalog['profiles']}
            self.assertEqual(entries[60]['same_as_mm'], [70])
            self.assertEqual(entries[100]['same_as_mm'], [110])
            self.assertEqual(entries[80]['same_as_mm'], [])

    def test_bad_table_lengths_values_and_extra_hardware_fields_fail(self):
        good = {'bram': {'delay_profile': [0] * 16384}}
        bad = [None, [], {'bram': {'delay_profile': [0]}}, {'format_version': 2, **good}, {'format_version': True, **good},
               {'bram': {'delay_profile': [0] * 16384, 't_timing': [0] * 10}},
               {'commands': ['HV ENABLE'], **good}]
        for value in [True, -1, 2**32, 1.5, 'NaN', '0xGG', None]:
            words = [0] * 16384
            words[7] = value
            bad.append({'bram': {'delay_profile': words}})
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'bad.json'
            for value in bad:
                path.write_text(json.dumps(value))
                with self.subTest(value=str(value)[:80]), self.assertRaises(ValueError):
                    assets.load_profile(path)

    def test_missing_source_cannot_create_partial_pack(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            with self.assertRaises(FileNotFoundError):
                assets.build_assets(root, root / 'output')
            self.assertFalse((root / 'output/focus_profiles.bin').exists())


if __name__ == '__main__':
    unittest.main()
