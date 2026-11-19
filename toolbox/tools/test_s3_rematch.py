"""Execute the rematch gate with Unicorn; pass an operator-owned main.flat
and optionally its version (11.3.0 by default).
Requires unicorn. No dumped bytes are committed or emitted.
"""
import sys
import importlib.util,itertools
from pathlib import Path
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import UC_ARM64_REG_X24,UC_ARM64_REG_NZCV,UC_ARM64_REG_X19
b=bytearray(Path(sys.argv[1]).read_bytes());B=0x7100000000
p=Path(__file__).with_name('make_s3_smallmatch.py');spec=importlib.util.spec_from_file_location('patch',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
version=sys.argv[2] if len(sys.argv)>2 else '11.3.0';R=m.REMATCH[version]
for rva,expected,patch in m.BUILDS[version][1]:
 assert b[rva:rva+len(bytes.fromhex(expected))]==bytes.fromhex(expected)
 b[rva:rva+len(bytes.fromhex(patch))]=bytes.fromhex(patch)
u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(B,(max(len(b),0x6200000)+4095)&~4095);u.mem_write(B,bytes(b));manager=0x7200000000;u.mem_map(manager,4096);u.mem_write(B+R['manager_slot'],manager.to_bytes(8,'little'));u.reg_write(UC_ARM64_REG_X24,B+R['manager_slot'])
destinations={B+R['wait'],B+R['lt'],B+R['next']};hit=[]
def stop(uc,pc,size,user):
 if pc in destinations:hit.append(pc);uc.emu_stop()
u.hook_add(UC_HOOK_CODE,stop)
for secondary,primaryOpen,secondaryOpen,negative in itertools.product((0,1),repeat=4):
 u.mem_write(manager+64,bytes([secondary]));u.mem_write(manager+41,bytes([primaryOpen]));u.mem_write(manager+73,bytes([secondaryOpen]));flags=0x80000000 if negative else 0;u.reg_write(UC_ARM64_REG_NZCV,flags);hit.clear();u.emu_start(B+R['gate'],B+R['wait']+4,count=30)
 opened=primaryOpen or (secondary and secondaryOpen)
 expected=B+(R['wait'] if opened else R['lt'] if negative else R['next'])
 assert hit==[expected],(secondary,primaryOpen,secondaryOpen,negative,hit,hex(expected))
 assert u.reg_read(UC_ARM64_REG_NZCV)==flags
print(version+': 16 ARM64 execution cases passed: either live session with backfill open holds the lobby, original LT branch and NZCV preserved')
