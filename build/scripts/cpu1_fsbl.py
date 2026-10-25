"""Project-local, idempotent FSBL hook. Does not patch the installed SDK."""
from pathlib import Path
import hashlib
import re
import os
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
FILES = {name: ROOT / 'common' / name for name in
         ('cpu1_control.h', 'cpu1_control.c', 'cpu1_platform.h')}
FILES['cpu1_fsbl.h'] = ROOT / 'build/src/fsbl/cpu1_fsbl.h'


def configure_cpu1_fsbl(workspace, platform_name='ubmi_p'):
    source = Path(workspace) / platform_name / 'zynq_fsbl'
    hooks = source / 'fsbl_hooks.c'
    text = hooks.read_text(encoding='utf-8')
    # Reject template drift instead of silently leaving a regenerated FSBL unpatched.
    pattern = r'(u32\s+FsblHookBeforeHandoff\(void\)\s*\{)(.*?)(\n\})'
    match = re.search(pattern, text, re.S)
    if not match or match.group(2).count('Status = XST_SUCCESS;') != 1:
        raise RuntimeError('FSBL handoff hook changed; review CPU1 integration')
    if 'cpu1_fsbl_before_handoff();' not in match.group(2):
        body = match.group(2).replace('Status = XST_SUCCESS;',
            'Status = XST_SUCCESS;\n\tcpu1_fsbl_before_handoff(); /* warning only */', 1)
        text = text[:match.start(2)] + body + text[match.end(2):]
    if '#include "cpu1_fsbl.h"' not in text:
        anchor = '#include "fsbl_hooks.h"'
        if text.count(anchor) != 1:
            raise RuntimeError('FSBL include anchor changed')
        text = text.replace(anchor, anchor + '\n#include "cpu1_fsbl.h"', 1)
    for name, origin in FILES.items():
        content = origin.read_bytes()
        dest = source / name
        if not dest.exists() or dest.read_bytes() != content:
            dest.write_bytes(content)
    if hooks.read_text(encoding='utf-8') != text:
        hooks.write_text(text, encoding='utf-8', newline='\n')
    verify_cpu1_fsbl(workspace, platform_name)
    return source


def verify_cpu1_fsbl(workspace, platform_name='ubmi_p'):
    source = Path(workspace) / platform_name / 'zynq_fsbl'
    text = (source / 'fsbl_hooks.c').read_text(encoding='utf-8')
    section = re.search(r'u32\s+FsblHookBeforeHandoff\(void\)\s*\{(.*?)\n\}', text, re.S)
    if (not section or section.group(1).count('cpu1_fsbl_before_handoff();') != 1
            or 'return (Status);' not in section.group(1)):
        raise RuntimeError('CPU1 FSBL hook missing/duplicated')
    for name, origin in FILES.items():
        if (source / name).read_bytes() != origin.read_bytes():
            raise RuntimeError('Stale CPU1 FSBL source: ' + name)
    return hashlib.sha256((source / 'cpu1_control.c').read_bytes()).hexdigest()


def build_cpu1_fsbl(workspace, platform_name='ubmi_p', tools=None):
    source = configure_cpu1_fsbl(workspace, platform_name)
    tools = Path(tools or os.environ.get('AMD_DESIGN_TOOLS_2026_1', 'D:/AMDDesignTools/2026.1'))
    cmake = tools / 'Vitis/tps/win64/cmake-4.0.1/bin/cmake.exe'
    env = os.environ.copy()
    env['PATH'] = str(tools / 'Vitis/gnu/aarch32/nt/gcc-arm-none-eabi/bin') + os.pathsep + env['PATH']
    subprocess.run([str(cmake), '--build', str(source / 'build'), '-j', '2'], env=env, check=True)
    verify_cpu1_fsbl(workspace, platform_name)
    elf = source / 'build/fsbl.elf'
    if b'@CPU1_FSBL disabled=' not in elf.read_bytes():
        raise RuntimeError('Built FSBL does not contain CPU1 diagnostic; refusing stale ELF')
    export = Path(workspace) / platform_name / 'export' / platform_name / 'sw/boot/fsbl.elf'
    if not export.parent.is_dir():
        raise RuntimeError('FSBL export directory missing')
    shutil.copy2(elf, export)
    if export.read_bytes() != elf.read_bytes():
        raise RuntimeError('FSBL export mismatch')
    print('CPU1_FSBL_REBUILT:', export, 'SHA256=' + hashlib.sha256(export.read_bytes()).hexdigest())
    return export


if __name__ == '__main__':
    import argparse
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--workspace', type=Path, required=True)
    args = parser.parse_args()
    build_cpu1_fsbl(args.workspace.resolve())
