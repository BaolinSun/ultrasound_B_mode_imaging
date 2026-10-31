import importlib.util,json,shutil,struct,tempfile,unittest
from pathlib import Path
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('focus_export',ROOT/'build/scripts/finalize_focus_bank.py')
module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
class FocusExportTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.stage=Path(self.tmp.name)
  c=(ROOT/'build/src/ps/bram/focus_bank_data.c').read_text(encoding='utf-8').split('\nconst focus_entry_t')[0]
  (self.stage/'focus_bank_data.c').write_text(c,encoding='utf-8')
  for d in range(20,171,10):
   values=json.loads((ROOT/f'esp32c61/configs/delay_profile_focus_{d}mm.json').read_text(encoding='utf-8-sig'))['bram']['delay_profile']
   words=[int(v,0) if isinstance(v,str) else v for v in values]
   (self.stage/f'{d}.bin').write_bytes(struct.pack('<16384I',*words))
  self.reference=Path('D:/MyProjects/py_prj/ultrasound_dev_kit')
 def test_all_sixteen_matlab_c_arrays_match_current_inputs(self):
  c,m=module.validate(self.stage,self.reference)
  self.assertEqual(c,(ROOT/'build/src/ps/bram/focus_bank_data.c').read_text(encoding='utf-8'))
  self.assertEqual(len({p['sha256'] for p in m['profiles']}),16)
 def test_bad_length_or_corrupt_raw_rejected(self):
  p=self.stage/'80.bin';raw=p.read_bytes();p.write_bytes(raw[:-1])
  with self.assertRaises(ValueError):module.validate(self.stage,self.reference)
  p.write_bytes(bytes([raw[0]^1])+raw[1:])
  with self.assertRaises(ValueError):module.validate(self.stage,self.reference)
 def test_interrupted_install_restores_prior_files(self):
  a=self.stage/'a.c';b=self.stage/'b.c';a.write_bytes(b'old-a');b.write_bytes(b'old-b');original=Path.replace
  def fail(path,target):
   if target==b:raise OSError('injected failure')
   return original(path,target)
  with patch.object(Path,'replace',fail):
   with self.assertRaises(OSError):module.install({a:b'new-a',b:b'new-b'})
  self.assertEqual(a.read_bytes(),b'old-a');self.assertEqual(b.read_bytes(),b'old-b')
if __name__=='__main__':unittest.main()
