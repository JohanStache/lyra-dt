#!/usr/bin/env python3
"""LYRA inside the real firmware (digiemu, headless).

    python tests/digiemu_lyra.py --digiemu DIR --fw <firmware folder> --stock Digitakt_OS1.54.syx
        --mods core-4.3.elemod machine-pages-1.10.elemod lyra-dt.elemod [others] --png DIR

Track 1 is switched to machine 90 (LYRA) through the machine menu. Checks:
  0. FUNC+SRC, DOWN to the end of the list, YES: track 1 is LYRA;
  1. the switch sets LYRA's defaults on both pages (the second page's in the sound's slots 46-52);
     SRC shows page 1, SRC again page 2 (PNGs);
  2. PLAY: the render calls lyra_render, track 1 renders as machine 90, the other tracks' blocks are
     left as playback made them; with SHRP, MOD, DETN, DRFT and VIB at 0 the block is a triangle at
     the trig's note and TUNE (measured from the samples);
  3. page 2 reaches the render: VIB 127 with LOGC AND and both rates at 0.05 Hz holds the HYPER LFO
     low, which takes the pitch to 0.5068 of the note (an octave down, near enough), DLY 0;
  4. LYRA's defaults (the delay on, DMOD) play with no fault and a sound, and so does every knob's
     nonlinear half at the top (CHN FM, the CHS LFO, DRIVE, the folds, the self-modulated delay);
  5. after STOP, once the AMP envelope has died away, LYRA sleeps: its blocks are zero;
  6. TRK: with a sample track beside it, track 1 on TRK has no FM (its pitch is the note's); with
     track 2 switched to LYRA from the menu, both render and are awake, and track 2's voices FM
     track 1.
"""
import argparse, math, os, struct, sys, time, types, zlib

ap = argparse.ArgumentParser()
ap.add_argument("--digiemu", required=True)
ap.add_argument("--fw", required=True)
ap.add_argument("--stock", required=True)
ap.add_argument("--elekloader", default=None)
ap.add_argument("--mods", nargs="+", required=True)
ap.add_argument("--png", required=True)
a = ap.parse_args()

if a.elekloader:
    sys.path.insert(0, a.elekloader)
from elekloader import syx as _syx, devices as _dev, elemod as _em, link as _link
_st = _syx.Syx.load(a.stock)
_d, _r = _dev.identify(_st.sha256)
MAP = _link.link([_em.load_any(p) for p in a.mods], _st.section(_d.main_section)).map

FW = os.path.join(a.digiemu, "portable", "firmware", a.fw)
sys.path.insert(0, a.digiemu)
syxname = [f for f in os.listdir(FW) if f.endswith(".syx")][0]
os.environ.update({
    "DT2_SYX": os.path.join(FW, syxname), "DT2_SECTIONS": FW + "/sections", "DT2_SNAPSHOTS": FW + "/snapshots",
    "DT2_PLUSDRIVE": FW + "/plusdrive.img", "DT2_MAIN_IMG": FW + "/sections/section_3_MAIN_OS.bin",
    "DT2_DEVICES": FW + "/devices" if os.path.isdir(FW + "/devices") else os.path.join(a.digiemu, "devices"),
    "DIGIKIT_PIT3_PROBE": "1"})
os.chdir(FW)
_tk = types.ModuleType("tkinter")
_tk.Frame = type("Frame", (), {}); _tk.Tk = type("Tk", (), {})
sys.modules["tkinter"] = _tk
for _n in ("ttk", "messagebox", "filedialog", "font"):
    sys.modules["tkinter." + _n] = types.ModuleType("tkinter." + _n)
from unicorn import UC_HOOK_CODE
from unicorn.m68k_const import UC_M68K_REG_A7
import emu.gui as G

SNAP = [os.path.join(dp, f) for dp, _, fs in os.walk(FW + "/snapshots") for f in fs if f == "gui.snap"][0]
os.makedirs(a.png, exist_ok=True)
K_FUNC, K_PLAY, K_STOP, K_YES, K_DOWN, K_SRC = 1, 10, 11, 12, 15, 20
K_T1, K_T2 = 24, 25                             # trig keys 1 and 2: on the SRC page they pick the track
LYRA = 90
SOUND = lambda kit, t: kit + 0x20 + 0xa2 * t
PARAM = lambda k: 0x36 + 2 * k


def png(path, fb):
    def ch(t_, d):
        return struct.pack(">I", len(d)) + t_ + d + struct.pack(">I", zlib.crc32(t_ + d) & 0xffffffff)
    big = []
    for y in range(64):
        line = b"".join(bytes([0 if fb[y * 128 + x] else 255] * 3) * 4 for x in range(128))
        big += [b"\0" + line] * 4
    open(path, "wb").write(b"\x89PNG\r\n\x1a\n" + ch(b"IHDR", struct.pack(">IIBBBBB", 512, 256, 8, 2, 0, 0, 0))
                           + ch(b"IDAT", zlib.compress(b"".join(big))) + ch(b"IEND", b""))


