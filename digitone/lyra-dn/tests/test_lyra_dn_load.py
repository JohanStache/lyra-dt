"""The whole Digitone's CPU load (digiemu's cycle clock: busy vs idle-spin cycles), stock vs LYRA."""
import os, sys, types, unittest
sys.modules["sdk_alpha_viewer"] = types.SimpleNamespace(watcher=lambda: None)
import sdk_alpha_devices
_orig = sdk_alpha_devices.Digiemu.session
def session(self, audio=False):
    import os
    from emu.session import Session
    os.environ.update(self.paths.env())
    return Session(self.paths.gui, self.paths.syx, audio=audio, timing={})
sdk_alpha_devices.Digiemu.session = session
from sdk_alpha_harness import sdk_example, sdk_tests
NEW = sdk_example("LYRA", os.environ.get("LYRA_DN", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")))
class Alias:
    def __init__(self, id, name): self.id, self.name = id, name
LITE = Alias(91, "LYRA LITE")

@sdk_tests
class Load:
    audio = True
    def load(self, label, ms=300):
        clk = self.panel.s.clock
        c0, i0 = clk.cycles, clk.idle_cycles
        v0 = {v: s.self_ for v, s in clk.vectors.items()}
        self.wait(ms)
        c, i = clk.cycles - c0, clk.idle_cycles - i0
        vec = {v: (s.self_ - v0.get(v, 0)) for v, s in clk.vectors.items()}
        top = sorted(vec.items(), key=lambda x: -x[1])[:4]
        print('%-36s busy %5.1f %%  by vector: %s  main %.1f %%' % (label, 100.0 * (c - i) / c,
              ', '.join('%s %.1f%%' % (v, 100.0 * n / c) for v, n in top),
              100.0 * (c - i - sum(vec.values())) / c), flush=True)

    def test_load(self):
        self.load('stock, nothing playing')
        for m in (LITE, NEW):
            for t in self.tracks: t.use(m)
            self.assertEqual(self.track.machine_id, m.id)
            self.panel.switch_track(0); self.wait(200)
            self.panel.keyboard(); self.panel.press_key(0); self.wait(300)
            self.load('%s, 1 voice' % m.name)
            for t in range(1, 4):
                self.panel.switch_track(t); self.wait(80); self.panel.press_key(2 * t); self.wait(80)
            self.wait(300)
            self.load('%s, 4 voices' % m.name)
            print('  voices', [self.panel.sounding_voices(t) for t in range(4)], flush=True)
            for t in range(4): self.panel.release_key(2 * t)
            self.wait(300)

if __name__ == '__main__':
    unittest.main(verbosity=2)
