#ifndef SWORD3_HOST_CHEAT_NEWPAL_H
#define SWORD3_HOST_CHEAT_NEWPAL_H

#include <stdint.h>

/* Entry points recovered from the pinned NewPAL Mach-O executable. */
void host_cheat_newpal_install(void);
int host_newpal_patch_jump(uintptr_t function, void *hook,
			   const uint32_t expected[4]);
int host_cheat_newpal_money(void);
int host_cheat_newpal_heal(void);
void host_cheat_newpal_level_up(int enabled);
void host_cheat_newpal_one_hit(int enabled);
void host_cheat_newpal_no_encounter(int enabled);

#endif
