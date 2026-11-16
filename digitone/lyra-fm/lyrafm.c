/* SPDX-License-Identifier: GPL-2.0-or-later */
/* LYRA FM: LYRA for the Digitone mk1 and Keys (OS 1.44), played by the Digitone's own FM engine.
 *
 * The four FM operators are LYRA's four oscillators, and the FM chip (the DSP) plays them: the main CPU
 * does not. What LYRA adds, on the main CPU:
 *   - every block (1500 a second, ev_render_params), each LYRA FM voice's FM settings, rewritten before the
 *     DSP takes them: the HYPER LFO (two LFOs by SUM, AND, OR, XOR, S&H or a chaotic map) on the
 *     modulators' levels, the detune and pair 2's ratios; a slow random drift on each operator's ratio
 *     (DRFT, as LYRA's DETN wander); FBK, the echoes' level and swing pushed back into the modulators'
 *     levels, the operators' feedback and pair 2's ratios;
 *   - on each voice's sound (the machine's render, which gets the FM voice's block from the DSP), LYRA's
 *     delay: a line at 6 kHz, darkened, saturating, folding past DLY 64, its time moved by the HYPER LFO
 *     (DMOD).
 * The echoes reach the oscillators at the block rate, not the audio rate: slow, swinging chaos rather than
 * the LYRA's ring-modulated scream. And the operators are FM's sines (HARM and FDBK give them edges).
 *
 * Its pages (a machine's knobs keep the SYN parameters' slots and ranges):
 *   SYN1 1/2  ALGO C A B HARM DTUN FDBK MIX   FM's own, which LYRA moves around
 *   SYN1 2/2  COFF AOFF BOFF B2OF              FM's ratio offsets, which DRFT and FBK move around
 *   SYN2 1/2  MOD  RTA  RTB  LOG  HLF  TIM  DLY  FBK   (this page shows three letters: RTA/RTB the
 *             HYPER LFO's rates, LOG its logic, HLF its depth, TIM the delay's time)
 *   SYN2 2/2  DMOD . . PHRT DRFT
 * MOD sets the modulators' levels (FM's A LEV and B LEV): the FM index. The operators' envelopes are held
 * open (a drone); the AMP page shapes the note. The Digitone's LFOs pointed at a SYN2 knob move FM's
 * operator settings behind it, not LYRA's control. Polyphonic: every voice its own LFOs, drift and delay.
 *
 * Interrupt level throughout; 32-bit arithmetic only. */
#include "digitone-mk1/machines.h"

#define LYRAFM_ID 92
#define VOICES 8
#define DLEN 1880                       /* the delay line: 313 ms at 6 kHz */

/* the parameter ids behind the knobs, for their slots (fw_params) */
enum { P_ALGO = 107, P_C, P_A, P_B, P_HARM, P_DTUN, P_FDBK, P_MIX, P_COFF, P_AOFF, P_B1OF, P_B2OF,
       P_AATK, P_ADEC, P_AEND, P_ALEV, P_BATK, P_BDEC, P_BEND, P_BLEV, P_ADEL, P_ATRG, P_ARST, P_BDEL,
       P_BTRG, P_BRST, P_PHRT };
/* the knobs (dnm_knob numbers) LYRA's controls are kept in */
enum { K_MOD = 8, K_RTA, K_RTB, K_LOGC, K_HLFO, K_TIME, K_DLY, K_FBK, K_DMOD = 24, K_DRFT = 28 };
enum { L_SUM, L_AND, L_OR, L_XOR, L_SH, L_CHS, LOGICS };

struct fm_voice {
    uint32_t la, lb, rnd;               /* the HYPER LFO's phases; the random state */
    int32_t hs, held, cx;               /* its output (Q15), S&H's value, the chaotic map's (Q15) */
    int32_t walk[4], goal[4], wait[4];  /* DRFT: each operator's slow random walk, Q15 */
    int32_t fb, env;                    /* FBK: the echoes slowed (signed) and their level, Q15 */
    int32_t lp, dt, tap0, tap1, w, ldc, dc;         /* the delay */
    int32_t key, tkey, target, wet, dry, regen, gin, dm2;  /* its knobs' values, worked out */
    int32_t quiet;                      /* blocks without sound in or out: asleep past a while */
    int32_t rkey;                       /* the rates' knobs, and their steps */
    uint32_t ia, ib;
};
static struct fm_voice fv[VOICES];
static int16_t line[VOICES][DLEN];      /* 29.4 KB */
static int32_t slot[160];               /* parameter id -> sound slot, filled at the first block */

