#include "sdl_bridge.h"
#include "audio_bridge.h"
#include "host_cheat.h"
#include "video_bridge.h"

#include <dlfcn.h>
#ifndef RTLD_DEFAULT
#define RTLD_DEFAULT ((void *)0)
#endif
#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <SDL2/SDL_image.h>
#include <SDL2/SDL_mixer.h>

#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/samplefmt.h>

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
#define PAL2_SE_HALT_CHANNEL 0x10010a4b4ull
#define PAL2_SE_CHANNEL_COUNT 0x1004cdcd8ull
#define PAL2_SE_CHANNELS 0x1004cdce0ull
#define PAL2_INPUT_MANAGER 0x10048ad50ull
#define PAL2_FIGHT_INSTANCE 0x100290f80ull
#define PAL2_FIGHT_ACTIVE 0x10046c6e8ull
#define PAL2_INPUT_RESET 0x1001e4ff8ull
#define PAL2_INPUT_MOUSE_LEFT 0x2d8
#define PAL2_INPUT_LAST_KEYDOWN 0x2494
#define PAL2_INPUT_SLOT_STATE 0
#define PAL2_INPUT_SLOT_FLAGS 8
#define PAL2_LIST_PENDING 0x4d8
#define PAL2_LIST_SELECTED 0x1280
#define PAL2_LIST_SELECTED_INDEX 0x1288
#define PAL2_LIST_STATE 0x24
#define PAL2_LIST_RELEASE_STATE 0x127a
#define PAL2_LIST_ITEM_POSITION 0x1ec
#define PAL2_LIST_SET_TWEEN 0x1000426fcull
#define PAL2_SELECTOR_RESULT 0x24
#define PAL2_SELECTOR_SELECTION 0x28
#define PAL2_SOUND_VOLUME 0x1004953fcull
#define PAL2_SOUND_MANAGER 0x10049e850ull
#define PAL2_PLAY_SOUND 0x100218f64ull
#define PAL2_CURSOR_SOUND 0x34
#define PAL2_CURSOR_SOUND_DEBOUNCE_MS 100
#define PAL2_TALK_RMLOCK 0x8000u
#define PAL2_FIGHT_COMMAND_UI 0xe1
#define PAL2_FIGHT_TACTIC_PENDING 0xe8
#define PAL2_FIGHT_TACTIC 0x124
#define PAL2_FIGHT_TACTIC_BLOCKED 0x5dc
#define PAL2_TACTIC_STRONG_ATTACK 2
#define PAL2_TACTIC_SIEGE 3

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
static int g_talk_space_se;
static int g_space_release_pending;
static SDL_Scancode g_movie_swallow_keyup;
static Uint32 g_cursor_sound_ms;
static SDL_AudioDeviceID g_guest_audio_device;
static SDL_AudioFormat g_guest_audio_format = AUDIO_S16SYS;

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

static _Atomic int g_music_halt;

#define HOST_AV_SLOTS 32
#define HOST_AV_MUSIC_CH (-2)

/*
 * Track each AVAudioPlayer independently. MP3 BGM prefers Mix MUSIC;
 * non-music data falls back to one EFFECT channel per slot.
 */
struct host_av_slot {
	Sword3AudioHandle *handle;
	void *token;
	int channel; /* slot index, or HOST_AV_MUSIC_CH */
	_Atomic int stopping;
};

static struct host_av_slot g_av_slots[HOST_AV_SLOTS];
static pthread_mutex_t g_av_lock = PTHREAD_MUTEX_INITIALIZER;

static int host_av_slot_index(const struct host_av_slot *slot)
{
	return (int)(slot - g_av_slots);
}

static void host_av_finish_token(void *token)
{
	static void (*notify_player)(void *);
	static int resolved;

	if (!resolved) {
		resolved = 1;
		notify_player = (void (*)(void *))dlsym(
			RTLD_DEFAULT,
			"sword3_ios_notify_av_audio_finished_player");
	}
	if (notify_player && token)
		notify_player(token);
}

