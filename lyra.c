/* SPDX-License-Identifier: GPL-2.0-or-later */
/* LYRA: an organismic drone machine for the Digitakt mk1, after SOMA Laboratory's LYRA-4/8 (no SOMA
 * code: a fresh, economical take on its ideas). Four oscillators in two pairs, each a triangle that SHRP
 * drives into a square; the pairs beat and wander (DETN); MOD cross-modulates them (from themselves,
 * the other pair, each other in a ring, the HYPER LFO, or the delay); a HYPER LFO (two LFOs, A and B,
 * combined by SUM / AND / OR / XOR, from slow drift to audio-rate growl) gives the vibrato and moves the
 * delay's time. And, as on the LYRA, the delay is part of the instrument: the sound goes into a delay
 * line of its own (12 kHz, up to 306 ms, dark and saturating, like a bucket brigade), DLY sets how much
 * of it you hear and how much it repeats, and FBK feeds what comes out of it back into every
 * oscillator. Turn both up and the network runs away into the LYRA's chaos.
 * The track's filter, amp envelope, LFOs and sends work on it as on a sample. Machine 90.
 *
 * Page LYRA 1/2 (the voices)                Page LYRA 2/2 (the HYPER LFO and the delay)
 *   A TUNE  stock: semitones                   A RT A   LFO A's rate, 0.05 Hz - 220 Hz
 *   B SHRP  to 64 triangle -> square;          B RT B   LFO B's rate
 *           past 64 DRIVE, off-centre          C LOGC   SUM AND OR XOR S&H CHS: how A and B combine
 *   C MOD   to 48 PM; past 48 + true FM        D VIB    vibrato depth (an octave up, one down)
 *   D SRC   SELF PAIR HLFO CHN DLY TRK         E TIME   the delay's time, 2-306 ms
 *   E DETN  beating and wander                 F DLY    wet level, then repeats to runaway;
 *   F INTV  pair 3-4's interval                         past 64 its input folded
 *   G FBK   the delay fed into every osc;      G DMOD   the HYPER LFO on the delay's time;
 *           past 64 folded, and FM from it              past 64 the delay's own output too
 *   H LEV   stock                              H FBK    (G of page 1 again)
 *
 * 0.5 is 0.4 sample for sample, lighter: the values worked out once until one changes (no divisions
 * a block), the HYPER LFO in a pass of its own, a branch-free triangle, smaller name tables.
 * 0.4 added SRC TRK (linked partner tracks) and the echoes decaying while asleep (see lyra_render).
 * 0.3 made the top half of SHRP, MOD, FBK, DLY and DMOD nonlinear (see where they're set) and added
 * two HYPER LFO modes: S&H (a random value each cycle of A) and CHS (a logistic map stepped by A,
 * with B sweeping it from order into chaos). Below those points the knobs act as in 0.2, except that
 * SHRP reaches its square at 64 and TIME tops out at 306 ms (the MD TRX build needed the RAM).
 *
 * Interrupt level, 32 samples a track a block; 32-bit arithmetic only. A track whose AMP envelope has
 * died away sleeps: its level (negative, Q31) settles at about -390 rather than 0 (seen in digiemu), so
 * under 2^16 (-90 dB) counts as silent, with no trig this block. Asleep, its delay stops too: the echoes
 * come back where they left off at the next trig, as a loop network that was only paused. */
#include "digitakt-mk1/core3.h"

#define LYRA_ID     90
#define LOOK_BR     0x86
#define K_TUNE      0
#define K_SHRP      1
#define K_MOD       2
#define K_SRC       3
#define K_DETN      4
#define K_INTV      5
#define K_FBK       6
#define K_LEV       7
#define X_RTA       8                   /* the extras, 8-14: the sound's slots 46-52 */
#define X_RTB       9
#define X_LOGC      10
#define X_VIB       11
#define X_TIME      12
#define X_DLY       13
#define X_DMOD      14

enum { S_SELF, S_PAIR, S_HLFO, S_CHAIN, S_DLY, S_TRK, SRCS };
enum { L_SUM, L_AND, L_OR, L_XOR, L_SH, L_CHS, LOGICS };
#define INTVS 11
#define DLEN  3760                      /* the delay line: 3760 samples at 12 kHz (48 kHz / 4), 313 ms */

/* pair 3-4's interval, Q16 */
static const int32_t intv_ratio[INTVS] = {
    65536, 77936, 82570, 87480, 98179, 116772, 131072, 196358, 262144, 32768, 43691,
};
/* the knobs' value names, 5 bytes each (NUL-padded): one table, not a pointer per name */
static const char intv_names[INTVS][5] = {
    "UNI", "+m3", "+M3", "+4TH", "+5TH", "+m7", "+OCT", "+O+5", "+2OC", "-OCT", "-5TH",
};
static const char src_names[SRCS][5] = { "SELF", "PAIR", "HLFO", "CHN", "DLY", "TRK" };

/* SRC's knob: 0.3's five sources keep their places (a saved SRC still picks the same one), TRK takes
 * the top seven steps */
