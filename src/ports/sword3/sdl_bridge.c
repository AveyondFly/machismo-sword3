#include "sdl_bridge.h"
#include "audio_bridge.h"

#include <dlfcn.h>
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)0)
#endif
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>

#ifndef SDL_WINDOW_METAL
#define SDL_WINDOW_METAL 0x20000000u
#endif

enum sword3_sdl_kind {
	KIND_WINDOW = 0,
	KIND_RENDERER,
	KIND_TEXTURE,
	KIND_SURFACE,
	KIND_RWOPS,
	KIND_COUNT
};

static const char *kind_name[KIND_COUNT] = {
	"SDL_Window",
	"SDL_Renderer",
	"SDL_Texture",
	"SDL_Surface",
	"SDL_RWops",
};

#define OWNER_CAP 4096

static pthread_mutex_t owner_lock = PTHREAD_MUTEX_INITIALIZER;
static void *owners[KIND_COUNT][OWNER_CAP];
static size_t owner_count[KIND_COUNT];
static int owner_checks_enabled = 1;

static int owner_enabled(void)
{
	static int cached = -1;
	const char *value;

	if (cached >= 0)
		return cached;
	value = getenv("SWORD3_SDL_OWNER_CHECK");
	if (value && strcmp(value, "0") == 0)
		cached = 0;
	else
		cached = 1;
	owner_checks_enabled = cached;
	return cached;
}

static void owner_abort(const char *api, enum sword3_sdl_kind kind, void *pointer)
{
	fprintf(stderr,
		"sword3-sdl: %s received %s %p not created by host SDL; "
		"aborting to avoid mixed-SDL corruption\n",
		api, kind_name[kind], pointer);
	abort();
}

static void owner_register(enum sword3_sdl_kind kind, void *pointer)
{
	if (!pointer || !owner_enabled())
		return;
	pthread_mutex_lock(&owner_lock);
	if (owner_count[kind] >= OWNER_CAP) {
		pthread_mutex_unlock(&owner_lock);
		fprintf(stderr, "sword3-sdl: owner table for %s is full\n",
			kind_name[kind]);
		abort();
	}
	owners[kind][owner_count[kind]++] = pointer;
	pthread_mutex_unlock(&owner_lock);
}

static int owner_contains_locked(enum sword3_sdl_kind kind, void *pointer)
{
	size_t i;

	for (i = 0; i < owner_count[kind]; i++) {
		if (owners[kind][i] == pointer)
			return 1;
	}
	return 0;
}

static int owner_has(enum sword3_sdl_kind kind, void *pointer)
{
	int found;

	if (!pointer)
		return 0;
	pthread_mutex_lock(&owner_lock);
	found = owner_contains_locked(kind, pointer);
	pthread_mutex_unlock(&owner_lock);
	return found;
}

static void owner_skip_unknown(const char *api, enum sword3_sdl_kind kind,
			       void *pointer)
{
	static unsigned skips;

	if (skips < 16) {
		skips++;
		fprintf(stderr,
			"sword3-sdl: %s skip unknown %s %p\n",
			api, kind_name[kind], pointer);
	}
}

static void owner_check(const char *api, enum sword3_sdl_kind kind, void *pointer)
{
	int found;

	if (!pointer || !owner_enabled())
		return;
	pthread_mutex_lock(&owner_lock);
	found = owner_contains_locked(kind, pointer);
	pthread_mutex_unlock(&owner_lock);
	if (!found)
		owner_abort(api, kind, pointer);
}

static int owner_live(const char *api, enum sword3_sdl_kind kind, void *pointer)
{
	if (!pointer || !owner_enabled())
		return 1;
	if (owner_has(kind, pointer))
		return 1;
	owner_skip_unknown(api, kind, pointer);
	return 0;
}

static void owner_unregister(const char *api, enum sword3_sdl_kind kind,
			     void *pointer)
{
	size_t i;

	if (!pointer)
		return;
	if (!owner_enabled())
		return;
	pthread_mutex_lock(&owner_lock);
	for (i = 0; i < owner_count[kind]; i++) {
		if (owners[kind][i] != pointer)
			continue;
		owners[kind][i] = owners[kind][owner_count[kind] - 1];
		owner_count[kind]--;
		pthread_mutex_unlock(&owner_lock);
		return;
	}
	pthread_mutex_unlock(&owner_lock);
	owner_skip_unknown(api, kind, pointer);
}

static void owner_drop(enum sword3_sdl_kind kind, void *pointer)
{
	size_t i;

	if (!pointer)
		return;
	pthread_mutex_lock(&owner_lock);
	for (i = 0; i < owner_count[kind]; i++) {
		if (owners[kind][i] != pointer)
			continue;
		owners[kind][i] = owners[kind][owner_count[kind] - 1];
		owner_count[kind]--;
		break;
	}
	pthread_mutex_unlock(&owner_lock);
}

/*
 * Display policy matches sword3/src/egl_shim.c: the game keeps its native
 * logical size (iOS SWD3 asks 800×600), the real window is fullscreen, and
 * SDL_RenderSetLogicalSize letterboxes.
 *
 * Official iOS input is the touch screen. UIGamePad::Update(SDL_Event*)
 * dispatches SDL_FINGERDOWN/UP/MOTION into InputKeyDown / InputClick; it
 * also has reserved SDL2 mouse / keyboard / joystick / controller paths.
 * Do not stub that function.
 *
 * Pal2's original RPG is a keyboard game (scancode slots + DOS key bitmap).
 * Native SDL controller events switch UIGamePad to mode 4, which fights
 * the keyboard path. The handheld pad is owned by gptokeyb (uinput
 * keyboard, real key-hold). This host must not open the joystick, must
 * strip SDL_INIT_JOYSTICK/GAMECONTROLLER/HAPTIC, and must drop leftover
 * joy/controller events so Pal2 never sees a pad.
 * Mouse motion is dropped: Pal2 treats coordinates vs (320,240) as a
 * held virtual stick. Real touch / mouse buttons still pass through.
 */
#define HOST_JOYSTICK_FLAGS \
	(SDL_INIT_JOYSTICK | SDL_INIT_HAPTIC | SDL_INIT_GAMECONTROLLER)
#define MENU_ITEMS 2
#define MENU_GAME_X 115
#define MENU_GAME_Y0 201
#define MENU_GAME_Y1 258
#define TOUCH_ID ((SDL_TouchID)1)
#define FINGER_ID ((SDL_FingerID)1)
#define GAME_W 640
#define GAME_H 480
#define GUEST_UIGAMEPAD 0x100304e28ull
#define GUEST_SCREEN 0x100319450ull
#define GUEST_MAP_ID 0x1002a99d4ull
#define GUEST_UI_FLAGS 0x1002a9a24ull
#define GUEST_CONTINUE_WIDGET 0x1002a9418ull
#define GUEST_MOUSE_X 0x2470
#define GUEST_MOUSE_Y 0x2474
#define GUEST_INPUT_MODE 0x23d4
#define GUEST_DPAD_SLOT 0x2404
#define GUEST_DPAD_STRIDE 0x18
#define GUEST_WIDGET_X 0x20
#define GUEST_VIEW_ORIGIN 0x1c0
#define GUEST_VIEW_SIZE 0x1c8
#define GUEST_VIEW_SCALE 0x1c
#define GUEST_FINGER_SIZE 0x19c
#define GUEST_FINGER_SCALE 0x1b8
#define GUEST_INPUT_MOUSE 3
#define GUEST_TITLE_MAP 0x1e
#define GUEST_UI_SAVE 2
#define PAL2_SCREEN 0x10049efc0ull
#define PAL2_COMPOSITOR 0x10021f1f0ull
#define PAL2_EVENT_OBJ 0x1004953b0ull
#define PAL2_MOVIE_STATUS 0x100542c50ull

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static int g_logical_w;
static int g_logical_h;
static float g_cursor_x;
static float g_cursor_y;
static int g_cursor_ready;
static int g_menu_item;
static int g_pointer_ui;
static int g_save_ui;
static int g_after_continue;