static void on_host_music_finished(void)
{
	struct host_av_slot *slot = NULL;
	void *token = NULL;
	Sword3AudioHandle *handle = NULL;
	int i;

	if (atomic_load(&g_music_halt))
		return;
	/*
	 * MUSIC fallback path (when EFFECT load fails). Find the slot that
	 * owns Mix music (channel == HOST_AV_MUSIC_CH) and DidFinish it.
	 */
	pthread_mutex_lock(&g_av_lock);
	for (i = 0; i < HOST_AV_SLOTS; i++) {
		if (g_av_slots[i].handle &&
		    g_av_slots[i].channel == HOST_AV_MUSIC_CH) {
			slot = &g_av_slots[i];
			break;
		}
	}
	if (slot && !atomic_load(&slot->stopping)) {
		token = slot->token;
		handle = slot->handle;
		slot->handle = NULL;
		slot->token = NULL;
		slot->channel = host_av_slot_index(slot);
	}
	pthread_mutex_unlock(&g_av_lock);
	if (handle)
		sword3_audio_close(handle);
	if (token)
		host_av_finish_token(token);
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
 * field, then waits for its AVAudioPlayer / AVPlayer to finish. Keep this
 * as a fallback if a movie status byte never clears.
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

static uint32_t pal2_talk_wait_lock(void)
{
	volatile int32_t *talk_id =
		(volatile int32_t *)(uintptr_t)(PAL2_EVENT_OBJ + 0x108);
	volatile uint32_t *ulck =
		(volatile uint32_t *)(uintptr_t)(PAL2_EVENT_OBJ + 0x168);
	volatile int32_t *channel_count =
		(volatile int32_t *)(uintptr_t)PAL2_SE_CHANNEL_COUNT;
	uint8_t *channels =
		*(uint8_t **)(uintptr_t)PAL2_SE_CHANNELS;
	int32_t playing;

	if (*talk_id == -1 || (*ulck & PAL2_TALK_RMLOCK) == 0 ||
	    *channel_count < 1 || !channels)
		return 0;
	playing = *(volatile int32_t *)(channels + 8);
	return playing > 0 ? *ulck : 0;
}

static void pal2_stop_waiting_se(void)
{
	int (*halt_channel)(int) =
		(int (*)(int))(uintptr_t)PAL2_SE_HALT_CHANNEL;

	/*
	 * Use the guest mixer's normal completion path. Its channel-done
	 * callback advances the RMlock wait just as a naturally ending SE
	 * does, without modifying Talk or script state.
	 */
	halt_channel(0);
}

static int pal2_input_released(const uint8_t *slot)
{
	return slot[PAL2_INPUT_SLOT_STATE] == 2 &&
	       (slot[PAL2_INPUT_SLOT_FLAGS] & 1) != 0;
}

static void pal2_release_keydown_latch(SDL_Scancode scancode)
{
	volatile int32_t *last_keydown =
		(volatile int32_t *)(uintptr_t)
			(PAL2_INPUT_MANAGER + PAL2_INPUT_LAST_KEYDOWN);

	/*
	 * UIGamePad records KEYDOWN here, but its KEYUP path only updates the
	 * per-key slot. Pal2's battle list treats any non-zero value as a held
	 * navigation key and replays cursor sound 0x34 every frame.
	 */
	if (*last_keydown == (int32_t)scancode)
		*last_keydown = 0;
}

void sword3_pal2_finish_list_release(void *opaque)
{
	static unsigned keyboard_finishes;
	uint8_t *list = opaque;
	uint8_t *input = (uint8_t *)(uintptr_t)PAL2_INPUT_MANAGER;
	uint8_t *mouse = input + PAL2_INPUT_MOUSE_LEFT;
	uint8_t *selected;
	int keyboard_release;
	void (*set_tween)(void *, int, int, int) =
		(void (*)(void *, int, int, int))(uintptr_t)PAL2_LIST_SET_TWEEN;

	if (!list)
		return;
	keyboard_release = list[PAL2_LIST_PENDING] &&
			   g_space_release_pending;
	if (!pal2_input_released(mouse) && !keyboard_release)
		return;

	/*
	 * Pal2's list activation accepts keyboard action 6, but its release
	 * animation is hard-wired to the mouse-left slot. Preserve the original
	 * mouse path and allow Space only while this list has a pending
	 * activation. This is the body of guest 0x100064f04 with that one
	 * additional release source.
	 */
	*(int32_t *)(list + PAL2_LIST_STATE) =
		*(int32_t *)(list + PAL2_LIST_SELECTED_INDEX);
	selected = *(uint8_t **)(list + PAL2_LIST_SELECTED);
	set_tween(selected + 8, 0, 1000, 1000);
	if (list[PAL2_LIST_PENDING]) {
		int32_t *position =
			(int32_t *)(selected + PAL2_LIST_ITEM_POSITION);

		position[0]--;
		position[1]--;
		list[PAL2_LIST_PENDING] = 0;
	}
	*(uint16_t *)(list + PAL2_LIST_RELEASE_STATE) = 0x101;
	if (keyboard_release) {
		g_space_release_pending = 0;
		if (keyboard_finishes++ < 8)
			fprintf(stderr,
				"sword3-sdl: Pal2 list release finished from Space\n");
	}
}

void *sword3_pal2_play_ui_sound(int sound_id)
{
	int32_t configured =
		*(volatile int32_t *)(uintptr_t)PAL2_SOUND_VOLUME;
	int32_t shifted = (int32_t)((uint32_t)configured << 7);
	int64_t product = (int64_t)shifted * (int32_t)0x2e8ba2e9;
	int volume = (int)(product >> 33) +
		     (int)((uint64_t)product >> 63);
	void *(*play)(void *, void *, int, int, int, int, int) =
		(void *(*)(void *, void *, int, int, int, int, int))
			(uintptr_t)PAL2_PLAY_SOUND;
	Uint32 now = SDL_GetTicks();

	/*
	 * Pal2's keyboard battle-list path asks for cursor sound 0x34 twice
	 * for one focus move. Keep distinct human inputs, but collapse calls
	 * made by the same release transition.
	 */
	if (sound_id == PAL2_CURSOR_SOUND && g_cursor_sound_ms &&
	    now - g_cursor_sound_ms < PAL2_CURSOR_SOUND_DEBOUNCE_MS)
		return NULL;
	if (sound_id == PAL2_CURSOR_SOUND)
		g_cursor_sound_ms = now;
	return play((void *)(uintptr_t)PAL2_SOUND_MANAGER, NULL, sound_id,
		    volume, 0, 1, 0);
}

void sword3_pal2_confirm_target(void *opaque, int selected, void *target)
{
	static unsigned confirms;
	uint8_t *selector = opaque;

	(void)target;
	if (!selector || selected <= 0)
		return;

	/*
	 * Pal2's target selector leaves its keyboard-confirm virtual method
	 * empty. Its touch-hit path commits the highlighted target by writing
	 * the node key to both the selection and result fields.
	 */
	*(int32_t *)(selector + PAL2_SELECTOR_SELECTION) = selected;
	*(int32_t *)(selector + PAL2_SELECTOR_RESULT) = selected;
	if (confirms++ < 8)
		fprintf(stderr,
			"sword3-sdl: Pal2 target confirmed from keyboard id=%d\n",
			selected);
}

static int pal2_handle_tactic_key(SDL_Event *event)
{
	static unsigned triggers;
	uint8_t *fight;
	SDL_Scancode scancode;
	int tactic;

	if (event->type != SDL_KEYDOWN && event->type != SDL_KEYUP)
		return 0;
	scancode = event->key.keysym.scancode;
	if (scancode == SDL_SCANCODE_M) {
		tactic = PAL2_TACTIC_SIEGE;
	} else if (scancode == SDL_SCANCODE_N) {
		tactic = PAL2_TACTIC_STRONG_ATTACK;
	} else {
		return 0;
	}

	/* Consume both halves everywhere. The singleton survives after battle,
	 * so the real scene flag and command-UI state are both required. */
	if (event->type != SDL_KEYDOWN ||
	    !*(volatile uint8_t *)(uintptr_t)PAL2_FIGHT_ACTIVE)
		return 1;
	fight = *(uint8_t *volatile *)(uintptr_t)PAL2_FIGHT_INSTANCE;
	if (!fight || !fight[PAL2_FIGHT_COMMAND_UI] ||
	    fight[PAL2_FIGHT_TACTIC_PENDING] ||
	    *(int32_t *)(fight + PAL2_FIGHT_TACTIC_BLOCKED) != 0)
		return 1;

	/* This is the native post-hit branch at 0x100063f64: a touch release
	 * stores the tactic, marks it pending, then clears the input queues. */
	*(int32_t *)(fight + PAL2_FIGHT_TACTIC) = tactic;
	fight[PAL2_FIGHT_TACTIC_PENDING] = 1;
	((void (*)(void *))(uintptr_t)PAL2_INPUT_RESET)(
		(void *)(uintptr_t)PAL2_INPUT_MANAGER);
	if (triggers++ < 8)
		fprintf(stderr, "sword3-sdl: Pal2 battle tactic %s submitted\n",
			tactic == PAL2_TACTIC_SIEGE ? "siege" : "strong attack");
	return 1;
}

static int rewrite_event(SDL_Event *event)
{
	int cheat_was_open;

	if (!event)
		return 0;
	if (event->type == SDL_KEYUP && g_movie_swallow_keyup != SDL_SCANCODE_UNKNOWN &&
	    event->key.keysym.scancode == g_movie_swallow_keyup) {
		g_movie_swallow_keyup = SDL_SCANCODE_UNKNOWN;
		return 0;
	}
	scale_pointer_event(event);
	if (is_guest_pad_event(event->type) || event->type == SDL_MOUSEMOTION)
		return 0;
	if (event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) {
		cheat_was_open = host_cheat_active();
		if (host_cheat_key(event->key.keysym.scancode,
				   event->type == SDL_KEYDOWN,
				   event->key.repeat)) {
			if (cheat_was_open != host_cheat_active())
				((void (*)(void *))(uintptr_t)PAL2_INPUT_RESET)(
					(void *)(uintptr_t)PAL2_INPUT_MANAGER);
			return 0;
		}
	}
	/* iOS SDL does not auto-repeat keys; Linux does. Save/list UIs
	 * treat each KEYDOWN as one step, so drop host repeats. */
	if (event->type == SDL_KEYDOWN && event->key.repeat)
		return 0;
	if (event->type == SDL_KEYDOWN)
		g_space_release_pending = 0;
	if (event->type == SDL_KEYUP)
		pal2_release_keydown_latch(event->key.keysym.scancode);
	if (pal2_handle_tactic_key(event))
		return 0;
	if ((event->type == SDL_KEYDOWN || event->type == SDL_KEYUP) &&
	    event->key.keysym.scancode == SDL_SCANCODE_SPACE) {
		if (event->type == SDL_KEYDOWN) {
			uint32_t talk_lock = pal2_talk_wait_lock();

			if (talk_lock) {
				pal2_stop_waiting_se();
				/* A pure RMlock is a voiced dialogue wait: consume A
				 * after ending the SE so it cannot also skip the next
				 * line. A counted UI lock is an item notice; let the
				 * same A reach the notice's normal confirm handler. */
				if (talk_lock == PAL2_TALK_RMLOCK) {
					g_talk_space_se = 1;
					return 0;
				}
			}
		}
		if (event->type == SDL_KEYUP && g_talk_space_se) {
			g_talk_space_se = 0;
			return 0;
		}
		if (event->type == SDL_KEYUP) {
			g_space_release_pending = 1;
		}
	}
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
	g_guest_audio_device = 0;
	g_guest_audio_format = AUDIO_S16SYS;
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
	host_cheat_install();
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
	if (host_cheat_active() && SDL_GetRenderTarget(renderer) == NULL)
		host_cheat_draw(renderer, g_logical_w, g_logical_h);
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
	static const Uint8 blocked[SDL_NUM_SCANCODES];
	const Uint8 *state = SDL_GetKeyboardState(numkeys);
	const Uint8 *guest_state = host_cheat_active() ? blocked : state;

	if (seen < 3) {
		seen++;
		fprintf(stderr,
			"sword3-sdl: GetKeyboardState arrows U%d D%d L%d R%d\n",
			state ? state[SDL_SCANCODE_UP] : 0,
			state ? state[SDL_SCANCODE_DOWN] : 0,
			state ? state[SDL_SCANCODE_LEFT] : 0,
			state ? state[SDL_SCANCODE_RIGHT] : 0);
	}
	return guest_state;
}

static int ensure_host_mixer(void);

/*
 * Pal2 was built against iOS SDL, where its only output device is ID 1.
 * SDL2 on Linux reserves ID 1 for the legacy SDL_OpenAudio API, so
 * SDL_OpenAudioDevice returns 2 or higher. The guest ignores that return
 * value and continues to pause/lock/close ID 1; translate those operations
 * to the device opened on its behalf.
 */
static SDL_AudioDeviceID guest_audio_device(SDL_AudioDeviceID dev)
{
	if (dev == 1 && g_guest_audio_device)
		return g_guest_audio_device;
	return dev;
}

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
	if (id && !iscapture) {
		g_guest_audio_device = id;
		if (obtained)
			g_guest_audio_format = obtained->format;
		else if (desired)
			g_guest_audio_format = desired->format;
	}
	fprintf(stderr, "sword3-sdl: OpenAudioDevice -> %u%s%s", id,
		id ? "" : " ", id ? "" : SDL_GetError());
	if (id && obtained)
		fprintf(stderr, " got freq=%d fmt=0x%x ch=%u samples=%u",
			obtained->freq, obtained->format, obtained->channels,
			obtained->samples);
	fprintf(stderr, "\n");
	return id;
}