PLAN, t = [], 600
def at(dt, *act):
    global t
    t += dt
    PLAN.append((t, act))
def tap(code, hold=25, gap=220):
    at(1, "press", code); at(hold, "release", code); at(gap, "wait")

at(0, "read", "before menu")
at(1, "press", K_FUNC); at(40, "wait"); tap(K_SRC, 25, 300); at(1, "release", K_FUNC); at(300, "wait")
for _ in range(10):
    tap(K_DOWN, 20, 150)
at(60, "snap", "t0_menu_lyra")
tap(K_YES, 25, 500)
at(0, "read", "menu picked")
tap(K_SRC, 25, 400)
at(60, "snap", "t1_src_page1")
tap(K_SRC, 25, 400)
at(60, "snap", "t2_src_page2")
tap(K_SRC, 25, 400)
at(0, "cap_reset"); tap(K_PLAY); at(2000, "cap_save", "defaults"); tap(K_STOP, 25, 600)
at(0, "set", "clean")
tap(K_PLAY); at(1000, "wait"); tap(K_STOP, 25, 600)
at(0, "cap_reset"); tap(K_PLAY); at(3000, "cap_save", "clean"); tap(K_STOP, 25, 600)
at(0, "set", "octave")
tap(K_PLAY); at(1000, "wait"); tap(K_STOP, 25, 600)
at(0, "cap_reset"); tap(K_PLAY); at(3000, "cap_save", "octave"); tap(K_STOP, 25, 50)
at(0, "cap_reset"); at(6000, "cap_save", "stopped")
# TRK: track 1 on TRK with MOD 100, first with a sample track beside it, then with track 2 a LYRA too
at(0, "set", "trk")
tap(K_PLAY); at(1000, "wait"); tap(K_STOP, 25, 600)
at(0, "cap_reset"); tap(K_PLAY); at(3000, "cap_save", "trk_alone"); tap(K_STOP, 25, 600)
tap(K_T2, 25, 400)
at(1, "press", K_FUNC); at(40, "wait"); tap(K_SRC, 25, 300); at(1, "release", K_FUNC); at(300, "wait")
for _ in range(10):
    tap(K_DOWN, 20, 150)
tap(K_YES, 25, 500)
at(0, "read2", "track 2 picked")
tap(K_T1, 25, 400)
at(0, "set", "linked")
tap(K_PLAY); at(1000, "wait"); tap(K_STOP, 25, 600)
at(0, "cap_reset"); tap(K_PLAY); at(3000, "cap_save", "linked"); tap(K_STOP, 25, 600)
# last, as it leaves the four phases spread (the pitch checks above count zero crossings of one wave)
at(0, "set", "wild")
at(0, "cap_reset"); tap(K_PLAY); at(2000, "cap_save", "wild"); tap(K_STOP, 25, 600)
STEPS = t + 20

st = {"n": 0, "cap": [], "calls": 0}
reads, caps, fails = {}, {}, []
def opt(i, n): return (i * 128 + 64) // n


def kit(uc):
    return struct.unpack(">I", uc.mem_read(MAP["fw_kit"], 4))[0]


def put(uc, k, v, track=0):                     # value k 0-14 (8-14: slots 46-52) of a track's sound
    s = SOUND(kit(uc), track)
    uc.mem_write(s + PARAM(k if k < 8 else 29 + k - 8), struct.pack(">h", v << 8))


def set_sound(uc, what):                       # SHRP MOD DETN FBK VIB DLY DMOD at 0, LEV 127, INTV UNI
    if what in ("trk", "linked"):               # everything off but MOD 100 from TRK, on both tracks
        for tr in ((0, 1) if what == "linked" else (0,)):
            for k, v in ((1, 0), (2, 100), (3, 124), (4, 0), (6, 0), (5, 0), (11, 0), (13, 0), (14, 0), (7, 127)):
                put(uc, k, v, tr)
        return
    if what == "wild":                          # every nonlinear half at the top: CHN FM, CHS, DRIVE, folds
        for k in range(1, 15):
            put(uc, k, 127)
        put(uc, 3, opt(3, 5)); put(uc, 10, opt(5, 6)); put(uc, 7, 100)
        return
    for k, v in ((1, 0), (2, 0), (4, 0), (6, 0), (5, 0), (11, 0), (13, 0), (14, 0), (7, 127)):
        put(uc, k, v)
    if what == "octave":                        # page 2: VIB 127, AND, RT A = RT B = 0.05 Hz
        for k, v in ((11, 127), (10, opt(1, 6)), (8, 0), (9, 0)):
            put(uc, k, v)


