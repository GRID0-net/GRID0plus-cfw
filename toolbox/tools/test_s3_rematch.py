"""Execute the rematch gate with Unicorn; pass an operator-owned main.flat.
Requires unicorn. No dumped bytes are committed or emitted.
"""
import sys
import importlib.util,itertools
from pathlib import Path
from unicorn import Uc,UC_ARCH_ARM64,UC_MODE_ARM,UC_HOOK_CODE
from unicorn.arm64_const import UC_ARM64_REG_X24,UC_ARM64_REG_NZCV,UC_ARM64_REG_X19
b=bytearray(Path(sys.argv[1]).read_bytes());B=0x7100000000
p=Path(__file__).with_name('make_s3_smallmatch.py');spec=importlib.util.spec_from_file_location('patch',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
for rva,expected,patch in m.RECORDS:
 assert b[rva:rva+len(bytes.fromhex(expected))]==bytes.fromhex(expected)
 b[rva:rva+len(bytes.fromhex(patch))]=bytes.fromhex(patch)
u=Uc(UC_ARCH_ARM64,UC_MODE_ARM);u.mem_map(B,(max(len(b),0x6200000)+4095)&~4095);u.mem_write(B,bytes(b));manager=0x7200000000;u.mem_map(manager,4096);u.mem_write(B+0x605c278,manager.to_bytes(8,'little'));u.reg_write(UC_ARM64_REG_X24,B+0x605c278)
destinations={B+0x389fb2c,B+0x389f798,B+0x389f658};hit=[]
def stop(uc,pc,size,user):
 if pc in destinations:hit.append(pc);uc.emu_stop()
u.hook_add(UC_HOOK_CODE,stop)
for secondary,primaryOpen,secondaryOpen,negative in itertools.product((0,1),repeat=4):
 u.mem_write(manager+64,bytes([secondary]));u.mem_write(manager+41,bytes([primaryOpen]));u.mem_write(manager+73,bytes([secondaryOpen]));flags=0x80000000 if negative else 0;u.reg_write(UC_ARM64_REG_NZCV,flags);hit.clear();u.emu_start(B+0x389f654,B+0x389fb30,count=30)
 opened=secondaryOpen if secondary else primaryOpen
 expected=B+(0x389fb2c if opened else 0x389f798 if negative else 0x389f658)
 assert hit==[expected],(secondary,primaryOpen,secondaryOpen,negative,hit,hex(expected))
 assert u.reg_read(UC_ARM64_REG_NZCV)==flags
print('16 ARM64 execution cases passed: active-session selection, open/closed gate, original LT branch and NZCV preserved')
