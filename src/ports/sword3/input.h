#ifndef SWORD3_INPUT_H
#define SWORD3_INPUT_H

#include <SDL2/SDL.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Sword3Input Sword3Input;

typedef struct Sword3InputEvent {
	SDL_Scancode scancode;
	SDL_bool pressed;
} Sword3InputEvent;

typedef void (*Sword3InputCallback)(const Sword3InputEvent *event,
				    void *userdata);

/*
 * The caller owns SDL initialization and the existing game window/event loop.
 * This module never creates a window and never calls SDL_Init or SDL_Quit.
 * All functions for one Sword3Input, including its callback, run on that event
 * loop thread. The callback may poll/query but must not destroy or remap the
 * input object.
 */
Sword3Input *sword3_input_create(Sword3InputCallback callback, void *userdata);
void sword3_input_destroy(Sword3Input *input);

/*
 * Feed events obtained from the game's SDL_PollEvent loop.  Returns one when
 * consumed, zero for unrelated events, and -1 on an allocation/open error.
 */
int sword3_input_handle_event(Sword3Input *input, const SDL_Event *event);

/* Returns one when an event was returned, zero when the queue is empty. */
int sword3_input_poll(Sword3Input *input, Sword3InputEvent *event);
SDL_bool sword3_input_is_down(const Sword3Input *input,
			      SDL_Scancode scancode);
/* Queue releases for every active key; returns -1 without changing state on OOM. */
int sword3_input_release_all(Sword3Input *input);

/* Change mappings after creation. SDL_SCANCODE_UNKNOWN disables a mapping. */
int sword3_input_map_button(Sword3Input *input, SDL_GameControllerButton button,
			    SDL_Scancode scancode);
int sword3_input_map_axis(Sword3Input *input, SDL_GameControllerAxis axis,
			  SDL_Scancode negative, SDL_Scancode positive);

#ifdef __cplusplus
}
#endif

#endif