static int32_t src_of(int32_t v)
{
    return v < 26 ? S_SELF : v < 52 ? S_PAIR : v < 77 ? S_HLFO : v < 103 ? S_CHAIN : v < 121 ? S_DLY : S_TRK;
}
static const char logic_names[LOGICS][5] = { "SUM", "AND", "OR", "XOR", "S&H", "CHS" };
/* each oscillator's share of DETN, Q4: the pairs beat at different speeds */
static const int8_t detn_share[4] = { -16, 16, -11, 21 };

/* What a block needs from the fifteen values, worked out once and kept until a value changes (most
 * blocks, nothing has: the divisions and squares then cost nothing) */
struct lyra_par {
    uint8_t key[15];                    /* the values it was worked out from, 0-127 (0xff: never) */
    uint8_t src, intv, logic;
    int32_t gain, g, drv, dbias, dzero, amt, famt, vd, wet, dry, regen, dm2, target;
    int32_t fmd, gfb, fbfm, gin, ds2, dd, dw;
    uint32_t ia, ib;
};

struct lyra_state {
    struct lyra_par par;
    uint32_t ph[4];                     /* the oscillators' phases, 2^32 a cycle */
    int32_t last[4];                    /* their last outputs, averaged over two samples (FM) */
    int32_t walk[4], goal[4], wait[4];  /* DETN's wander: a slow random walk each, Q15 */
    uint32_t la, lb;                    /* the HYPER LFO's two phases */
    int32_t hs;                         /* its output, slewed, Q15 */
    int32_t held, cx;                   /* S&H's value; CHS's logistic map, Q15 (0, 1) */
    int32_t fl, fe, tl, fp, fq0, fq1;                 /* FBK's FM signal slowed (~60 Hz) and its level (~30 Hz); the line's output, slowed */
    int32_t ms[4];                      /* the FM sources, slowed (~60 Hz, at 12 kHz), for MOD's true-FM half */
    int32_t dc, ldc, tdc, odc;          /* DC blocks, Q8: the voices', the delay loop's, the self-modulated read's, DRIVE's output */
    int32_t slept;                      /* blocks asleep, for the echoes' decay at the next trig */
    uint32_t rnd;
    /* the delay */
    int32_t acc;                        /* the 4 samples going into the next 12 kHz one */
    int32_t lp;                         /* the line's low-pass (bucket-brigade dark) */
    int32_t dt;                         /* the delay time, 12 kHz samples Q8, gliding to TIME */
    int32_t tap0, tap1;                 /* the last two reads, interpolated up to 48 kHz */
    int32_t w;                          /* the write index */
};
struct lyra_state lyra_st[8];
int16_t lyra_line[8][DLEN];             /* 58.75 KB */
/* TRK: each track's voices and delay output this block, at 12 kHz, for its partner (1-2, 3-4, 5-6,
 * 7-8) to read. A partner on the left gets this block's, one on the right the last block's */
int16_t lyra_link_v[8][8], lyra_link_d[8][8];
uint32_t lyra_awake;                    /* the LYRA tracks that rendered (rather than slept) last */

static inline int32_t tri(uint32_t ph)  /* Q15, -32769 .. 32767 */
{
    int32_t x = (int32_t)(ph + 0x40000000u) >> 15, sg = x >> 31;
    return 32767 - ((x ^ sg) - sg);     /* |x| without a branch */
}

static inline int32_t clip(int32_t v)
{
    return v > 32767 ? 32767 : v < -32767 ? -32767 : v;
}

/* the line's saturation: 1.5x for small signals, bending smoothly to full scale (3x - x^3) / 2 */
static inline int32_t sat(int32_t x)
{
    int32_t x2;
    x = clip(x);
    x2 = (x * x) >> 15;
    return (x * (98304 - x2)) >> 16;
}

/* an endless triangle folder: x (Q15) times g (Q8, 256 is unity) folded back into range, over and over;
 * the fold is the triangle wave itself, so it costs what an oscillator does */
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

static int32_t val(int32_t v)          /* 8.8 -> 0-127 */
{
    v = (v + 128) >> 8;
    return v < 0 ? 0 : v > 127 ? 127 : v;
}

/* u * (1 + dev / 65536), |dev| a few thousand */
static uint32_t detune(uint32_t u, int32_t dev)
{
    return u + (uint32_t)((int32_t)(u >> 16) * dev + (((int32_t)(u & 0xffff) * dev) >> 16));
}

/* 2^(e / 2048), as m (Q13, 8192-16383) << oct */
static inline int32_t exp2q(int32_t e, int32_t *oct)
{
    *oct = e >> 11;
    return (int32_t)(fw_pitch_tab[10752 + (e & 2047)] >> 16);
}

/* the HYPER LFO's rate: 0.05 Hz * 2^(v / 10.5), as a phase increment at 48 kHz (0.05 Hz = 4474) */
static uint32_t rate_inc(int32_t v)
{
    int32_t oct, m = exp2q(v * 195, &oct);
    return (uint32_t)((4474 * m) >> 13) << oct;
}

