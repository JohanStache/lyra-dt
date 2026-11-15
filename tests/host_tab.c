/* the firmware's pitch table, built at start-up (kept out of host_render.c, which sees it as const) */
#include <stdint.h>
#include <math.h>
uint32_t fw_pitch_tab[14849];
void host_tab_fill(void)
{
    for (int i = 0; i <= 14848; i++)
        fw_pitch_tab[i] = (uint32_t)llround(pow(2.0, (i - 10752) / 2048.0) * (1 << 29));
}
