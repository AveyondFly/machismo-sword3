#include "host_prepare.h"

#include <SDL2/SDL.h>
#include <stdio.h>

int sword3_host_prepare(void)
{
	SDL_version linked;
	SDL_version compiled;

	SDL_VERSION(&compiled);
	SDL_GetVersion(&linked);
	if (linked.major != 2) {
		fprintf(stderr,
		        "sword3-host: unsupported SDL runtime %u.%u.%u\n",
		        linked.major, linked.minor, linked.patch);
		return -1;
	}

	/*
	 * The normal iOS SDL UIApplication delegate calls SDL_SetMainReady before
	 * invoking SDL_main.  Sword3 bypasses UIApplicationMain, so reproduce that
	 * one platform-independent contract for the host SDL implementation.
	 */
	SDL_SetMainReady();
	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
	/*
	 * gptokeyb owns the physical pad and injects a uinput keyboard.
	 * Disable mouse/touch synthesis so one physical action does not
	 * become both a finger event and a mouse event.
	 */
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
	setvbuf(stderr, NULL, _IOLBF, 0);
	setvbuf(stdout, NULL, _IOLBF, 0);
	fprintf(stderr,
	        "sword3-host: SDL headers %u.%u.%u, runtime %u.%u.%u, main ready\n",
	        compiled.major, compiled.minor, compiled.patch,
	        linked.major, linked.minor, linked.patch);
	if (getenv("SDL_VIDEODRIVER"))
		fprintf(stderr, "sword3-host: SDL_VIDEODRIVER=%s\n",
		        getenv("SDL_VIDEODRIVER"));
	else
		fprintf(stderr,
		        "sword3-host: SDL_VIDEODRIVER unset; host SDL selects the backend\n");
	fprintf(stderr,
	        "sword3-host: WAYLAND_DISPLAY=%s XDG_RUNTIME_DIR=%s\n",
	        getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(unset)",
	        getenv("XDG_RUNTIME_DIR") ? getenv("XDG_RUNTIME_DIR") : "(unset)");
	return 0;
}