void sword3_SDL_MixAudio(Uint8 *dst, const Uint8 *src, Uint32 len, int volume)
{
	/*
	 * Pal2's bundled SDL_MixAudio reads the format from its private audio
	 * device. OpenAudioDevice is hosted, so that guest pointer stays NULL
	 * and the bundled function leaves dst silent. Mix with the format
	 * obtained by the host device instead.
	 */
	SDL_MixAudioFormat(dst, src, g_guest_audio_format, len, volume);
}

void sword3_SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on)
{
	SDL_AudioDeviceID actual = guest_audio_device(dev);

	fprintf(stderr, "sword3-sdl: PauseAudioDevice %u -> %u pause=%d\n",
		dev, actual, pause_on);
	SDL_PauseAudioDevice(actual, pause_on);
}

void sword3_SDL_LockAudio(void)
{
	if (g_guest_audio_device)
		SDL_LockAudioDevice(g_guest_audio_device);
}

void sword3_SDL_UnlockAudio(void)
{
	if (g_guest_audio_device)
		SDL_UnlockAudioDevice(g_guest_audio_device);
}

void sword3_SDL_LockAudioDevice(SDL_AudioDeviceID dev)
{
	SDL_LockAudioDevice(guest_audio_device(dev));
}

void sword3_SDL_UnlockAudioDevice(SDL_AudioDeviceID dev)
{
	SDL_UnlockAudioDevice(guest_audio_device(dev));
}

