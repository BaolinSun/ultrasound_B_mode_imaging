"""Build deterministic, flash-backed focus assets without changing source tables."""
import argparse
import gzip
import hashlib
import json
from pathlib import Path
import re
import struct
import zlib

DEPTHS = tuple(range(20, 171, 10))
WORDS = 16384


def load_profile(path):
    data = json.loads(path.read_text(encoding='utf-8-sig'))
    if not isinstance(data, dict) or set(data) - {'description', 'notes', 'metadata', 'format_version', 'bram'}:
        raise ValueError(f'{path.name}: unsupported profile fields')
    version = data.get('format_version', 1)
    if type(version) is not int or version != 1 or not isinstance(data.get('bram'), dict) or set(data['bram']) != {'delay_profile'}:
        raise ValueError(f'{path.name}: expected only bram.delay_profile')
    source = data['bram']['delay_profile']
    if not isinstance(source, list) or len(source) != WORDS:
        raise ValueError(f'{path.name}: expected {WORDS} words')
    words = []
    for value in source:
        if isinstance(value, str) and re.fullmatch(r'(?:0[xX][0-9a-fA-F]+|[0-9]+)', value):
            value = int(value, 16 if value.lower().startswith('0x') else 10)
        if type(value) is not int or not 0 <= value <= 0xffffffff:
            raise ValueError(f'{path.name}: invalid uint32 word at {len(words)}')
        words.append(value)
    return words


def build_assets(source_dir, output_dir):
    profiles, pack = [], bytearray()
    for depth in DEPTHS:
        path = source_dir / f'delay_profile_focus_{depth}mm.json'
        words = load_profile(path)
        raw = struct.pack(f'<{WORDS}I', *words)
        digest = hashlib.sha256(raw).hexdigest()
        item = {'focus_mm': depth, 'words': WORDS, 'source': path.name,
                'crc32': f'{zlib.crc32(raw):08x}', 'sha256': digest,
                'url': f'/focus/{depth}-{digest[:12]}.json'}
        document = json.dumps({'format_version': 1, 'bram': {'delay_profile': words}}, separators=(',', ':')).encode()
        encoded = gzip.compress(document, compresslevel=9, mtime=0)
        item.update(offset=len(pack), length=len(encoded))
        profiles.append(item)
        pack.extend(encoded)
    public = []
    for item in profiles:
        public.append({**{k: v for k, v in item.items() if k not in ('offset', 'length')},
                       'same_as_mm': [p['focus_mm'] for p in profiles if p['sha256'] == item['sha256'] and p != item]})
    catalog = {'version': 1, 'profiles': public}
    text = json.dumps(catalog, separators=(',', ':'))
    header = '#pragma once\n/* Generated from configs; offsets address compressed flash bytes. */\n'
    header += 'typedef struct { const char *url; unsigned offset, length; } focus_asset_t;\n'
    header += 'static const focus_asset_t focus_assets[] = {\n'
    header += ''.join(f'    {{"{p["url"]}", {p["offset"]}u, {p["length"]}u}},\n' for p in profiles) + '};\n'
    header += 'static const char focus_catalog_json[] = ' + json.dumps(text) + ';\n'
    output_dir.mkdir(parents=True, exist_ok=True)
    for name, content in [('focus_profiles.bin', bytes(pack)), ('focus_assets.h', header.encode()), ('focus_catalog.json', text.encode())]:
        target = output_dir / name
        if not target.exists() or target.read_bytes() != content:
            target.write_bytes(content)
    return catalog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    try:
        build_assets(args.source, args.output)
    except (OSError, ValueError, TypeError) as exc:
        parser.exit(1, f'Focus asset build failed: {exc}\n')


if __name__ == '__main__':
    main()
