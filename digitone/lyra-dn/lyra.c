/* SPDX-License-Identifier: GPL-2.0-or-later */
/* LYRA for the Digitone mk1 and Digitone Keys (OS 1.44), a port of LYRA for the Digitakt mk1 (lyra-dt):
 * a drone machine after SOMA Laboratory's LYRA-4/8 (no SOMA code). Four triangle oscillators in two
 * pairs, SHRP (to square, then DRIVE), cross-modulation (MOD from SRC), a HYPER LFO (two LFOs by LOGC)
 * and its own delay network (FBK into the oscillators, DLY, DMOD), as on the Digitakt; see lyra-dt's
 * README for what every knob does.
 *
 *   LYRA 1/2: A TUNE  B SHRP  C MOD  D SRC   E DETN  F INTV  G FBK   H LEV
 *   LYRA 2/2: A RT A  B RT B  C LOGC D VIB   E TIME  F DLY   G DMOD  H FBK
 *
 * Built with elekloader's machine SDK (sdk_alpha_machine.h) on machines-dn1. One voice per track
 * (ELEK_MONO_LEGATO): a new note moves the running drone's pitch rather than starting another, so at
 * most four LYRAs run, one a track, each with its own oscillators and delay (the same CPU per track as
 * on the Digitakt). The state is the track's, in this mod's own RAM: a voice's 256 bytes are too few
 * for it, and the delay line is 7.3 KB. TRK links tracks 1-2 and 3-4.
 *
 * Differences from the Digitakt's: the Digitone's own filter, effects and AMP envelope follow it;
 * SRC, INTV and LOGC step through their choices (0-5, 0-10, 0-5) and TUNE is in semitones (-24..24);
 * the exponential tables are LYRA's own (the SDK gives no firmware tables); a track asleep for a while
 * comes back with its echoes decayed only when the same voice plays it again.
 *
 * 0.2 runs the voices at 24 kHz (16 samples a block, each written out at 48 kHz with a halfway one
 * before it): about 30 % less CPU. The plain and default sounds are as 0.1's; the wildest settings lose
 * some of their fizz above 12 kHz.
 *
 * 0.3 adds LYRA LITE (machine 91): one oscillator a pair (two, not four), the HYPER LFO at 12 kHz and the
 * delay at 6 kHz (its echoes were darkened to 2.5 kHz anyway): about a third less CPU than LYRA. Fewer
 * detuned oscillators cancel each other, so it is a little louder. LYRA itself is 0.2's, sample for sample.
 *
 * Interrupt level, 32 samples a voice a block; 32-bit arithmetic only. */
#include "elekloader/sdk_alpha_machine.h"

enum { K_TUNE, K_SHRP, K_MOD, K_SRC, K_DETN, K_INTV, K_FBK, K_LEV,
       X_RTA, X_RTB, X_LOGC, X_VIB, X_TIME, X_DLY, X_DMOD, KNOBS };
enum { S_SELF, S_PAIR, S_HLFO, S_CHAIN, S_DLY, S_TRK, SRCS };
enum { L_SUM, L_AND, L_OR, L_XOR, L_SH, L_CHS, LOGICS };
#define INTVS 11
#define DLEN  3760                      /* the delay line: 3760 samples at 12 kHz (48 kHz / 4), 313 ms */

/* pair 3-4's interval, Q16 */
static const int32_t intv_ratio[INTVS] = {
    65536, 77936, 82570, 87480, 98179, 116772, 131072, 196358, 262144, 32768, 43691,
};
static const char *const intv_names[INTVS] = {
    "UNI", "+m3", "+M3", "+4TH", "+5TH", "+m7", "+OCT", "+O+5", "+2OC", "-OCT", "-5TH",
};
static const char *const src_names[SRCS] = { "SELF", "PAIR", "HLFO", "CHN", "DLY", "TRK" };
static const char *const logic_names[LOGICS] = { "SUM", "AND", "OR", "XOR", "S&H", "CHS" };
/* each oscillator's share of DETN, Q4: the pairs beat at different speeds */
static const int8_t detn_share[4] = { -16, 16, -11, 21 };

