#!/usr/bin/env python3
"""LYRA's sound checked on the PC: tests/measure.py [host] (needs numpy; the host from host_render.c)
Parameters: 1 SHRP 2 MOD 3 SRC 4 DETN 5 INTV 6 FBK 7 LEV | 8 RT A 9 RT B 10 LOGC 11 VIB 12 TIME 13 DLY 14 DMOD
98=N stops the voices after N x 10 blocks (the delay runs on); 99=1 the AMP envelope has died away."""
import subprocess, sys, numpy as np
FS = 48000
HOST = sys.argv[1] if len(sys.argv) > 1 else "/tmp/lyra_host"
fails = []
def opt(i, n): return (i * 128 + 64) // n          # the knob value that picks option i of n
CLEAN = dict(k1=0, k2=0, k4=0, k6=0, k11=0, k13=0, k14=0)   # SHRP, MOD, DETN, FBK, VIB, DLY, DMOD at 0
def render(note=69, blocks=1500, **kw):
    args = [HOST, str(note), str(blocks)] + ["%s=%d" % (k[1:], v) for k, v in kw.items()]
    raw = subprocess.run(args, capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype=np.int32).astype(np.float64) / 2**31
def need(c, what):
    print(("  ok   " if c else "  FAIL ") + what)
    if not c: fails.append(what)
