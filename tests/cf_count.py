#!/usr/bin/env python3
"""Counts the ColdFire instructions lyra_render runs a block, in Unicorn (CFV4E), for 8 LYRA tracks.

    m68k-linux-gnu-gcc -mcpu=54455 -O2 -fno-pic -ffreestanding -fno-builtin -nostdlib -I <sdk include> -c tests/cf_harness.c -o h.o
    m68k-linux-gnu-gcc (same flags) -c lyra.c -o l.o
    m68k-linux-gnu-ld -Ttext=0x10000 -Tbss=0x40000 -e _start h.o l.o -o t.elf
    m68k-linux-gnu-objcopy -O binary -j .text -j .rodata -j .data t.elf t.bin
    python3 tests/cf_count.py t.elf
"""
import subprocess, struct, sys
from unicorn import *
from unicorn.m68k_const import *
ELF = sys.argv[1] if len(sys.argv) > 1 else "t.elf"
sym = {l.split()[2]: int(l.split()[0], 16) for l in subprocess.run(["m68k-linux-gnu-nm", ELF], capture_output=True, text=True).stdout.splitlines() if len(l.split()) == 3}
uc = Uc(UC_ARCH_M68K, UC_MODE_BIG_ENDIAN)
uc.ctl_set_cpu_model(UC_CPU_M68K_CFV4E)
uc.mem_map(0, 0x200000); uc.mem_write(0x10000, open((sys.argv[1] if len(sys.argv) > 1 else "t.elf").replace(".elf", ".bin"), "rb").read())
uc.mem_write(sym["fw_pitch_tab"], b"".join(struct.pack(">I", round(2 ** ((i - 10752) / 2048) * (1 << 29))) for i in range(14849)))
uc.mem_write(sym["fw_render_machine"], bytes([90] * 8))
for t in range(8): uc.mem_write(sym["fw_voice_note"] + 4 * t, struct.pack(">i", 48 << 16))
n = [0]; seen = {}
def hk(u, a, s, d):
    n[0] += 1
    w = u.mem_read(a, 2); seen[a] = w
# every function of the mod's code (not just lyra_render, nor only its first few KB)
nm = [l.split() for l in subprocess.run(["m68k-linux-gnu-nm", "-S", "-n", ELF], capture_output=True, text=True).stdout.splitlines()]
fns = [(int(x[0], 16), int(x[1], 16)) for x in nm if len(x) == 4 and x[2] in "tT" and x[3] != "_start"]
for a_, n_ in fns:
    uc.hook_add(UC_HOOK_CODE, hk, begin=a_, end=a_ + n_ - 1)
def run(settings, label, asleep=False):
    uc.mem_write(sym["fw_amp_env"], b"".join(struct.pack(">iii", 0, -390 if asleep else -0x40000000, 0) for t in range(8)))
    for t in range(8):
        base = sym["fw_voice_params"] + 2 * 53 * t
        for k, v in settings.items():
            uc.mem_write(base + 2 * (k if k < 8 else 29 + k - 8), struct.pack(">h", v << 8))
        uc.mem_write(base, struct.pack(">h", 0x4000))
    for blk in range(4):
        n[0] = 0
        uc.reg_write(UC_M68K_REG_A7, 0xf0000)
        try: uc.emu_start(sym["_start"], 0xdead, count=600000)
        except UcError: pass
    print("%-40s %5d instructions a track a block (%3.0f a sample)" % (label, n[0] // 8, n[0] / 8 / 32))
D = {1: 24, 2: 0, 3: 0, 4: 40, 5: 0, 6: 0, 7: 100, 8: 40, 9: 54, 10: 0, 11: 24, 12: 90, 13: 40, 14: 20}
run(D, "defaults (SUM, VIB 24, delay on, DMOD)")
run({**D, 1: 60, 2: 70, 3: 102, 6: 90, 10: 112, 11: 50, 13: 100, 14: 60}, "busy 0.2-style (DLY FM, FBK, XOR...)")
run({**D, 1: 127, 2: 127, 3: 30, 6: 127, 10: 115, 11: 60, 13: 127, 14: 127}, "everything nonlinear at 127 (CHS)")
run({**D, 2: 100, 3: 124, 6: 110, 13: 90}, "TRK, all eight linked in pairs, FBK 110")
run({**D, 1: 0, 2: 0, 11: 0, 6: 0, 13: 0, 14: 0}, "plain (no SHRP/FM/FBK/VIB, delay silent)")
run(D, "asleep", asleep=True)