/* the delay time: 24 * 2^(v * 117 / 2048) samples at 12 kHz (2 ms - 306 ms), Q8 */
static int32_t time_q8(int32_t v)
{
    int32_t oct, m = exp2q(v * 117, &oct);
    return ((24 * 256 * m) >> 13) << oct;
}

/* The HYPER LFO for a block: hs after each sample's step. It depends on nothing else, so it runs
 * before the voices, in a function of its own, and leaves the voices' loop its registers (0.5). */
static __attribute__((noinline)) void lyra_hlfo(struct lyra_state *s, int32_t logic, uint32_t ia, uint32_t ib,
                                                int32_t *hsv)
{
    int i;
        uint32_t la = s->la, lb = s->lb;
        int32_t hs = s->hs, h;
        switch (logic) {
        case L_SUM:
            for (i = 0; i < 32; i++) {
                la += ia; lb += ib;
                h = (tri(la) + tri(lb)) >> 1;
                hsv[i] = hs += (h - hs) >> 4;   /* soften the logic's edges a little */
            }
            break;
        case L_AND:
        case L_OR:
        case L_XOR: {                           /* the two square waves through AND, OR or XOR: */
            /* the truth table, bit (a * 2 + b) for A's and B's squares (1 in a cycle's 2nd half) */
            uint32_t tt = logic == L_AND ? 8 : logic == L_OR ? 14 : 6;
            for (i = 0; i < 32; i++) {
                la += ia; lb += ib;
                h = (tt >> (((la >> 31) << 1) | (lb >> 31))) & 1 ? 32767 : -32767;
                hsv[i] = hs += (h - hs) >> 4;
            }
            break;
        }
        case L_SH:                              /* a new random value each cycle of A, held */
            for (i = 0; i < 32; i++) {
                la += ia; lb += ib;
                if (la < ia)
                    s->held = (int32_t)(xorshift(&s->rnd) >> 16) - 32768;
                hsv[i] = hs += (s->held - hs) >> 4;
            }
            break;
        default:                                /* CHS: the logistic map, a step each cycle of A, */
            for (i = 0; i < 32; i++) {          /* B sweeping it from order (r 3.5) into chaos (3.99) */
                la += ia; lb += ib;
                if (la < ia) {
                    int32_t cx = s->cx, y = (cx * (32768 - cx)) >> 15, r = 14336 + (((tri(lb) + 32769) * 2007) >> 16);
                    cx = (y * r) >> 12;
                    s->cx = cx <= 0 || cx >= 32767 ? 12345 : cx;
                }
                h = s->cx * 2 - 32767;
                hsv[i] = hs += (h - hs) >> 4;
            }
            break;
        }
        s->la = la; s->lb = lb; s->hs = hs;
}