static const char *ignore_ios_driver(const char *driver_name, const char *ios_name)
{
	if (driver_name && SDL_strcasecmp(driver_name, ios_name) == 0) {
		fprintf(stderr,
			"sword3-sdl: ignoring iOS-only driver '%s'\n",
			driver_name);
		return NULL;
	}
	return driver_name;
}

static Uint32 sanitize_window_flags(Uint32 flags)
{
	if (flags & (SDL_WINDOW_METAL | SDL_WINDOW_VULKAN)) {
		fprintf(stderr,
			"sword3-sdl: dropping Metal/Vulkan window flags 0x%x\n",
			flags);
		flags &= ~(SDL_WINDOW_METAL | SDL_WINDOW_VULKAN);
	}
	if (!(flags & SDL_WINDOW_OPENGL)) {
		flags |= SDL_WINDOW_OPENGL;
		fprintf(stderr, "sword3-sdl: adding SDL_WINDOW_OPENGL\n");
	}
	flags |= SDL_WINDOW_SHOWN;
	return flags;
}

static void parse_logical_override(int *w, int *h)
{
	const char *env;
	int lw;
	int lh;

	env = getenv("SWORD3_LOGICAL");
	if (!env || !*env)
		env = getenv("SWORD3_RES");
	if (env && sscanf(env, "%dx%d", &lw, &lh) == 2 && lw > 0 && lh > 0) {
		fprintf(stderr, "sword3-sdl: logical override %dx%d\n", lw, lh);
		*w = lw;
		*h = lh;
	}
}

static void apply_logical_size(SDL_Renderer *renderer)
{
	if (!renderer || g_logical_w <= 0 || g_logical_h <= 0)
		return;
#ifdef SDL_HINT_RENDER_LOGICAL_SIZE_MODE
	SDL_SetHint(SDL_HINT_RENDER_LOGICAL_SIZE_MODE, "letterbox");
#endif
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "linear");
	if (SDL_RenderSetLogicalSize(renderer, g_logical_w, g_logical_h) != 0)
		fprintf(stderr, "sword3-sdl: RenderSetLogicalSize %dx%d failed (%s)\n",
			g_logical_w, g_logical_h, SDL_GetError());
	else
		fprintf(stderr, "sword3-sdl: logical %dx%d letterbox\n",
			g_logical_w, g_logical_h);
}

#define HOST_MOUSE_WHICH 0x53574d33u

static void scale_pointer_event(SDL_Event *event)
{
	float lx;
	float ly;
	int x;
	int y;

	if (!g_renderer || g_logical_w <= 0 || g_logical_h <= 0)
		return;
	switch (event->type) {
	case SDL_MOUSEMOTION:
		if (event->motion.which == HOST_MOUSE_WHICH)
			return;
		x = event->motion.x;
		y = event->motion.y;
		break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
		if (event->button.which == HOST_MOUSE_WHICH)
			return;
		x = event->button.x;
		y = event->button.y;
		break;
	default:
		return;
	}
	SDL_RenderWindowToLogical(g_renderer, x, y, &lx, &ly);
	if (event->type == SDL_MOUSEMOTION) {
		event->motion.x = (Sint32)lx;
		event->motion.y = (Sint32)ly;
	} else {
		event->button.x = (Sint32)lx;
		event->button.y = (Sint32)ly;
	}
}

static void clamp_cursor(void)
{
	if (g_logical_w <= 0 || g_logical_h <= 0)
		return;
	if (g_cursor_x < 1.0f)
		g_cursor_x = 1.0f;
	if (g_cursor_y < 1.0f)
		g_cursor_y = 1.0f;
	if (g_cursor_x > (float)(g_logical_w - 1))
		g_cursor_x = (float)(g_logical_w - 1);
	if (g_cursor_y > (float)(g_logical_h - 1))
		g_cursor_y = (float)(g_logical_h - 1);
}

static void warp_menu_item(int item)
{
	int gy;
	int changed;

	if (item < 0)
		item = 0;
	if (item >= MENU_ITEMS)
		item = MENU_ITEMS - 1;
	changed = (g_menu_item != item);
	g_menu_item = item;
	gy = item ? MENU_GAME_Y1 : MENU_GAME_Y0;
	if (g_logical_w > 0 && g_logical_h > 0) {
		g_cursor_x = (float)MENU_GAME_X * (float)g_logical_w /
			     (float)GAME_W;
		g_cursor_y = (float)gy * (float)g_logical_h / (float)GAME_H;
	}
	clamp_cursor();
	g_cursor_ready = 1;
	if (changed) {
		static unsigned warp_seen;
		if (warp_seen < 16) {
			warp_seen++;
			fprintf(stderr,
				"sword3-sdl: menu item %d cursor %.0f,%.0f\n",
				g_menu_item, g_cursor_x, g_cursor_y);
		}
	}
}

static void ensure_cursor(void)
{
	if (g_cursor_ready || g_logical_w <= 0 || g_logical_h <= 0)
		return;
	warp_menu_item(0);
}

static void pal2_log_viewport(void)
{
	static int logged;
	uint8_t *screen = (uint8_t *)(uintptr_t)PAL2_SCREEN;
	volatile int *origin = (volatile int *)(screen + GUEST_VIEW_ORIGIN);
	volatile int *size = (volatile int *)(screen + GUEST_VIEW_SIZE);
	volatile float *scale = (volatile float *)(screen + GUEST_VIEW_SCALE);
	volatile int *finger_size = (volatile int *)(screen + GUEST_FINGER_SIZE);
	volatile float *finger_scale = (volatile float *)(screen + GUEST_FINGER_SCALE);
	int lw = g_logical_w > 0 ? g_logical_w : GAME_W;
	int lh = g_logical_h > 0 ? g_logical_h : GAME_H;

	if (finger_size[0] <= 0 || finger_size[1] <= 0) {
		finger_size[0] = lw;
		finger_size[1] = lh;
	}
	if (finger_scale[0] == 0.0f && finger_scale[1] == 0.0f) {
		finger_scale[0] = 1.0f;
		finger_scale[1] = 1.0f;
	}
	if (!logged) {
		logged = 1;
		fprintf(stderr,
			"sword3-sdl: Pal2 view origin %d,%d size %dx%d scale %.3f,%.3f finger %dx%d\n",
			origin[0], origin[1], size[0], size[1], scale[0],
			scale[1], finger_size[0], finger_size[1]);
	}
}

static int guest_data_ok(uintptr_t addr)
{
	static int logged;

	/*
	 * Sword3 BSS pokes (GUEST_SCREEN / GUEST_UIGAMEPAD / ...) sit inside
	 * Pal2's real __DATA. Writing them corrupts engine state and can
	 * leave the compositor's present-enable flag cleared.
	 */
	(void)addr;
	if (!logged) {
		logged = 1;
		fprintf(stderr, "sword3-sdl: Pal2 guest BSS pokes disabled\n");
	}
	return 0;
}

static void maybe_init_guest_viewport(uint8_t *screen)
{
	static int logged;
	volatile int *origin = (volatile int *)(screen + GUEST_VIEW_ORIGIN);
	volatile int *size = (volatile int *)(screen + GUEST_VIEW_SIZE);
	volatile float *scale = (volatile float *)(screen + GUEST_VIEW_SCALE);
	volatile int *finger_size = (volatile int *)(screen + GUEST_FINGER_SIZE);
	volatile float *finger_scale = (volatile float *)(screen + GUEST_FINGER_SCALE);
	int lw = g_logical_w > 0 ? g_logical_w : 800;
	int lh = g_logical_h > 0 ? g_logical_h : 600;

	if (size[0] > 0 && size[1] > 0)
		return;
	origin[0] = 0;
	origin[1] = 0;
	size[0] = lw;
	size[1] = lh;
	scale[0] = (float)GAME_W / (float)lw;
	scale[1] = (float)GAME_H / (float)lh;
	if (finger_size[0] <= 0 || finger_size[1] <= 0) {
		finger_size[0] = lw;
		finger_size[1] = lh;
	}
	if (finger_scale[0] == 0.0f && finger_scale[1] == 0.0f) {
		finger_scale[0] = 1.0f;
		finger_scale[1] = 1.0f;
	}
	if (!logged) {
		logged = 1;
		fprintf(stderr,
			"sword3-sdl: guest viewport %dx%d scale %.3f,%.3f\n",
			lw, lh, scale[0], scale[1]);
	}
}