_spin = G.spin
def spin(m, pc, *args, **kw):
    uc = m.uc
    if st["n"] == 0:
        def on_render(u, ad, s_, d):
            sp = u.reg_read(UC_M68K_REG_A7)
            ra, blocks = struct.unpack(">II", u.mem_read(sp, 8))
            st["calls"] += 1
            st["blocks"] = blocks
            st["before"] = u.mem_read(blocks, 32 * 4 * 8)
            st["machine0"] = u.mem_read(MAP["fw_render_machine"], 1)[0]
            st["machine1"] = u.mem_read(MAP["fw_render_machine"] + 1, 1)[0]
            st["start"] = struct.unpack(">I", u.mem_read(MAP["fw_voice_start"], 4))[0]
            st["note"] = struct.unpack(">i", u.mem_read(MAP["fw_voice_note"], 4))[0]
            st["tune"] = struct.unpack(">h", u.mem_read(MAP["fw_voice_params"], 2))[0]
            st["env"] = struct.unpack(">i", u.mem_read(MAP["fw_amp_env"] + 4, 4))[0]
            if not st.get("ra"):
                st["ra"] = ra
                def back(u2, ad2, s2, d2):
                    if not st.get("blocks"):
                        return
                    after = u2.mem_read(st["blocks"], 32 * 4 * 8)
                    blk = struct.unpack(">32i", after[:128])
                    others = after[256:] == st["before"][256:]          # tracks 3-8 (track 2 may be a LYRA)
                    if st["machine1"] != LYRA:
                        others = others and after[128:256] == st["before"][128:256]
                    awake = struct.unpack(">I", u2.mem_read(MAP["lyra_awake"], 4))[0] if "lyra_awake" in MAP else -1
                    st["cap"].append((st["machine0"], st["start"] & 1, st["note"], st["tune"], blk, others, st["env"], st["machine1"], awake))
                    st["blocks"] = 0
                u.hook_add(UC_HOOK_CODE, back, begin=ra, end=ra)
                u.ctl_flush_tb()
        uc.hook_add(UC_HOOK_CODE, on_render, begin=MAP["lyra_render"], end=MAP["lyra_render"])
        uc.ctl_flush_tb()
    for when, act in PLAN:
        if when != st["n"]:
            continue
        k = act[0]
        if k in ("press", "release"):
            E.inbox.append((k, act[1], 0))
        elif k == "snap":
            png(os.path.join(a.png, act[1] + ".png"), E.fb)
        elif k == "set":
            set_sound(uc, act[1])
        elif k in ("read", "read2"):
            s = SOUND(kit(uc), 0 if k == "read" else 1)
            reads[act[1]] = dict(machine=uc.mem_read(s + 0x7e, 1)[0],
                                 params=[struct.unpack(">h", uc.mem_read(s + PARAM(k if k < 8 else 29 + k - 8), 2))[0] >> 8
                                         for k in range(15)])
        elif k == "cap_reset":
            st["cap"] = []
        elif k == "cap_save":
            caps[act[1]] = list(st["cap"])
    st["n"] += 1
    r = _spin(m, pc, *args, **kw)
    if st["n"] >= STEPS:
        E.stop_flag.set()
    return r
G.spin = spin

E = G.Emulator(SNAP, syx=os.environ["DT2_SYX"], realtime=False, audio=True)
t0 = time.time()
E.run()
print("error:", E.error, "steps:", st["n"], "lyra_render calls:", st["calls"], "%.0fs" % (time.time() - t0))
for k_, v_ in reads.items():
    print("read %-14s %s" % (k_, v_))


def need(cond, what):
    print(("  ok   " if cond else "  FAIL ") + what)
    if not cond:
        fails.append(what)


def pitch(cap):
    run, best = [], []
    for c in cap:
        if c[1]:
            run = []
        run.append(c)
        if len(run) > len(best):
            best = list(run)
    x = [v / 2 ** 31 for c in best for v in c[4]]
    zc = [i for i in range(1, len(x)) if x[i - 1] <= 0 < x[i]]
    f = 48000 * (len(zc) - 1) / (zc[-1] - zc[0]) if len(zc) > 2 else 0
    want = 261.6256 * 2 ** ((best[0][2] / 65536 + (best[0][3] - 0x4000) / 256 - 60) / 12) if best else 0
    return f, want, (max(abs(v) for v in x) if x else 0), len(x)


need(not E.error, "the firmware runs on")
need(reads.get("before menu", {}).get("machine") != LYRA and reads.get("menu picked", {}).get("machine") == LYRA,
     "FUNC+SRC, DOWN to the end of the list, YES: track 1's sound is LYRA (machine 90) (%s -> %s)"
     % (reads.get("before menu", {}).get("machine"), reads.get("menu picked", {}).get("machine")))
