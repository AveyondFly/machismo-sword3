#ifndef SWORD3_HOST_MENU_H
#define SWORD3_HOST_MENU_H

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

int host_menu_active(void);
void host_menu_open(void);
void host_menu_open_book(void);
void host_menu_close(void);
int host_menu_take_pending(int *slot);
int host_menu_button(int button, int down);
int host_menu_axis(Uint8 axis, Sint16 value);
void host_menu_draw(SDL_Renderer *renderer, int logical_w, int logical_h);

#ifdef __cplusplus
}
#endif

#endif
