#!/usr/bin/env python3
"""Exercise the actual updater ZIP extractor against a synthetic combined bundle.

Needs a host C compiler and minizip development headers. No console data.
"""
from pathlib import Path
import subprocess
import tempfile
import zipfile

source = (Path(__file__).resolve().parents[1] / 'source/update.c').read_text()
functions = source[source.index('static bool zipPathSafe'):source.index('Grid0plusUpdateResult update_apply')]
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    c = root / 'extract.c'
    c.write_text('''#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <minizip/unzip.h>
#define GRID0PLUS_UPDATE_PHASE_INSTALL 1
static void (*s_progressCb)(int,long,long);
static long s_progressTotal;
''' + functions + '\nint main(int argc,char **argv) { return argc==3 && extractZip(argv[1],argv[2]) ? 0 : 1; }\n')
    exe = root / 'extract'
    subprocess.run(['cc', '-Wall', '-Wextra', str(c), '-lminizip', '-o', str(exe)], check=True)
    for existing in (False, True):
        dst = root / ('enabled' if existing else 'absent')
        dst.mkdir()
        frozen = {
            'atmosphere/contents/4200000000005A54/exefs.nsp': b'old module',
            'atmosphere/contents/4200000000005A54/flags/boot2.flag': b'',
            'atmosphere/contents/4200000000005A54/zt/identity.secret': b'private synthetic identity',
            'switch/.overlays/sys-GRID0+.ovl': b'old overlay',
        }
        if existing:
            for name, body in frozen.items():
                p=dst/name; p.parent.mkdir(parents=True,exist_ok=True); p.write_bytes(body)
        bundle = root / 'bundle.zip'
        with zipfile.ZipFile(bundle,'w') as z:
            z.writestr('switch/grid0plus-toolbox.nro',b'new toolbox')
            for name in frozen: z.writestr(name,b'replacement')
        subprocess.run([str(exe),str(bundle),str(dst)],check=True)
        assert (dst/'switch/grid0plus-toolbox.nro').read_bytes()==b'new toolbox'
        for name, body in frozen.items():
            p=dst/name
            assert p.read_bytes()==body if existing else not p.exists(),name
    missing=root/'missing.zip'
    with zipfile.ZipFile(missing,'w') as z: z.writestr('switch/.overlays/sys-GRID0+.ovl',b'overlay')
    assert subprocess.run([str(exe),str(missing),str(dst)]).returncode!=0
print('PASS: Toolbox updates; installed module, boot flag, identity and overlay stay unchanged; absent module stays absent; missing NRO rejected')