static void sync_ui_mode(void)
{
	if (!g_pointer_ui)
		return;
	g_pointer_ui = 0;
	g_save_ui = 0;
	g_after_continue = 0;
}

static void sync_game_pointer(void)
{
	static int logged;
	uint8_t *pad;
	uint8_t *screen;
	int mx;
	int my;
	int lw;
	int lh;

	if (!guest_data_ok(GUEST_UIGAMEPAD) || !guest_data_ok(GUEST_SCREEN))
		return;
	pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
	screen = (uint8_t *)(uintptr_t)GUEST_SCREEN;
	maybe_init_guest_viewport(screen);
	if (!g_pointer_ui || !g_cursor_ready)
		return;
	lw = g_logical_w > 0 ? g_logical_w : GAME_W;
	lh = g_logical_h > 0 ? g_logical_h : GAME_H;
	mx = (int)(g_cursor_x * (float)GAME_W / (float)lw);
	my = (int)(g_cursor_y * (float)GAME_H / (float)lh);
	if (mx < 1)
		mx = 1;
	if (my < 1)
		my = 1;
	if (mx > GAME_W - 1)
		mx = GAME_W - 1;
	if (my > GAME_H - 1)
		my = GAME_H - 1;
	*(volatile int *)(pad + GUEST_MOUSE_X) = mx;
	*(volatile int *)(pad + GUEST_MOUSE_Y) = my;
	*(volatile int *)(pad + GUEST_INPUT_MODE) = GUEST_INPUT_MOUSE;
	if (!logged) {
		logged = 1;
		fprintf(stderr,
			"sword3-sdl: guest MouseXY %d,%d (cursor %.0f,%.0f)\n",
			mx, my, g_cursor_x, g_cursor_y);
	}
}

static void tick_ios_display_links(void)
{
	static void (*tick)(void);
	static int resolved;

	if (!resolved) {
		resolved = 1;
		tick = (void (*)(void))dlsym(RTLD_DEFAULT,
					     "sword3_ios_tick_display_links");
	}
	if (tick)
		tick();
}

/*
 * Pal2's SDLThread_Loop ticks (0x1000052ec) then presents from the idle
 * path (0x1001ef694 -> 0x10021f1f0). iOS SDL normally also drives that
 * compositor from CADisplayLink; CreateWindow is hooked so the link never
 * starts.
 *
 * Do not call the game tick from the host pump: the loop already runs it
 * once after draining PollEvent, and a second tick while a key is held
 * makes save-slot UIs advance twice per tap.
 *
 * Do not present from the host while the idle path is running either.
 * Present-without-tick shows the previous frame, then the loop ticks and
 * presents the new one — that alternation is the flicker. Host-present
 * only when event flag bit 5 (0x20) skips idle present (splash / movie).
 */

/*
 * Pal2's splash (SS_DOMO) sets event flag bit 5 (0x20) and a non-zero pause
 * field, then waits for its AVAudioPlayer to finish. The AVAudioPlayer shim
 * now immediately calls audioPlayerDidFinishPlaying:successfully: on play,
 * which should clear the movie state naturally. Keep this as a fallback.
 */
static void pal2_force_advance_splash(void)
{
	static int attempts;
	volatile uint8_t *flags =
		(volatile uint8_t *)(uintptr_t)PAL2_EVENT_OBJ;
	volatile uint32_t *pause =
		(volatile uint32_t *)(uintptr_t)(PAL2_EVENT_OBJ + 4);
	volatile uint8_t *movie_status =
		(volatile uint8_t *)(uintptr_t)PAL2_MOVIE_STATUS;

	if (!g_renderer)
		return;
	attempts++;

	/*
	 * The game's splash plays a sequence of movies (events 0x61, 0x62,
	 * ...). Each movie sets bit 5 of the event flags and a non-zero
	 * pause, then waits for the movie to finish. Our AVPlayer/AVAudioPlayer
	 * shims don't drive the movie state machine, so the movie status
	 * byte at 0x100542c50 stays non-zero and the movie never completes.
	 *
	 * Periodically force the movie status to 0 so the movie update calls
	 * stop_movie, clears bit 5, and lets the game advance to the next
	 * event in the sequence. Do NOT set bit 2 or clear pause manually —
	 * let the game's own state machine handle that.
	 */
	if (!(*flags & 0x20u) || *pause == 0u)
		return;
	if (*movie_status == 0u)
		return;
	if (attempts < 30)
		return;

	fprintf(stderr,
		"sword3-sdl: Pal2 forcing movie stop (flags=0x%x pause=%u movie_status=%u)\n",
		*flags, *pause, *movie_status);
	*movie_status = 0;
	attempts = 0;
}

static int pal2_idle_present_skipped(void)
{
	volatile uint8_t *flags =
		(volatile uint8_t *)(uintptr_t)PAL2_EVENT_OBJ;

	return (*flags & 0x20u) != 0;
}

static void pal2_kick_present(void)
{
	uint8_t *screen = (uint8_t *)(uintptr_t)PAL2_SCREEN;
	void *gfx;
	SDL_Renderer *renderer;
	static int reentrant;
	static unsigned seen;
	static Uint32 last;
	static int logged_null;
	static int logged_mismatch;
	static int logged_idle;
	Uint32 now;
	void (*present)(void *);

	if (reentrant || !g_renderer)
		return;
	if (!screen[0x160]) {
		screen[0x160] = 1;
		fprintf(stderr,
			"sword3-sdl: Pal2 compositor enable flag was 0; set\n");
	}
	pal2_force_advance_splash();
	if (!pal2_idle_present_skipped()) {
		if (!logged_idle) {
			logged_idle = 1;
			fprintf(stderr,
				"sword3-sdl: Pal2 idle present owns the compositor\n");
		}
		return;
	}
	now = SDL_GetTicks();
	if (last != 0 && now - last < 16)
		return;
	last = now;
	gfx = *(void **)screen;
	if (!gfx) {
		if (!logged_null && now > 2000) {
			logged_null = 1;
			fprintf(stderr,
				"sword3-sdl: Pal2 screen object gfx pointer is null\n");
		}
		return;
	}
	renderer = *(SDL_Renderer **)((uint8_t *)gfx + 8);
	if (renderer != g_renderer) {
		if (!logged_mismatch) {
			logged_mismatch = 1;
			fprintf(stderr,
				"sword3-sdl: Pal2 compositor renderer %p != host %p\n",
				renderer, g_renderer);
		}
		return;
	}
	pal2_log_viewport();
	reentrant = 1;
	if (seen < 8) {
		seen++;
		fprintf(stderr, "sword3-sdl: Pal2 compositor kick #%u\n", seen);
	}
	present = (void (*)(void *))(uintptr_t)PAL2_COMPOSITOR;
	present(screen);
	reentrant = 0;
}

static void apply_pad_pointer(void)
{
	pal2_log_viewport();
	sync_ui_mode();
	if (g_pointer_ui) {
		ensure_cursor();
		sync_game_pointer();
	}
}

static int is_guest_pad_event(Uint32 type)
{
	return type >= SDL_JOYAXISMOTION && type < SDL_FINGERDOWN;
}

static int rewrite_event(SDL_Event *event)
{
	if (!event)
		return 0;
	scale_pointer_event(event);
	if (is_guest_pad_event(event->type) || event->type == SDL_MOUSEMOTION)
		return 0;
	/* iOS SDL does not auto-repeat keys; Linux does. Save/list UIs
	 * treat each KEYDOWN as one step, so drop host repeats. */
	if (event->type == SDL_KEYDOWN && event->key.repeat)
		return 0;
	if (event->type == SDL_MOUSEBUTTONDOWN ||
	    event->type == SDL_MOUSEBUTTONUP) {
		if (event->button.which != HOST_MOUSE_WHICH) {
			g_cursor_x = (float)event->button.x;
			g_cursor_y = (float)event->button.y;
			g_cursor_ready = 1;
			clamp_cursor();
		}
		return 1;
	}
	if (event->type == SDL_FINGERDOWN || event->type == SDL_FINGERUP ||
	    event->type == SDL_FINGERMOTION) {
		if (g_logical_w > 0 && g_logical_h > 0) {
			g_cursor_x = event->tfinger.x * (float)g_logical_w;
			g_cursor_y = event->tfinger.y * (float)g_logical_h;
			g_cursor_ready = 1;
			clamp_cursor();
		}
		return 1;
	}
	return 1;
}