void sword3_SDL_CloseAudioDevice(SDL_AudioDeviceID dev)
{
	SDL_AudioDeviceID actual = guest_audio_device(dev);

	fprintf(stderr, "sword3-sdl: CloseAudioDevice %u -> %u\n", dev,
		actual);
	SDL_CloseAudioDevice(actual);
	if (actual == g_guest_audio_device)
		g_guest_audio_device = 0;
}

static Sword3AudioBridge *g_av_audio_bridge;

static void on_host_channel_finished(int channel)
{
	struct host_av_slot *slot;
	void *token = NULL;
	Sword3AudioHandle *handle = NULL;

	if (channel < 0 || channel >= HOST_AV_SLOTS)
		return;
	slot = &g_av_slots[channel];
	pthread_mutex_lock(&g_av_lock);
	if (!slot->handle || slot->channel != channel) {
		pthread_mutex_unlock(&g_av_lock);
		return;
	}
	if (atomic_load(&slot->stopping)) {
		pthread_mutex_unlock(&g_av_lock);
		return;
	}
	token = slot->token;
	handle = slot->handle;
	slot->handle = NULL;
	slot->token = NULL;
	pthread_mutex_unlock(&g_av_lock);
	if (handle)
		sword3_audio_close(handle);
	if (token)
		host_av_finish_token(token);
}

static int ensure_host_mixer(void)
{
	Sword3AudioConfig cfg;
	int mix_flags = MIX_INIT_OGG | MIX_INIT_MP3 | MIX_INIT_FLAC;
	int i;

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
			Mix_AllocateChannels(HOST_AV_SLOTS);
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
	Mix_AllocateChannels(HOST_AV_SLOTS);
	Mix_Volume(-1, MIX_MAX_VOLUME);
	Mix_VolumeMusic(MIX_MAX_VOLUME);
	Mix_ChannelFinished(on_host_channel_finished);
	Mix_HookMusicFinished(on_host_music_finished);
	for (i = 0; i < HOST_AV_SLOTS; i++) {
		g_av_slots[i].channel = i;
		g_av_slots[i].handle = NULL;
		g_av_slots[i].token = NULL;
		atomic_store(&g_av_slots[i].stopping, 0);
	}
	return 1;
}

static struct host_av_slot *host_av_find_token_locked(void *token)
{
	int i;

	if (!token)
		return NULL;
	for (i = 0; i < HOST_AV_SLOTS; i++) {
		if (g_av_slots[i].token == token && g_av_slots[i].handle)
			return &g_av_slots[i];
	}
	return NULL;
}

static struct host_av_slot *host_av_alloc_locked(void)
{
	int i;

	for (i = 0; i < HOST_AV_SLOTS; i++) {
		if (!g_av_slots[i].handle)
			return &g_av_slots[i];
	}
	return NULL;
}

static void host_av_stop_slot_locked(struct host_av_slot *slot)
{
	Sword3AudioHandle *handle;
	int was_music;
	int mix_ch;

	if (!slot || !slot->handle)
		return;
	atomic_store(&slot->stopping, 1);
	handle = slot->handle;
	was_music = (slot->channel == HOST_AV_MUSIC_CH);
	mix_ch = host_av_slot_index(slot);
	slot->handle = NULL;
	slot->token = NULL;
	slot->channel = mix_ch;
	pthread_mutex_unlock(&g_av_lock);
	if (was_music)
		atomic_store(&g_music_halt, 1);
	sword3_audio_stop(handle);
	sword3_audio_close(handle);
	if (was_music)
		atomic_store(&g_music_halt, 0);
	pthread_mutex_lock(&g_av_lock);
	atomic_store(&slot->stopping, 0);
}

