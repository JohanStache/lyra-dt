#include <stdint.h>
volatile uint8_t fw_render_machine[8];
volatile int16_t fw_voice_params[53 * 8];
volatile int32_t fw_voice_note[8];
volatile uint32_t fw_voice_start;
volatile int32_t fw_amp_env[24];
uint32_t fw_pitch_tab[14849];
const char fw_bitmap_vt[4];
const char cm_ui_v32[4];
int32_t blocks[256];
void lyra_render(int32_t *);
void _start(void) { lyra_render(blocks); for (;;) ; }