static int next_translated_event(SDL_Event *event, int timeout)
{
	Uint32 start = SDL_GetTicks();
	int result;
	int slice;
	int blocking = timeout < 0;

	for (;;) {
		tick_ios_display_links();
		pal2_kick_present();
		apply_pad_pointer();
		if (timeout == 0)
			slice = 0;
		else if (blocking)
			slice = 16;
		else {
			int left = timeout - (int)(SDL_GetTicks() - start);

			if (left <= 0)
				return 0;
			slice = left > 16 ? 16 : left;
		}
		result = SDL_WaitEventTimeout(event, slice);
		if (result <= 0) {
			if (timeout == 0 ||
			    (!blocking &&
			     (int)(SDL_GetTicks() - start) >= timeout)) {
				return 0;
			}
			continue;
		}
		if (rewrite_event(event))
			return 1;
	}
}

int sword3_ret0(void)
{
	return 0;
}

int sword3_SDL_InitSubSystem(Uint32 flags)
{
	Uint32 requested = flags;
	Uint32 host_flags = flags & ~(Uint32)HOST_JOYSTICK_FLAGS;
	int result;

	if (requested & HOST_JOYSTICK_FLAGS)
		fprintf(stderr,
			"sword3-sdl: dropping joystick init 0x%x (gptokeyb owns the pad)\n",
			requested & (Uint32)HOST_JOYSTICK_FLAGS);
	result = host_flags ? SDL_InitSubSystem(host_flags) : 0;
	fprintf(stderr, "sword3-sdl: SDL_InitSubSystem(0x%x) -> %d (%s)\n",
		requested, result, result == 0 ? "ok" : SDL_GetError());
	return result;
}

int sword3_SDL_VideoInit(const char *driver_name)
{
	const char *requested = driver_name;
	const char *active;
	int result;

	SDL_SetHint(SDL_HINT_VIDEO_MINIMIZE_ON_FOCUS_LOSS, "0");
	SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
	SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
	driver_name = ignore_ios_driver(driver_name, "uikit");
	result = SDL_VideoInit(driver_name);
	active = SDL_GetCurrentVideoDriver();
	fprintf(stderr,
		"sword3-sdl: SDL_VideoInit(%s) -> %d driver=%s (%s)\n",
		requested ? requested : "NULL", result,
		active ? active : "(none)",
		result == 0 ? "ok" : SDL_GetError());
	return result;
}

void sword3_SDL_VideoQuit(void)
{
	fprintf(stderr, "sword3-sdl: SDL_VideoQuit -> exit\n");
	_exit(0);
}

int sword3_SDL_AudioInit(const char *driver_name)
{
	const char *requested = driver_name;
	const char *active;
	int result;

	driver_name = ignore_ios_driver(driver_name, "coreaudio");
	result = SDL_AudioInit(driver_name);
	active = SDL_GetCurrentAudioDriver();
	fprintf(stderr,
		"sword3-sdl: SDL_AudioInit(%s) -> %d driver=%s (%s)\n",
		requested ? requested : "NULL", result,
		active ? active : "(none)",
		result == 0 ? "ok" : SDL_GetError());
	return result;
}

void sword3_SDL_AudioQuit(void)
{
	fprintf(stderr, "sword3-sdl: SDL_AudioQuit\n");
	SDL_AudioQuit();
}

const char *sword3_SDL_GetError(void)
{
	return SDL_GetError();
}

Uint32 sword3_SDL_GetTicks(void)
{
	return SDL_GetTicks();
}

SDL_Window *sword3_SDL_CreateWindow(const char *title, int x, int y, int w,
				    int h, Uint32 flags)
{
	SDL_Window *window;
	const char *driver;
	SDL_DisplayMode mode;
	int disp_w = w;
	int disp_h = h;
	int actual_w = 0;
	int actual_h = 0;
	Uint32 try_flags;

	(void)x;
	(void)y;
	g_logical_w = w;
	g_logical_h = h;
	parse_logical_override(&g_logical_w, &g_logical_h);
	if (SDL_GetCurrentDisplayMode(0, &mode) == 0 && mode.w > 0 &&
	    mode.h > 0) {
		disp_w = mode.w;
		disp_h = mode.h;
	}
	flags = sanitize_window_flags(flags);
	try_flags = flags | SDL_WINDOW_FULLSCREEN_DESKTOP;
	window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
				  SDL_WINDOWPOS_CENTERED, disp_w, disp_h,
				  try_flags);
	if (!window) {
		fprintf(stderr,
			"sword3-sdl: FULLSCREEN_DESKTOP failed (%s); trying exclusive\n",
			SDL_GetError());
		try_flags = flags | SDL_WINDOW_FULLSCREEN;
		window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED,
					  SDL_WINDOWPOS_CENTERED, disp_w,
					  disp_h, try_flags);
	}
	if (window)
		SDL_GetWindowSize(window, &actual_w, &actual_h);
	driver = SDL_GetCurrentVideoDriver();
	fprintf(stderr,
		"sword3-sdl: CreateWindow asked %dx%d logical %dx%d actual %dx%d flags=0x%x driver=%s -> %p%s%s\n",
		w, h, g_logical_w, g_logical_h, actual_w, actual_h, try_flags,
		driver ? driver : "(none)", window, window ? "" : " ",
		window ? "" : SDL_GetError());
	g_window = window;
	if (!g_cursor_ready && g_logical_w > 0 && g_logical_h > 0)
		warp_menu_item(0);
	owner_register(KIND_WINDOW, window);
	return window;
}

void sword3_SDL_DestroyWindow(SDL_Window *window)
{
	owner_check("SDL_DestroyWindow", KIND_WINDOW, window);
	fprintf(stderr, "sword3-sdl: ignore DestroyWindow on port window\n");
}

void sword3_SDL_SetWindowSize(SDL_Window *window, int w, int h)
{
	owner_check("SDL_SetWindowSize", KIND_WINDOW, window);
	g_logical_w = w;
	g_logical_h = h;
	parse_logical_override(&g_logical_w, &g_logical_h);
	apply_logical_size(g_renderer);
	fprintf(stderr, "sword3-sdl: SetWindowSize %dx%d -> logical %dx%d\n",
		w, h, g_logical_w, g_logical_h);
}

void sword3_SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
	static int logged;

	if (window)
		owner_check("SDL_GetWindowSize", KIND_WINDOW, window);
	if (g_logical_w > 0 && g_logical_h > 0) {
		if (w)
			*w = g_logical_w;
		if (h)
			*h = g_logical_h;
		if (!logged) {
			logged = 1;
			fprintf(stderr,
				"sword3-sdl: GetWindowSize -> logical %dx%d\n",
				g_logical_w, g_logical_h);
		}
		return;
	}
	SDL_GetWindowSize(window, w, h);
}

void sword3_SDL_ShowWindow(SDL_Window *window)
{
	owner_check("SDL_ShowWindow", KIND_WINDOW, window);
	SDL_ShowWindow(window);
}

int sword3_SDL_SetWindowDisplayMode(SDL_Window *window,
				    const SDL_DisplayMode *mode)
{
	owner_check("SDL_SetWindowDisplayMode", KIND_WINDOW, window);
	return SDL_SetWindowDisplayMode(window, mode);
}

