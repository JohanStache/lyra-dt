import subprocess, struct, sys
sys.path.insert(0, __import__('os').environ.get('DIGIEMU', '../digiemu'))  # a digiemu checkout
from unicorn import *
from unicorn.m68k_const import *
from emu.cftiming import CycleClock
ELF='t.elf'
sym = {l.split()[2]: int(l.split()[0], 16) for l in subprocess.run(["m68k-linux-gnu-nm", ELF], capture_output=True, text=True).stdout.splitlines() if len(l.split()) == 3}
dis = subprocess.run(["m68k-linux-gnu-objdump", "-d", ELF], capture_output=True, text=True).stdout
lines = dis[dis.index("<_start>:"):].splitlines()[1:]
after = next(int(lines[i + 1].split(":")[0].strip(), 16) for i, l in enumerate(lines) if "blockrun" in l)
class M: pass
uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
uc.mem_map(0, 0x200000); uc.mem_write(0x10000, open('t.bin', "rb").read())
uc.mem_write(sym['cc'], struct.pack('>128i', *([-1] * 128)))
m = M(); m.uc = uc; m.raise_vector = lambda *a, **k: None
clk = CycleClock(m)
def run(settings, label, blocks=8):
    for t in range(4):
        uc.mem_write(sym['kn'] + 60 * t, struct.pack('>15i', *[settings.get(k, 0) for k in range(15)]))
    res = []
    for b in range(blocks):
        uc.mem_write(sym['start_flag'], struct.pack('>I', 1 if b == 0 else 0))
        uc.reg_write(UC_M68K_REG_A7, 0x1f0000)
        c0 = clk.cycles
        uc.emu_start(sym['_start'], after)
        res.append(clk.cycles - c0)
    c = sum(res[3:]) / len(res[3:]) / 4
    print("%-40s %6.0f cycles a track a block  (%4.1f %%)" % (label, c, 100 * c / (250e6 / 1500)))
D = {0: 0, 1: 24, 2: 0, 3: 0, 4: 40, 5: 0, 6: 0, 7: 100, 8: 40, 9: 54, 10: 0, 11: 24, 12: 90, 13: 40, 14: 20}
run(D, "defaults")
run({**D, 1: 60, 2: 70, 3: 4, 6: 90, 10: 5, 11: 50, 13: 100, 14: 60}, "busy (DLY FM, FBK, CHS, VIB, SHRP, DMOD)")
run({**D, 1: 127, 2: 127, 3: 1, 6: 127, 10: 5, 11: 60, 13: 127, 14: 127}, "everything nonlinear at 127")
run({**D, 2: 100, 3: 5, 6: 110, 13: 90}, "TRK, tracks linked in pairs, FBK 110")
run({**D, 1: 0, 2: 0, 11: 0, 6: 0, 13: 0, 14: 0}, "plain")