static inline int32_t clip(int32_t v)
{
    return v > 32767 ? 32767 : v < -32767 ? -32767 : v;
}
static inline int32_t tri(uint32_t ph)
{
    int32_t x = (int32_t)(ph + 0x40000000u) >> 15, sg = x >> 31;
    return 32767 - ((x ^ sg) - sg);
}
static inline int32_t sat(int32_t x)
{
    int32_t x2;
    x = clip(x);
    x2 = (x * x) >> 15;
    return (x * (98304 - x2)) >> 16;
}
static inline int32_t fold(int32_t x, int32_t g)
{
    return tri(((uint32_t)(-x) * (uint32_t)g) << 7);
}
static uint32_t xorshift(uint32_t *s)
{
    uint32_t x = *s ? *s : 0x9e3779b9u;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return *s = x;
}
static const uint16_t exp2_tab[65] = {
    8192, 8281, 8371, 8463, 8555, 8648, 8742, 8837, 8933, 9031, 9129, 9228, 9329, 9431, 9533, 9637, 9742,
    9848, 9955, 10064, 10173, 10284, 10396, 10509, 10624, 10739, 10856, 10975, 11094, 11215, 11337, 11460,
    11585, 11711, 11839, 11968, 12098, 12230, 12363, 12498, 12634, 12771, 12910, 13051, 13193, 13337, 13482,
    13629, 13777, 13927, 14079, 14232, 14387, 14544, 14702, 14862, 15024, 15188, 15353, 15520, 15689, 15860,
    16033, 16208, 16384,
};
static inline int32_t exp2q(int32_t e, int32_t *oct)
{
    int32_t f = e & 2047, k = f >> 5, a = exp2_tab[k];
    *oct = e >> 11;
    return a + (((exp2_tab[k + 1] - a) * (f & 31)) >> 5);
}
/* the HYPER LFO's rate, 0.05 Hz * 2^(v / 10.5), as a phase step a block (32 samples at 48 kHz) */
static uint32_t rate_inc(int32_t v)
{
    int32_t oct, m = exp2q(v * 195, &oct);
    return ((uint32_t)((4474 * m) >> 13) << oct) * 32;
}
/* the delay time: 24 * 2^(v * 117 / 2048) samples at 12 kHz (2 ms - 306 ms), Q8 */
static int32_t time_q8(int32_t v)
{
    int32_t oct, m = exp2q(v * 117, &oct);
    return ((24 * 256 * m) >> 13) << oct;
}

static int32_t val(int32_t voice, int32_t id)       /* the voice's value of a parameter, whole steps */
{
    return FW_VOICE_PARAM(voice, slot[id]) >> 8;
}

/* One block of the HYPER LFO: hs, Q15 */
static void hlfo(struct fm_voice *s, int32_t logic, uint32_t ia, uint32_t ib)
{
    uint32_t la = s->la + ia, lb = s->lb + ib;
    int32_t h;
    switch (logic) {
    case L_SUM:
        h = (tri(la) + tri(lb)) >> 1;
        break;
    case L_SH:                          /* A clocks samples of B */
        if (la < ia)
            s->held = tri(lb);
        h = s->held;
        break;
    case L_CHS:                         /* A clocks a logistic map, its r swept by B: 3.6 .. 4 */
        if (la < ia) {
            int32_t r = 3 * 32768 + 19661 + ((tri(lb) + 32768) * 13107 >> 16), x = s->cx;
            if (x <= 0 || x >= 32768)
                x = 12345;
            x = (r * ((x * (32768 - x)) >> 15)) >> 15;     /* r x (1 - x), all within 32 bits */
            s->cx = x > 32700 ? 32700 : x < 60 ? 60 : x;
        }
        h = s->cx * 2 - 32767;
        break;
    default: {                          /* the gates: A's square and B's, by AND, OR or XOR */
        int32_t tt = logic == L_AND ? 8 : logic == L_OR ? 14 : 6;
        h = (tt >> (((la >> 31) << 1) | (lb >> 31))) & 1 ? 32767 : -32767;
    }
    }
    s->la = la;
    s->lb = lb;
    s->hs += (h - s->hs) >> 3;          /* softened, ~5 ms */
}

