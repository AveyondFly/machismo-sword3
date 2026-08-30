#ifndef SWORD3_HOST_FONT_H
#define SWORD3_HOST_FONT_H

#include <SDL2/SDL_ttf.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Outline CJK font for host overlays. Skips Latin-only faces so labels
 * do not turn into tofu. Prefers the Android port's CS.ttf when present.
 */
TTF_Font *host_cjk_font(int pt);

#ifdef __cplusplus
}
#endif

#endif
