#!/usr/bin/env python3
"""Six LYRA patches rendered on the PC (the raw machine with its own delay network; no track filter,\nAMP envelope or Digitakt effects): lyra_previews.wav. "runaway, bending" stops its voices at 4.7 s."""
import subprocess, sys, numpy as np, wave
HOST = sys.argv[1] if len(sys.argv) > 1 else "/tmp/lyra_host"
def opt(i, n): return (i * 128 + 64) // n
P = [  # name, note, {param: value}; 1 SHRP 2 MOD 3 SRC 4 DETN 5 INTV 6 FBK 7 LEV | 8 RT A 9 RT B 10 LOGC 11 VIB 12 TIME 13 DLY 14 DMOD
 ("organ haze",          36, {1: 10, 4: 70, 5: opt(4, 11), 11: 14, 8: 30, 9: 38, 12: 100, 13: 50, 14: 15, 7: 110}),
 ("driven pair FM",      33, {1: 100, 2: 96, 3: opt(1, 5), 5: opt(6, 11), 8: 40, 9: 47, 10: opt(5, 6), 11: 10, 4: 40, 12: 80, 13: 70, 14: 40, 7: 100}),
 ("screaming network",   38, {1: 30, 6: 127, 4: 50, 5: opt(3, 11), 8: 30, 9: 41, 10: opt(5, 6), 11: 20, 12: 70, 13: 90, 14: 60, 7: 95}),
 ("runaway, bending",    31, {1: 70, 6: 96, 4: 60, 5: opt(6, 11), 8: 20, 9: 23, 11: 10, 12: 110, 13: 120, 14: 120, 7: 95, 98: 700}),
 ("linked pair (TRK)",   38, {1: 50, 2: 90, 3: 124, 4: 40, 5: opt(4, 11), 6: 100, 8: 25, 9: 31, 10: opt(5, 6), 11: 15, 12: 85, 13: 95, 14: 70, 7: 95, 97: 64 + 7}),
 ("chaos LFO, S&H FM",   45, {1: 40, 2: 110, 3: opt(2, 5), 4: 30, 5: opt(4, 11), 8: 70, 9: 25, 10: opt(4, 6), 11: 30, 12: 95, 13: 80, 14: 70, 7: 100}),
]
out, fade = [], np.linspace(0, 1, 24000)
for name, note, kw in P:
    args = [HOST, str(note), str(15 * 48000 // 32)] + ["%d=%d" % kv for kv in kw.items()]
    x = np.frombuffer(subprocess.run(args, capture_output=True, check=True).stdout, dtype=np.int32) / 2**31
    if 97 in kw:                                  # a linked pair: tracks 1 and 2 together (the same run)
        x = 0.6 * (x + np.frombuffer(subprocess.run(args + ["96=1"], capture_output=True, check=True).stdout, dtype=np.int32) / 2**31)
    x = x.copy(); x[:24000] *= fade; x[-24000:] *= fade[::-1]
    out += [x * 0.8, np.zeros(12000)]
y = (np.clip(np.concatenate(out), -1, 1) * 32767).astype("<i2")
w = wave.open(sys.argv[2] if len(sys.argv) > 2 else "lyra_previews.wav", "wb")
w.setnchannels(1); w.setsampwidth(2); w.setframerate(48000); w.writeframes(y.tobytes()); w.close()
