#include "elekloader/sdk_alpha_machine.h"
const char sdk_alpha_abi_v1[4];
extern const struct elek_machine *const sdk_alpha_registration_machine;
int32_t kn[4][15];
uint8_t st[4][256];
int32_t cc[128];
int32_t outb[4][32];
uint32_t start_flag, idle_v;
void blockrun(void)
{
    for (int t = 0; t < 4; t++) {
        struct elek_voice v = { kn[t], (uint32_t)t, start_flag, (48 + 7 * t) << 16, 100, st[t], idle_v, cc };
        sdk_alpha_registration_machine->render(&v, outb[t]);
    }
}
void _start(void) { blockrun(); for (;;) ; }
