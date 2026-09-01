#ifndef SWORD3_HOST_BATTLE_MENU_H
#define SWORD3_HOST_BATTLE_MENU_H

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host-drawn battle command UI, including magic / item / special submenus.
 * The grid always keeps 攻击/奇术/物品/绝招/防御; extras append after
 * that, and the last cell is 逃跑 (native 战术 is hidden). Native chrome
 * is hidden; native code only carries out the chosen action. Target
 * picking stays native.
 */
void host_battle_install(void);
int host_battle_active(void);
int host_battle_owns_pad(void);
void host_battle_poll(void);
void host_battle_close(void);
int host_battle_button(int button, int down);
int host_battle_axis(Uint8 axis, Sint16 value);
int host_battle_flee(void);
int host_battle_skip_blit(int x, int y, int w, int h);
void host_battle_draw(SDL_Renderer *renderer, int logical_w, int logical_h);

#ifdef __cplusplus
}
#endif

#endif
