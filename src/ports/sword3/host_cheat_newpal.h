#ifndef SWORD3_HOST_CHEAT_NEWPAL_H
#define SWORD3_HOST_CHEAT_NEWPAL_H

#include <stdint.h>

/* Entry points recovered from the pinned NewPAL Mach-O executable. */
/* Runs before the game entry point; later hooks install on first frame. */
void host_newpal_prepare(void);
void host_cheat_newpal_install(void);
int host_newpal_patch_jump(uintptr_t function, void *hook,
			   const uint32_t expected[4]);
int host_cheat_newpal_money(void);
int host_cheat_newpal_heal(void);
int host_cheat_newpal_has_puppet(void);
int host_cheat_newpal_fill_puppet(void);

enum {
	NP_WAKE_LINGER,
	NP_WAKE_YUERU
};
int host_cheat_newpal_wake_scene(void);
int host_cheat_newpal_can_wake(int who);
int host_cheat_newpal_wake(int who);
void host_cheat_newpal_level_up(int enabled);
void host_cheat_newpal_one_hit(int enabled);
void host_cheat_newpal_no_encounter(int enabled);
/* Also skips encounters, including penalty-zone battles. */
void host_cheat_newpal_noclip(int enabled);

#endif