void lyra_render(int32_t *blocks)
{
    int t, i, k;
    for (t = 0; t < 8; t++) {
        const volatile int16_t *p = &fw_voice_params[53 * t];
        const volatile int16_t *x = p + 29 - 8;     /* the extras: slot 46 + (k - 8), params at slot 17 */
        struct lyra_state *s = &lyra_st[t];
        int16_t *line = lyra_line[t];
        int32_t *out = &blocks[32 * t];
        int32_t pitch, shrp, mod, src, detn, intv, fbk, lev, gain, rta, rtb, logic, vib, dtime, dly, dmod;
        int32_t g, amt, famt, vd, dev, env, wet, dry, regen, dm2, target, fmd, fbfm, gfb, gin, ds2, drv, dbias, dzero;
        uint32_t base, inc[4], ia, ib;
        if (fw_render_machine[t] != LYRA_ID)
            continue;
        env = fw_amp_env[3 * t + 1];
        if (!(fw_voice_start & (1u << t)) && env > -65536 && env < 65536) {
            for (i = 0; i < 32; i++)
                out[i] = 0;
            if (s->slept < (1 << 20))
                s->slept++;
            lyra_awake &= ~(1u << t);           /* a sleeping partner gives TRK nothing */
            continue;
        }
        lyra_awake |= 1u << t;
        pitch = ((int32_t)p[K_TUNE] - 0x4000) * 256 + fw_voice_note[t] + (3 << 16);
        pitch = pitch < 0 ? 0 : pitch > (87 << 16) ? 87 << 16 : pitch;
        base = (23410327u >> 13) * (fw_pitch_tab[pitch / 384] >> 16);
        {
            uint8_t key[15];
            struct lyra_par *pp = &s->par;
            int changed = 0;
            for (k = 1; k < 15; k++) {
                key[k] = (uint8_t)val(k < 8 ? p[k] : x[k]);
                changed |= key[k] != pp->key[k];
            }
            if (changed || pp->key[0] != 0xa5) {
                shrp = key[K_SHRP]; mod = key[K_MOD]; detn = key[K_DETN]; fbk = key[K_FBK]; lev = key[K_LEV];
                rta = key[X_RTA]; rtb = key[X_RTB]; vib = key[X_VIB]; dtime = key[X_TIME]; dly = key[X_DLY];
                dmod = key[X_DMOD];
                src = src_of(key[K_SRC]);
                intv = key[K_INTV] * INTVS / 128;
                logic = key[X_LOGC] * LOGICS / 128;
                gain = lev * lev;
                g = 256 + ((shrp < 64 ? shrp * shrp : 4096) >> 1);    /* SHRP to 64: up to x9 into the clip, near square */
                /* SHRP past 64: the LYRA's DRIVE, on the four voices together, before the delay (so it's in the
                 * network): up to 8x into the saturation, a little off-centre for the even harmonics */
                drv = shrp > 64 ? 256 + ((shrp - 64) * (shrp - 64) * 1792) / 3969 : 0;
                dbias = shrp > 64 ? (shrp - 64) * 224 : 0;
                dzero = sat(dbias);
                amt = mod < 72 ? mod * mod : 5184;          /* PM stops growing at 72: past it, MOD grows the true FM */
                famt = fbk < 88 ? (fbk * fbk) >> 2 : 1936;   /* and FBK's PM at 88 */
                vd = vib * vib;
                ia = rate_inc(rta);
                ib = rate_inc(rtb);
                /* DLY: the first half brings the echoes in (wet 0 - 0.7, the dry down to 0.8), the second
                 * raises the repeats; with the line's 1.5x saturation the loop's gain passes 1 at about 110,
                 * and from there it runs on by itself */
                wet = dly < 64 ? dly * 90 / 64 : 90;        /* /128 */
                dry = 128 - wet * 26 / 90;
                regen = dly < 32 ? 0 : ((dly - 32) * (dly - 32) * 32440) / 9025;    /* Q15, up to 0.99 */
                dm2 = dmod * dmod;
                target = time_q8(dtime);
                /* 0.3, the nonlinear half of each knob:
                 * MOD past 48 adds true, through-zero FM (the oscillators pull each other's pitch, not just
                 * their phase); FBK past 64 folds the delay's output before it goes back in, and adds FM from
                 * it too; DLY past 64 drives the delay's input into a folder; DMOD past 64 lets the delay's
                 * own output move its time (a self-modulated delay: it bends its own echoes) */
                fmd = mod > 48 ? ((mod - 48) * (mod - 48) * 16129) / 6241 : 0;  /* the pitch swings up to +-98 % (down to -88 %) */
                gfb = fbk > 64 ? 256 + ((fbk - 64) * (fbk - 64) * 256) / 3969 : 256;           /* fold to 2x */
                fbfm = fbk > 64 ? ((fbk - 64) * (fbk - 64) * 12288) / 3969 : 0;  /* and +-75 % more from FBK */
                gin = dly > 64 ? 256 + ((dly - 64) * (dly - 64) * 384) / 3969 : 256;          /* fold to 2.5x */
                ds2 = dmod > 64 ? ((dmod - 64) * (dmod - 64) * 16129) / 3969 : 0;
                for (k = 1; k < 15; k++)
                    pp->key[k] = key[k];
                pp->key[0] = 0xa5;
                pp->src = (uint8_t)src; pp->intv = (uint8_t)intv; pp->logic = (uint8_t)logic;
                pp->gain = gain; pp->g = g; pp->drv = drv; pp->dbias = dbias; pp->dzero = dzero;
                pp->amt = amt; pp->famt = famt; pp->vd = vd; pp->wet = wet; pp->dry = dry; pp->regen = regen;
                pp->dm2 = dm2; pp->target = target; pp->fmd = fmd; pp->gfb = gfb; pp->fbfm = fbfm;
                pp->gin = gin; pp->ds2 = ds2; pp->ia = ia; pp->ib = ib;
                pp->dd = (detn * detn * 84) >> 10;  /* DETN: up to 35 cents of detune and 25 of wander */
                pp->dw = detn * detn;
            }
            src = pp->src; intv = pp->intv; logic = pp->logic;
            gain = pp->gain; g = pp->g; drv = pp->drv; dbias = pp->dbias; dzero = pp->dzero;
            amt = pp->amt; famt = pp->famt; vd = pp->vd; wet = pp->wet; dry = pp->dry; regen = pp->regen;
            dm2 = pp->dm2; target = pp->target; fmd = pp->fmd; gfb = pp->gfb; fbfm = pp->fbfm;
            gin = pp->gin; ds2 = pp->ds2; ia = pp->ia; ib = pp->ib;
        }

        /* Waking: the AMP envelope is applied after the machine, so nothing LYRA plays while it's
         * closed can be heard, and asleep the delay stands still. Rather than the old echoes coming
         * back as they were, they come back decayed as far as they would have been (the loop's gain
         * once per pass of the delay); a loop that runs on by itself (DLY past ~110) keeps them. */
        if (s->slept) {
            int32_t lg = (regen * 3) >> 1, passes = s->dt > 0 ? (int32_t)(((uint32_t)s->slept << 11) / (uint32_t)s->dt) : 1 << 20;
            if (lg < 31130) {                   /* under 0.95 */
                int32_t fac = 32767;
                for (k = 0; k < passes && fac > 32; k++)
                    fac = (fac * lg) >> 15;
                if (fac <= 32) {
                    for (k = 0; k < DLEN; k++)
                        line[k] = 0;
                    s->tap0 = s->tap1 = s->lp = s->fp = s->fq0 = s->fq1 = s->ldc = s->tdc = s->tl = 0;
                } else if (fac < 32767) {
                    for (k = 0; k < DLEN; k++)
                        line[k] = (int16_t)((line[k] * fac) >> 15);
                    s->tap0 = (s->tap0 * fac) >> 15;
                    s->tap1 = (s->tap1 * fac) >> 15;
                    s->ldc = (((s->ldc >> 8) * fac) >> 15) * 256;
                }
            }
            s->slept = 0;
        }
        /* the oscillators' increments this block: the interval, DETN's detune and wander */
        {
            uint32_t r2 = (base >> 10) * (uint32_t)(intv_ratio[intv] >> 6);
            int32_t dd = s->par.dd, dw = s->par.dw;    /* up to 35 and 25 cents */
            for (k = 0; k < 4; k++) {
                if (--s->wait[k] <= 0) {
                    uint32_t r = xorshift(&s->rnd);
                    s->goal[k] = (int32_t)(r >> 17) - 16384;
                    s->goal[k] *= 2;
                    s->wait[k] = 200 + (int32_t)((r >> 3) & 511);       /* a new goal each 0.13-0.47 s */
                }
                s->walk[k] += (s->goal[k] - s->walk[k]) >> 8;
                dev = ((dd * detn_share[k]) >> 4) + ((s->walk[k] * dw) >> 19);
                inc[k] = detune(k < 2 ? base : r2, dev);
            }
        }

        /* The samples: the HYPER LFO first, a block at a time (lyra_hlfo), then the four oscillators and
         * the delay, sample by sample. The state in locals (the stores to out[] would otherwise make the
         * compiler reload it each time), the choices made once a block. */
            {
                uint32_t p0 = s->ph[0], p1 = s->ph[1], p2 = s->ph[2], p3 = s->ph[3];
                int32_t l0 = s->last[0], l1 = s->last[1], l2 = s->last[2], l3 = s->last[3];
                int32_t hs, acc = s->acc, tap0 = s->tap0, tap1 = s->tap1, dt = s->dt, w = s->w, hsv[32];
                int vib_on = vd != 0, shape = g != 256, fm_on = fmd || fbfm;
                int trk = src == S_TRK, pt = t ^ 1;
                int dcb = drv || famt || (amt && (src == S_DLY || trk));
                /* TRK: the partner's voices (the FM source) and its delay (FBK's signal), if it's a LYRA
                 * that's awake; else nothing */
                const int16_t *pv = 0, *pd = 0;
                if (trk && fw_render_machine[pt] == LYRA_ID && (lyra_awake & (1u << pt))) {
                    pv = lyra_link_v[pt];
                    pd = lyra_link_d[pt];
                }
                /* the states the nonlinear parts use stay in s: they're touched at 12 kHz or only when
                 * that part is on, and keeping them out of registers keeps the plain path fast */
                int32_t fd = 0, fq0 = s->fq0, fq1 = s->fq1;
                uint32_t c0 = inc[0], c1 = inc[1], c2 = inc[2], c3 = inc[3];    /* the increments, set at 12 kHz */
                if (s->cx <= 0 || s->cx >= 32767)
                    s->cx = 12345;                          /* the logistic map's seed (0 and 1 are its traps) */
                if (dt < (2 << 8))
                    dt = target;                            /* a fresh state: no glide from 0 */
                lyra_hlfo(s, logic, ia, ib, hsv);
                for (i = 0; i < 32; i++) {
                    int32_t r0, r1, r2, r3, o0, o1, o2, o3, m, d, ph4 = (i & 3) + 1;
                    int32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
                    uint32_t q0, q1, q2, q3;
                    /* what the delay gives back, interpolated from 12 kHz */
                    d = tap0 + (((tap1 - tap0) * ph4) >> 2);
                    hs = hsv[i];                            /* the HYPER LFO, from its own pass */
                    /* the FM sources, at audio rate (PM) */
                    if (amt) {
                        switch (src) {
                        case S_SELF:  s0 = l0; s1 = l1; s2 = l2; s3 = l3; break;
                        case S_PAIR:  s0 = s1 = (l2 + l3) >> 1; s2 = s3 = (l0 + l1) >> 1; break;
                        case S_HLFO:  s0 = s1 = s2 = s3 = hs; break;
                        case S_CHAIN: s0 = l1; s1 = l2; s2 = l3; s3 = l0; break;
                        case S_DLY:   s0 = s1 = s2 = s3 = d; break;
                        default:      s0 = s1 = s2 = s3 = pv ? pv[i >> 2] : 0; break;
                        }
                    }
                    if (famt) {                             /* FBK's signal: the delay's output (TRK: the partner's); */
                        if (gfb != 256)                     /* past 64 folded (made at 12 kHz, below) */
                            fd = fq0 + (((fq1 - fq0) * ph4) >> 2);
                        else
                            fd = trk ? (pd ? pd[i >> 2] : 0) : clip(d);
                    }
                    /* every fourth sample (12 kHz), the slow part: vibrato and true FM move the increments */
                    if (ph4 == 1) {
                        c0 = inc[0]; c1 = inc[1]; c2 = inc[2]; c3 = inc[3];
                        if (vib_on) {
                            int32_t dv = (hs * vd) >> 13;   /* Q16: up to an octave */
                            if (dv < 0)
                                dv >>= 1;                   /* and down to one */
                            dv >>= 6;
                            c0 += (uint32_t)((int32_t)(c0 >> 10) * dv);
                            c1 += (uint32_t)((int32_t)(c1 >> 10) * dv);
                            c2 += (uint32_t)((int32_t)(c2 >> 10) * dv);
                            c3 += (uint32_t)((int32_t)(c3 >> 10) * dv);
                        }
                        if (fm_on) {
                            /* true FM, through zero: the sources slowed to ~60 Hz, so they bend and pull the
                             * pitches (and lock them, and let go) rather than turning into sidebands and
                             * hiss; and the echoes' level pushes every pitch up: loud echoes sound higher,
                             * which changes what the delay hears next - the loop that makes it lurch */
                            int32_t fb, f0, f1, f2, f3, ad = fd < 0 ? -fd : fd;
                            int32_t *ms = s->ms;
                            ms[0] += (s0 - ms[0]) >> 5; ms[1] += (s1 - ms[1]) >> 5;
                            ms[2] += (s2 - ms[2]) >> 5; ms[3] += (s3 - ms[3]) >> 5;
                            s->fl += (fd - s->fl) >> 5;
                            s->fe += (ad - s->fe) >> 6;
                            fb = ((s->fl + s->fe) * fbfm) >> 15;
                            f0 = (((ms[0] * fmd) >> 15) + fb) >> 4;
                            f1 = (((ms[1] * fmd) >> 15) + fb) >> 4;
                            f2 = (((ms[2] * fmd) >> 15) + fb) >> 4;
                            f3 = (((ms[3] * fmd) >> 15) + fb) >> 4;
                            /* never all the way down: a slow source can hold a pitch at 0 and stall it */
                            f0 = f0 < -900 ? -900 : f0; f1 = f1 < -900 ? -900 : f1;
                            f2 = f2 < -900 ? -900 : f2; f3 = f3 < -900 ? -900 : f3;
                            c0 += (uint32_t)((int32_t)(c0 >> 10) * f0);
                            c1 += (uint32_t)((int32_t)(c1 >> 10) * f1);
                            c2 += (uint32_t)((int32_t)(c2 >> 10) * f2);
                            c3 += (uint32_t)((int32_t)(c3 >> 10) * f3);
                        }
                    }
                    p0 += c0;
                    p1 += c1;
                    p2 += c2;
                    p3 += c3;
                    q0 = p0; q1 = p1; q2 = p2; q3 = p3;
                    if (amt) {                              /* PM: phase offsets from the source */
                        q0 += (uint32_t)(s0 * amt) << 3;
                        q1 += (uint32_t)(s1 * amt) << 3;
                        q2 += (uint32_t)(s2 * amt) << 3;
                        q3 += (uint32_t)(s3 * amt) << 3;
                    }
                    if (famt) {                             /* FBK: the delay's output into every oscillator */
                        uint32_t pfb = (uint32_t)(fd * famt) << 4;
                        q0 += pfb; q1 += pfb; q2 += pfb; q3 += pfb;
                    }
                    r0 = tri(q0); r1 = tri(q1); r2 = tri(q2); r3 = tri(q3);
                    l0 = (l0 + r0) >> 1; l1 = (l1 + r1) >> 1; l2 = (l2 + r2) >> 1; l3 = (l3 + r3) >> 1;
                    if (shape) {
                        o0 = clip((r0 * g) >> 8); o1 = clip((r1 * g) >> 8);
                        o2 = clip((r2 * g) >> 8); o3 = clip((r3 * g) >> 8);
                    } else {
                        o0 = r0; o1 = r1; o2 = r2; o3 = r3;
                    }
                    m = (o0 + o1 + o2 + o3) >> 2;           /* the voices, what goes into the delay */
                    if (drv)                                /* DRIVE */
                        m = sat(((m * drv) >> 8) + dbias) - dzero;
                    if (dcb) {                              /* a DC block (~7 Hz, its level moved at 12 kHz): DRIVE is */
                        if (ph4 == 4)                       /* off-centre, and feedback PM (FBK, or MOD from DLY) can */
                            s->dc += (m * 256 - s->dc) >> 8;    /* leave the voices off centre too (0.2: up to 0.14) */
                        m -= s->dc >> 8;
                    }
    #ifdef LYRA_HOST_MUTE
                    if (lyra_host_mute)                     /* the PC tests: stop the voices, hear the network */
                        m = 0;
    #endif
                    acc += m;
                    if (ph4 == 4) {                         /* a 12 kHz sample: write one, read one */
                        int32_t in, dd2, ri, f, a, b;
                        in = acc >> 2;
                        acc = 0;
                        if (gin != 256)                     /* DLY past 64 drives the line's input into a folder */
                            in = fold(in, gin);
                        s->lp += (in - s->lp) >> 1;         /* dark, about 2.5 kHz */
                        dt += (target - dt) >> 7;           /* TIME glides, so turning it bends the echoes */
                        dd2 = dt;
                        if (dm2)                            /* DMOD: the HYPER LFO moves the time, up to +-50 % */
                            dd2 += ((dt >> 6) * ((hs * dm2) >> 15)) >> 9;
                        if (ds2) {                          /* and past 64 the line's own output, slowed, up to +-50 % more */
                            s->tl += (tap1 - s->tl) >> 6;
                            dd2 += ((dt >> 6) * ((s->tl * ds2) >> 15)) >> 9;
                        }
                        dd2 = dd2 < (2 << 8) ? 2 << 8 : dd2 > ((DLEN - 2) << 8) ? (DLEN - 2) << 8 : dd2;
                        ri = w - (dd2 >> 8);
                        if (ri < 0)
                            ri += DLEN;
                        f = dd2 & 255;
                        a = line[ri];
                        b = line[ri > 0 ? ri - 1 : DLEN - 1];
                        tap0 = tap1;
                        tap1 = a + (((b - a) * f) >> 8);
                        if (ds2) {                          /* a self-modulated read rectifies (it moves with the */
                            s->tdc += (tap1 * 256 - s->tdc) >> 9;   /* signal it reads): block the DC it makes */
                            tap1 -= s->tdc >> 8;
                        }
                        if (gfb != 256) {                   /* FBK past 64: the tap folded, then darkened (~950 Hz) */
                            int32_t xs = trk ? (pd ? pd[i >> 2] : 0) : tap1;   /* as the LYRA's delay is: the */
                            fq0 = fq1;                      /* loop screams instead of turning into hiss */
                            s->fp += (fold(xs, gfb) - s->fp) >> 1;
                            fq1 = s->fp;
                        }
                        /* the loop gets a DC block (~4 Hz): a saturating loop with gain over 1 otherwise
                         * finds a DC level it can hold, latches there and clips everything to silence */
                        in = sat(s->lp + ((tap1 * regen) >> 15));
                        s->ldc += (in * 256 - s->ldc) >> 9;     /* Q8: a plain integer one stalls up to 511 off */
                        line[w] = (int16_t)clip(in - (s->ldc >> 8));
                        if (++w == DLEN)
                            w = 0;
                        lyra_link_v[t][i >> 2] = (int16_t)clip(m);     /* for the partner's TRK */
                        lyra_link_d[t][i >> 2] = (int16_t)clip(tap1);
                    }
                    {
                        int32_t o = clip((m * dry + d * wet) >> 7);
                        if (drv) {                          /* DRIVE's off-centre peaks, clipped here with the echoes, */
                            if (ph4 == 4)                   /* leave an offset: one more DC block (~7 Hz) */
                                s->odc += (o * 256 - s->odc) >> 8;
                            o = clip(o - (s->odc >> 8));
                        }
                        out[i] = o * gain * 4;              /* Q15 x Q14 -> Q31 */
                    }
                }
                s->ph[0] = p0; s->ph[1] = p1; s->ph[2] = p2; s->ph[3] = p3;
                s->last[0] = l0; s->last[1] = l1; s->last[2] = l2; s->last[3] = l3;
                s->fq0 = fq0; s->fq1 = fq1;
                s->acc = acc; s->tap0 = tap0; s->tap1 = tap1; s->dt = dt; s->w = w;
            }
        
    }
}