static inline void put(int16_t *words, int32_t id, int32_t value)   /* a word, 8.8, clamped to 0-127 */
{
    words[slot[id]] = (int16_t)((value < 0 ? 0 : value > 127 * 256 ? 127 * 256 : value));
}

/* Each block, before the DSP takes the voices' settings: LYRA FM's voices get theirs from LYRA. */
void lyrafm_params(int16_t *words)
{
    int32_t v, k;
    if (!slot[P_ALGO]) {
        for (k = P_ALGO; k <= P_PHRT; k++)
            slot[k] = ((const int32_t *)(fw_params + 60 * k))[1];
        if (!slot[P_ALGO])
            return;
    }
    for (v = 0; v < VOICES; v++) {
        int16_t *w = words + CORE_PARAMS_VOICE * v;
        struct fm_voice *s = &fv[v];
        int32_t mod, depth, fbk, drft, logic, h, e, lev, d, ofs;
        if ((w[0] >> 8) != LYRAFM_ID)
            continue;
        mod = val(v, P_AATK);
        logic = val(v, P_ALEV) * LOGICS >> 7;
        depth = val(v, P_BATK);
        fbk = val(v, P_BLEV);
        drft = val(v, P_BDEL);
        k = val(v, P_ADEC) | val(v, P_AEND) << 8;
        if (k != s->rkey || !s->ia) {   /* the rates, worked out again only when one moves */
            s->rkey = k;
            s->ia = rate_inc(k & 255);
            s->ib = rate_inc(k >> 8);
        }
        if (s->quiet <= 1500)           /* asleep (the voice silent a second): its LFOs and drift wait */
            hlfo(s, logic, s->ia, s->ib);
        h = s->hs;
        e = s->env;
        /* the modulators' levels: MOD, the HYPER LFO (+-48 at depth 127) one way on A and the other on B,
         * and the echoes' level (FBK) on both */
        d = (h * depth) >> 13;                          /* 8.8 steps: up to +-127 * 256 / 2 */
        lev = mod * 256 + ((e * fbk) >> 9);
        put(w, P_ALEV, lev + d);
        put(w, P_BLEV, lev - (d >> 1));
        /* held open: the operators' envelopes at full, no delays */
        put(w, P_AATK, 0); put(w, P_ADEC, 127 * 256); put(w, P_AEND, 127 * 256);
        put(w, P_BATK, 0); put(w, P_BDEC, 127 * 256); put(w, P_BEND, 127 * 256);
        put(w, P_ADEL, 0); put(w, P_BDEL, 0);
        /* the detune: the HYPER LFO a little */
        put(w, P_DTUN, w[slot[P_DTUN]] + ((h * depth) >> 15) * 12);
        /* the operators' feedback: the echoes' level */
        put(w, P_FDBK, w[slot[P_FDBK]] + ((e * fbk) >> 10));
        /* the ratio offsets (8.8, 64 the middle): each drifts (DRFT); pair 2 swings with the echoes (FBK)
         * and the HYPER LFO */
        for (k = 0; k < 4; k++) {
            if (s->quiet <= 1500 && --s->wait[k] <= 0) {
                uint32_t r = xorshift(&s->rnd);
                s->goal[k] = (int32_t)(r & 0xffff) - 32768;
                s->wait[k] = 300 + (int32_t)((r >> 16) & 2047);     /* a new goal every 0.2-1.6 s */
            }
            s->walk[k] += (s->goal[k] - s->walk[k]) >> 9;
            ofs = (s->walk[k] * drft) >> 11;                        /* up to +-8 steps of the offset */
            if (k >= 2)
                ofs += ((s->fb * fbk) >> 10) + ((h * depth) >> 14);
            words[CORE_PARAMS_VOICE * v + slot[P_COFF + k]] =
                (int16_t)(w[slot[P_COFF + k]] + ofs < 0 ? 0 : w[slot[P_COFF + k]] + ofs > 32767 ? 32767
                          : w[slot[P_COFF + k]] + ofs);
        }
    }
}

