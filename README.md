# LYRA for Digitakt mk1 — v0.5

An organismic drone machine after SOMA Laboratory's LYRA-4/LYRA-8, in the spirit of the LYRA engine in
Motor:Cycles for the Model:Cycles. Four oscillators in two pairs, each a triangle that SHRP drives
into a square; they beat and wander, cross-modulate, get vibrato from a HYPER LFO (two LFOs combined
by logic, from slow drift to audio-rate growl), and — new in 0.2 — run through a **delay feedback
network inside the machine**, as on the LYRA: the sound goes into its own delay line, the delay's
output can be fed back into every oscillator (FBK) or be their FM source (SRC DLY), its repeats can run
away on their own (DLY), and the HYPER LFO moves its time (DMOD). That loop is where the LYRA's chaos
comes from.

**0.3 makes it properly nonlinear.** In 0.2 the extremes turned into steady hiss. Now the **top half**
of five knobs does something that bends, lurches and locks instead:

| knob | lower half (as in 0.2) | upper half (new) |
|---|---|---|
| SHRP | triangle → square (now reached at 64) | **DRIVE**: the voices into an off-centre saturation, up to 8×, before the delay (so the drive is inside the loop) |
| MOD | phase modulation | past 48, **true through-zero FM** as well: the sources (slowed to ~60 Hz) pull the pitches, lock them and let go |
| FBK | the delay's output into the oscillators | past 64 that signal is **folded** and darkened first, and its **level pushes the pitch up**: loud echoes sound higher, which changes what the delay hears next |
| DLY | echoes, then repeats to runaway | past 64 the line's input goes through a **wavefolder** |
| DMOD | the HYPER LFO moves the delay time | past 64 the **delay's own output moves its time** too: a self-modulated delay that bends its own echoes |

**0.4 links tracks, LYRA-8 style.** SRC has a sixth source, **TRK**: a LYRA's partner track (1↔2,
3↔4, 5↔6, 7↔8) becomes its FM source, and its **FBK takes the partner's delay** instead of its own.
Put two partner tracks on TRK and you have eight voices and two delay networks feeding each other.
And while a track sleeps (its AMP envelope closed), its echoes now **decay as they would have**
instead of freezing and coming back stale at the next trig.

And **LOGC** has two new modes (0.3): **S&H** (a random value each cycle of LFO A, held: stepped pitch jumps)
and **CHS**, a logistic map stepped by LFO A, with LFO B sweeping it from order (period-doubling) into
full chaos.