mp_ = reads.get("menu picked", {}).get("params", [None] * 15)
need(mp_[1:7] == [24, 0, 0, 40, 0, 0], "the switch sets page 1's defaults: SHRP 24 MOD 0 SELF DETN 40 UNI FBK 0 (%s)" % mp_[1:7])
need(mp_[8:15] == [40, 54, 0, 24, 90, 40, 20], "and page 2's in slots 46-52: RT A 40, RT B 54, SUM, VIB 24, TIME 90, DLY 40, DMOD 20 (%s)" % mp_[8:15])
need(st["calls"] > 1000, "the render calls lyra_render every block (%d)" % st["calls"])
sc = caps.get("clean", [])
mine = [c for c in sc if c[0] == LYRA]
need(len(mine) > 100, "track 1 renders as machine 90 while playing (%d of %d blocks)" % (len(mine), len(sc)))
need(all(c[5] for c in sc + caps.get("octave", []) + caps.get("stopped", []) + caps.get("defaults", []) + caps.get("wild", [])), "the other tracks' blocks are left as playback made them")
f, want, pk, n = pitch(mine)
need(n > 256 and pk > 0.3, "the block is LYRA's (peak %.2f over %d samples)" % (pk, n))
need(want and abs(f / want - 1) < 0.003, "everything off: its pitch is the trig's note and TUNE: %.2f Hz (%.2f)" % (f, want))
f2, want2, pk2, n2 = pitch([c for c in caps.get("octave", []) if c[0] == LYRA])
need(want2 and abs(f2 / (0.50684 * want2) - 1) < 0.003, "page 2 reaches the render: VIB 127, AND, 0.05 Hz holds the pitch at %.2f Hz (%.2f)" % (f2, 0.50684 * want2))
dfl = [c for c in caps.get("defaults", []) if c[0] == LYRA]
pk3 = max((abs(v) for c in dfl for v in c[4]), default=0) / 2 ** 31
need(len(dfl) > 100 and pk3 > 0.05, "the defaults (delay on, DMOD) play: %d blocks, peak %.2f" % (len(dfl), pk3))
wl = [c for c in caps.get("wild", []) if c[0] == LYRA]
pk4 = max((abs(v) for c in wl for v in c[4]), default=0) / 2 ** 31
need(len(wl) > 100 and 0.05 < pk4 <= 1.0, "everything nonlinear at 127 (CHN, CHS, DRIVE, the folds, the self-modulated delay) plays on the ColdFire: %d blocks, peak %.2f" % (len(wl), pk4))
def rough(cap):                                 # mean |x[n] - x[n-1]| over mean |x|: a triangle's is small
    x = [v / 2 ** 31 for c in cap for v in c[4]]
    if len(x) < 2: return 0
    return sum(abs(x[i] - x[i - 1]) for i in range(1, len(x))) / max(1e-9, sum(abs(v) for v in x))
ta = [c for c in caps.get("trk_alone", []) if c[0] == LYRA]
fa, wa, _, _ = pitch(ta)
need(wa and abs(fa / wa - 1) < 0.003, "TRK with a sample track as partner: no FM, the pitch is the note's (%.2f Hz for %.2f)" % (fa, wa))
need(reads.get("track 2 picked", {}).get("machine") == LYRA, "track 2 switched to LYRA from the menu too")
lk = [c for c in caps.get("linked", []) if c[0] == LYRA]
both = [c for c in lk if c[7] == LYRA and c[8] != -1 and (c[8] & 3) == 3]
need(len(both) > 100, "linked: tracks 1 and 2 both render as LYRA and are both awake (%d of %d blocks)" % (len(both), len(lk)))
need(rough(lk) > 2 * rough(ta), "linked: track 2's voices FM track 1 (roughness %.3f against %.3f alone)" % (rough(lk), rough(ta)))
stopped = [c for c in caps.get("stopped", []) if c[0] == LYRA]
dead = [c for c in stopped if -65536 < c[6] < 65536 and not c[1]]
envs = [c[6] for c in stopped]
print("  stopped: %d blocks, AMP level first %s last %s min %s; nonzero blocks %d" % (len(stopped), envs[:1], envs[-1:], min(envs) if envs else None, sum(1 for c in stopped if any(c[4]))))
need(len(dead) > 50 and all(not any(c[4]) for c in dead),
     "after STOP, once the AMP envelope has died away (under -90 dB), LYRA sleeps: %d blocks, all zero" % len(dead))
print("\n%s" % ("ALL DIGIEMU CHECKS PASSED" if not fails else "FAIL\n  " + "\n  ".join(fails)))
sys.exit(1 if fails else 0)
