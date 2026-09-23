#ifndef SWORD3_HOST_CHEAT_NEWPAL_H
#define SWORD3_HOST_CHEAT_NEWPAL_H

/* Entry points recovered from the pinned NewPAL Mach-O executable. */
void host_cheat_newpal_install(void);
int host_cheat_newpal_money(void);
int host_cheat_newpal_heal(void);
void host_cheat_newpal_level_up(int enabled);
void host_cheat_newpal_one_hit(int enabled);
void host_cheat_newpal_no_encounter(int enabled);

#endif