__attribute__((visibility("default")))
void sword3_host_stop_memory_audio_for(void *token)
{
	struct host_av_slot *slot;

	if (!token)
		return;
	pthread_mutex_lock(&g_av_lock);
	slot = host_av_find_token_locked(token);
	if (!slot) {
		pthread_mutex_unlock(&g_av_lock);
		return;
	}
	fprintf(stderr, "sword3-sdl: stop Mix ch=%d token=%p\n",
		slot->channel, token);
	host_av_stop_slot_locked(slot);
	pthread_mutex_unlock(&g_av_lock);
}

__attribute__((visibility("default")))
int sword3_host_play_memory_audio_for(void *token, const void *data,
				      size_t size, int loops)
{
	static unsigned seen;
	struct host_av_slot *slot;
	struct host_av_slot *old;
	Sword3AudioHandle *handle;
	Sword3AudioHandle *old_handle = NULL;
	int old_was_music = 0;
	int channel;
	int is_music = 0;
	int mix_ch;

	if (!data || size == 0 || !token)
		return -1;
	if (!ensure_host_mixer())
		return -1;

	handle = sword3_audio_open_memory(g_av_audio_bridge, SWORD3_AUDIO_MUSIC,
					  data, size);
	is_music = handle != NULL;
	if (!handle) {
		handle = sword3_audio_open_memory(g_av_audio_bridge,
						  SWORD3_AUDIO_EFFECT, data,
						  size);
	}
	if (!handle) {
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
	sword3_audio_set_volume(handle, MIX_MAX_VOLUME);
	if (loops < 0)
		loops = -1;

	pthread_mutex_lock(&g_av_lock);
	old = host_av_find_token_locked(token);
	if (old && old->handle) {
		atomic_store(&old->stopping, 1);
		old_handle = old->handle;
		old_was_music = (old->channel == HOST_AV_MUSIC_CH);
		old->handle = NULL;
		old->token = NULL;
		old->channel = host_av_slot_index(old);
	}
	slot = host_av_alloc_locked();
	if (!slot) {
		pthread_mutex_unlock(&g_av_lock);
		if (old_handle) {
			if (old_was_music)
				atomic_store(&g_music_halt, 1);
			sword3_audio_stop(old_handle);
			sword3_audio_close(old_handle);
			if (old_was_music)
				atomic_store(&g_music_halt, 0);
		}
		sword3_audio_close(handle);
		fprintf(stderr, "sword3-sdl: no free AVAudioPlayer slot\n");
		return -1;
	}
	mix_ch = host_av_slot_index(slot);
	slot->handle = handle;
	slot->token = token;
	slot->channel = is_music ? HOST_AV_MUSIC_CH : mix_ch;
	atomic_store(&slot->stopping, 0);
	pthread_mutex_unlock(&g_av_lock);

	if (old_handle) {
		if (old_was_music)
			atomic_store(&g_music_halt, 1);
		sword3_audio_stop(old_handle);
		sword3_audio_close(old_handle);
		if (old_was_music)
			atomic_store(&g_music_halt, 0);
		if (old)
			atomic_store(&old->stopping, 0);
	}

	if (is_music)
		atomic_store(&g_music_halt, 0);
	channel = is_music ? sword3_audio_play(handle, loops) :
			     sword3_audio_play_on(handle, mix_ch, loops);
	if (channel < 0) {
		pthread_mutex_lock(&g_av_lock);
		if (slot->handle == handle) {
			slot->handle = NULL;
			slot->token = NULL;
			slot->channel = mix_ch;
		}
		pthread_mutex_unlock(&g_av_lock);
		sword3_audio_close(handle);
		return -1;
	}

	if (seen < 32) {
		int freq = 0, ch = 0;
		Uint16 fmt = 0;

		seen++;
		Mix_QuerySpec(&freq, &fmt, &ch);
		fprintf(stderr,
			"sword3-sdl: play audio token=%p %zu bytes loops=%d Mix ch=%d%s mag=%02x%02x%02x%02x spec=%dHz\n",
			token, size, loops,
			is_music ? HOST_AV_MUSIC_CH : mix_ch,
			is_music ? " music-fallback" : "",
			((const unsigned char *)data)[0],
			size > 1 ? ((const unsigned char *)data)[1] : 0,
			size > 2 ? ((const unsigned char *)data)[2] : 0,
			size > 3 ? ((const unsigned char *)data)[3] : 0,
			freq);
	}
	return 0;
}

__attribute__((visibility("default")))
void sword3_host_pause_memory_audio_for(void *token)
{
	struct host_av_slot *slot;

	pthread_mutex_lock(&g_av_lock);
	slot = host_av_find_token_locked(token);
	if (slot) {
		if (slot->channel == HOST_AV_MUSIC_CH)
			Mix_PauseMusic();
		else if (slot->channel >= 0)
			Mix_Pause(slot->channel);
	}
	pthread_mutex_unlock(&g_av_lock);
}

__attribute__((visibility("default")))
int sword3_host_memory_audio_playing_for(void *token)
{
	struct host_av_slot *slot;
	int playing = 0;

	pthread_mutex_lock(&g_av_lock);
	slot = host_av_find_token_locked(token);
	if (slot) {
		if (slot->channel == HOST_AV_MUSIC_CH)
			playing = Mix_PlayingMusic() && !Mix_PausedMusic();
		else if (slot->channel >= 0)
			playing = Mix_Playing(slot->channel) &&
				  !Mix_Paused(slot->channel);
	}
	pthread_mutex_unlock(&g_av_lock);
	return playing;
}

__attribute__((visibility("default")))
void sword3_host_set_memory_audio_volume_for(void *token, int volume)
{
	struct host_av_slot *slot;

	if (volume < 0)
		volume = 0;
	if (volume > MIX_MAX_VOLUME)
		volume = MIX_MAX_VOLUME;
	pthread_mutex_lock(&g_av_lock);
	slot = host_av_find_token_locked(token);
	if (slot && slot->handle)
		sword3_audio_set_volume(slot->handle, volume);
	pthread_mutex_unlock(&g_av_lock);
}

__attribute__((visibility("default")))
int sword3_host_play_memory_audio(const void *data, size_t size, int loops)
{
	/*
	 * Legacy entry: no token. Use a stable fake token so callers that
	 * still go through the single-stream API don't collide with real
	 * AVAudioPlayer instances.
	 */
	return sword3_host_play_memory_audio_for((void *)(uintptr_t)1, data,
						 size, loops);
}

__attribute__((visibility("default")))
void sword3_host_stop_memory_audio(void)
{
	sword3_host_stop_memory_audio_for((void *)(uintptr_t)1);
}

__attribute__((visibility("default")))
void sword3_host_pause_memory_audio(void)
{
	sword3_host_pause_memory_audio_for((void *)(uintptr_t)1);
}

__attribute__((visibility("default")))
void sword3_host_resume_memory_audio(void)
{
	Mix_Resume(-1);
	Mix_ResumeMusic();
}

__attribute__((visibility("default")))
int sword3_host_memory_audio_playing(void)
{
	return sword3_host_memory_audio_playing_for((void *)(uintptr_t)1);
}

__attribute__((visibility("default")))
void sword3_host_set_memory_audio_volume(int volume)
{
	sword3_host_set_memory_audio_volume_for((void *)(uintptr_t)1, volume);
}

struct movie_audio {
	AVFormatContext *format;
	AVCodecContext *codec;
	AVFrame *frame;
	AVPacket *packet;
	SDL_AudioDeviceID device;
	SDL_AudioStream *stream;
	void *packed;
	size_t packed_size;
	void *pcm;
	size_t pcm_size;
	int stream_index;
	int eof;
	int packet_pending;
};

static struct {
	Sword3Video *video;
	Sword3VideoFrame frame;
	SDL_Texture *texture;
	struct movie_audio audio;
	void (*done)(void);
	Uint32 start_ms;
	Uint32 last_present_ms;
	unsigned frames;
	int have_frame;
	int active;
	int closing;
	int tex_w;
	int tex_h;
} g_movie;

static void movie_audio_close(void)
{
	struct movie_audio *audio = &g_movie.audio;

	if (audio->device) {
		SDL_PauseAudioDevice(audio->device, 1);
		SDL_ClearQueuedAudio(audio->device);
		SDL_CloseAudioDevice(audio->device);
	}
	if (audio->stream)
		SDL_FreeAudioStream(audio->stream);
	av_packet_free(&audio->packet);
	av_frame_free(&audio->frame);
	avcodec_free_context(&audio->codec);
	avformat_close_input(&audio->format);
	free(audio->packed);
	free(audio->pcm);
	memset(audio, 0, sizeof(*audio));
}

static int movie_audio_open(const char *path)
{
	struct movie_audio *audio = &g_movie.audio;
	const AVCodec *decoder = NULL;
	SDL_AudioSpec want;
	SDL_AudioSpec have;
	int stream_index;
	int rc;

	memset(audio, 0, sizeof(*audio));
	audio->stream_index = -1;
	rc = avformat_open_input(&audio->format, path, NULL, NULL);
	if (rc < 0)
		return -1;
	rc = avformat_find_stream_info(audio->format, NULL);
	if (rc < 0)
		goto fail;
	stream_index = av_find_best_stream(audio->format, AVMEDIA_TYPE_AUDIO,
					   -1, -1, &decoder, 0);
	if (stream_index < 0 || !decoder)
		goto fail;
	audio->stream_index = stream_index;
	audio->codec = avcodec_alloc_context3(decoder);
	if (!audio->codec)
		goto fail;
	rc = avcodec_parameters_to_context(
		audio->codec, audio->format->streams[stream_index]->codecpar);
	if (rc < 0)
		goto fail;
	rc = avcodec_open2(audio->codec, decoder, NULL);
	if (rc < 0)
		goto fail;
	audio->frame = av_frame_alloc();
	audio->packet = av_packet_alloc();
	if (!audio->frame || !audio->packet)
		goto fail;
	SDL_zero(want);
	want.freq = 44100;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 2048;
	audio->device = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	if (!audio->device) {
		fprintf(stderr, "sword3-sdl: movie audio device failed: %s\n",
			SDL_GetError());
		goto fail;
	}
	SDL_PauseAudioDevice(audio->device, 0);
	return 0;

fail:
	movie_audio_close();
	return -1;
}

static int movie_audio_ensure_stream(const AVFrame *frame)
{
	struct movie_audio *audio = &g_movie.audio;
	enum AVSampleFormat packed;
	SDL_AudioFormat sdl_fmt;
	int channels;
	int sample_rate;

	if (audio->stream)
		return 0;
	channels = frame->ch_layout.nb_channels;
	if (channels <= 0)
		channels = audio->codec->ch_layout.nb_channels;
	if (channels <= 0)
		return -1;
	packed = av_get_packed_sample_fmt((enum AVSampleFormat)frame->format);
	if (packed == AV_SAMPLE_FMT_FLT)
		sdl_fmt = AUDIO_F32SYS;
	else if (packed == AV_SAMPLE_FMT_S16)
		sdl_fmt = AUDIO_S16SYS;
	else
		return -1;
	sample_rate = frame->sample_rate > 0 ? frame->sample_rate :
		audio->codec->sample_rate;
	audio->stream = SDL_NewAudioStream(sdl_fmt, channels, sample_rate,
					   AUDIO_S16SYS, 2, 44100);
	if (!audio->stream) {
		fprintf(stderr, "sword3-sdl: movie audio stream failed: %s\n",
			SDL_GetError());
		return -1;
	}
	return 0;
}

static int movie_audio_put_frame(AVFrame *frame)
{
	struct movie_audio *audio = &g_movie.audio;
	enum AVSampleFormat packed;
	int planar;
	int channels;
	int samples;
	int sample_bytes;
	size_t bytes;
	int available;
	int got;

	if (movie_audio_ensure_stream(frame) != 0)
		return -1;
	channels = frame->ch_layout.nb_channels;
	if (channels <= 0)
		channels = audio->codec->ch_layout.nb_channels;
	samples = frame->nb_samples;
	if (channels <= 0 || samples <= 0)
		return 0;
	packed = av_get_packed_sample_fmt((enum AVSampleFormat)frame->format);
	planar = av_sample_fmt_is_planar((enum AVSampleFormat)frame->format);
	sample_bytes = av_get_bytes_per_sample(packed);
	if (sample_bytes <= 0)
		return -1;
	bytes = (size_t)samples * (size_t)channels * (size_t)sample_bytes;
	if (bytes > audio->packed_size) {
		void *grown = realloc(audio->packed, bytes);

		if (!grown)
			return -1;
		audio->packed = grown;
		audio->packed_size = bytes;
	}
	if (planar) {
		int sample;
		int channel;
		unsigned char *out = audio->packed;

		for (sample = 0; sample < samples; ++sample) {
			for (channel = 0; channel < channels; ++channel) {
				memcpy(out,
				       frame->extended_data[channel] +
					       sample * sample_bytes,
				       (size_t)sample_bytes);
				out += sample_bytes;
			}
		}
	} else {
		memcpy(audio->packed, frame->extended_data[0], bytes);
	}
	if (SDL_AudioStreamPut(audio->stream, audio->packed, (int)bytes) != 0)
		return -1;
	available = SDL_AudioStreamAvailable(audio->stream);
	if (available <= 0)
		return 0;
	if ((size_t)available > audio->pcm_size) {
		void *grown = realloc(audio->pcm, (size_t)available);

		if (!grown)
			return -1;
		audio->pcm = grown;
		audio->pcm_size = (size_t)available;
	}
	got = SDL_AudioStreamGet(audio->stream, audio->pcm, available);
	if (got > 0)
		SDL_QueueAudio(audio->device, audio->pcm, (Uint32)got);
	return 0;
}

static void movie_audio_pump(void)
{
	struct movie_audio *audio = &g_movie.audio;
	int attempts;
	int rc;

	if (!audio->device || audio->eof)
		return;
	for (attempts = 0; attempts < 24; ++attempts) {
		if (SDL_GetQueuedAudioSize(audio->device) >= 44100u * 4u / 2u)
			return;
		rc = avcodec_receive_frame(audio->codec, audio->frame);
		if (rc == 0) {
			if (movie_audio_put_frame(audio->frame) != 0) {
				fprintf(stderr,
					"sword3-sdl: movie audio convert failed\n");
				audio->eof = 1;
				return;
			}
			av_frame_unref(audio->frame);
			continue;
		}
		if (rc == AVERROR_EOF) {
			audio->eof = 1;
			return;
		}
		if (rc != AVERROR(EAGAIN)) {
			fprintf(stderr,
				"sword3-sdl: movie audio decode failed\n");
			audio->eof = 1;
			return;
		}
		if (audio->packet_pending) {
			rc = avcodec_send_packet(audio->codec, audio->packet);
			if (rc == 0) {
				audio->packet_pending = 0;
				av_packet_unref(audio->packet);
				continue;
			}
			if (rc != AVERROR(EAGAIN)) {
				audio->eof = 1;
				return;
			}
		}
		rc = av_read_frame(audio->format, audio->packet);
		if (rc == AVERROR_EOF) {
			avcodec_send_packet(audio->codec, NULL);
			continue;
		}
		if (rc < 0) {
			audio->eof = 1;
			return;
		}
		if (audio->packet->stream_index != audio->stream_index) {
			av_packet_unref(audio->packet);
			continue;
		}
		audio->packet_pending = 1;
	}
}

static void movie_close(int notify)
{
	void (*done)(void) = g_movie.done;

	if (g_movie.closing || (!g_movie.active && !g_movie.video))
		return;
	g_movie.closing = 1;
	g_movie.active = 0;
	g_movie.done = NULL;
	g_movie.have_frame = 0;
	g_movie.frames = 0;
	movie_audio_close();
	if (g_movie.texture) {
		owner_unregister("movie", KIND_TEXTURE, g_movie.texture);
		SDL_DestroyTexture(g_movie.texture);
		g_movie.texture = NULL;
	}
	sword3_video_close(g_movie.video);
	g_movie.video = NULL;
	g_movie.tex_w = 0;
	g_movie.tex_h = 0;
	if (notify && done)
		done();
	g_movie.closing = 0;
}

static int movie_ensure_texture(int width, int height)
{
	if (g_movie.texture && g_movie.tex_w == width &&
	    g_movie.tex_h == height)
		return 0;
	if (g_movie.texture) {
		owner_unregister("movie", KIND_TEXTURE, g_movie.texture);
		SDL_DestroyTexture(g_movie.texture);
		g_movie.texture = NULL;
	}
	g_movie.texture = SDL_CreateTexture(g_renderer, SDL_PIXELFORMAT_RGBA32,
					    SDL_TEXTUREACCESS_STREAMING,
					    width, height);
	if (!g_movie.texture) {
		fprintf(stderr, "sword3-sdl: movie texture failed: %s\n",
			SDL_GetError());
		return -1;
	}
	owner_register(KIND_TEXTURE, g_movie.texture);
	g_movie.tex_w = width;
	g_movie.tex_h = height;
#if SDL_VERSION_ATLEAST(2, 0, 12)
	SDL_SetTextureScaleMode(g_movie.texture, SDL_ScaleModeLinear);
#endif
	return 0;
}

static void movie_present(void)
{
	double elapsed;
	double hold;
	int decoded;

	if (!g_movie.active || !g_movie.video)
		return;
	elapsed = (double)(SDL_GetTicks() - g_movie.start_ms) / 1000.0;
	movie_audio_pump();
	for (;;) {
		hold = g_movie.frame.duration_seconds;
		if (hold <= 0.0)
			hold = 1.0 / 30.0;
		if (g_movie.have_frame &&
		    g_movie.frame.pts_seconds + hold > elapsed)
			break;
		decoded = sword3_video_next_frame(g_movie.video, &g_movie.frame);
		if (decoded <= 0) {
			fprintf(stderr, "sword3-sdl: movie finished (%s)\n",
				decoded < 0 ? sword3_video_error(g_movie.video) :
					      "eof");
			movie_close(1);
			return;
		}
		g_movie.have_frame = 1;
		if (movie_ensure_texture(g_movie.frame.width,
					 g_movie.frame.height) != 0) {
			movie_close(1);
			return;
		}
		if (SDL_UpdateTexture(g_movie.texture, NULL, g_movie.frame.rgba,
				      g_movie.frame.stride) != 0) {
			fprintf(stderr, "sword3-sdl: movie UpdateTexture: %s\n",
				SDL_GetError());
			movie_close(1);
			return;
		}
		g_movie.frames++;
		if (g_movie.frames == 1)
			fprintf(stderr,
				"sword3-sdl: movie first frame %dx%d\n",
				g_movie.frame.width, g_movie.frame.height);
		else if ((g_movie.frames % 30) == 0)
			fprintf(stderr, "sword3-sdl: movie frame %u t=%.2f\n",
				g_movie.frames, g_movie.frame.pts_seconds);
	}
	if (g_movie.texture)
		SDL_RenderCopy(g_renderer, g_movie.texture, NULL, NULL);
}

static void movie_skip(SDL_Scancode scancode)
{
	if (!g_movie.active)
		return;
	fprintf(stderr, "sword3-sdl: skipping movie\n");
	g_movie_swallow_keyup = scancode;
	movie_close(1);
}

static void movie_pump_events(void)
{
	SDL_Event event;

	SDL_PumpEvents();
	while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT,
			      SDL_LASTEVENT) > 0) {
		if (event.type == SDL_QUIT) {
			movie_close(1);
			return;
		}
		if (event.type == SDL_KEYDOWN && !event.key.repeat &&
		    (event.key.keysym.scancode == SDL_SCANCODE_SPACE ||
		     event.key.keysym.scancode == SDL_SCANCODE_RETURN ||
		     event.key.keysym.scancode == SDL_SCANCODE_ESCAPE)) {
			movie_skip(event.key.keysym.scancode);
			return;
		}
		if (event.type == SDL_FINGERDOWN ||
		    event.type == SDL_MOUSEBUTTONDOWN) {
			movie_skip(SDL_SCANCODE_SPACE);
			return;
		}
	}
}

