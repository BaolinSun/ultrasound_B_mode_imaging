"""Check actual ELF focus bytes, read-only placement, and catalogue equality."""
import argparse,json,struct,zlib,hashlib
from pathlib import Path
root=Path(__file__).resolve().parents[2]
def check(elf):
 data=elf.read_bytes()
 assert data[:6]==b'\x7fELF\x01\x01','Expected little-endian ELF32'
 phoff=struct.unpack_from('<I',data,28)[0];size,count=struct.unpack_from('<HH',data,42)
 segments=[struct.unpack_from('<8I',data,phoff+i*size) for i in range(count)]
 shoff=struct.unpack_from('<I',data,32)[0];shsize,shcount=struct.unpack_from('<HH',data,46)
 sections=[struct.unpack_from('<10I',data,shoff+i*shsize) for i in range(shcount)]
 symbols={}
 for sec in sections:
  if sec[1]!=2:continue
  strings=sections[sec[6]];names=data[strings[4]:strings[4]+strings[5]]
  for pos in range(sec[4],sec[4]+sec[5],sec[9]):
   name,value,size,info,other,index=struct.unpack_from('<IIIBBH',data,pos)
   text=names[name:names.find(b'\0',name)].decode('ascii',errors='replace')
   if text.startswith('focus_delay_') and index<len(sections):symbols[text]=(value,size,sections[index][2])
 manifest=json.loads((root/'build/src/ps/bram/focus_bank_manifest.json').read_text())
 assert len(symbols)==16
 for p in manifest['profiles']:
  address,size,flags=symbols[f'focus_delay_{p["depth_mm"]:03}mm']
  assert flags&2 and not flags&1 and size==65536 and 0x100000<=address and address+size<=0x1c000000
  raw=None
  for typ,off,va,pa,filesz,memsz,flags,align in segments:
   if typ==1 and va<=address and address+size<=va+filesz:raw=data[off+address-va:off+address-va+size];break
  assert raw is not None and hashlib.sha256(raw).hexdigest()==p['sha256'] and f'{zlib.crc32(raw):08x}'==p['crc32']
 print('FOCUS_ELF_OK:',elf,'16 read-only DDR arrays; 1048576 bytes; actual CRC/SHA match manifest')
if __name__=='__main__':
 ap=argparse.ArgumentParser();ap.add_argument('elf',type=Path);a=ap.parse_args();check(a.elf)