def spec(x):
    x = x[len(x) // 4:]; w = np.blackman(len(x))
    return np.fft.rfftfreq(len(x), 1 / FS), np.abs(np.fft.rfft(x * w))
def level_at(x, f0):
    f, m = spec(x); return 20 * np.log10(m[np.abs(f - f0) < 3].max() / m.max() + 1e-12)
def peaks(x, n):
    f, m = spec(x); m = m.copy(); out = []
    for _ in range(n):
        j = np.argmax(m); out.append(f[j]); m[max(0, j - 20):j + 20] = 0
    return sorted(out)
def centroid(x):
    f, m = spec(x); return (f * m).sum() / m.sum()
def db(x): return 20 * np.log10(np.sqrt((x ** 2).mean()) + 1e-12)

print("the voices")
x = render(**CLEAN)
p = peaks(x, 1)[0]
need(abs(p - 440) < 1, "A4 (note 69), everything off: the oscillators sit on 440 Hz (%.2f)" % p)
need(-21 < level_at(x, 1320) < -17, "SHRP 0 is a triangle: 3rd harmonic %.1f dB (a triangle's is -19)" % level_at(x, 1320))
x = render(**dict(CLEAN, k1=64))
need(level_at(x, 1320) > -11, "SHRP 64 is near a square: 3rd harmonic %.1f dB (a square's is -9.5)" % level_at(x, 1320))
need(level_at(x, 880) < -40, "  and still odd harmonics only: 2nd %.1f dB" % level_at(x, 880))
for name, i, f2 in (("+5TH", 4, 659.26), ("+OCT", 6, 880.0), ("-OCT", 9, 220.0), ("+m3", 1, 523.25)):
    x = render(**dict(CLEAN, k5=opt(i, 11)))
    need(level_at(x, f2) > -3 and level_at(x, 440) > -3, "INTV %s: pair 3-4 on %.1f Hz beside 440" % (name, f2))
x0, x1 = render(blocks=6000, **CLEAN), render(blocks=6000, **dict(CLEAN, k4=127))
def beat(x):
    env = np.abs(x).reshape(-1, 480).max(1)[50:]; return env.std() / env.mean()
need(beat(x1) > 3 * beat(x0) + 0.02, "DETN beats: level swing %.3f against %.3f at 0" % (beat(x1), beat(x0)))
need(np.allclose(render(blocks=3000, **dict(CLEAN, k4=127)), render(blocks=3000, **dict(CLEAN, k4=127))), "DETN's wander is repeatable from a cold start")
def pitch_track(x):
    seg = 4800; out = []
    for i in range(0, len(x) - seg, seg):
        s = x[i:i + seg] * np.hanning(seg); m = np.abs(np.fft.rfft(s, 8 * seg)); out.append(np.argmax(m) * FS / (8 * seg))
    return np.array(out)
pt = pitch_track(render(blocks=12000, **dict(CLEAN, k11=127, k8=20, k9=20)))
need(pt.max() > 800 and pt.min() < 260, "VIB 127, a slow HYPER LFO: up to an octave and down to one (%.0f-%.0f Hz)" % (pt.min(), pt.max()))
x = render(**dict(CLEAN, k11=60, k8=110, k9=110))
f, m = spec(x); side = 20 * np.log10(m[np.abs(f - 440) > 20].max() / m.max())
need(side > -25, "an audio-rate HYPER LFO growls: sidebands %.1f dB" % side)
c0 = centroid(render(**CLEAN))
for nm, i in (("SELF", 0), ("PAIR", 1), ("HLFO", 2), ("CHN", 3), ("DLY", 4)):
    kw = dict(CLEAN, k2=100, k3=opt(i, 5), k5=opt(4, 11))
    if i == 2: kw.update(k8=127, k9=127)
    if i == 4: kw.update(k13=40)
    c = centroid(render(**kw))
    need(c > (1.2 if i == 2 else 1.5) * c0, "MOD 100 from %s brightens it: centroid %.0f Hz against %.0f" % (nm, c, c0))
for logic in range(6):
    x = render(**dict(CLEAN, k10=opt(logic, 6), k11=40, k8=60, k9=75))
    need(np.isfinite(x).all() and np.abs(x).max() > 0.1, "LOGC %s plays" % ["SUM", "AND", "OR", "XOR", "S&H", "CHS"][logic])

print("the delay network")
dry = render(blocks=3000, **dict(CLEAN, k11=90, k8=10, k9=13))         # a slow, deep vibrato: no two moments alike
for tv in (30, 90, 127):
    want = 24 * 2 ** (tv * 117 / 2048) / 12000
    wet = render(blocks=3000, **dict(CLEAN, k11=90, k8=10, k9=13, k12=tv, k13=63))
    res = wet - dry * (128 - 88 * 26 / 90) / 128                     # what the echo added
    a, b = res[48000:96000], dry[48000 - 20000:96000]
    cc = np.correlate(b, a, "valid")[::-1]
    lag = np.argmax(cc) / FS
    need(abs(lag - want) < max(0.03 * want, 0.00025), "TIME %d: the echo comes %.1f ms late (%.1f ms expected)" % (tv, lag * 1000, want * 1000))
need(np.array_equal(render(**dict(CLEAN, k12=90)), render(**CLEAN)), "DLY 0: no echo at all (the same samples as without it)")
def tail(dly, **more):
    x = render(blocks=4500, **dict(CLEAN, k1=40, k13=dly, k12=60, k98=150, **more))   # voices stop at 1 s
    return db(x[int(1.0 * FS) - 4800:int(1.0 * FS)]), db(x[int(2.5 * FS):int(3.0 * FS)])
for dly, want in ((40, "dies"), (90, "dies"), (127, "runs")):
    before, late = tail(dly)
    if want == "dies":
        need(late < before - 40, "DLY %d: after the voices stop, the echoes die away (%.0f dB, 1.5 s later %.0f dB)" % (dly, before, late))
    else:
        need(late > before - 20, "DLY %d: the loop runs on by itself (%.0f dB, 1.5 s later %.0f dB)" % (dly, before, late))
def flat(x):
    f, m = spec(x); m = m[(f > 50) & (f < 8000)] + 1e-12; return np.exp(np.log(m).mean()) / m.mean()
f0_, f1_ = flat(render(**dict(CLEAN, k1=40, k13=90))), flat(render(**dict(CLEAN, k1=40, k13=90, k6=110)))
need(f1_ > 5 * f0_, "FBK 110 into DLY 90: the network turns to chaos (spectral flatness %.3f against %.4f)" % (f1_, f0_))
x = render(blocks=3000, **dict(CLEAN, k13=60, k14=0)); y = render(blocks=3000, **dict(CLEAN, k13=60, k14=127, k8=60, k9=60))
need(np.abs(x - y).max() > 0.05, "DMOD moves the echoes (they differ from DMOD 0)")
c = centroid(render(**dict(CLEAN, k6=127, k13=50)))
need(c > 1.5 * c0, "FBK 127: centroid %.0f Hz against %.0f" % (c, c0))

print("0.3: the nonlinear halves")
def pitch_track2(x, seg=2400):
    out = []
    for i in range(0, len(x) - seg, seg):
        s_ = x[i:i + seg] * np.hanning(seg); m_ = np.abs(np.fft.rfft(s_, 8 * seg)); f_ = np.fft.rfftfreq(8 * seg, 1 / FS); m_[f_ < 40] = 0
        out.append(f_[np.argmax(m_)])
    return np.array(out)
slow = dict(CLEAN, k3=opt(2, 5), k8=20, k9=20)              # FM from a slow HYPER LFO (SUM), VIB 0
pa = pitch_track2(render(blocks=9000, **dict(slow, k2=48)))
pb = pitch_track2(render(blocks=9000, **dict(slow, k2=127)))
need(np.ptp(pa) < 5 and np.ptp(pb) > 200, "MOD past 48 is true FM: a slow source bends the pitch (MOD 48: %.0f Hz of swing, MOD 127: %.0f Hz)" % (np.ptp(pa), np.ptp(pb)))
need(pb.min() > 30, "  and never stalls it at 0 (lowest %.0f Hz)" % pb.min())
def h2(x):
    f, m = spec(x); return 20 * np.log10(m[np.abs(f - 2 * 659.26) < 3].max() / m.max() + 1e-12)
dr0, dr1 = render(**dict(CLEAN, k1=64, k4=50, k5=opt(4, 11))), render(**dict(CLEAN, k1=127, k4=50, k5=opt(4, 11)))
need(level_at(dr0, 880) < -60 and level_at(dr1, 880) > -35, "SHRP past 64 is DRIVE, off-centre: the 2nd harmonic %.0f dB at 64, %.0f dB at 127" % (level_at(dr0, 880), level_at(dr1, 880)))
need(abs(dr1[FS // 2:].mean()) < 0.01, "  with its DC blocked (mean %.4f)" % dr1[FS // 2:].mean())
up0 = np.median(pitch_track2(render(blocks=6000, **dict(CLEAN, k13=60, k6=64))))
up1 = np.median(pitch_track2(render(blocks=6000, **dict(CLEAN, k13=60, k6=100))))
need(up1 > up0 * 1.02, "FBK past 64: the echoes' level pushes the pitch up (median %.0f Hz at 64, %.0f at 100)" % (up0, up1))
held = dict(CLEAN, k10=opt(1, 6), k8=0, k9=0, k13=90, k12=60)     # the HYPER LFO held still (AND, 0.05 Hz)
a_, b_ = render(blocks=3000, **dict(held, k14=64)), render(blocks=3000, **dict(held, k14=127))
need(np.abs(a_ - b_).max() > 0.05, "DMOD past 64: the delay's own output moves its time (differs from 64 with the HYPER LFO held)")
x = render(blocks=4500, **dict(CLEAN, k1=40, k13=127, k6=96, k12=110, k14=120, k98=150))
need(db(x[int(2.5 * FS):]) > -40 and abs(x[int(1.5 * FS):].mean()) < 0.01, "the runaway loop keeps going after the voices stop and doesn't latch on DC (%.0f dB, mean %.4f)" % (db(x[int(2.5 * FS):]), x[int(1.5 * FS):].mean()))
vibsh = pitch_track2(render(blocks=6000, **dict(CLEAN, k10=opt(4, 6), k8=60, k9=0, k11=60)), 1200)
need(len(np.unique(np.round(12 * np.log2(vibsh / 440)))) >= 4, "LOGC S&H: the pitch jumps between held values (%d different semitones)" % len(np.unique(np.round(12 * np.log2(vibsh / 440)))))
x = render(blocks=9000, **dict(CLEAN, k10=opt(5, 6), k8=60, k9=0, k11=100))
pt_ = pitch_track2(x, 1200); ac = np.corrcoef(pt_[:-40], pt_[40:])[0, 1]
need(np.isfinite(x).all() and np.ptp(pt_) > 100, "LOGC CHS: the logistic map moves the pitch (%.0f Hz of swing)" % np.ptp(pt_))

print("0.4: TRK, the linked tracks, and the echoes' decay while asleep")
TRK, SELF_ = 124, 10                                   # SRC's knob values for TRK and SELF
def inharm(x, f0):                                     # energy off the harmonics of f0 (above 20 Hz), dB
    f, s_ = spec(x); p = s_ ** 2; k = np.abs(f / f0 - np.round(f / f0)) * f0
    return 10 * np.log10(p[(k > 6) & (f > 20)].sum() / p.sum() + 1e-15)
c_plain = centroid(render(note=45, **CLEAN))
ih0, ih = inharm(render(note=45, **CLEAN), 110), inharm(render(note=45, **dict(CLEAN, k2=100, k3=TRK)), 110)
need(ih < ih0 + 3, "TRK with no LYRA partner: no FM (energy off the harmonics %.0f dB, %.0f dB with MOD 0)" % (ih, ih0))
lk = dict(CLEAN, k2=100, k3=TRK)
x7, x5 = render(note=45, **dict(lk, k97=71)), render(note=45, **dict(lk, k97=69))
need(centroid(x7) > 1.5 * c_plain, "TRK, both tracks: track 1 is FM'd by track 2's voices (centroid %.0f Hz against %.0f)" % (centroid(x7), c_plain))
need(np.abs(x7 - x5).max() > 0.05, "  and what it sounds like follows track 2's note (+7 and +5 differ)")
y = render(note=45, **dict(lk, k97=71, k96=1))
need(centroid(y) > 1.5 * c_plain, "  and the other way round: track 2 is FM'd by track 1 (centroid %.0f Hz)" % centroid(y))
one = render(note=45, **dict(lk, k97=71, k95=SELF_))
need(np.abs(one - x7).max() > 0.01 and centroid(one) > 1.5 * c_plain, "one-way: track 2 on SELF still drives track 1 on TRK")
fl_0 = flat(render(note=45, blocks=3000, **dict(CLEAN, k1=40, k3=TRK, k13=90, k97=71)))
fl_1 = flat(render(note=45, blocks=3000, **dict(CLEAN, k1=40, k3=TRK, k13=90, k6=110, k97=71)))
need(fl_1 > 5 * fl_0, "TRK with FBK 110: each track's FBK takes the other's delay, a cross-coupled network (flatness %.3f against %.4f)" % (fl_1, fl_0))
def wake(dly, **more):     # voices for 1 s, asleep for 1 s, then a trig with the voices muted: only the old echoes
    x = render(blocks=4500, **dict(CLEAN, k1=40, k13=dly, k12=60, k94=150, k93=150, k98=300, **more))
    return db(x[int(1.0 * FS) - 4800:int(1.0 * FS)]), db(x[int(2.0 * FS) + 240:int(2.0 * FS) + 2400])
before, after = wake(60)
need(after < before - 60, "DLY 60: after a 1 s sleep the old echoes are gone, as they'd have decayed (%.0f dB before, %.0f dB on waking)" % (before, after))
before, after = wake(127)
need(after > before - 20, "DLY 127: a loop that runs on by itself comes back running (%.0f dB before, %.0f dB on waking)" % (before, after))

print("level and safety")
x = render(**{"k99": 1}); need(np.abs(x).max() == 0, "a silent AMP envelope and no trig: LYRA sleeps (zeros)")
x = render(k7=127, **dict(CLEAN)); need(0.95 < np.abs(x).max() <= 1.0, "LEV 127 peaks at full scale: %.3f" % np.abs(x).max())
rng = np.random.default_rng(1); worst = 0
for _ in range(100):
    kw = {"k%d" % k: int(rng.integers(0, 128)) for k in range(1, 15)}; kw["k7"] = 127
    x = render(note=int(rng.integers(0, 88)), blocks=600, **kw); worst = max(worst, np.abs(x).max())
    if not np.isfinite(x).all(): worst = 9
need(worst <= 1.0, "100 random settings at LEV 127: never past full scale (peak %.3f)" % worst)
rng = np.random.default_rng(12); worst = 0
for _ in range(40):
    kw = {"k%d" % k: int(rng.integers(0, 128)) for k in range(1, 15)}; kw["k7"] = 100
    x = render(note=int(rng.integers(20, 70)), blocks=3000, **kw)[FS // 2:]; worst = max(worst, abs(x.mean()))
need(worst < 0.03, "40 random settings: no DC offset to speak of (worst mean %.4f; 0.2 reached 0.14)" % worst)
print("FAILED: %d" % len(fails) if fails else "all passed")
sys.exit(1 if fails else 0)
