#ifndef SWORD3_HOST_CHEAT_H
#define SWORD3_HOST_CHEAT_H

#include <SDL2/SDL.h>

/* Paladin 2 host-side trainer overlay. Select opens it; the options
 * intentionally keep only UI state for now, without guest hooks. */
int host_cheat_active(void);
int host_cheat_key(SDL_Scancode scancode, int down, int repeat);
void host_cheat_draw(SDL_Renderer *renderer, int logical_w, int logical_h);

#endif