/* ---- the pages (machine-pages draws them) -------------------------------------------------------- */

/* never in the audio path: compiled for size */
#define LYRA_COLD __attribute__((optimize("Os"), cold))

static LYRA_COLD int32_t str(char *buf, const char *s)
{
    int k = 0;
    while ((buf[k] = s[k]) != 0)
        k++;
    return 1;
}

static LYRA_COLD int32_t num(char *buf, int32_t v)
{
    char d[8];
    int n = 0, k = 0;
    do {
        d[n++] = (char)('0' + v % 10);
        v /= 10;
    } while (v);
    while (n)
        buf[k++] = d[--n];
    buf[k] = 0;
    return k;
}

static LYRA_COLD int32_t pick(char *buf, int32_t value, const char (*names)[5], int32_t n)
{
    return str(buf, names[val(value) * n / 128]);
}

static LYRA_COLD int32_t src_text(char *buf, int32_t v, int32_t c, int32_t m)
{
    (void)c; (void)m;
    return str(buf, src_names[src_of(val(v))]);
}

static LYRA_COLD int32_t intv_text(char *buf, int32_t v, int32_t c, int32_t m)
{
    (void)c; (void)m;
    return pick(buf, v, intv_names, INTVS);
}

static LYRA_COLD int32_t logic_text(char *buf, int32_t v, int32_t c, int32_t m)
{
    (void)c; (void)m;
    return pick(buf, v, logic_names, LOGICS);
}