/* What a block needs from the knobs, worked out once and kept until one changes */
struct lyra_par {
    uint8_t key[15];                    /* the values it was worked out from (key[0]: 0xa5 once set) */
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
    int32_t fl, fe, tl, fp, fq0, fq1;   /* FBK's FM signal slowed and its level; the line's output, slowed */
    int32_t ms[4];                      /* the FM sources, slowed, for MOD's true-FM half */
    int32_t dc, ldc, tdc, odc;          /* DC blocks, Q8 */
    int32_t slept;                      /* blocks asleep, for the echoes' decay */
    uint32_t rnd;
    int32_t po;                         /* the last 24 kHz output, for the halfway sample */
    int32_t acc, lp, dt, tap0, tap1, w; /* the delay: input sum, low-pass, time (Q8), reads, write index */
};
/* The four tracks' LYRAs. One voice a track plays (ELEK_MONO_LEGATO), so the track's state is the voice's */
static struct lyra_state lyra_st[4];
static int16_t lyra_line[4][DLEN];      /* 29.4 KB */
/* TRK: each track's voices and delay output this block, at 12 kHz, for its partner (1-2, 3-4) */
static int16_t lyra_link_v[4][8], lyra_link_d[4][8];
static uint32_t lyra_count, lyra_seen[4];   /* renders so far, and each track's last: who is awake */
static const void *lyra_voice[4];       /* the voice (its state) that last played each track */

struct lyra_voice_state { uint32_t on; };   /* 0: zeroed (boot, or the voice came to LYRA): start clean */

static const uint16_t exp2_tab[65] = { 8192,8281,8371,8463,8555,8648,8742,8837,8933,9031,9129,9228,9329,9431,9533,9637,9742,9848,9955,10064,10173,10284,10396,10509,10624,10739,10856,10975,11094,11215,11337,11460,11585,11711,11839,11968,12098,12230,12363,12498,12634,12771,12910,13051,13193,13337,13482,13629,13777,13927,14079,14232,14387,14544,14702,14862,15024,15188,15353,15520,15689,15860,16033,16208,16384 };   /* 8192 * 2^(k / 64) */

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

/* u * (1 + dev / 65536), |dev| a few thousand */
static uint32_t detune(uint32_t u, int32_t dev)
{
    return u + (uint32_t)((int32_t)(u >> 16) * dev + (((int32_t)(u & 0xffff) * dev) >> 16));
}