static void movie_host_tick(void)
{
	Uint32 now;

	if (!g_movie.active || !g_renderer)
		return;
	now = SDL_GetTicks();
	if (g_movie.last_present_ms && now - g_movie.last_present_ms < 16)
		return;
	SDL_SetRenderTarget(g_renderer, NULL);
	SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
	SDL_RenderClear(g_renderer);
	movie_present();
	if (!g_movie.active)
		return;
	SDL_RenderPresent(g_renderer);
	g_movie.last_present_ms = now;
}

static void movie_run_until_done(void)
{
	fprintf(stderr, "sword3-sdl: movie blocking present loop\n");
	while (g_movie.active) {
		movie_pump_events();
		if (!g_movie.active)
			break;
		movie_host_tick();
		SDL_Delay(8);
	}
}

__attribute__((visibility("default")))
int sword3_host_video_playing(void)
{
	return g_movie.active;
}

__attribute__((visibility("default")))
int sword3_host_play_video_file(const char *path, void (*done)(void))
{
	if (!path || !path[0] || !g_renderer)
		return -1;
	movie_close(0);
	g_movie.video = sword3_video_open(path);
	if (!g_movie.video) {
		fprintf(stderr, "sword3-sdl: video open failed: %s\n", path);
		return -1;
	}
	if (movie_audio_open(path) != 0)
		fprintf(stderr, "sword3-sdl: movie audio unavailable for %s\n",
			path);
	g_movie.done = done;
	g_movie.start_ms = SDL_GetTicks();
	g_movie.last_present_ms = 0;
	g_movie.have_frame = 0;
	g_movie.active = 1;
	fprintf(stderr, "sword3-sdl: playing video %s\n", path);
	movie_run_until_done();
	return 0;
}

__attribute__((visibility("default")))
void sword3_host_stop_video(void)
{
	if (!g_movie.active)
		return;
	fprintf(stderr, "sword3-sdl: stopping video\n");
	movie_close(1);
}
