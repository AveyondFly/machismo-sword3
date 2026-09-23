#ifndef SWORD3_HOST_CHEAT_H
#define SWORD3_HOST_CHEAT_H

#include <SDL2/SDL.h>

/* Shared trainer overlay; NewPAL currently provides the menu without effects. */
void host_cheat_install(void);
int host_cheat_active(void);
int host_cheat_key(SDL_Scancode scancode, int down, int repeat);
int host_cheat_newpal_event(const SDL_Event *event);
void host_cheat_draw(SDL_Renderer *renderer, int logical_w, int logical_h);

#endif