/* the rate in Hz: .05 .. 9.9, then whole Hz */
static LYRA_COLD int32_t rate_text(char *buf, int32_t v, int32_t c, int32_t m)
{
    int32_t oct, f = exp2q(val(v) * 195, &oct), mhz = ((50 * f) >> 13) << oct, k;
    (void)c; (void)m;
    if (mhz < 1000) {                   /* .05 - .99 */
        buf[0] = '.';
        buf[1] = (char)('0' + (mhz / 100) % 10);
        buf[2] = (char)('0' + (mhz / 10) % 10);
        buf[3] = 0;
        return 1;
    }
    if (mhz < 10000) {                  /* 1.0 - 9.9 */
        k = num(buf, mhz / 1000);
        buf[k++] = '.';
        buf[k++] = (char)('0' + (mhz / 100) % 10);
        buf[k] = 0;
        return 1;
    }
    num(buf, mhz / 1000);
    return 1;
}

/* the delay time in ms (12 kHz samples / 12) */
static LYRA_COLD int32_t time_text(char *buf, int32_t v, int32_t c, int32_t m)
{
    (void)c; (void)m;
    num(buf, (time_q8(val(v)) + 6 * 256) / (12 * 256));
    return 1;
}

/* A track switched to LYRA starts from LYRA's values. TUNE and LEV are left as they were. */
static const int8_t lyra_def[15] = {
    -1, 24, 0, 0, 40, 0, 0, -1,             /* SHRP MOD SRC=SELF DETN INTV=UNI FBK */
    40, 54, 0, 24, 90, 40, 20,              /* RT A .7 Hz, RT B 1.8 Hz, SUM, VIB, TIME 73 ms, DLY, DMOD */
};