SDL_Renderer *sword3_SDL_CreateRenderer(SDL_Window *window, int index,
					Uint32 flags)
{
	SDL_Renderer *renderer;
	SDL_RendererInfo info;
	const char *name = "(none)";

	owner_check("SDL_CreateRenderer", KIND_WINDOW, window);
	renderer = SDL_CreateRenderer(window, index, flags);
	if (!renderer) {
		fprintf(stderr,
			"sword3-sdl: CreateRenderer index=%d flags=0x%x failed (%s); "
			"trying software\n",
			index, flags, SDL_GetError());
		renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
	}
	if (!renderer) {
		fprintf(stderr,
			"sword3-sdl: software renderer failed (%s); trying default\n",
			SDL_GetError());
		renderer = SDL_CreateRenderer(window, -1, 0);
	}
	if (renderer && SDL_GetRendererInfo(renderer, &info) == 0)
		name = info.name;
	fprintf(stderr, "sword3-sdl: CreateRenderer -> %p driver=%s%s%s\n",
		renderer, name, renderer ? "" : " ",
		renderer ? "" : SDL_GetError());
	g_renderer = renderer;
	apply_logical_size(renderer);
	owner_register(KIND_RENDERER, renderer);
	return renderer;
}

void sword3_SDL_DestroyRenderer(SDL_Renderer *renderer)
{
	(void)renderer;
	fprintf(stderr, "sword3-sdl: DestroyRenderer -> exit\n");
	_exit(0);
}

int sword3_SDL_RenderSetLogicalSize(SDL_Renderer *renderer, int w, int h)
{
	int use_w = g_logical_w > 0 ? g_logical_w : w;
	int use_h = g_logical_h > 0 ? g_logical_h : h;

	owner_check("SDL_RenderSetLogicalSize", KIND_RENDERER, renderer);
	if (w != use_w || h != use_h)
		fprintf(stderr,
			"sword3-sdl: RenderSetLogicalSize %dx%d -> logical %dx%d\n",
			w, h, use_w, use_h);
	return SDL_RenderSetLogicalSize(renderer, use_w, use_h);
}

int sword3_SDL_RenderSetScale(SDL_Renderer *renderer, float scaleX,
			      float scaleY)
{
	owner_check("SDL_RenderSetScale", KIND_RENDERER, renderer);
	return SDL_RenderSetScale(renderer, scaleX, scaleY);
}

int sword3_SDL_SetRenderDrawBlendMode(SDL_Renderer *renderer,
				      SDL_BlendMode blendMode)
{
	owner_check("SDL_SetRenderDrawBlendMode", KIND_RENDERER, renderer);
	return SDL_SetRenderDrawBlendMode(renderer, blendMode);
}

int sword3_SDL_SetRenderDrawColor(SDL_Renderer *renderer, Uint8 r, Uint8 g,
				  Uint8 b, Uint8 a)
{
	static unsigned seen;

	owner_check("SDL_SetRenderDrawColor", KIND_RENDERER, renderer);
	if (seen < 8) {
		seen++;
		fprintf(stderr, "sword3-sdl: SetRenderDrawColor rgba=%u,%u,%u,%u\n",
			r, g, b, a);
	}
	return SDL_SetRenderDrawColor(renderer, r, g, b, a);
}

int sword3_SDL_RenderClear(SDL_Renderer *renderer)
{
	static unsigned seen;

	owner_check("SDL_RenderClear", KIND_RENDERER, renderer);
	if (seen < 5) {
		seen++;
		fprintf(stderr, "sword3-sdl: RenderClear\n");
	}
	return SDL_RenderClear(renderer);
}

int sword3_SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
			  const SDL_Rect *srcrect, const SDL_Rect *dstrect)
{
	owner_check("SDL_RenderCopy", KIND_RENDERER, renderer);
	if (!owner_live("SDL_RenderCopy", KIND_TEXTURE, texture))
		return -1;
	return SDL_RenderCopy(renderer, texture, srcrect, dstrect);
}

int sword3_SDL_RenderCopyF(SDL_Renderer *renderer, SDL_Texture *texture,
			   const SDL_Rect *srcrect, const SDL_FRect *dstrect)
{
	static unsigned seen;
	int result;

	owner_check("SDL_RenderCopyF", KIND_RENDERER, renderer);
	if (!owner_live("SDL_RenderCopyF", KIND_TEXTURE, texture))
		return -1;
	result = SDL_RenderCopyF(renderer, texture, srcrect, dstrect);
	if (seen < 12 || result != 0) {
		if (seen < 12)
			seen++;
		fprintf(stderr,
			"sword3-sdl: RenderCopyF tex=%p dst=%s%.1fx%.1f -> %d%s%s\n",
			texture,
			dstrect ? "" : "full ",
			dstrect ? (double)dstrect->w : 0.0,
			dstrect ? (double)dstrect->h : 0.0, result,
			result == 0 ? "" : " ",
			result == 0 ? "" : SDL_GetError());
	}
	return result;
}

int sword3_SDL_RenderCopyEx(SDL_Renderer *renderer, SDL_Texture *texture,
			    const SDL_Rect *srcrect, const SDL_Rect *dstrect,
			    const double angle, const SDL_Point *center,
			    const SDL_RendererFlip flip)
{
	owner_check("SDL_RenderCopyEx", KIND_RENDERER, renderer);
	if (!owner_live("SDL_RenderCopyEx", KIND_TEXTURE, texture))
		return -1;
	return SDL_RenderCopyEx(renderer, texture, srcrect, dstrect, angle,
				center, flip);
}

int sword3_SDL_RenderCopyExF(SDL_Renderer *renderer, SDL_Texture *texture,
			     const SDL_Rect *srcrect, const SDL_FRect *dstrect,
			     const double angle, const SDL_FPoint *center,
			     const SDL_RendererFlip flip)
{
	static unsigned seen;
	int result;

	owner_check("SDL_RenderCopyExF", KIND_RENDERER, renderer);
	if (!owner_live("SDL_RenderCopyExF", KIND_TEXTURE, texture))
		return -1;
	result = SDL_RenderCopyExF(renderer, texture, srcrect, dstrect, angle,
				   center, flip);
	if (seen < 8 || result != 0) {
		if (seen < 8)
			seen++;
		fprintf(stderr,
			"sword3-sdl: RenderCopyExF tex=%p dst=%s%.1fx%.1f angle=%.1f -> %d%s%s\n",
			texture,
			dstrect ? "" : "full ",
			dstrect ? (double)dstrect->w : 0.0,
			dstrect ? (double)dstrect->h : 0.0, angle, result,
			result == 0 ? "" : " ",
			result == 0 ? "" : SDL_GetError());
	}
	return result;
}

void sword3_SDL_RenderPresent(SDL_Renderer *renderer)
{
	static unsigned seen;
	int x, y;
	Uint8 r, g, b, a;
	SDL_BlendMode blend;
	SDL_Rect arm_h, arm_v;

	owner_check("SDL_RenderPresent", KIND_RENDERER, renderer);
	apply_pad_pointer();
	seen++;
	if (seen <= 8 || (seen % 120) == 0)
		fprintf(stderr, "sword3-sdl: RenderPresent #%u\n", seen);
	if (g_pointer_ui && g_cursor_ready && g_logical_w > 0 &&
	    SDL_GetRenderTarget(renderer) == NULL) {
		x = (int)g_cursor_x;
		y = (int)g_cursor_y;
		SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
		SDL_GetRenderDrawBlendMode(renderer, &blend);
		SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
		SDL_SetRenderDrawColor(renderer, 0, 0, 0, 230);
		arm_h.x = x - 11;
		arm_h.y = y - 2;
		arm_h.w = 23;
		arm_h.h = 5;
		arm_v.x = x - 2;
		arm_v.y = y - 11;
		arm_v.w = 5;
		arm_v.h = 23;
		SDL_RenderFillRect(renderer, &arm_h);
		SDL_RenderFillRect(renderer, &arm_v);
		SDL_SetRenderDrawColor(renderer, 255, 220, 64, 255);
		arm_h.x = x - 9;
		arm_h.y = y - 1;
		arm_h.w = 19;
		arm_h.h = 3;
		arm_v.x = x - 1;
		arm_v.y = y - 9;
		arm_v.w = 3;
		arm_v.h = 19;
		SDL_RenderFillRect(renderer, &arm_h);
		SDL_RenderFillRect(renderer, &arm_v);
		SDL_SetRenderDrawColor(renderer, r, g, b, a);
		SDL_SetRenderDrawBlendMode(renderer, blend);
	}
	SDL_RenderPresent(renderer);
}

