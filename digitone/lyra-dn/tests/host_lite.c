/* Renders LYRA (Digitone) on the PC: host_render NOTE BLOCKS k=v ... > raw int32, track 1's voice.
 * NOTE is MIDI; k a knob id 0-14 (sdk ranges), v its value. 97=N: track 2 also plays, N-64 semitones
 * away (TRK's partner); 96=1 writes track 2's block. 94=N/93=G: track 1 is silent from block N*10 for G*10.
 * cc -I <elekloader>/elekloader/sdk/include host_render.c -o lyra_dn_host */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
const char sdk_alpha_abi_v1[4];
#include "../lyra.c"
#define render render_lite
int main(int argc, char **argv)
{
    static int32_t kn[2][15], st[2][64];
    static int32_t cc[128];
    static int32_t out[2][32];
    int note = atoi(argv[1]), blocks = atoi(argv[2]), a, b, t, second = -1000, show2 = 0, gap_at = -1, gap = 0;
    uint32_t idle[2] = {0, 0};
    for (a = 0; a < 128; a++) cc[a] = -1;
    for (t = 0; t < 2; t++) for (a = 0; a < 15; a++) kn[t][a] = knobs[a].def;
    for (a = 3; a < argc; a++) {
        int k = atoi(argv[a]), v = atoi(strchr(argv[a], '=') + 1);
        if (k == 97) { second = v - 64; continue; }
        if (k == 96) { show2 = v; continue; }
        if (k == 94) { gap_at = v * 10; continue; }
        if (k == 93) { gap = v * 10; continue; }
        kn[0][k] = kn[1][k] = v;
    }
    for (b = 0; b < blocks; b++) {
        for (t = 0; t < (second > -1000 ? 2 : 1); t++) {
            struct elek_voice v = { kn[t], (uint32_t)t, b == 0, (note + (t ? second : 0)) << 16, 100, st[t], 0, cc };
            if (t == 0 && gap_at >= 0 && b >= gap_at && b < gap_at + gap) { idle[0] += 32; memset(out[0], 0, 128); continue; }
            v.idle = idle[t]; idle[t] = 0;
            render(&v, out[t]);
        }
        fwrite(out[show2], 4, 32, stdout);
    }
    return 0;
}