static void lyra_switch(int32_t track, int32_t from, uint8_t *sound)
{
    int k;
    (void)from;
    if (track >= 0 && track < 8) {          /* a clean delay line for the new machine */
        for (k = 0; k < DLEN; k++)
            lyra_line[track][k] = 0;
        lyra_st[track].dt = 0;
        lyra_st[track].par.key[0] = 0;      /* work the values out afresh */
    }
    if (!sound)
        return;
    for (k = 0; k < 15; k++)
        if (lyra_def[k] >= 0) {
            int slot = k < 8 ? k : 29 + (k - 8);    /* the extras: slots 46-52 */
            sound[FW_SOUND_PARAM(slot)] = (uint8_t)lyra_def[k];
            sound[FW_SOUND_PARAM(slot) + 1] = 0;
        }
}

#define KNOB(label, long_name, def_, fmt_, more) \
    { .name = label, .lname = long_name, .look = LOOK_BR, .flags = CM_RANGE | CM_DEFAULT | (more), \
      .min = 0, .max = 127 << 8, .def = (def_) << 8, .fmt = fmt_ }

static const struct cm_src_page lyra_pages[] = {
    { "LYRA 1/2", { 0, 1, 2, 3, 4, 5, 6, 7 } },
    { "LYRA 2/2", { 8, 9, 10, 11, 12, 13, 14, 6 } },
};

