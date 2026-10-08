#!/usr/bin/env python3
"""Inject startup failures into the actual LateInitialize body using host service stubs."""
from pathlib import Path
import os
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "source/main.cpp").read_text()
body = source[source.index("        bool LateInitialize("):source.index("        void NodeThreadMain")]
prefix = '\n#include <cassert>\n#include <cstdio>\nint failure=0, tc=0,cc=0,bc=0;\n#define R_FAILED(r) ((r)!=0)\ntemplate<class F> struct Guard { F f; ~Guard(){f();} };\nstruct GuardMaker { template<class F> Guard<F> operator+(F f){return {f};} };\n#define ON_SCOPE_EXIT auto cleanup = GuardMaker{} + [&]()\nint timeInitialize(){if(failure==1)return 1; ++tc;return 0;}\nint csrngInitialize(){if(failure==2)return 1;++cc;return 0;}\nint bsdInitialize(void*,int,int){if(failure==3)return 1;++bc;return 0;}\nint socketInitialize(void*){return failure==4;}\nvoid timeExit(){--tc;} void csrngExit(){--cc;} void bsdExit(){--bc;}\nnamespace ztnx {template<class... T> void Trace(const char*,T...) {} }\nint BsdConfig=0;\nstruct {int num_bsd_sessions=2;int bsd_service_type=1;} SocketConfig;\n'
suffix = '\nint main(){for(failure=1;failure<=4;++failure){for(int n=0;n<100;++n){assert(!LateInitialize(true,true,true));assert(tc==0&&cc==0&&bc==0);}}\nfailure=0;assert(LateInitialize(true,true,true));assert(tc==1&&cc==1&&bc==1);timeExit();csrngExit();bsdExit();assert(LateInitialize(false,false,false));assert(tc==0&&cc==0&&bc==0);puts("400 injected startup failures retain no service references; success keeps each service once.");}\n'
with tempfile.TemporaryDirectory(prefix="grid0-late-init-") as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(prefix + body + suffix)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++17", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
