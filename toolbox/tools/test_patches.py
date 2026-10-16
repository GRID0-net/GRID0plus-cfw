#!/usr/bin/env python3
"""Run the real patches.c against a fake GitHub, a fake mirror and a fake SD card.

The network calls are replaced by a shim that serves files from a directory,
so this checks the manifest parsing, the hash comparison, installing from
patches.zip, removal of dropped patches and the mirror fallback, all on a PC.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import tempfile
import shutil
import zipfile

toolbox = Path(__file__).resolve().parents[1]
update = (toolbox / 'source/update.c').read_text()
helpers = (update[update.index('// Walk complete JSON'):update.index('static bool parseReleaseJson')] +
           update[update.index('bool mkdirParents'):update.index('static const char *updatePaths')])

SHIM = r'''
#include <openssl/sha.h>
#include <sys/stat.h>
#include <dirent.h>
#define FS_MAX_PATH 0x301
static inline void sha256CalculateHash(void *d, const void *s, size_t n) { SHA256(s, n, d); }
static inline int fsdevCommitDevice(const char *n) { (void)n; return 0; }
'''

HARNESS = r'''
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include "net.h"
#include "json.h"
#include "config.h"
#include "certs.h"
#include "patches.h"
#include "update.h"

char g_server_ip[GRID0PLUS_SERVER_IP_MAX] = "10.0.0.1";
void grid0plus_trace(const char *s) { fprintf(stderr, "trace: %s\n", s); }
bool grid0plus_ensure_dir(const char *p) { mkdir(p, 0777); return true; }
void grid0plus_remove_dir_and_contents(const char *dir) {
    DIR *d = opendir(dir); if (!d) { rmdir(dir); return; }
    struct dirent *e; char p[1024];
    while ((e = readdir(d))) { if (e->d_name[0] == '.') continue; snprintf(p, sizeof p, "%s/%s", dir, e->d_name); remove(p); }
    closedir(d); rmdir(dir);
}
bool net_ready(void) { return true; }

// update_check()'s release list, as the fake GitHub API would answer it.
const char *update_release_list(int *error) {
    static char buf[1 << 16]; char p[1024];
    snprintf(p, sizeof p, "%s/api.github.com/repos/GRID0-net/GRID0plus-cfw/releases", getenv("FAKE"));
    FILE *f = fopen(p, "rb");
    if (!f) { *error = getenv("DOWN") ? NET_ERR_CONNECT : 404; return NULL; }
    buf[fread(buf, 1, sizeof buf - 1, f)] = 0; fclose(f); *error = 0; return buf;
}

// Serves FAKE/<host>/<path>; a missing file is a 404.
static char *fakePath(const char *host, const char *path) {
    static char p[2048]; const char *q = strchr(path, '?');
    snprintf(p, sizeof p, "%s/%s%.*s", getenv("FAKE"), host, q ? (int)(q - path) : (int)strlen(path), path);
    return p;
}
unsigned char *net_https_get(const char *host, int port, const char *path, size_t *len, int *status) {
    (void)port; FILE *f = fopen(fakePath(host, path), "rb");
    if (!f) { *status = getenv("DOWN") && strstr(getenv("DOWN"), host) ? NET_ERR_CONNECT : 404; return NULL; }
    unsigned char *b = malloc(1 << 20); *len = fread(b, 1, 1 << 20, f); fclose(f); *status = 200; return b;
}
long net_https_get_to_file(const char *host, int port, const char *path, FILE *out, int *status, net_progress_fn cb) {
    size_t n; unsigned char *b = net_https_get(host, port, path, &n, status);
    (void)cb; if (!b) return -1; fwrite(b, 1, n, out); free(b); return (long)n;
}
''' + helpers + r'''
int main(int argc, char **argv) {
    if (!strcmp(argv[1], "check")) {
        Grid0plusPatchStatus st = patches_check();
        printf("%d %d %d %d %s\n", st.error, st.serial, st.game_changes, st.system_changes, st.source ? st.source : "-");
        return 0;
    }
    if (!strcmp(argv[1], "apply")) {
        Grid0plusPatchStatus st = patches_check();
        if (st.error) return 2;
        return patches_apply(NULL) ? 0 : 1;
    }
    if (!strcmp(argv[1], "remove")) { patches_remove_all(); return 0; }
    return 3;
}
'''


def ips(tag):
    return b'PATCH' + tag.encode() + b'EOF'


def publish(fake, entries, serial, where=('github', 'mirror'), corrupt=None):
    """entries: list of (kind, dest, data)."""
    patches = []
    files = {}
    for kind, dest, data in entries:
        sha = hashlib.sha256(data).hexdigest()
        e = {'name': dest.split('/')[1], 'kind': kind, 'build_id': 'X', 'dest': dest, 'size': len(data),
             'sha256': sha, 'asset': sha + '.ips', 'targets': ['console', 'emulator'] if kind == 'game' else ['console']}
        if kind == 'game':
            e['title_id'] = '0100C2500FC20000'
        patches.append(e)
        files[sha + '.ips'] = data if sha != corrupt else data + b'!'
    manifest = json.dumps({'serial': serial, 'patches': patches}).encode()
    Path(fake).mkdir(parents=True, exist_ok=True)
    zbuf = Path(fake) / 'patches.zip'
    with zipfile.ZipFile(zbuf, 'w') as z:
        for name, data in files.items():
            z.writestr(name, data)
    for d in where:
        if d == 'github':
            tag = f'patches-{serial}'
            api = Path(fake) / 'api.github.com/repos/GRID0-net/GRID0plus-cfw/releases'
            api.parent.mkdir(parents=True, exist_ok=True)
            api.write_text(json.dumps([
                {'tag_name': 'v0.9.0', 'draft': False},
                {'tag_name': tag, 'draft': False}]))
            base = Path(fake) / f'github.com/GRID0-net/GRID0plus-cfw/releases/download/{tag}'
        else:
            base = Path(fake) / '10.0.0.1/grid0plus/patches'
        base.mkdir(parents=True, exist_ok=True)
        (base / 'manifest.json').write_bytes(manifest)
        (base / 'patches.zip').write_bytes(zbuf.read_bytes())


def main():
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        (tmp / 'patches_host_shim.h').write_text(SHIM)
        (tmp / 'harness.c').write_text(HARNESS)
        exe = tmp / 'patches_test'
        subprocess.run(['cc', '-std=gnu11', '-Wall', '-Werror', '-DGRID0PLUS_HOST_TEST', f'-I{tmp}',
                        f'-I{toolbox}/source', str(tmp / 'harness.c'), str(toolbox / 'source/patches.c'),
                        '-o', str(exe), '-lminizip', '-lz', '-lcrypto'], check=True)
        card, fake = tmp / 'card', tmp / 'fake'
        (card / 'sdmc:').mkdir(parents=True)
        env = dict(os.environ, FAKE=str(fake))

        def run(*args, **extra):
            r = subprocess.run([str(exe), *args], cwd=card, env={**env, **extra}, capture_output=True, text=True)
            if os.environ.get('PDEBUG'): print(args, r.stdout, r.stderr)
            return r.returncode, r.stdout.strip()

        sd = card / 'sdmc:/atmosphere'
        game = ('game', 'exefs_patches/s3certpin_bypass/AAAA.ips', ips('g1'))
        system = ('system', 'exefs_patches/disable_ca_verification/BBBB.ips', ips('s1'))
        game_twin = ('game', 'exefs_patches/s3certpin_bypass/AAAA0000.ips', ips('g1'))
        publish(fake, [game, system, game_twin], 1)

        assert run('check') == (0, '0 1 2 1 GitHub'), run('check')
        assert run('apply')[0] == 0
        assert (sd / game[1]).read_bytes() == game[2] and (sd / system[1]).read_bytes() == system[2]
        assert run('check') == (0, '0 1 0 0 GitHub'), 'nothing to do after installing'

        (sd / system[1]).write_bytes(b'PATCHold')
        assert run('check') == (0, '0 1 0 1 GitHub'), 'a changed file on the card is replaced'
        assert run('apply')[0] == 0 and (sd / system[1]).read_bytes() == system[2]

        # A bad file in the zip replaces nothing.
        game2 = ('game', game[1], ips('g2'))
        publish(fake, [game2, system], 2, corrupt=hashlib.sha256(game2[2]).hexdigest())
        assert run('apply')[0] == 1
        assert (sd / game[1]).read_bytes() == game[2], 'a hash mismatch must not replace the old file'

        # Dropping a patch removes it, and its directory once empty.
        publish(fake, [game2, system], 3)
        assert run('check') == (0, '0 3 2 0 GitHub'), run('check')
        assert run('apply')[0] == 0
        assert not (sd / game_twin[1]).exists() and (sd / game[1]).read_bytes() == game2[2]

        # GitHub down: the mirror answers.
        shutil.rmtree(fake)
        publish(fake, [game2, system], 3, where=('mirror',))
        assert run('check', DOWN='api.github.com') == (0, '0 3 0 0 GRID0+ server'), run('check', DOWN='api.github.com')
        shutil.rmtree(fake)
        fake.mkdir()
        assert run('check', DOWN='api.github.com')[1].startswith('404 0 0 0'), 'no source: an error, no changes'

        # A retired patch on an otherwise current card is still a change.
        (sd / 'exefs_patches/account_delete_user_fix').mkdir(parents=True)
        (sd / 'exefs_patches/account_delete_user_fix/C.ips').write_bytes(b'x')
        publish(fake, [game2, system], 3)
        assert run('check') == (0, '0 3 0 1 GitHub'), run('check')
        shutil.rmtree(sd / 'exefs_patches/account_delete_user_fix')

        # Retired directories go on apply; Switch to Default removes ours only.
        (sd / 'exefs_patches/account_delete_user_fix').mkdir(parents=True)
        (sd / 'exefs_patches/account_delete_user_fix/C.ips').write_bytes(b'x')
        (sd / 'exefs_patches/someone_else').mkdir()
        (sd / 'exefs_patches/someone_else/D.ips').write_bytes(b'y')
        publish(fake, [game2, system], 3)
        assert run('apply')[0] == 0
        assert not (sd / 'exefs_patches/account_delete_user_fix').exists()
        assert run('remove')[0] == 0
        assert not (sd / game[1]).exists() and not (sd / system[1]).exists()
        assert (sd / 'exefs_patches/someone_else/D.ips').exists(), 'other homebrew patches are left alone'

        # Files from a Toolbox that bundled them are adopted, so Default removes them.
        (sd / game[1]).parent.mkdir(parents=True, exist_ok=True)
        (sd / system[1]).parent.mkdir(parents=True, exist_ok=True)
        (sd / game[1]).write_bytes(game2[2])
        (sd / system[1]).write_bytes(system[2])
        assert run('check') == (0, '0 3 0 0 GitHub')
        assert run('remove')[0] == 0 and not (sd / game[1]).exists()
        real_release(exe)
    print('patches: all checks passed')


def real_release(exe):
    """The real manifest and deflated patches.zip, as publish-patches.sh stages them."""
    repo = toolbox.parent
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        stage = tmp / 'fake/github.com/GRID0-net/GRID0plus-cfw/releases/download/patches-real'
        subprocess.run(['bash', str(repo / 'tools/publish-patches.sh'), '--stage', str(stage)],
                       check=True, capture_output=True)
        api = tmp / 'fake/api.github.com/repos/GRID0-net/GRID0plus-cfw/releases'
        api.parent.mkdir(parents=True)
        api.write_text(json.dumps([{'tag_name': 'patches-real', 'draft': False}]))
        card = tmp / 'card'
        (card / 'sdmc:').mkdir(parents=True)
        manifest = json.loads((stage / 'manifest.json').read_text())
        game = sum(1 for p in manifest['patches'] if p['kind'] == 'game')
        system = sum(1 for p in manifest['patches'] if p['kind'] == 'system')
        env = dict(os.environ, FAKE=str(tmp / 'fake'))
        out = lambda *a: subprocess.run([str(exe), *a], cwd=card, env=env, capture_output=True, text=True)
        first = out('check').stdout.split()
        assert first[2:4] == [str(game), str(system)], (first, game, system)
        assert out('apply').returncode == 0
        assert out('check').stdout.split()[2:4] == ['0', '0']
        for p in manifest['patches']:
            on_card = card / 'sdmc:/atmosphere' / p['dest']
            assert hashlib.sha256(on_card.read_bytes()).hexdigest() == p['sha256'], p['dest']


main()