/* 2^(e / 2048), as m (Q13, 8192-16384) << oct */
static inline int32_t exp2q(int32_t e, int32_t *oct)
{
    int32_t f = e & 2047, k = f >> 5, a = exp2_tab[k];
    *oct = e >> 11;
    return a + (((exp2_tab[k + 1] - a) * (f & 31)) >> 5);
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
static inline __attribute__((always_inline)) void lyra_hlfo(struct lyra_state *s, int32_t logic, uint32_t ia,
                                                             uint32_t ib, int32_t *hsv, const int n)
/* n samples: 16 at 24 kHz, or 8 at 12 kHz (LITE); sl softens the edges as fast either way */
{
    int i, sl = n == 8 ? 2 : 3;
        uint32_t la = s->la, lb = s->lb;
        int32_t hs = s->hs, h;
        switch (logic) {
        case L_SUM:
            for (i = 0; i < n; i++) {
                la += ia; lb += ib;
                h = (tri(la) + tri(lb)) >> 1;
                hsv[i] = hs += (h - hs) >> sl;   /* soften the logic's edges a little */
            }
            break;
        case L_AND:
        case L_OR:
        case L_XOR: {                           /* the two square waves through AND, OR or XOR: */
            /* the truth table, bit (a * 2 + b) for A's and B's squares (1 in a cycle's 2nd half) */
            uint32_t tt = logic == L_AND ? 8 : logic == L_OR ? 14 : 6;
            for (i = 0; i < n; i++) {
                la += ia; lb += ib;
                h = (tt >> (((la >> 31) << 1) | (lb >> 31))) & 1 ? 32767 : -32767;
                hsv[i] = hs += (h - hs) >> sl;
            }
            break;
        }
        case L_SH:                              /* a new random value each cycle of A, held */
            for (i = 0; i < n; i++) {
                la += ia; lb += ib;
                if (la < ia)
                    s->held = (int32_t)(xorshift(&s->rnd) >> 16) - 32768;
                hsv[i] = hs += (s->held - hs) >> sl;
            }
            break;
        default:                                /* CHS: the logistic map, a step each cycle of A, */
            for (i = 0; i < n; i++) {          /* B sweeping it from order (r 3.5) into chaos (3.99) */
                la += ia; lb += ib;
                if (la < ia) {
                    int32_t cx = s->cx, y = (cx * (32768 - cx)) >> 15, r = 14336 + (((tri(lb) + 32769) * 2007) >> 16);
                    cx = (y * r) >> 12;
                    s->cx = cx <= 0 || cx >= 32767 ? 12345 : cx;
                }
                h = s->cx * 2 - 32767;
                hsv[i] = hs += (h - hs) >> sl;
            }
            break;
        }
        s->la = la; s->lb = lb; s->hs = hs;
}

static __attribute__((noinline)) void lyra_hlfo16(struct lyra_state *s, int32_t logic, uint32_t ia, uint32_t ib,
                                                  int32_t *hsv)
{
    lyra_hlfo(s, logic, ia, ib, hsv, 16);
}

static __attribute__((noinline)) void lyra_hlfo8(struct lyra_state *s, int32_t logic, uint32_t ia, uint32_t ib,
                                                 int32_t *hsv)
{
    lyra_hlfo(s, logic, ia, ib, hsv, 8);
}

static void lyra_reset(int t)
{
    struct lyra_state *s = &lyra_st[t];
    uint32_t *p = (uint32_t *)s;
    volatile int16_t *line = lyra_line[t];
    unsigned k;
    for (k = 0; k < sizeof(*s) / 4; k++)
        p[k] = 0;
    for (k = 0; k < DLEN; k++)
        line[k] = 0;
    s->rnd = 0x2545f491u * (uint32_t)(t + 1);
}

/* One voice's block. LITE (a constant: the compiler builds the two machines' loops apart) plays one
 * oscillator a pair, oscillators 1 and 3, instead of four: about a third less CPU. */
static inline __attribute__((always_inline)) void lyra_block(const struct elek_voice *v, elek_sample *out,
                                                             const int LITE)
{
    struct lyra_voice_state *vs = ELEK_STATE(struct lyra_voice_state, v);
    int t = (int)(v->track & 3), i, k;
    struct lyra_state *s = &lyra_st[t];
    int16_t *line = lyra_line[t];
    int32_t pitch, shrp, mod, src, detn, intv, fbk, lev, gain, rta, rtb, logic, vib, dtime, dly, dmod;
    int32_t g, amt, famt, vd, dev, wet, dry, regen, dm2, target, fmd, fbfm, gfb, gin, ds2, drv, dbias, dzero;
    uint32_t base, inc[4], ia, ib;
    if (!vs->on) {                      /* the voice is new to LYRA: a clean LYRA for its track */
        lyra_reset(t);
        vs->on = 1;
    } else if (v->idle && v->state == lyra_voice[t]) {
        s->slept = v->idle >= (32u << 20) ? 1 << 20 : (int32_t)(v->idle >> 5);   /* asleep that many blocks */
    }
    lyra_voice[t] = v->state;
    lyra_seen[t] = ++lyra_count;
    pitch = v->pitch + v->knob[K_TUNE] * 65536;
    pitch = pitch < 0 ? 0 : pitch > (108 << 16) ? 108 << 16 : pitch;
    base = elek_phase_inc(pitch);
        {
            uint8_t key[15];
            struct lyra_par *pp = &s->par;
            int changed = 0;
            for (k = 1; k < 15; k++) {
                key[k] = (uint8_t)v->knob[k];
                changed |= key[k] != pp->key[k];
            }
            if (changed || pp->key[0] != 0xa5) {
                shrp = key[K_SHRP]; mod = key[K_MOD]; detn = key[K_DETN]; fbk = key[K_FBK]; lev = key[K_LEV];
                rta = key[X_RTA]; rtb = key[X_RTB]; vib = key[X_VIB]; dtime = key[X_TIME]; dly = key[X_DLY];
                dmod = key[X_DMOD];
                src = key[K_SRC] < SRCS ? key[K_SRC] : SRCS - 1;
                intv = key[K_INTV] < INTVS ? key[K_INTV] : INTVS - 1;
                logic = key[X_LOGC] < LOGICS ? key[X_LOGC] : LOGICS - 1;
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
            for (k = 0; k < 4; k += LITE ? 2 : 1) {
                if (--s->wait[k] <= 0) {
                    uint32_t r = xorshift(&s->rnd);
                    s->goal[k] = (int32_t)(r >> 17) - 16384;
                    s->goal[k] *= 2;
                    s->wait[k] = 200 + (int32_t)((r >> 3) & 511);       /* a new goal each 0.13-0.47 s */
                }
                s->walk[k] += (s->goal[k] - s->walk[k]) >> 8;
                dev = ((dd * detn_share[LITE && k == 2 ? 3 : k]) >> 4) + ((s->walk[k] * dw) >> 19);  /* LITE: the two widest apart */
                inc[k] = detune(k < 2 ? base : r2, dev) << 1;     /* 24 kHz: twice the step */
            }
        }

        /* The samples: the HYPER LFO first, a block at a time (lyra_hlfo), then the four oscillators and
         * the delay, sample by sample. The state in locals (the stores to out[] would otherwise make the
         * compiler reload it each time), the choices made once a block. */
            {
                uint32_t p0 = s->ph[0], p1 = s->ph[1], p2 = s->ph[2], p3 = s->ph[3];
                int32_t l0 = s->last[0], l1 = s->last[1], l2 = s->last[2], l3 = s->last[3];
                int32_t po = s->po, hs, acc = s->acc, tap0 = s->tap0, tap1 = s->tap1, dt = s->dt, w = s->w, hsv[32];
                int vib_on = vd != 0, shape = g != 256, fm_on = fmd || fbfm;
                int trk = src == S_TRK, pt = t ^ 1;
                int dcb = drv || famt || (amt && (src == S_DLY || trk));
                /* TRK: the partner's voices (the FM source) and its delay (FBK's signal), if it's a LYRA
                 * that's awake; else nothing */
                const int16_t *pv = 0, *pd = 0;
                if (trk && lyra_seen[pt] && lyra_count - lyra_seen[pt] <= 4) {     /* it played within a block */
                    pv = lyra_link_v[pt];
                    pd = lyra_link_d[pt];
                }
                /* the states the nonlinear parts use stay in s: they're touched at 12 kHz or only when
                 * that part is on, and keeping them out of registers keeps the plain path fast */
                int32_t fd = 0, fq0 = s->fq0, fq1 = s->fq1;
                uint32_t c0 = inc[0], c1 = inc[1], c2 = inc[2], c3 = inc[3];    /* the increments, set at 12 kHz */
                if (s->cx <= 0 || s->cx >= 32767)
                    s->cx = 12345;                          /* the logistic map's seed (0 and 1 are its traps) */
                int32_t tgt = LITE ? target >> 1 : target;  /* the time in the line's samples (LITE: 6 kHz) */
                if (dt < (2 << 8))
                    dt = tgt;                               /* a fresh state: no glide from 0 */
                if (LITE)
                    lyra_hlfo8(s, logic, ia << 2, ib << 2, hsv);     /* 12 kHz, each value twice */
                else
                    lyra_hlfo16(s, logic, ia << 1, ib << 1, hsv);    /* 24 kHz */
                for (i = 0; i < 16; i++) {             /* 24 kHz: each sample written twice (below) */
                    int32_t r0, r1, r2, r3, o0, o1, o2, o3, m, d, ph4 = (i & 1) * 2 + 2;
                    /* the delay steps at 12 kHz, LITE's at 6 kHz: dph is where this sample is between two */
                    int32_t dph = LITE ? (i & 3) + 1 : ph4, step = dph == 4;
                    int32_t s0 = 0, s1 = 0, s2 = 0, s3 = 0;
                    uint32_t q0, q1, q2, q3;
                    /* what the delay gives back, interpolated from 12 kHz */
                    d = tap0 + (((tap1 - tap0) * dph) >> 2);
                    hs = hsv[LITE ? i >> 1 : i];            /* the HYPER LFO, from its own pass */
                    /* the FM sources, at audio rate (PM) */
                    if (amt) {
                        if (LITE && src == S_SELF) {
                            s0 = l0; s2 = l2;
                        } else if (LITE && (src == S_PAIR || src == S_CHAIN)) {
                            s0 = l2; s2 = l0;               /* each from the other pair's one */
                        } else switch (src) {
                        case S_SELF:  s0 = l0; s1 = l1; s2 = l2; s3 = l3; break;
                        case S_PAIR:  s0 = s1 = (l2 + l3) >> 1; s2 = s3 = (l0 + l1) >> 1; break;
                        case S_HLFO:  s0 = s1 = s2 = s3 = hs; break;
                        case S_CHAIN: s0 = l1; s1 = l2; s2 = l3; s3 = l0; break;
                        case S_DLY:   s0 = s1 = s2 = s3 = d; break;
                        default:      s0 = s1 = s2 = s3 = pv ? pv[i >> 1] : 0; break;
                        }
                    }
                    if (famt) {                             /* FBK's signal: the delay's output (TRK: the partner's); */
                        if (gfb != 256)                     /* past 64 folded (made at 12 kHz, below) */
                            fd = fq0 + (((fq1 - fq0) * dph) >> 2);
                        else
                            fd = trk ? (pd ? pd[i >> 1] : 0) : clip(d);
                    }
                    /* every fourth sample (12 kHz), the slow part: vibrato and true FM move the increments */
                    if (ph4 == 2) {
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
                            ms[0] += (s0 - ms[0]) >> 5; if (!LITE) ms[1] += (s1 - ms[1]) >> 5;
                            ms[2] += (s2 - ms[2]) >> 5; if (!LITE) ms[3] += (s3 - ms[3]) >> 5;
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
                    m = LITE ? (o0 + o2) >> 1 : (o0 + o1 + o2 + o3) >> 2;           /* the voices, what goes into the delay */
                    if (drv)                                /* DRIVE */
                        m = sat(((m * drv) >> 8) + dbias) - dzero;
                    if (dcb) {                              /* a DC block (~7 Hz, its level moved at 12 kHz): DRIVE is */
                        if (ph4 == 4)                       /* off-centre, and feedback PM (FBK, or MOD from DLY) can */
                            s->dc += (m * 256 - s->dc) >> 8;    /* leave the voices off centre too (0.2: up to 0.14) */
                        m -= s->dc >> 8;
                    }
                    acc += m;
                    if (step) {                             /* a 12 kHz sample (LITE 6 kHz): write one, read one */
                        int32_t in, dd2, ri, f, a, b;
                        in = acc >> (LITE ? 2 : 1);
                        acc = 0;
                        if (gin != 256)                     /* DLY past 64 drives the line's input into a folder */
                            in = fold(in, gin);
                        s->lp += LITE ? in - s->lp : (in - s->lp) >> 1;     /* dark, about 2.5 kHz (LITE: 3 kHz at most) */
                        dt += (tgt - dt) >> (LITE ? 6 : 7); /* TIME glides, so turning it bends the echoes */
                        dd2 = dt;
                        if (dm2)                            /* DMOD: the HYPER LFO moves the time, up to +-50 % */
                            dd2 += ((dt >> 6) * ((hs * dm2) >> 15)) >> 9;
                        if (ds2) {                          /* and past 64 the line's own output, slowed, up to +-50 % more */
                            s->tl += (tap1 - s->tl) >> (LITE ? 5 : 6);
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
                            s->tdc += (tap1 * 256 - s->tdc) >> (LITE ? 8 : 9);   /* signal it reads): block the DC it makes */
                            tap1 -= s->tdc >> 8;
                        }
                        if (gfb != 256) {                   /* FBK past 64: the tap folded, then darkened (~950 Hz) */
                            int32_t xs = trk ? (pd ? pd[i >> 1] : 0) : tap1;   /* as the LYRA's delay is: the */
                            fq0 = fq1;                      /* loop screams instead of turning into hiss */
                            s->fp += (fold(xs, gfb) - s->fp) >> 1;
                            fq1 = s->fp;
                        }
                        /* the loop gets a DC block (~4 Hz): a saturating loop with gain over 1 otherwise
                         * finds a DC level it can hold, latches there and clips everything to silence */
                        in = sat(s->lp + ((tap1 * regen) >> 15));
                        s->ldc += (in * 256 - s->ldc) >> (LITE ? 8 : 9);     /* Q8: a plain integer one stalls up to 511 off */
                        line[w] = (int16_t)clip(in - (s->ldc >> 8));
                        if (++w == DLEN)
                            w = 0;
                        lyra_link_v[t][i >> 1] = (int16_t)clip(m);     /* for the partner's TRK */
                        lyra_link_d[t][i >> 1] = (int16_t)clip(tap1);
                        if (LITE) {                         /* 12 kHz slots: the one before too */
                            lyra_link_v[t][(i >> 1) - 1] = lyra_link_v[t][i >> 1];
                            lyra_link_d[t][(i >> 1) - 1] = lyra_link_d[t][i >> 1];
                        }
                    }
                    {
                        int32_t o = clip((m * dry + d * wet) >> 7);
                        if (drv) {                          /* DRIVE's off-centre peaks, clipped here with the echoes, */
                            if (ph4 == 4)                   /* leave an offset: one more DC block (~7 Hz) */
                                s->odc += (o * 256 - s->odc) >> 8;
                            o = clip(o - (s->odc >> 8));
                        }
                        o = o * gain * 4;                   /* Q15 x Q14 -> Q31 */
                        out[2 * i] = (po >> 1) + (o >> 1);  /* back to 48 kHz: halfway, then the sample */
                        out[2 * i + 1] = po = o;
                    }
                }
                s->ph[0] = p0; s->ph[2] = p2;
                if (!LITE) { s->ph[1] = p1; s->ph[3] = p3; }
                s->last[0] = l0; s->last[2] = l2;
                if (!LITE) { s->last[1] = l1; s->last[3] = l3; }
                s->fq0 = fq0; s->fq1 = fq1;
                s->po = po;
                s->acc = acc; s->tap0 = tap0; s->tap1 = tap1; s->dt = dt; s->w = w;
            }
        
}

static void render(const struct elek_voice *v, elek_sample *out) { lyra_block(v, out, 0); }
static void render_lite(const struct elek_voice *v, elek_sample *out) { lyra_block(v, out, 1); }

/* ---- the pages ----------------------------------------------------------------------------------- */

static const struct elek_knob knobs[KNOBS] = {
    [K_TUNE] = { "TUNE", .min = -24, .max = 24 },
    [K_SHRP] = { "SHRP", .max = 127, .def = 24 },
    [K_MOD]  = { "MOD",  .max = 127 },
    [K_SRC]  = { "SRC",  .max = SRCS - 1 },
    [K_DETN] = { "DETN", .max = 127, .def = 40 },
    [K_INTV] = { "INTV", .max = INTVS - 1 },
    [K_FBK]  = { "FBK",  .max = 127 },
    [K_LEV]  = { "LEV",  .max = 127, .def = 100 },
    [X_RTA]  = { "RT A", .max = 127, .def = 40 },
    [X_RTB]  = { "RT B", .max = 127, .def = 54 },
    [X_LOGC] = { "LOGC", .max = LOGICS - 1 },
    [X_VIB]  = { "VIB",  .max = 127, .def = 24 },
    [X_TIME] = { "TIME", .max = 127, .def = 90 },
    [X_DLY]  = { "DLY",  .max = 127, .def = 40 },
    [X_DMOD] = { "DMOD", .max = 127, .def = 20 },
};

/* the HYPER LFO's rate in Hz: .05 .. 9.9, then whole Hz */
static const char *rate_text(const struct elek_ui *ui, int32_t v)
{
    int32_t oct, f = exp2q(v * 195, &oct), mhz = ((50 * f) >> 13) << oct;
    return mhz < 1000 ? elek_fixed(ui, "", mhz / 10, 2) : mhz < 10000 ? elek_fixed(ui, "", mhz / 100, 1)
                      : elek_num(ui, "", mhz / 1000);
}

static void present(const struct elek_ui *ui)
{
    const int32_t *k = ui->knob;
    ui->view[K_TUNE].value = elek_num(ui, k[K_TUNE] > 0 ? "+" : "", k[K_TUNE]);
    ui->view[K_SRC].value = src_names[k[K_SRC] < 0 ? 0 : k[K_SRC] % SRCS];
    ui->view[K_INTV].value = intv_names[k[K_INTV] < 0 ? 0 : k[K_INTV] % INTVS];
    ui->view[X_LOGC].value = logic_names[k[X_LOGC] < 0 ? 0 : k[X_LOGC] % LOGICS];
    ui->view[X_RTA].value = rate_text(ui, k[X_RTA]);
    ui->view[X_RTB].value = rate_text(ui, k[X_RTB]);
    ui->view[X_TIME].value = elek_num(ui, "", (time_q8(k[X_TIME]) + 6 * 256) / (12 * 256));   /* ms */
}

static const struct elek_machine machine = {
    .id = 90, .name = "LYRA", .short_name = "LYRA", ELEK_KNOBS(knobs),
    .pages = {
        { "LYRA 1/2", { &knobs[K_TUNE], &knobs[K_SHRP], &knobs[K_MOD], &knobs[K_SRC],
                        &knobs[K_DETN], &knobs[K_INTV], &knobs[K_FBK], &knobs[K_LEV] } },
        { "LYRA 2/2", { &knobs[X_RTA], &knobs[X_RTB], &knobs[X_LOGC], &knobs[X_VIB],
                        &knobs[X_TIME], &knobs[X_DLY], &knobs[X_DMOD], &knobs[K_FBK] } },
    },
    .present = present, .render = render,
    .play = ELEK_MONO_LEGATO, .prio = ELEK_PRIO_LAST,
};
ELEK_REGISTER_MACHINE(machine);

/* LYRA LITE: the same knobs, two oscillators */
static const struct elek_machine machine_lite = {
    .id = 91, .name = "LYRA LITE", .short_name = "LYRL", ELEK_KNOBS(knobs),
    .pages = {
        { "LITE 1/2", { &knobs[K_TUNE], &knobs[K_SHRP], &knobs[K_MOD], &knobs[K_SRC],
                        &knobs[K_DETN], &knobs[K_INTV], &knobs[K_FBK], &knobs[K_LEV] } },
        { "LITE 2/2", { &knobs[X_RTA], &knobs[X_RTB], &knobs[X_LOGC], &knobs[X_VIB],
                        &knobs[X_TIME], &knobs[X_DLY], &knobs[X_DMOD], &knobs[K_FBK] } },
    },
    .present = present, .render = render_lite,
    .play = ELEK_MONO_LEGATO, .prio = ELEK_PRIO_LAST,
};
ELEK_REGISTER_MACHINE(machine_lite);
