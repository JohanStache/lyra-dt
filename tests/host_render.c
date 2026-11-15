/* Renders LYRA on the PC: host_render NOTE BLOCKS k=v ... > raw int32 (track 1's block, or track 2's).
 * k is a parameter 0-14 (0-7 the knobs A-H, 8-14 the second page), v 0-127. TUNE stays 0.
 * Test switches:
 *   98=N  the voices stop (not the delay) after N x 10 blocks
 *   99=1  the AMP envelope has died away and there's no trig: LYRA sleeps
 *   97=N  track 2 is a LYRA too, with the same values, N - 64 semitones from track 1 (TRK's partner)
 *   95=v  track 2's SRC is v instead
 *   96=1  write track 2's block instead of track 1's
 *   94=N  track 1 sleeps from block N x 10 ...
 *   93=G  ... for G x 10 blocks, then gets a new trig
 * cc -I <elekloader>/elekloader/sdk/include host_render.c host_tab.c -o lyra_host */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#define LYRA_HOST_MUTE
int lyra_host_mute;
#include "../lyra.c"
volatile uint8_t fw_render_machine[8];
volatile int16_t fw_voice_params[53 * 8];
volatile int32_t fw_voice_note[8];
volatile uint32_t fw_voice_start;
volatile int32_t fw_amp_env[24];
void host_tab_fill(void);
const char fw_bitmap_vt[4];
const char cm_ui_v32[4];

static void put(int t, int k, int v)
{
    volatile int16_t *p = &fw_voice_params[53 * t];
    if (k < 8) p[k] = (int16_t)(v << 8); else p[29 + k - 8] = (int16_t)(v << 8);
}

int main(int argc, char **argv)
{
    int mute_at = -1, asleep = 0, b, a, t, note = atoi(argv[1]), blocks = atoi(argv[2]);
    int second = -1000, src2 = -1, show2 = 0, gap_at = -1, gap = 0;
    static int32_t out[256];
    host_tab_fill();
    for (t = 0; t < 2; t++) {
        for (a = 0; a < 15; a++)                    /* LYRA's defaults, LEV 100 */
            put(t, a, a < 8 ? lyra_ui.base.knob[a].def >> 8 : lyra_ui.extra[a - 8].def >> 8);
        fw_voice_params[53 * t + K_TUNE] = 0x4000;
        put(t, K_LEV, 100);
    }
    for (a = 3; a < argc; a++) {
        int k = atoi(argv[a]), v = atoi(strchr(argv[a], '=') + 1);
        switch (k) {
        case 99: asleep = 1; continue;
        case 98: mute_at = v * 10; continue;
        case 97: second = v - 64; continue;
        case 96: show2 = v; continue;
        case 95: src2 = v; continue;
        case 94: gap_at = v * 10; continue;
        case 93: gap = v * 10; continue;
        }
        put(0, k, v);
        put(1, k, v);
    }
    if (src2 >= 0)
        put(1, K_SRC, src2);
    fw_render_machine[0] = LYRA_ID;
    if (second > -1000)
        fw_render_machine[1] = LYRA_ID;
    fw_voice_note[0] = note << 16;
    fw_voice_note[1] = (note + (second > -1000 ? second : 0)) << 16;
    for (b = 0; b < blocks; b++) {
        int off = asleep || (gap_at >= 0 && b >= gap_at && b < gap_at + gap);
        /* the AMP level: negative, Q31; about -390 once it has died away */
        fw_amp_env[1] = off ? -390 : -0x40000000;
        fw_amp_env[4] = -0x40000000;
        fw_voice_start = (b == 0 && !asleep) || (gap_at >= 0 && b == gap_at + gap) ? 1 : 0;
        if (b == 0 && second > -1000)
            fw_voice_start |= 2;
        lyra_host_mute = mute_at >= 0 && b >= mute_at;
        lyra_render(out);
        fwrite(out + (show2 ? 32 : 0), 4, 32, stdout);
    }
    return 0;
}