int sword3_SDL_RenderDrawPointsF(SDL_Renderer *renderer,
				 const SDL_FPoint *points, int count)
{
	owner_check("SDL_RenderDrawPointsF", KIND_RENDERER, renderer);
	return SDL_RenderDrawPointsF(renderer, points, count);
}

int sword3_SDL_RenderFillRectsF(SDL_Renderer *renderer, const SDL_FRect *rects,
				int count)
{
	owner_check("SDL_RenderFillRectsF", KIND_RENDERER, renderer);
	return SDL_RenderFillRectsF(renderer, rects, count);
}

int sword3_SDL_SetRenderTarget(SDL_Renderer *renderer, SDL_Texture *texture)
{
	owner_check("SDL_SetRenderTarget", KIND_RENDERER, renderer);
	if (!owner_live("SDL_SetRenderTarget", KIND_TEXTURE, texture))
		return -1;
	return SDL_SetRenderTarget(renderer, texture);
}

int sword3_SDL_GetRendererOutputSize(SDL_Renderer *renderer, int *w, int *h)
{
	owner_check("SDL_GetRendererOutputSize", KIND_RENDERER, renderer);
	if (g_logical_w > 0 && g_logical_h > 0) {
		if (w)
			*w = g_logical_w;
		if (h)
			*h = g_logical_h;
		return 0;
	}
	return SDL_GetRendererOutputSize(renderer, w, h);
}

SDL_Texture *sword3_SDL_CreateTexture(SDL_Renderer *renderer, Uint32 format,
				      int access, int w, int h)
{
	SDL_Texture *texture;

	owner_check("SDL_CreateTexture", KIND_RENDERER, renderer);
	texture = SDL_CreateTexture(renderer, format, access, w, h);
	owner_register(KIND_TEXTURE, texture);
	fprintf(stderr,
		"sword3-sdl: CreateTexture %ux%u format=0x%x access=%d -> %p%s%s\n",
		(unsigned)w, (unsigned)h, format, access, texture,
		texture ? "" : " ", texture ? "" : SDL_GetError());
	return texture;
}

void sword3_SDL_DestroyTexture(SDL_Texture *texture)
{
	if (!owner_live("SDL_DestroyTexture", KIND_TEXTURE, texture))
		return;
	owner_unregister("SDL_DestroyTexture", KIND_TEXTURE, texture);
	SDL_DestroyTexture(texture);
}

SDL_Texture *sword3_SDL_CreateTextureFromSurface(SDL_Renderer *renderer,
						 SDL_Surface *surface)
{
	SDL_Texture *texture;

	owner_check("SDL_CreateTextureFromSurface", KIND_RENDERER, renderer);
	owner_check("SDL_CreateTextureFromSurface", KIND_SURFACE, surface);
	texture = SDL_CreateTextureFromSurface(renderer, surface);
	owner_register(KIND_TEXTURE, texture);
	fprintf(stderr,
		"sword3-sdl: CreateTextureFromSurface %dx%d -> %p%s%s\n",
		surface ? surface->w : 0, surface ? surface->h : 0, texture,
		texture ? "" : " ", texture ? "" : SDL_GetError());
	return texture;
}

int sword3_SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
			     const void *pixels, int pitch)
{
	static unsigned seen;
	int result;
	int tw = 0;
	int th = 0;
	unsigned sample = 0;
	int i;

	if (!owner_live("SDL_UpdateTexture", KIND_TEXTURE, texture))
		return -1;
	result = SDL_UpdateTexture(texture, rect, pixels, pitch);
	SDL_QueryTexture(texture, NULL, NULL, &tw, &th);
	if (pixels && pitch >= 4) {
		const Uint8 *row = pixels;

		for (i = 0; i < 16 && i * 4 < pitch; i++)
			sample |= row[i * 4] | row[i * 4 + 1] |
				  row[i * 4 + 2] | row[i * 4 + 3];
	}
	if (seen < 8 || result != 0) {
		seen++;
		fprintf(stderr,
			"sword3-sdl: UpdateTexture tex=%p %ux%u %s%dx%d pitch=%d sample=0x%x -> %d%s%s\n",
			texture, (unsigned)tw, (unsigned)th, rect ? "" : "full ",
			rect ? rect->w : 0, rect ? rect->h : 0, pitch, sample,
			result, result == 0 ? "" : " ",
			result == 0 ? "" : SDL_GetError());
	}
	return result;
}

int sword3_SDL_SetTextureBlendMode(SDL_Texture *texture,
				   SDL_BlendMode blendMode)
{
	if (!owner_live("SDL_SetTextureBlendMode", KIND_TEXTURE, texture))
		return -1;
	return SDL_SetTextureBlendMode(texture, blendMode);
}

int sword3_SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect,
			   void **pixels, int *pitch)
{
	static unsigned seen;
	int result;
	int tw = 0;
	int th = 0;

	if (!owner_live("SDL_LockTexture", KIND_TEXTURE, texture))
		return -1;
	result = SDL_LockTexture(texture, rect, pixels, pitch);
	if (seen < 8 || result != 0) {
		seen++;
		SDL_QueryTexture(texture, NULL, NULL, &tw, &th);
		fprintf(stderr,
			"sword3-sdl: LockTexture tex=%p %ux%u -> %d%s%s\n",
			texture, (unsigned)tw, (unsigned)th, result,
			result == 0 ? "" : " ",
			result == 0 ? "" : SDL_GetError());
	}
	return result;
}

void sword3_SDL_UnlockTexture(SDL_Texture *texture)
{
	static unsigned seen;
	int tw = 0;
	int th = 0;

	if (!owner_live("SDL_UnlockTexture", KIND_TEXTURE, texture))
		return;
	if (seen < 8) {
		seen++;
		SDL_QueryTexture(texture, NULL, NULL, &tw, &th);
		fprintf(stderr, "sword3-sdl: UnlockTexture tex=%p %ux%u\n",
			texture, (unsigned)tw, (unsigned)th);
	}
	SDL_UnlockTexture(texture);
}

int sword3_SDL_SetSurfacePalette(SDL_Surface *surface, SDL_Palette *palette)
{
	owner_check("SDL_SetSurfacePalette", KIND_SURFACE, surface);
	return SDL_SetSurfacePalette(surface, palette);
}

int sword3_SDL_UpperBlit(SDL_Surface *src, const SDL_Rect *srcrect,
			 SDL_Surface *dst, SDL_Rect *dstrect)
{
	owner_check("SDL_UpperBlit", KIND_SURFACE, src);
	owner_check("SDL_UpperBlit", KIND_SURFACE, dst);
	return SDL_UpperBlit(src, srcrect, dst, dstrect);
}

int sword3_SDL_UpperBlitScaled(SDL_Surface *src, const SDL_Rect *srcrect,
			       SDL_Surface *dst, SDL_Rect *dstrect)
{
	owner_check("SDL_UpperBlitScaled", KIND_SURFACE, src);
	owner_check("SDL_UpperBlitScaled", KIND_SURFACE, dst);
	return SDL_UpperBlitScaled(src, srcrect, dst, dstrect);
}

SDL_Surface *sword3_SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int width,
						   int height, int depth,
						   Uint32 format)
{
	SDL_Surface *surface =
		SDL_CreateRGBSurfaceWithFormat(flags, width, height, depth,
					       format);
	owner_register(KIND_SURFACE, surface);
	return surface;
}

void sword3_SDL_FreeSurface(SDL_Surface *surface)
{
	owner_unregister("SDL_FreeSurface", KIND_SURFACE, surface);
	SDL_FreeSurface(surface);
}

SDL_RWops *sword3_SDL_RWFromFile(const char *file, const char *mode)
{
	SDL_RWops *rw = SDL_RWFromFile(file, mode);
	owner_register(KIND_RWOPS, rw);
	return rw;
}

#define GUEST_RW_SEEK_SET 0
#define GUEST_RW_SEEK_END 2
#define IMG_SLURP_MAX (64 * 1024 * 1024)

/*
 * Guest SDL 2.0.10 RWops and host SDL2 RWops share this prefix. File-backed
 * host objects are passed through; memory RWops created by unhooked
 * SDL_RWFromMem/ConstMem are copied out through the vtable so host SDL_image
 * never walks a guest object.
 */
