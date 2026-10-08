#!/usr/bin/env python3
"""Inject resource failures into the actual node initialization body."""
from pathlib import Path
import os
import subprocess
import tempfile

source = (Path(__file__).resolve().parents[1] / "source/zt_port.cpp").read_text()
body = source[source.index("    bool Port::Initialize("):source.index("    bool Port::switchNetwork(")]
prefix = r'''
#include <cassert>
#include <cstdint>
#include <cstddef>
#include <cstdio>
int failure=0, sockets=0, nodes=0;
int close(int){ --sockets; return 0; }
template<class F> struct Guard { F f; ~Guard(){f();} };
struct GuardMaker { template<class F> Guard<F> operator+(F f){return {f};} };
#define ON_SCOPE_EXIT auto cleanup = GuardMaker{} + [&]()
namespace ams::init {
struct Allocator { std::size_t GetAllocatableSize(){return failure==2 ? 0 : 1000000;} };
Allocator* GetAllocator(){static Allocator a;return &a;}
}
struct ZT_Node {};
struct ZT_Node_Config {int enableEncryptedHello,lowBandwidthMode;};
struct ZT_Node_Callbacks {
 int version;
 void (*statePutFunction)(),(*stateGetFunction)(),(*wirePacketSendFunction)(),
 (*virtualNetworkFrameFunction)(),(*virtualNetworkConfigFunction)(),
 (*eventCallback)(),(*pathCheckFunction)(),(*pathLookupFunction)();
};
enum ZT_ResultCode {ZT_RESULT_OK,ZT_RESULT_ERROR};
ZT_ResultCode ZT_Node_new(ZT_Node**n,const ZT_Node_Config*,void*,void*,const ZT_Node_Callbacks*,int64_t){
 if(failure==3) { return ZT_RESULT_ERROR; }
 *n=new ZT_Node;++nodes;return ZT_RESULT_OK;
}
void ZT_Node_delete(ZT_Node*n){delete n;--nodes;}
ZT_ResultCode ZT_Node_join(ZT_Node*,uint64_t,void*,void*){return failure==4?ZT_RESULT_ERROR:ZT_RESULT_OK;}
uint64_t ZT_Node_address(ZT_Node*){return 1;}
namespace ztnx {
template<class...T> void Trace(const char*,T...){}
template<class...T> void Event(const char*,T...){}
uint32_t ConfigIpv4(const char*){return 0;}
bool ConfigFlag(const char*,bool d){return d;}
int64_t NowMs(){return 1;}
bool StartNatMapper(uint64_t){return false;}
constexpr int DefaultWirePort=9993;
constexpr std::size_t NodeAllocLowWater=300000;
struct Port {
 struct Vnet {void reset(){} void setEmit(void(*)(),void*){}} m_vnet;
 uint64_t m_nwid=0; int m_wireFd=-1;int m_wireLocalPort=0;
 ZT_Node*m_node=nullptr;int m_lastError=0;bool m_natEnabled=false;int64_t m_startedMs=0;
 static void cbEmit(){} static void cbStatePut(){} static void cbStateGet(){}
 static void cbWireSend(){} static void cbVirtualFrame(){} static void cbNetworkConfig(){} static void cbEvent(){}
 bool openWireSocket(){if(failure==1)return false;m_wireFd=7;m_wireLocalPort=9993;++sockets;return true;}
 void writeStatus(bool){} void subscribeBroadcast(){}
 bool Initialize(uint64_t);
};
'''
suffix = r'''
}
int main(){
 ztnx::Port p;
 for(failure=1;failure<=4;++failure)for(int n=0;n<100;++n){
  assert(!p.Initialize(1));assert(sockets==0&&nodes==0);
  assert(p.m_wireFd==-1&&p.m_node==nullptr&&p.m_wireLocalPort==0);
 }
 failure=0;assert(p.Initialize(1));assert(sockets==1&&nodes==1);
 ZT_Node_delete(p.m_node);close(p.m_wireFd);
 puts("400 failed node startups release sockets and nodes; success retains its resources.");
}
'''
with tempfile.TemporaryDirectory(prefix="grid0-node-init-") as directory:
    cpp = Path(directory) / "test.cpp"
    binary = Path(directory) / "test"
    cpp.write_text(prefix + body + suffix)
    subprocess.run([os.environ.get("CXX", "g++"), "-std=c++20", "-Wall", "-Wextra", "-Werror", str(cpp), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