/* Each block, the voice's sound from the DSP: LYRA's delay over it, four samples (one 12 kHz step) at a
 * time: the echo, interpolated across the four, mixed in; then the four, summed, into the line. */
static void lyrafm_render(int32_t voice, int32_t *block, const struct dnm_voice *v)
{
    struct fm_voice *s = &fv[voice];
    int16_t *ln = line[voice];
    int32_t i, g, any, key, dt, tap0, tap1, w, dry, wet;
    any = block[0] | block[5] | block[11] | block[16] | block[21] | block[27] | block[31];
    if (v->fresh && s->quiet > 1500) {  /* a note after a long sleep: an empty line */
        for (i = 0; i < DLEN; i++)
            ln[i] = 0;
        s->tap0 = s->tap1 = s->fb = s->env = 0;
    }
    if (!any && s->quiet > 1500) {      /* asleep: a second without sound in or out */
        for (i = 0; i < 32; i++)
            block[i] = 0;
        return;
    }
    key = dnm_knob(voice, K_DLY) | dnm_knob(voice, K_DMOD) << 16;
    if (key != s->key || !s->target) {  /* the knobs' values, worked out again only when one moves */
        int32_t dly = (key & 0xffff) >> 8, dmod = key >> 24;
        s->key = key;
        s->wet = dly < 64 ? dly * 90 / 64 : 90;
        s->dry = 128 - s->wet * 26 / 90;
        s->regen = dly < 32 ? 0 : ((dly - 32) * (dly - 32) * 32440) / 9025;
        s->gin = dly > 64 ? 256 + ((dly - 64) * (dly - 64) * 384) / 3969 : 256;
        s->dm2 = dmod * dmod;
    }
    i = dnm_knob(voice, K_TIME);
    if (i != s->tkey || !s->target) {
        s->tkey = i;
        s->target = time_q8(i >> 8) >> 1;  /* in the line's samples, 6 kHz */
    }
    dry = s->dry;
    wet = s->wet << 5;                  /* Q15 echo x wet / 128, to the block's scale (<< 12) */
    dt = s->dt; tap0 = s->tap0; tap1 = s->tap1; w = s->w;
    if (dt < (2 << 8))
        dt = s->target;
    for (g = 0; g < 32; g += 8) {
        int32_t *b = block + g, step = (tap1 - tap0) >> 3, d = tap0, in = 0, dd, ri, f, a, c, k;
        /* out: the voice (dry) and the echo between the last two reads; the block has 16x headroom over
         * an FM voice's peak, so no clip. In: the eight's mean */
        for (k = 0; k < 8; k++) {
            int32_t x = b[k];
            in += x >> 3;
            d += step;
            b[k] = (x >> 7) * dry + d * wet;
        }
        in >>= 12;                      /* Q15 */
        /* 6 kHz: write one, read one (the line darkens its input below 3 kHz anyway) */
        s->dc += (in * 256 - s->dc) >> 7;
        in -= s->dc >> 8;
        if (s->gin != 256)
            in = fold(in, s->gin);
        s->lp = in;
        dt += (s->target - dt) >> 6;
        dd = dt;
        if (s->dm2)
            dd += ((dt >> 6) * ((s->hs * s->dm2) >> 15)) >> 9;
        dd = dd < (2 << 8) ? 2 << 8 : dd > ((DLEN - 2) << 8) ? (DLEN - 2) << 8 : dd;
        ri = w - (dd >> 8);
        if (ri < 0)
            ri += DLEN;
        f = dd & 255;
        a = ln[ri];
        c = ln[ri > 0 ? ri - 1 : DLEN - 1];
        tap0 = tap1;
        tap1 = a + (((c - a) * f) >> 8);
        in = sat(s->lp + ((tap1 * s->regen) >> 15));
        s->ldc += (in * 256 - s->ldc) >> 8;
        ln[w] = (int16_t)clip(in - (s->ldc >> 8));
        if (++w == DLEN)
            w = 0;
    }
    /* FBK's signals, for the next block's settings: the echoes slowed (~30 Hz) and their level */
    s->fb += (tap1 - s->fb) >> 4;
    s->env += (((tap1 < 0 ? -tap1 : tap1) << 1) - s->env) >> 5;
    if (s->env > 32767)
        s->env = 32767;
    s->quiet = any || tap1 > 16 || tap1 < -16 ? 0 : s->quiet + 1;
    s->dt = dt; s->tap0 = tap0; s->tap1 = tap1; s->w = w;
}