struct guest_rwops {
	int64_t (*size)(struct guest_rwops *context);
	int64_t (*seek)(struct guest_rwops *context, int64_t offset, int whence);
	size_t (*read)(struct guest_rwops *context, void *ptr, size_t size,
		       size_t maxnum);
	size_t (*write)(struct guest_rwops *context, const void *ptr, size_t size,
			size_t num);
	int (*close)(struct guest_rwops *context);
	uint32_t type;
};

static void img_init_once(void)
{
	int flags = IMG_INIT_JPG | IMG_INIT_PNG | IMG_INIT_TIF | IMG_INIT_WEBP;
	int got = IMG_Init(flags);
	const SDL_version *linked = IMG_Linked_Version();

	fprintf(stderr,
		"sword3-sdl: IMG_Init(0x%x) -> 0x%x headers=%u.%u.%u runtime=%u.%u.%u (%s)\n",
		flags, got, SDL_IMAGE_MAJOR_VERSION, SDL_IMAGE_MINOR_VERSION,
		SDL_IMAGE_PATCHLEVEL,
		linked ? linked->major : 0, linked ? linked->minor : 0,
		linked ? linked->patch : 0,
		(got & flags) == flags ? "ok" : IMG_GetError());
}

static void ensure_img_init(void)
{
	static pthread_once_t once = PTHREAD_ONCE_INIT;

	pthread_once(&once, img_init_once);
}

static void guest_rw_close(struct guest_rwops *rw, int freesrc)
{
	if (!rw || !freesrc)
		return;
	owner_drop(KIND_RWOPS, rw);
	if (rw->close)
		rw->close(rw);
}

static SDL_Surface *load_typed_rw(struct guest_rwops *rw, int freesrc,
				  const char *type)
{
	SDL_Surface *surface;
	SDL_RWops *mem;
	int64_t nbytes;
	size_t got;
	void *buf;

	if (!rw)
		return IMG_LoadTyped_RW(NULL, freesrc, type);

	if (owner_has(KIND_RWOPS, rw)) {
		surface = IMG_LoadTyped_RW((SDL_RWops *)rw, freesrc, type);
		if (freesrc)
			owner_drop(KIND_RWOPS, rw);
		return surface;
	}

	if (!rw->size || !rw->seek || !rw->read) {
		SDL_SetError("IMG_LoadTyped_RW: RWops missing size/seek/read");
		guest_rw_close(rw, freesrc);
		return NULL;
	}
	nbytes = rw->size(rw);
	if (nbytes < 0) {
		nbytes = rw->seek(rw, 0, GUEST_RW_SEEK_END);
		if (nbytes < 0 || rw->seek(rw, 0, GUEST_RW_SEEK_SET) < 0) {
			SDL_SetError("IMG_LoadTyped_RW: cannot size RWops");
			guest_rw_close(rw, freesrc);
			return NULL;
		}
	}
	if (nbytes <= 0 || nbytes > IMG_SLURP_MAX) {
		SDL_SetError("IMG_LoadTyped_RW: invalid source size %lld",
			     (long long)nbytes);
		guest_rw_close(rw, freesrc);
		return NULL;
	}
	buf = malloc((size_t)nbytes);
	if (!buf) {
		SDL_SetError("IMG_LoadTyped_RW: out of memory");
		guest_rw_close(rw, freesrc);
		return NULL;
	}
	if (rw->seek(rw, 0, GUEST_RW_SEEK_SET) < 0) {
		free(buf);
		SDL_SetError("IMG_LoadTyped_RW: seek failed");
		guest_rw_close(rw, freesrc);
		return NULL;
	}
	got = rw->read(rw, buf, 1, (size_t)nbytes);
	guest_rw_close(rw, freesrc);
	if (got == 0) {
		free(buf);
		SDL_SetError("IMG_LoadTyped_RW: empty read");
		return NULL;
	}
	mem = SDL_RWFromConstMem(buf, (int)got);
	if (!mem) {
		free(buf);
		return NULL;
	}
	surface = IMG_LoadTyped_RW(mem, 1, type);
	free(buf);
	return surface;
}

static void log_img_surface(const char *api, SDL_Surface *surface)
{
	fprintf(stderr, "sword3-sdl: %s -> %p %dx%d%s%s\n", api, surface,
		surface ? surface->w : 0, surface ? surface->h : 0,
		surface ? "" : " ", surface ? "" : SDL_GetError());
}

SDL_Surface *sword3_IMG_Load(const char *file)
{
	SDL_Surface *surface;
	char api[256];

	ensure_img_init();
	surface = IMG_Load(file);
	snprintf(api, sizeof(api), "IMG_Load(%s)", file ? file : "NULL");
	log_img_surface(api, surface);
	owner_register(KIND_SURFACE, surface);
	return surface;
}

SDL_Surface *sword3_IMG_Load_RW(SDL_RWops *src, int freesrc)
{
	SDL_Surface *surface;
	char api[80];

	ensure_img_init();
	surface = load_typed_rw((struct guest_rwops *)src, freesrc, NULL);
	snprintf(api, sizeof(api), "IMG_Load_RW(%p freesrc=%d)", src, freesrc);
	log_img_surface(api, surface);
	owner_register(KIND_SURFACE, surface);
	return surface;
}

SDL_Surface *sword3_IMG_LoadTyped_RW(SDL_RWops *src, int freesrc,
				     const char *type)
{
	SDL_Surface *surface;
	char api[96];

	ensure_img_init();
	surface = load_typed_rw((struct guest_rwops *)src, freesrc, type);
	snprintf(api, sizeof(api), "IMG_LoadTyped_RW(%p freesrc=%d type=%s)",
		 src, freesrc, type ? type : "NULL");
	log_img_surface(api, surface);
	owner_register(KIND_SURFACE, surface);
	return surface;
}

int sword3_SDL_PeepEvents(SDL_Event *events, int numevents,
			  SDL_eventaction action, Uint32 minType,
			  Uint32 maxType)
{
	int n;
	int i;
	int kept;

	if (action != SDL_ADDEVENT)
		apply_pad_pointer();
	n = SDL_PeepEvents(events, numevents, action, minType, maxType);
	if (action == SDL_ADDEVENT || n <= 0 || !events)
		return n;
	kept = 0;
	for (i = 0; i < n; i++) {
		if (!rewrite_event(&events[i]))
			continue;
		if (kept != i)
			events[kept] = events[i];
		kept++;
	}
	return kept;
}

void sword3_SDL_PumpEvents(void)
{
	SDL_PumpEvents();
	tick_ios_display_links();
	pal2_kick_present();
	apply_pad_pointer();
}

int sword3_SDL_PollEvent(SDL_Event *event)
{
	static unsigned seen;
	static unsigned empty;
	int result = next_translated_event(event, 0);

	if (seen < 8) {
		seen++;
		fprintf(stderr, "sword3-sdl: PollEvent -> %d type=%u\n", result,
			event && result ? event->type : 0u);
	} else if (result == 0 && empty < 4) {
		empty++;
		fprintf(stderr, "sword3-sdl: PollEvent empty #%u\n", empty);
	}
	return result;
}

int sword3_SDL_WaitEventTimeout(SDL_Event *event, int timeout)
{
	return next_translated_event(event, timeout);
}

const Uint8 *sword3_SDL_GetKeyboardState(int *numkeys)
{
	static unsigned seen;
	const Uint8 *state = SDL_GetKeyboardState(numkeys);

	if (seen < 3) {
		seen++;
		fprintf(stderr,
			"sword3-sdl: GetKeyboardState arrows U%d D%d L%d R%d\n",
			state ? state[SDL_SCANCODE_UP] : 0,
			state ? state[SDL_SCANCODE_DOWN] : 0,
			state ? state[SDL_SCANCODE_LEFT] : 0,
			state ? state[SDL_SCANCODE_RIGHT] : 0);
	}
	return state;
}

static int ensure_host_mixer(void);

/*
 * Same contract as master/Sword3: the guest mixer owns a real SDL device and
 * its callback. Host Mix_PlayMusic is a second path used only for
 * AVAudioPlayer MP3s. Do not steal this open — Pal2's story/field cues go
 * through the guest callback, and a fake device id leaves them silent.
 */
