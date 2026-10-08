#!/usr/bin/env python3
"""Ensure the real exit path unregisters every MITM before closing responders."""
from pathlib import Path
import subprocess
import tempfile
source=(Path(__file__).resolve().parents[1]/'source/zt_port.cpp').read_text()
body=source[source.index('    namespace {\n        constinit std::atomic<unsigned> g_registeredMitms'):source.index('    void RequireSystemMemoryHeadroom')]
prefix=r'''
#include <atomic>
#include <cassert>
#include <cstdio>
#include <initializer_list>
int failures=0,calls=0,sleeps=0,exits=0;
struct Result {int v;int GetValue()const{return v;}};
#define R_SUCCEEDED(r) ((r).v==0)
namespace sm {
struct ServiceName {const char*n;static ServiceName Encode(const char*n){return {n};}};
namespace mitm {Result UninstallMitm(ServiceName){++calls;if(failures){--failures;return {1};}return {0};}}
}
struct TimeSpan {static int FromSeconds(int n){return n;}};
namespace os {void SleepThread(int){++sleeps;}}
void svcExitProcess(){++exits;throw 7;}
namespace ztnx {
constexpr unsigned BsdMitmRegistration=1,NifmMitmRegistration=2;
template<class...T> void Trace(const char*,T...){}
'''
suffix=r'''
}
int main(){
 for(unsigned mask=0;mask<4;++mask)for(int retry=0;retry<4;++retry){
  failures=mask?retry:0;calls=sleeps=exits=0;
  ztnx::SetMitmRegistration(mask,true);
  try {ztnx::StopProcessSafely();}catch(int value){assert(value==7);}
  assert(exits==1);assert(ztnx::g_registeredMitms.load()==0);
  assert(calls==int((mask&1)!=0)+int((mask&2)!=0)+(mask?retry:0));
 }
 puts("16 shutdown cases: every live MITM removed before process exit, including failed-unregister retries.");
}
'''
with tempfile.TemporaryDirectory() as directory:
 p=Path(directory);c=p/'exit.cpp';binary=p/'exit'
 c.write_text(prefix+body+suffix)
 subprocess.run(['g++','-std=c++20','-Wall','-Wextra','-Werror',str(c),'-o',str(binary)],check=True)
 subprocess.run([str(binary)],check=True)