/* ---- the pages ---- */
static void put_int(char *buf, int32_t v)
{
    char t[8];
    int32_t n = 0;
    if (v < 0) {
        *buf++ = '-';
        v = -v;
    }
    do {
        t[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v && n < 7);
    while (n)
        *buf++ = t[--n];
    *buf = 0;
}
static void logic_text(int32_t value, char *buf)
{
    static const char names[LOGICS][4] = { "SUM", "AND", "OR", "XOR", "S&H", "CHS" };
    const char *n = names[((value >> 8) * LOGICS) >> 7];
    while ((*buf++ = *n++))
        ;
}
static void rate_text(int32_t value, char *buf)    /* Hz: .05 .. 9.9, then whole */
{
    int32_t oct, f = exp2q((value >> 8) * 195, &oct), mhz = ((50 * f) >> 13) << oct;
    if (mhz < 10000) {
        int32_t whole = mhz / 1000, frac = (mhz % 1000) / 10;
        if (whole)
            put_int(buf, whole), buf += whole > 9 ? 2 : 1;
        *buf++ = '.';
        if (whole) {
            *buf++ = (char)('0' + frac / 10);
            *buf = 0;
        } else {
            *buf++ = (char)('0' + frac / 10);
            *buf++ = (char)('0' + frac % 10);
            *buf = 0;
        }
    } else
        put_int(buf, mhz / 1000);
}
static void time_text(int32_t value, char *buf)    /* ms */
{
    put_int(buf, (time_q8(value >> 8) + 6 * 256) / (12 * 256));
}

static const struct dnm_knob knobs[DNM_KNOBS_ALL] = {
    /* SYN1 1/2: FM's own, its looks and values */
    [0] = { "ALGO", 0, P_ALGO }, [1] = { "C", 0, P_C }, [2] = { "A", 0, P_A }, [3] = { "B", 0, P_B },
    [4] = { "HARM", 0, P_HARM }, [5] = { "DTUN", 0, P_DTUN }, [6] = { "FDBK", 0, P_FDBK }, [7] = { "MIX", 0, P_MIX },
    /* SYN2 1/2: LYRA's */
    [8] = { "MOD", 0, 0 }, [9] = { "RTA", rate_text, 0 }, [10] = { "RTB", rate_text, 0 },
    [11] = { "LOG", logic_text, 0 }, [12] = { "HLF", 0, 0 }, [13] = { "TIM", time_text, 0 },
    [14] = { "DLY", 0, 0 }, [15] = { "FBK", 0, 0 },
    /* SYN1 2/2: FM's ratio offsets */
    [16] = { "COFF", 0, P_COFF }, [17] = { "AOFF", 0, P_AOFF }, [18] = { "BOFF", 0, P_B1OF }, [19] = { "B2OF", 0, P_B2OF },
    /* SYN2 2/2 */
    [24] = { "DMOD", 0, 0 }, [27] = { "PHRT", 0, P_PHRT }, [28] = { "DRFT", 0, 0 },
};
/* what a track gets when it switches to LYRA FM: SYN1 as FM's defaults leave it; MOD 40, RT A 40, RT B 54,
 * SUM, HLFO 30, TIME 90, DLY 40, FBK 0; DMOD 20, DRFT 40 */
static const int16_t defaults[DNM_KNOBS_ALL] = {
    -1, -1, -1, -1, -1, -1, -1, -1,
    40 << 8, 40 << 8, 54 << 8, 0, 30 << 8, 90 << 8, 40 << 8, 0,
    -1, -1, -1, -1, -1, -1, -1, -1,
    20 << 8, -1, -1, -1, 40 << 8, -1, -1, -1,
};

static const uint16_t icon[16] = {
    0x0000, 0x0000, 0x0410, 0x0A28, 0x1144, 0x2082, 0x2082, 0x4101,
    0x4101, 0x2082, 0x2082, 0x1144, 0x0A28, 0x0410, 0x0000, 0x0000,
};

const struct dnm_machine lyrafm_machine = {
    LYRAFM_ID, "LYRA FM", icon, knobs, lyrafm_render, DNM_OWN_ENV | DNM_PAGES2, defaults,
};