SDL_AudioDeviceID sword3_SDL_OpenAudioDevice(const char *device, int iscapture,
					     const SDL_AudioSpec *desired,
					     SDL_AudioSpec *obtained,
					     int allowed_changes)
{
	SDL_AudioDeviceID id;

	if (desired)
		fprintf(stderr,
			"sword3-sdl: OpenAudioDevice want freq=%d fmt=0x%x ch=%u samples=%u cb=%p\n",
			desired->freq, desired->format, desired->channels,
			desired->samples, (void *)desired->callback);
	id = SDL_OpenAudioDevice(device, iscapture, desired, obtained,
				 allowed_changes);
	fprintf(stderr, "sword3-sdl: OpenAudioDevice -> %u%s%s", id,
		id ? "" : " ", id ? "" : SDL_GetError());
	if (id && obtained)
		fprintf(stderr, " got freq=%d fmt=0x%x ch=%u samples=%u",
			obtained->freq, obtained->format, obtained->channels,
			obtained->samples);
	fprintf(stderr, "\n");
	return id;
}

void sword3_SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on)
{
	fprintf(stderr, "sword3-sdl: PauseAudioDevice %u pause=%d\n", dev,
		pause_on);
	SDL_PauseAudioDevice(dev, pause_on);
}

void sword3_SDL_LockAudioDevice(SDL_AudioDeviceID dev)
{
	SDL_LockAudioDevice(dev);
}

void sword3_SDL_UnlockAudioDevice(SDL_AudioDeviceID dev)
{
	SDL_UnlockAudioDevice(dev);
}

void sword3_SDL_CloseAudioDevice(SDL_AudioDeviceID dev)
{
	fprintf(stderr, "sword3-sdl: CloseAudioDevice %u\n", dev);
	SDL_CloseAudioDevice(dev);
}

static Sword3AudioBridge *g_av_audio_bridge;
static Sword3AudioHandle *g_av_audio_handle;

static int ensure_host_mixer(void)
{
	Sword3AudioConfig cfg;
	int mix_flags = MIX_INIT_OGG | MIX_INIT_MP3 | MIX_INIT_FLAC;

	if (g_av_audio_bridge)
		return 1;
	if (SDL_WasInit(SDL_INIT_AUDIO) == 0 &&
	    SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		fprintf(stderr, "sword3-sdl: SDL_INIT_AUDIO failed (%s)\n",
			SDL_GetError());
		return 0;
	}
	{
		int inited = Mix_Init(mix_flags);

		fprintf(stderr,
			"sword3-sdl: Mix_Init want=0x%x got=0x%x%s%s%s\n",
			mix_flags, inited,
			(inited & MIX_INIT_MP3) ? " mp3" : "",
			(inited & MIX_INIT_OGG) ? " ogg" : "",
			(inited & MIX_INIT_FLAC) ? " flac" : "");
	}
	if (Mix_QuerySpec(NULL, NULL, NULL)) {
		cfg = (Sword3AudioConfig){ SWORD3_AUDIO_ATTACH, 44100,
					   MIX_DEFAULT_FORMAT, 2, 2048,
					   mix_flags };
	} else {
		cfg = (Sword3AudioConfig){ SWORD3_AUDIO_OPEN_AND_OWN, 44100,
					   MIX_DEFAULT_FORMAT, 2, 2048,
					   mix_flags };
		if (Mix_OpenAudioDevice(44100, MIX_DEFAULT_FORMAT, 2, 2048, NULL,
					SDL_AUDIO_ALLOW_FREQUENCY_CHANGE |
					SDL_AUDIO_ALLOW_CHANNELS_CHANGE) == 0) {
			Mix_AllocateChannels(16);
			cfg.ownership = SWORD3_AUDIO_ATTACH;
			fprintf(stderr,
				"sword3-sdl: host Mix_OpenAudioDevice ok\n");
		}
	}
	g_av_audio_bridge = sword3_audio_bridge_open(&cfg);
	if (!g_av_audio_bridge) {
		fprintf(stderr, "sword3-sdl: audio bridge open failed (%s)\n",
			SDL_GetError());
		return 0;
	}
	Mix_Volume(-1, MIX_MAX_VOLUME);
	Mix_VolumeMusic(MIX_MAX_VOLUME);
	return 1;
}

__attribute__((visibility("default")))
int sword3_host_play_memory_audio(const void *data, size_t size, int loops)
{
	static unsigned seen;

	if (!data || size == 0)
		return -1;
	if (!ensure_host_mixer())
		return -1;
	if (g_av_audio_handle) {
		sword3_audio_stop(g_av_audio_handle);
		sword3_audio_close(g_av_audio_handle);
		g_av_audio_handle = NULL;
	}
	g_av_audio_handle = sword3_audio_open_memory(g_av_audio_bridge,
						     SWORD3_AUDIO_MUSIC, data,
						     size);
	if (!g_av_audio_handle)
		g_av_audio_handle = sword3_audio_open_memory(
			g_av_audio_bridge, SWORD3_AUDIO_EFFECT, data, size);
	if (!g_av_audio_handle) {
		fprintf(stderr,
			"sword3-sdl: load audio %zu bytes mag=%02x%02x%02x%02x failed (%s)\n",
			size,
			size > 0 ? ((const unsigned char *)data)[0] : 0,
			size > 1 ? ((const unsigned char *)data)[1] : 0,
			size > 2 ? ((const unsigned char *)data)[2] : 0,
			size > 3 ? ((const unsigned char *)data)[3] : 0,
			SDL_GetError());
		return -1;
	}
	sword3_audio_set_volume(g_av_audio_handle, MIX_MAX_VOLUME);
	/*
	 * Pal2 always calls setNumberOfLoops:0 (AVAudioPlayer: play once).
	 * Field BGM is supposed to keep going across the system menu; Mix
	 * treats 0 as "play once", so the save screen outlives the track.
	 */
	if (loops <= 0)
		loops = -1;
	if (seen < 12) {
		int freq = 0, ch = 0, rc;
		Uint16 fmt = 0;

		seen++;
		rc = sword3_audio_play(g_av_audio_handle, loops);
		Mix_QuerySpec(&freq, &fmt, &ch);
		fprintf(stderr,
			"sword3-sdl: play audio %zu bytes loops=%d mag=%02x%02x%02x%02x rc=%d playing=%d spec=%dHz\n",
			size, loops,
			((const unsigned char *)data)[0],
			size > 1 ? ((const unsigned char *)data)[1] : 0,
			size > 2 ? ((const unsigned char *)data)[2] : 0,
			size > 3 ? ((const unsigned char *)data)[3] : 0,
			rc, Mix_PlayingMusic(), freq);
		return rc;
	}
	return sword3_audio_play(g_av_audio_handle, loops);
}

__attribute__((visibility("default")))
void sword3_host_stop_memory_audio(void)
{
	if (!g_av_audio_handle)
		return;
	sword3_audio_stop(g_av_audio_handle);
	sword3_audio_close(g_av_audio_handle);
	g_av_audio_handle = NULL;
}

__attribute__((visibility("default")))
void sword3_host_pause_memory_audio(void)
{
	Mix_PauseMusic();
	fprintf(stderr, "sword3-sdl: pause music playing=%d paused=%d\n",
		Mix_PlayingMusic(), Mix_PausedMusic());
}

__attribute__((visibility("default")))
void sword3_host_resume_memory_audio(void)
{
	Mix_ResumeMusic();
	fprintf(stderr, "sword3-sdl: resume music playing=%d paused=%d\n",
		Mix_PlayingMusic(), Mix_PausedMusic());
}

__attribute__((visibility("default")))
int sword3_host_memory_audio_playing(void)
{
	return Mix_PlayingMusic() && !Mix_PausedMusic();
}

__attribute__((visibility("default")))
void sword3_host_set_memory_audio_volume(int volume)
{
	if (volume < 0)
		volume = 0;
	if (volume > MIX_MAX_VOLUME)
		volume = MIX_MAX_VOLUME;
	Mix_VolumeMusic(volume);
	Mix_Volume(-1, volume);
}
