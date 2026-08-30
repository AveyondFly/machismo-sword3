#ifndef SWORD3_HOST_CHEAT_H
#define SWORD3_HOST_CHEAT_H

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host overlay copied from the Android trainer. Y opens and closes it;
 * while it is open the pad stays on this menu. Money and full HP write
 * through the same Golds cache / ROLE::SetHp path the engine uses.
 * 战后升级 hooks CalLevel; 一击必杀 hooks ROLE::HitDamage. 不遇敌 skips
 * LoadBattle when Lua PlayerMove queued a random fight.
 */
void host_cheat_install(void);
void host_cheat_poll(void);
int host_cheat_active(void);
int host_cheat_button(int button, int down);
void host_cheat_draw(SDL_Renderer *renderer, int logical_w, int logical_h);

#ifdef __cplusplus
}
#endif

#endif
