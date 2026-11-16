"""The whole Digitone's load with LYRA FM in digiemu: lyrafm_load.py FIRMWARE.syx (built with elekloader.patch,
its .map.json beside it). ELEKLOADER and ELEKLOADER_DIGIEMU: the checkouts."""
import os, sys, json, types
ELEK = os.environ['ELEKLOADER']; DIGIEMU = os.environ['ELEKLOADER_DIGIEMU']   # checkouts
sys.path.insert(0, os.path.join(ELEK, 'tests')); sys.path.insert(0, DIGIEMU)
sys.modules["sdk_alpha_viewer"] = types.SimpleNamespace(watcher=lambda: None)
import sdk_alpha_devices as D
from emu.session import Session
syx = sys.argv[1]; work = 'lyrafm_work'; os.makedirs(work, exist_ok=True)
emu = D.Digiemu(syx, None, work); sym = json.load(open(syx + '.map.json'))
os.environ.update(emu.paths.env())
s = Session(emu.paths.gui, emu.paths.syx, audio=True, timing={})
panel = D.Digitone1(s, sym, [])
def load(label, ms=300):
    clk = s.clock; c0, i0 = clk.cycles, clk.idle_cycles
    s.run_ms(ms)
    c, i = clk.cycles - c0, clk.idle_cycles - i0
    print('%-36s busy %5.1f %%   voices %s' % (label, 100.0 * (c - i) / c, [panel.sounding_voices(t) for t in range(4)]), flush=True)
s.run_ms(500); load('stock, nothing playing')
print('select', panel.select(0, 92), flush=True)
panel.keyboard(); s.run_ms(300)
load('LYRA FM selected, silent')
panel.press_key(0); s.run_ms(300); load('LYRA FM, 1 voice')
for k in (2, 4, 5): panel.press_key(k); s.run_ms(60)
s.run_ms(300); load('LYRA FM, 4 voices')
for k in (7, 9, 11, 12): panel.press_key(k); s.run_ms(60)
s.run_ms(300); load('LYRA FM, 8 voices')
for k in (0, 2, 4, 5, 7, 9, 11, 12): panel.release_key(k)
s.run_ms(2500); load('released, 2.5 s later')
