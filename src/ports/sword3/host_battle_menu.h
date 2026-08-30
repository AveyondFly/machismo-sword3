#ifndef SWORD3_HOST_BATTLE_MENU_H
#define SWORD3_HOST_BATTLE_MENU_H

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Host-drawn battle command UI, including magic / item / special submenus.
 * Native battle menus are not driven. Native code is only called to carry
 * out the chosen action (attack/target, cast, use item, special, defend).
 * Target picking stays native.
 */
void host_battle_install(void);
int host_battle_active(void);
int host_battle_owns_pad(void);
void host_battle_poll(void);
void host_battle_close(void);
int host_battle_button(int button, int down);
int host_battle_axis(Uint8 axis, Sint16 value);
int host_battle_skip_blit(int x, int y, int w, int h);
void host_battle_draw(SDL_Renderer *renderer, int logical_w, int logical_h);

#ifdef __cplusplus
}
#endif

#endif
