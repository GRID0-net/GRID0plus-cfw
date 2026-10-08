#!/usr/bin/env python3
"""Exercise the real release parser and component ZIP installer without console data."""
from pathlib import Path
import json
import os
import warnings
import subprocess
import tempfile
import zipfile

source = (Path(__file__).resolve().parents[1] / 'source/update.c').read_text()
parser = source[source.index('// Walk complete JSON'):source.index('static int semverCompare')]
extractor = source[source.index('static bool zipPathSafe'):source.index('Grid0plusUpdateResult update_apply')]
prefix = '''#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#include <sys/stat.h>
#include <minizip/unzip.h>
typedef enum {GRID0PLUS_UPDATE_TOOLBOX=1,GRID0PLUS_UPDATE_SYSMODULE=2,GRID0PLUS_UPDATE_BOTH=3} Grid0plusUpdateTarget;
#define GRID0PLUS_UPDATE_PHASE_INSTALL 1
static void (*s_progressCb)(int,long,long);
static long s_progressTotal;
static char s_releaseTag[64];
static int no_replace_rename(const char *from, const char *to) {
 struct stat st;
 if(stat(to,&st)==0){errno=EEXIST;return -1;}
 if(getenv("FAIL_INSTALL") && strstr(from,".grid0-update.tmp")){errno=EIO;return -1;}
 return rename(from,to);
}
#define rename no_replace_rename
'''
suffix = '''
int main(int argc,char **argv) {
 if(argc==5 && !strcmp(argv[1],"extract")) return extractZip(argv[2],argv[3],atoi(argv[4])) ? 0 : 1;
 if(argc==3 && !strcmp(argv[1],"parse")) {
  FILE*f=fopen(argv[2],"rb"); if(!f)return 2;
  char b[65536];size_t n=fread(b,1,sizeof(b)-1,f);fclose(f);b[n]=0;
  int a=0,c=0,d=0;long size=0;char url[512];
  if(!parseReleaseJson(b,&a,&c,&d,url,sizeof(url),&size)) return 1;
  printf("%s %ld\\n",s_releaseTag,size);return 0;
 }
 return 2;
}
'''
with tempfile.TemporaryDirectory() as tmp:
    root = Path(tmp)
    c = root/'update.c'; c.write_text(prefix+parser+extractor+suffix)
    exe=root/'update'
    subprocess.run(['cc','-Wall','-Wextra','-Werror',str(c),'-lminizip','-o',str(exe)],check=True)
    payloads={
      'switch/grid0plus-toolbox.nro':b'new toolbox',
      'atmosphere/contents/4200000000005A54/exefs.nsp':b'new module',
      'switch/.overlays/sys-GRID0+.ovl':b'new overlay',
    }
    protected={
      'atmosphere/contents/4200000000005A54/flags/boot2.flag':b'',
      'atmosphere/contents/4200000000005A54/zt/identity.secret':b'private synthetic identity',
      'config/sys-GRID+.ini':b'bsd_mitm=0\nnifm_mitm=0\n',
      'atmosphere/hosts/emummc.txt':b'user hosts',
    }
    bundle=root/'bundle.zip'
    with zipfile.ZipFile(bundle,'w') as z:
      for n,b in payloads.items():z.writestr(n,b)
      for n in protected:z.writestr(n,b'unwanted replacement')
    def runzip(z,d,t):return subprocess.run([str(exe),'extract',str(z),str(d),str(t)])
    for target in (1,2,3):
      for existing in (False,True):
        dst=root/f'{target}-{existing}';dst.mkdir()
        if existing:
          for n,b in {**protected,**{k:b'old binary' for k in payloads}}.items():
            p=dst/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b)
        assert runzip(bundle,dst,target).returncode==0
        for n,b in payloads.items():
          chosen=(n.startswith('switch/grid0plus') and target&1) or (not n.startswith('switch/grid0plus') and target&2)
          p=dst/n
          if chosen:assert p.read_bytes()==b,n
          elif existing:assert p.read_bytes()==b'old binary',n
          else:assert not p.exists(),n
        for n,b in protected.items():
          p=dst/n
          if existing:assert p.read_bytes()==b,n
          else:assert not p.exists(),n
    # Failed placement restores the original even with non-overwriting rename.
    dst=root/'placement-failure';dst.mkdir()
    p=dst/'switch/grid0plus-toolbox.nro';p.parent.mkdir(parents=True);p.write_bytes(b'old binary')
    env=dict(os.environ,FAIL_INSTALL='1')
    assert subprocess.run([str(exe),'extract',str(bundle),str(dst),'1'],env=env).returncode!=0
    assert p.read_bytes()==b'old binary'
    assert not list(dst.rglob('*.grid0-update.tmp'))
    # Missing/duplicate/path-traversal payloads must not replace any binary.
    for case in ('missing','duplicate','unsafe'):
      dst=root/case;dst.mkdir()
      for n in payloads:
        p=dst/n;p.parent.mkdir(parents=True,exist_ok=True);p.write_bytes(b'old binary')
      zpath=root/f'{case}.zip'
      with zipfile.ZipFile(zpath,'w') as z:
        for n,b in payloads.items():
          if case=='missing' and n.endswith('.ovl'):continue
          z.writestr(n,b)
        if case=='duplicate':
          with warnings.catch_warnings():
            warnings.simplefilter('ignore',UserWarning)
            z.writestr('switch/grid0plus-toolbox.nro',b'duplicate')
        if case=='unsafe':z.writestr('../outside',b'bad')
      assert runzip(zpath,dst,3).returncode!=0
      for n in payloads:assert (dst/n).read_bytes()==b'old binary'
      assert not list(dst.rglob('*.grid0-update.tmp'))
    for tag in ('v0.8.4','v0.8.4-sys.42'):
      obj={'draft':False,'prerelease':False,'tag_name':tag,'body':'escaped " } { text',
           'assets':[{'name':'unrelated','size':1}, {'name':f'GRID0-cfw-{tag}.zip','size':5000,
             'browser_download_url':f'https://github.com/GRID0-net/GRID0plus-cfw/releases/download/{tag}/GRID0-cfw-{tag}.zip'}]}
      for indent in (None,2):
        p=root/'release.json';p.write_text(json.dumps(obj,indent=indent))
        result=subprocess.run([str(exe),'parse',str(p)],capture_output=True,text=True)
        assert result.returncode==0,result.stderr
        assert result.stdout.strip()==f'{tag} 5000'
      bad=json.loads(json.dumps(obj));del bad['assets'][1]['browser_download_url']
      bad['assets'].append({'name':'wrong sibling','browser_download_url':obj['assets'][1]['browser_download_url']})
      p.write_text(json.dumps(bad));assert subprocess.run([str(exe),'parse',str(p)]).returncode!=0
      for field in ('draft','prerelease'):
        bad=dict(obj);bad[field]=True;p.write_text(json.dumps(bad));assert subprocess.run([str(exe),'parse',str(p)]).returncode!=0
print('PASS: three component choices; boot flags/settings/identity preserved; invalid bundles staged without replacement; numeric and sys-only releases parse independently of whitespace')