static const struct cm_ui32 lyra_ui = {
    .base = {
        .abi = cm_ui_v32,
        .page_from = 3,
        .group = "LYRA",
        .on_switch = lyra_switch,
        .knob = {
            [K_SHRP] = KNOB("SHRP", "Sharpness", 24, 0, 0),
            [K_MOD]  = KNOB("MOD", "FM Depth", 0, 0, 0),
            [K_SRC]  = KNOB("SRC", "FM Source", 0, src_text, CM_NOT_SAMPLE),
            [K_DETN] = KNOB("DETN", "Detune Drift", 40, 0, 0),
            [K_INTV] = KNOB("INTV", "Pair 2 Interval", 0, intv_text, 0),
            [K_FBK]  = KNOB("FBK", "Delay Feedback", 0, 0, 0),
        },
    },
    .extra = {
        KNOB("RT A", "Hyper LFO A Rate", 40, rate_text, 0),
        KNOB("RT B", "Hyper LFO B Rate", 54, rate_text, 0),
        KNOB("LOGC", "Hyper LFO Logic", 0, logic_text, 0),
        KNOB("VIB", "Vibrato Depth", 24, 0, 0),
        KNOB("TIME", "Delay Time", 90, time_text, 0),
        KNOB("DLY", "Delay Amount", 40, 0, 0),
        KNOB("DMOD", "Delay Time Mod", 20, 0, 0),
    },
    .page_count = 2,
    .pages = lyra_pages,
};

/* 11 x 7 icon: a lyre, four strings in a frame */
static const uint32_t lyra_px[11] = {
    0x70000000, 0x88000000, 0xfe000000, 0x82000000, 0xfe000000, 0x82000000,
    0xfe000000, 0x82000000, 0xfe000000, 0x88000000, 0x70000000,
};
static const uint32_t lyra_mask[11] = {
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
    0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000, 0xfe000000,
};
static const struct {
    const void *vt;
    int32_t width, height, one;
    const uint32_t *px, *mask;
    int32_t zero;
} lyra_icon = { fw_bitmap_vt, 11, 7, 1, lyra_px, lyra_mask, 0 };

const struct cm_machine lyra_machine = {
    LYRA_ID, "LYRA", "LYRA", &lyra_icon, 3, LYRA_ID, CM_UI_TAG, &lyra_ui.base,
};
