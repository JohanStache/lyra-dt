"""LYRA (lyra-dn) on the Digitone mk1 in digiemu: picked, its pages, its sound, one voice a track."""
import os, struct, unittest
import sys, types
sys.modules["sdk_alpha_viewer"] = types.SimpleNamespace(watcher=lambda: None)
from sdk_alpha_harness import SdkTestCase, sdk_example, sdk_tests
LYRA = sdk_example("LYRA", os.environ.get("LYRA_DN", os.path.join(os.path.dirname(os.path.abspath(__file__)), "..")))
OUT = os.environ.get('LYRA_OUT', 'lyra_dn_emu')

def save_frame(panel, name):
    os.makedirs(OUT, exist_ok=True)
    from emu.panel import write_png
    write_png(panel.screen(), os.path.join(OUT, name + '.png'), scale=4)

@sdk_tests
class Lyra:
    audio = True

    def plain(self, used):
        for label, v in (('MOD', 0), ('DETN', 0), ('VIB', 0), ('DLY', 0), ('DMOD', 0), ('FBK', 0)):
            used.knob(label).set(v)

    def test_pages(self):
        used = self.track.use(LYRA)
        self.assertEqual(self.track.machine_id, 90)
        self.track.show(0); self.panel.settle(quiet_ms=600, limit_ms=3000); save_frame(self.panel, 'page1')
        self.track.show(1); self.panel.settle(quiet_ms=600, limit_ms=3000); save_frame(self.panel, 'page2')
        used.knob('SRC').set(5)
        self.track.show(0); self.panel.settle(quiet_ms=600, limit_ms=3000); save_frame(self.panel, 'page1_src_trk')

    def test_plain_pitch(self):
        used = self.track.use(LYRA)
        self.plain(used)
        s = self.track.play(0, hold_ms=800)
        print('plain: peak %d frequency %s' % (s.peak, s.frequency))
        self.assertGreater(s.peak, 1000)
        self.assertIsNotNone(s.frequency)

    def test_default_and_wild_sound(self):
        used = self.track.use(LYRA)
        s = self.track.play(0, hold_ms=800)
        print('defaults: peak %d' % s.peak)
        self.assertGreater(s.peak, 300)
        for label in ('SHRP', 'MOD', 'FBK', 'DLY', 'DMOD'):
            used.knob(label).set(127)
        s = self.track.play(0, hold_ms=1500)
        print('nonlinear: peak %d' % s.peak)
        self.assertGreater(s.peak, 1000)

    def test_one_voice_per_track(self):
        self.track.use(LYRA)
        self.panel.keyboard(); self.wait(50)
        self.panel.press_key(0); self.wait(100)
        self.panel.press_key(4); self.wait(100)
        self.panel.press_key(7); self.wait(300)
        n = self.panel.sounding_voices(0)
        print('voices sounding with three keys held: %d' % n)
        for k in (0, 4, 7):
            self.panel.release_key(k)
        self.assertEqual(n, 1)

    def test_four_tracks_trk(self):
        for t in self.tracks:
            u = t.use(LYRA)
        u.knob('SRC').set(5)        # track 4: TRK from track 3
        self.panel.keyboard()
        start = self.panel.audio_position()
        for t in range(4):          # hold a key on each track in turn
            self.panel.switch_track(t); self.wait(100)
            self.panel.press_key(2 * t); self.wait(200)
        self.wait(1000)
        from sdk_alpha_harness import Sound
        s = Sound(self.panel.audio_since(start, skip_ms=100))
        voices = [self.panel.sounding_voices(t) for t in range(4)]
        print('four tracks: peak %d, voices %s' % (s.peak, voices))
        for t in range(4):
            self.panel.release_key(2 * t)
        self.assertGreater(s.peak, 300)

if __name__ == '__main__':
    unittest.main(verbosity=2)
