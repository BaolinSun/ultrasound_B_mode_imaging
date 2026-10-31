"""Validate MATLAB-generated C/raw outputs before installing either source copy."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

ROOT=Path(__file__).resolve().parents[2]
def validate(stage, reference):
    c=(stage/'focus_bank_data.c').read_text(encoding='utf-8-sig')
    c='\n'.join(line.rstrip() for line in c.splitlines())+'\n'
    profiles=[];seen=set()
    for depth in range(20,171,10):
        raw=(stage/f'{depth}.bin').read_bytes()
        if len(raw)!=65536: raise ValueError(f'{depth}: expected 65536 bytes')
        match=re.search(rf'focus_delay_{depth:03}mm\[FOCUS_WORDS\]\s*=\s*\{{(.*?)\}};',c,re.S)
        if not match: raise ValueError(f'{depth}: C array missing')
        words=[int(v,16) for v in re.findall(r'0x[0-9A-Fa-f]+',match[1])]
        if len(words)!=16384 or struct.pack('<16384I',*words)!=raw: raise ValueError(f'{depth}: C/raw mismatch')
        source=ROOT/'esp32c61/configs'/f'delay_profile_focus_{depth}mm.json'
        values=json.loads(source.read_text(encoding='utf-8-sig'))['bram']['delay_profile']
        values=[int(v,0) if isinstance(v,str) else v for v in values]
        if words!=values: raise ValueError(f'{depth}: MATLAB differs from current JSON; resolve explicitly')
        sha=hashlib.sha256(raw).hexdigest()
        if sha in seen: raise ValueError(f'{depth}: duplicate table')
        seen.add(sha)
        profiles.append(dict(depth_mm=depth,words=16384,crc32=f'{zlib.crc32(raw):08x}',sha256=sha))
    c+='\nconst focus_entry_t focus_entries[FOCUS_COUNT] = {\n'
    c+=''.join(f'    {{{p["depth_mm"]}U, 0x{p["crc32"]}U, focus_delay_{p["depth_mm"]:03}mm}},\n' for p in profiles)+'};\n'
    sources={str(p.relative_to(reference)).replace('\\','/'):hashlib.sha256(p.read_bytes()).hexdigest()
             for p in sorted((reference/'tools/para_cal').rglob('*')) if p.suffix in ('.m','.csv')}
    manifest=dict(version=1,byte_order='little',profiles=profiles,source_sha256=sources,
                  c_sha256=hashlib.sha256(c.encode()).hexdigest())
    return c,manifest

def install(targets):
    # Validate first; prepare all replacements before touching installed files.
    originals={p:p.read_bytes() if p.exists() else None for p in targets}
    staged={p:p.with_name(p.name+'.tmp') for p in targets}
    changed=[]
    try:
        for p,body in targets.items():staged[p].write_bytes(body)
        for p,temp in staged.items():temp.replace(p);changed.append(p)
    except Exception:
        for p in reversed(changed):
            if originals[p] is None:p.unlink()
            else:p.write_bytes(originals[p])
        raise
    finally:
        for temp in staged.values():
            if temp.exists():temp.unlink()

def main():
    ap=argparse.ArgumentParser();ap.add_argument('--stage',type=Path,required=True);ap.add_argument('--reference',type=Path,required=True)
    args=ap.parse_args();c,manifest=validate(args.stage,args.reference)
    targets={}
    for folder in (ROOT/'build/src/ps/bram',ROOT/'workspace/B_mode_focus_imaging/src/bram'):
        targets[folder/'focus_bank_data.c']=c.encode()
        targets[folder/'focus_bank_manifest.json']=(json.dumps(manifest,indent=2)+'\n').encode()
    install(targets)
    print('Installed 16 MATLAB focus arrays (1 MiB), all equal to current JSON.')
if __name__=='__main__': main()
