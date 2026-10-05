"""Read-only disassembly of the verified local game copies, at preferred addresses."""
from pathlib import Path
import sys
ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'test/logs/steam_analysis/deps'))
import pefile
from capstone import Cs, CS_ARCH_X86, CS_MODE_32

def image(edition='steam'):
    path = ROOT/'test/runtime/MBAACC_1/MBAA.exe' if edition == 'steam' else Path(
        r'I:\work_space\CCCaster_verB\test\runtime\MBAACC_1\MBAA.exe')
    pe = pefile.PE(str(path))
    return pe.get_memory_mapped_image()

if __name__ == '__main__':
    data = image(sys.argv[1])
    md = Cs(CS_ARCH_X86, CS_MODE_32)
    for arg in sys.argv[2:]:
        start, length = (int(x,16) for x in arg.split(':'))
        for ins in md.disasm(data[start-0x400000:start-0x400000+length],start):
            print(f'{ins.address:08X} {ins.bytes.hex():24s} {ins.mnemonic} {ins.op_str}')