No SOMA code is used; this is a fresh, economical take on the ideas. Not affiliated with or
endorsed by SOMA Laboratory or Elektron. Machine id **90**.
GPL-2.0-or-later (it builds on elekloader's SDK, as TONE does).

> Unofficial and unsupported. Flashing modified firmware is at your own risk. The stock Digitakt OS
> always recovers the unit: hold FUNC at power-on, TRIG 4 (OS UPGRADE), send the stock .syx.

## Install

Download `lyra-dt-0.5-os1.54.elemod` from this repository's
[Releases](../../releases) and build a firmware with it in the
[elekloader Mod Shop](https://elekloader.github.io/shop/): add it as a file of your own, tick
**machine-pages** (LYRA draws its two pages with it) and anything else you want (digihealth is
recommended, for its CPU meter), build, and send the .syx to the Digitakt (hold FUNC at power-on,
TRIG 4: OS UPGRADE). Or link it yourself with elekloader's `patch` tool.

It needs a Digitakt mk1 on **OS 1.54**. The OS version shows as whatever the builder names it.

Firmware builds are not published here: they contain Elektron's OS.

**From 0.4:** the same sound, sample for sample (checked on 121 random settings, linked tracks
included), lighter on the CPU and a smaller file; sounds load as they were.

## With the Mod Shop's mods

`elemods/lyra-dt-0.5-os1.54.elemod` works in the shop's builder (https://elekloader.github.io/shop/)
as a file of your own: add it beside the shop's machine-pages (1.7 or 1.2), and digihealth if you
want it. Checked with elekloader's link check against every Digitakt mk1 OS 1.54 mod the shop lists
(its catalog of 2026-11-15), each with the core the builder picks for it:

| shop mod | with LYRA |
|---|---|
| digihealth 1.0 | ✓ (and in the shop build above, run in digiemu) |
| DigiSlicer 2.5, Digi Sophie 1.2.0, DT-FM 1.2.1, Digi Mono 0.24 (+ Digi 101, Digi Macro), Digi Poly 2.1, Digi Matrix 1.0c, Digi Utils 1.9a, DigiString 0.8.0 (and -fast, which needs digihealth) | ✓ |
| DigiFilter 1.3, FILTER-PLUS 1.0, Filter-FX 1.2, Flex-Plus 1.0 | ✓ |
| Tone-FX 4.1a, tonefx 3.2a3, Digi EQ 1.0c (the builder gives them core 4.0) | ✓ |
| OB-1 0.6 (needs machine-pages 1.2) | ✓ with machine-pages 1.2 |
| **DigiNeighbor 0.6** | ✗ it overlaps machine-pages itself (1.2 and 1.7), which LYRA needs for its pages. DT-FM and OB-1 can't share a build with it either |

Splash screens and DigiCandy screens patch nothing LYRA uses. Several mods at once: the shop
builder's own check has the last word on RAM.

**From 0.3 (to 0.4):** sounds load as they were: SRC's five sources keep their places on the knob (TRK takes
its top seven steps, 121-127). TIME tops out at 306 ms (was 319): the MD TRX build needed the RAM
for 0.4's code.

**From 0.2:** sounds still load, but anything set in the upper halves above now sounds different
(that's the point), SHRP's square is at 64 rather than 127, and LOGC has six positions instead of four,
so a saved LOGC lands on a neighbouring mode (0.2's AND reads as OR, OR as XOR, XOR as CHS): re-pick
it. TIME topped out at 319 ms in 0.3 (was 333): its extra state didn't fit in the MD TRX build otherwise.

**From 0.1:** the knob layout changed (below). A sound saved with 0.1's LYRA opens with page 2's
last three values read as TIME, DLY and DMOD, and its DRFT read as FBK: re-pick LYRA from the
menu to get 0.2's defaults. VTGT (vibrato on one pair) and BAL went to make room for the delay; DRFT
is now part of DETN.

## Picking it

**FUNC + SRC** opens the machine menu. LYRA is last in the list. Step down to it and press **YES**.
A track switched to LYRA starts from the defaults below (and a cleared delay line). TUNE and LEV keep
what they had. **SRC** flips between the two pages.

## Page 1: the voices (LYRA 1/2)

| knob | | default | |
|---|---|---|---|
| A | TUNE | — | The stock TUNE |
| B | SHRP | 24 | 0-64: triangle driven into a square. **64-127: DRIVE**, off-centre (even harmonics), into the delay too |
| C | MOD | 0 | FM depth. Phase modulation; **past 48 true FM joins in** and pulls the pitches around, up to ±98 % at 127 |
| D | SRC | SELF | The FM source. **SELF**: each oscillator modulates itself. **PAIR**: 1-2 by 3-4 and 3-4 by 1-2. **HLFO**: the HYPER LFO (use audio-rate RT A/B). **CHN**: a ring, 1←2←3←4←1. **DLY**: the delay's output. **TRK** (the last few steps): the partner track's voices, and FBK then takes the partner's delay (below) |
| E | DETN | 40 | Detune within and between the pairs (up to ~35 cents), and each oscillator's slow wander (up to ~25 cents) |
| F | INTV | UNI | Pair 3-4's interval: UNI, +m3, +M3, +4TH, +5TH, +m7, +OCT, +O+5, +2OC, -OCT, -5TH |
| G | FBK | 0 | **The delay's output fed back into every oscillator.** Past 64 it's folded first and its level raises the pitch: the network screams and lurches |
| H | LEV | — | The stock LEV |

## Page 2: the HYPER LFO and the delay (LYRA 2/2)

| knob | | default | |
|---|---|---|---|
| A | RT A | 40 (.70 Hz) | LFO A's rate, 0.05 Hz to 220 Hz (shown in Hz) |
| B | RT B | 54 (1.8 Hz) | LFO B's rate |
| C | LOGC | SUM | How A and B combine. **SUM**: the two triangles added. **AND** / **OR** / **XOR**: the two square waves through that logic, softened a little at the edges. **S&H**: a random value each cycle of A. **CHS**: a logistic map stepped by A, B sweeping it from order into chaos |
| D | VIB | 24 | Vibrato depth, up to an octave up and down, on all four oscillators |
| E | TIME | 90 (~71 ms) | The delay's time, 2-306 ms (shown in ms). It glides when you turn it, so the echoes bend like tape |
| F | DLY | 40 | The delay. 0: off. Up to 64 brings the echoes in; from 64 the repeats grow **and the line's input is folded**; past about **110 the loop runs on by itself** and keeps going after the voices stop |
| G | DMOD | 20 | The HYPER LFO on the delay's time, up to ±50 %. **Past 64 the delay's own output moves it too** |
| H | FBK | — | Page 1's G again, so the whole network is on one page |

All fifteen values take p-locks and LFOs. Page 2's values live in the sound's spare slots 46-52,
which machine-pages 1.10 saves.

### The delay line

12 kHz (every four samples averaged into one), 3760 samples a track, low-passed at about 2.5 kHz and
saturating (1.5× for quiet signals, bending smoothly into full scale), so it's dark and grainy like a
bucket-brigade delay, and a runaway loop compresses rather than exploding. The loop has a DC block
(~4 Hz): without one a saturating loop over unity gain can lock onto a DC level and go silent. It belongs to the track:
the Digitakt's own delay and reverb sends still work on top of it.

**Tails stop at the AMP envelope.** The Digitakt applies the track's AMP after the machine (the
elekloader SDK documents the order: render, then overdrive, filter, AMP), so nothing LYRA plays once
the AMP has closed can be heard, however long its loop rings. For a loop that keeps going after the
voices, give the AMP a long HOLD/DEC or use an INF trig; for tails past the note, the Digitakt's own
delay and reverb sends, which come after the AMP.

While the AMP is closed LYRA sleeps (it costs nothing), and its delay stands still. From 0.4, at the
next trig the old echoes come back **as decayed as they would have been** (the loop's gain once per
pass of the delay, for the time it slept), not frozen; a loop that runs on by itself (DLY past ~110)
comes back running.

## TRK: linked tracks (LYRA-8 mode)

Partners are 1↔2, 3↔4, 5↔6 and 7↔8. With SRC on **TRK**:

- **MOD** takes the partner's voices as its FM source (phase modulation, and past 48 the true FM).
- **FBK** takes the partner's **delay** output instead of the track's own: with both partners on TRK
  and FBK up, each delay feeds the other track's oscillators, whose sound goes into its own delay,
  which feeds the first track's oscillators: a cross-coupled network. FBK past 64 folds it as usual.
- It works one way too: only track 1 on TRK means track 2 plays normally and drives track 1.
- A partner that isn't a LYRA, or is asleep, gives nothing: TRK then sounds like MOD 0 / FBK 0.

The track on the right (2, 4, 6, 8) hears its partner's current block; the one on the left hears
the partner's previous block (0.67 ms later), as they're rendered in order. Tune the two tracks to
different notes (a fifth, say) and different HYPER LFO rates, and the pair lurches and locks against
each other.

## Playing it

- **AMP page**: slow ATK, HOLD at max, a long DEC, or set the trig's length to INF. One long trig; LYRA
  doesn't reset its phases, so retrigs don't click.
- **The chaos**: the upper halves. A good way in: SHRP 90 (drive), DLY 90, then FBK up past 64 slowly;
  DMOD past 64 makes the echoes bend themselves; LOGC CHS with RT B swept from low to high walks the
  vibrato from steady, through period-doubling, into chaos. Pull any of them back under 64 for the
  tamer 0.2 behaviour.
- **Pitch tearing**: SRC HLFO or PAIR, MOD 100-127, slow RT A/B: the true FM drags the voices around
  and they lock and unlock against each other.
- **The LYRA-8 trick**: SRC DLY with MOD 40-60: the echoes modulate the voices that make them.
- The Digitakt's delay send on top, for space; the DISTORT page (third build) for the LYRA's drive.

Starting points (`docs/lyra_previews.mp3`: these six, 15 s each, from the machine's own code; the track
filter, AMP envelope and Digitakt effects aren't in it):

| | SHRP | MOD / SRC | DETN | INTV | FBK | RT A / RT B | LOGC | VIB | TIME | DLY | DMOD |
|---|---|---|---|---|---|---|---|---|---|---|---|
| organ haze (low C) | 10 | 0 | 70 | +5TH | 0 | 30 / 38 | SUM | 14 | 100 | 50 | 15 |
| driven pair FM | 100 | 96 PAIR | 40 | +OCT | 0 | 40 / 47 | CHS | 10 | 80 | 70 | 40 |
| screaming network | 30 | 0 | 50 | +4TH | 127 | 30 / 41 | CHS | 20 | 70 | 90 | 60 |
| runaway, bending | 70 | 0 | 60 | +OCT | 96 | 20 / 23 | SUM | 10 | 110 | 120 | 120 |
| linked pair (tracks 1+2, a fifth apart) | 50 | 90 TRK | 40 | +5TH | 100 | 25 / 31 | CHS | 15 | 85 | 95 | 70 |
| chaos LFO, S&H FM | 40 | 110 HLFO | 30 | +5TH | 0 | 70 / 25 | S&H | 30 | 95 | 80 | 70 |

In "runaway, bending" the voices are cut at 4.7 s (a test switch on the PC, not something the unit
does): what you hear after that is the loop sustaining itself, bending its own echoes.

## CPU

Cycles a LYRA track takes a block, estimated with digiemu's ColdFire cycle model (the MCF54418
manual's timing tables: divides, multiplies, branch prediction, register stalls; `tests/cf_cycles.py`).
A block is 32 samples, about 167,000 cycles at 250 MHz:

| | 0.4 | 0.5 | |
|---|---|---|---|
| the defaults (delay on, DMOD) | 10,978 (6.6 %) | 10,351 (6.2 %) | -6 % |
| busy: FM from the delay, FBK, CHS, VIB, SHRP, DMOD | 14,257 (8.6 %) | 14,310 (8.6 %) | same |
| every knob's nonlinear half at 127 | 16,052 (9.6 %) | 15,983 (9.6 %) | same |
| TRK, all eight tracks linked in pairs, FBK 110 | 15,317 (9.2 %) | 14,325 (8.6 %) | -6.5 % |
| plain (SHRP, MOD, FBK, VIB off, delay silent) | 8,544 (5.1 %) | 8,546 (5.1 %) | same |
| asleep | 130 | 137 | |

What 0.5 changes: the fifteen values are worked out once and kept until one changes (0.4 did it
every block, divisions included); the HYPER LFO runs as a pass of its own before the voices, which
leaves their loop more registers; the knobs' value names are packed. The sound is the same.

**A correction to 0.3's and 0.4's figures:** their instruction counts stopped at the first 8 KB of
the render, so they were low. Counted properly, and in cycles, a playing LYRA track is about
**5-10 % of the CPU** (6 % at the defaults), not 4-7 %. Two to four LYRA tracks are comfortable; a
full nonlinear patch on all eight is not. The estimate assumes no cache misses: watch SYSTEM INFO
(digihealth) on the unit.

Faster variants were tried and left out: a dedicated loop for MOD and FBK at 0 took the defaults
to 5.6 %, but its 2.6 KB more code doesn't fit the MD TRX build without shortening the delay.

**The file:** `lyra-dt-0.5-os1.54.elemod` is 31,335 bytes (0.4: 33,600): 9,717 bytes of code
(0.4: 10,044), 81 relocations (104), a shorter description. The delay lines take 58.75 KB of RAM.

## Tested

On the PC (`tests/measure.py`, the machine's C code, 54 checks; also built with
`-fsanitize=undefined,address`: no overflow, no out-of-bounds). All of 0.2's (pitch, SHRP's square,
every INTV, DETN, VIB, the HYPER LFO, all FM sources, every LOGC mode, delay times 6.8 / 70.8 / 305.7
ms against 6.6 / 70.6 / 305.6, DLY 0 untouched, echoes dying at DLY 40 and 90, the loop running on at
127, FBK's chaos, the sleep, levels), and for 0.3: MOD past 48 is true FM (a slow source swings the
pitch 815 Hz at 127, 0 at 48) and never stalls it at 0; SHRP past 64 drives, off-centre (2nd harmonic
-90 dB at 64, -13 dB at 127) with the DC blocked; FBK past 64 pushes the pitch up with the echoes'
level (440 → 502 Hz); DMOD past 64 moves the delay with its own output; the runaway loop keeps going
after the voices stop without latching on DC; S&H jumps between held pitches; CHS moves the pitch;
100 random settings never pass full scale; 40 random settings never leave a DC offset to speak of
(worst 0.006; 0.2 reached 0.14). For 0.4: TRK with no LYRA partner adds no FM; with a partner
(rendered as two tracks) track 1 is FM'd by track 2 (centroid 284 → 6,856 Hz) and the other way
round, the result follows the partner's note, one-way linking works, and with FBK 110 the
cross-coupled delays turn to chaos (flatness 0.37 against 0.0007); after a 1 s sleep at DLY 60 the
old echoes are gone on waking (-7 dB → silence), while a runaway loop at DLY 127 comes back running
(-7 → -8 dB).

In digiemu, with the real OS 1.54 (`tests/digiemu_lyra.py`), on all four builds, the Mod Shop's (core 4.1, machine-pages 1.7, digihealth 1.0) included: FUNC+SRC to LYRA,
YES; both pages' defaults written; the render calls LYRA every block, track 1 renders as machine 90,
the other tracks' blocks are untouched; everything off, the pitch is the trig's note (261.57 Hz for
261.63); page 2 reaches the render (132.57 Hz for 132.60); the defaults play; **every nonlinear half at
127 at once (CHN FM, CHS, DRIVE, the folds, the self-modulated delay) plays on the ColdFire with no
fault**; after STOP it sleeps. And for TRK: with a sample track beside it, track 1 on TRK has no FM (261.37
Hz for 261.63); track 2 switched to LYRA from the menu, both render and are awake, and track 2's
voices FM track 1 (its waveform 3.4-3.8× rougher than alone).

0.5 against 0.4: the same samples, bit for bit, on 121 random settings (`tests/exact.py`).

**Not tested:** 0.5 on the unit (0.4 has been, and sounds right). Turning knobs in the emulator (its harness can't; the values were
written into the sound). Saving and reloading a sound with LYRA. Real CPU timing (digiemu's timing
run didn't finish here). A pattern opened on stock firmware plays LYRA tracks as plain sample tracks.

## Source

`lyra.c`, `mod.json`. Build with elekloader's SDK and a ColdFire cross compiler:

    ELEKLOADER_CROSS=m68k-linux-gnu- python -m elekloader.sdk.build . --stock Digitakt_OS1.54.syx --out build

`tests/host_render.c` + `tests/host_tab.c` render it on the PC
(`cc -I <elekloader>/elekloader/sdk/include tests/host_render.c tests/host_tab.c -lm -o lyra_host`),
then `python3 tests/measure.py` and `python3 tests/render_previews.py`. `tests/digiemu_lyra.py` is the
emulator check, `tests/cf_count.py` (with `tests/cf_harness.c`) the instruction count,
`tests/timing_*.keys` digiemu `fwcheck --timing` scripts for 4 and 8 LYRA tracks.

Inspiration: SOMA Laboratory LYRA-4/LYRA-8 (Vlad Kreimer); the LYRA engine in Nuno Zimas' Motor:Cycles
for the Model:Cycles. No code from either.
