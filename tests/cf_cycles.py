"""Cycles a LYRA track takes a block (MCF54418 timing tables via digiemu's CycleClock), 8 tracks."""
import os, subprocess, struct, sys
sys.path.insert(0, os.environ.get('DIGIEMU', '../digiemu'))  # a digiemu checkout
from unicorn import *
from unicorn.m68k_const import *
from emu.cftiming import CycleClock
ELF = sys.argv[1]
sym = {l.split()[2]: int(l.split()[0], 16) for l in subprocess.run(["m68k-linux-gnu-nm", ELF], capture_output=True, text=True).stdout.splitlines() if len(l.split()) == 3}
dis = subprocess.run(["m68k-linux-gnu-objdump", "-d", ELF], capture_output=True, text=True).stdout
start = sym["_start"]
# the instruction after the call to lyra_render: where _start loops
lines = dis[dis.index("<_start>:"):].splitlines()[1:]
after = None
for i, l in enumerate(lines):
    if "lyra_render" in l and ("jsr" in l or "bsr" in l):
        after = int(lines[i + 1].split(":")[0].strip(), 16); break
class M: pass
uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN); uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
uc.mem_map(0, 0x200000); uc.mem_write(0x10000, open(ELF.replace('.elf', '.bin'), "rb").read())
uc.mem_write(sym["fw_pitch_tab"], b"".join(struct.pack(">I", round(2 ** ((i - 10752) / 2048) * (1 << 29))) for i in range(14849)))
uc.mem_write(sym["fw_render_machine"], bytes([90] * 8))
for t in range(8): uc.mem_write(sym["fw_voice_note"] + 4 * t, struct.pack(">i", (48 + t) << 16))
m = M(); m.uc = uc; m.raise_vector = lambda *a, **k: None
clk = CycleClock(m)
def run(settings, label, asleep=False, blocks=6):
    uc.mem_write(sym["fw_amp_env"], b"".join(struct.pack(">iii", 0, -390 if asleep else -0x40000000, 0) for t in range(8)))
    for t in range(8):
        base = sym["fw_voice_params"] + 2 * 53 * t
        for k, v in settings.items():
            uc.mem_write(base + 2 * (k if k < 8 else 29 + k - 8), struct.pack(">h", v << 8))
        uc.mem_write(base, struct.pack(">h", 0x4000))
    res = []
    for b in range(blocks):
        uc.reg_write(UC_M68K_REG_A7, 0xf0000)
        c0 = clk.cycles
        uc.emu_start(start, after)
        res.append(clk.cycles - c0)
    c = sum(res[2:]) / len(res[2:]) / 8
    print("%-42s %6.0f cycles a track a block  (%4.1f %% of a block at 250 MHz)" % (label, c, 100 * c / (250e6 / 1500)))
    return c
D = {1: 24, 2: 0, 3: 0, 4: 40, 5: 0, 6: 0, 7: 100, 8: 40, 9: 54, 10: 0, 11: 24, 12: 90, 13: 40, 14: 20}
run(D, "defaults (SUM, VIB 24, delay on, DMOD)")
run({**D, 1: 60, 2: 70, 3: 102, 6: 90, 10: 112, 11: 50, 13: 100, 14: 60}, "busy (DLY FM, FBK, CHS, VIB, SHRP, DMOD)")
run({**D, 1: 127, 2: 127, 3: 30, 6: 127, 10: 115, 11: 60, 13: 127, 14: 127}, "everything nonlinear at 127 (CHS)")
run({**D, 2: 100, 3: 124, 6: 110, 13: 90}, "TRK, all eight linked in pairs, FBK 110")
run({**D, 1: 0, 2: 0, 11: 0, 6: 0, 13: 0, 14: 0}, "plain (no SHRP/FM/FBK/VIB, delay silent)")
run(D, "asleep", asleep=True)
if clk.decode_mismatches: print("decode mismatches:", clk.decode_mismatches)
