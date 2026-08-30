#include "sdl_bridge.h"
#include "host_menu.h"
#include "host_battle_menu.h"
#include "video_bridge.h"

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <sys/stat.h>

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
	owner_abort(api, kind, pointer);
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
 * dispatches SDL_FINGER* into InputKeyDown / InputClick, and also has
 * real SDL2 keyboard (mode keys) and controller (mode 4) branches.
 * Do not stub that function.
 *
 * Title and save are tap UIs. There the pad is a virtual pointer:
 *   stick / D-pad -> on-screen cursor (MouseXY + input mode 3)
 *   A             -> SDL_FINGERDOWN/UP at the cursor
 *   B             -> mouse right
 *   START swallowed; SELECT -> Escape; SELECT+START exits
 *
 * Field and system menu:
 *   SELECT        -> open the host-drawn system menu on the field
 *                    (does not open the iOS touch menu). Title/load
 *                    still send Escape. SWORD3_NATIVE_MENU=1 restores
 *                    the old tap / draw_gate path on SELECT.
 *   L3            -> open the original iOS system menu (compare only).
 *   B             -> close/back in the host menu. Original menu: tap
 *                    the back icon. Field: swallowed.
 *   A             -> confirm in the host menu; field Return talks.
 *   D-pad / stick in host menu -> tabs, or 天书 存盘/读取/记载/设置/离开.
 *                    Up on 天书 returns to the tab strip.
 *
 * In the field the physical pad is the pad:
 *   stick         -> SDL_CONTROLLERAXIS (UIGamePad mode 4 / virtual cross)
 *   D-pad         -> keyboard arrows (setting.lua KB_UP=82 etc.)
 *   A talks (Return); B back-only; SELECT host menu; L3 original menu
 *
 * Battle commands are a host-drawn overlay (攻击/奇术/物品/绝招/防御,
 * plus host magic/item/special submenus). Native menus are not driven.
 * The game is called only to carry out the chosen action. Target picking
 * stays on Battle_Input.
 *
 * After Continue, map 0 is no longer treated as the title so the
 * first field does not keep the mouse cursor.
 */
#define STICK_DEADZONE 14000
#define A_CHATTER_MS 200
#define CURSOR_PX_PER_SEC 380.0f
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
#define GUEST_MENU_PAGE 0x1002a9a20ull
#define GUEST_SYS_PAGE 0x1002aa46cull
#define GUEST_SYS_LEVEL 0x1002aa470ull
#define GUEST_SYS_SUB 0x1002aa474ull
#define GUEST_SYS_SLOT 0x1002aa480ull
#define GUEST_HALT_INP 0x1002aa484ull
#define GUEST_IN_MENU_SYSTEM 0x1002aa488ull
#define GUEST_SYS_COUNT 0x1002aa48cull
#define GUEST_MENU_OBJ 0x1002aa498ull
#define GUEST_FEXECUTE 0x1002a86a8ull
#define GUEST_SYS_MODE 0x1002a8500ull
#define GUEST_SYS_LAYER 0x1002a99ccull
#define GUEST_DRAW_GATE 0x10030f48cull
/* installer(0xea60) is the title table. installer(1) is the walking
 * field (click stub + talk confirm). Host must not call it to "close"
 * or "restore" the menu: fex==RET is the normal field table. */
#define GUEST_INSTALLER 0x100023d8cull
#define GUEST_FEX_RET 0x10005b87cull
#define GUEST_FEX_FIELD 0x1000831a8ull
#define GUEST_FEX_SYSPAGE 0x10002d018ull
#define GUEST_REBIND_ACTIONS 0x1001fa5e8ull
#define GUEST_PAD_READY 0x248c
#define GUEST_SLOT_MASK 0x10
#define GUEST_ACTION7_MASK 0x40u
#define GUEST_SAVE_FILE 0x100028318ull
#define GUEST_LOAD_GAME 0x100028f20ull
#define GUEST_RESET_KEYS 0x1001c18d4ull
#define GUEST_SAVE_INDEX 0x1002a9ca4ull
#define GUEST_LAYER_FIELD 0xea60
#define GUEST_LAYER_WALK 1
#define GUEST_DRAW_OBJ 0x10030f488ull
#define GUEST_STORE_DRAW 0x1001ccb98ull
#define MENU_TRACE_PATH "/tmp/sword3-menu-trace"
#define MENU_TRACE_Q 32
#define MENU_OPEN_WAIT_MS 900
#define MENU_BACK_GRACE_MS 450
#define MENU_CHROME_GONE_MS 250
#define GUEST_MENU_DIALOG 0x1002aa7d0ull
#define GUEST_LOAD_ACTIVE 0x1002aa3c8ull
#define GUEST_MENU_WIDGET_ROOT 0x1002ab840ull
#define GUEST_WIDGET_NEXT 0xb8
#define GUEST_WIDGET_ID 0x8c
#define GUEST_WIDGET_X 0x20
#define GUEST_WIDGET_Y 0x24
#define GUEST_WIDGET_H 0x94
#define GUEST_WIDGET_W 0x98
#define GUEST_WIDGET_SHOW 0x9f
#define GUEST_OVERLAY_CONFIG 0x1c0
#define GUEST_OVERLAY_RECT 0x70
#define GUEST_OVERLAY_ENABLE 0x328
#define GUEST_OVERLAY_HUD 0x330
#define GUEST_MENU_BACK 0x2495
#define GUEST_MENU_TAB0 0x2496
#define GUEST_MENU_TAB1 0x2497
#define GUEST_MENU_TAB2 0x2498
#define GUEST_CONTINUE_WIDGET 0x1002a9418ull
#define GUEST_MOUSE_X 0x2470
#define GUEST_MOUSE_Y 0x2474
#define GUEST_INPUT_MODE 0x23d4
#define GUEST_KEY_SLOT 0x320
#define GUEST_KEY_STRIDE 0x18
#define GUEST_PAD_SLOT 0x20c0
#define GUEST_PAD_BUTTONS 16
#define GUEST_DPAD_SLOT 0x2404
#define GUEST_DPAD_STRIDE 0x18
#define GUEST_VIEW_ORIGIN 0x1c0
#define GUEST_VIEW_SIZE 0x1c8
#define GUEST_VIEW_SCALE 0x1c
#define GUEST_FINGER_SIZE 0x19c
#define GUEST_FINGER_SCALE 0x1b8
#define GUEST_KEYSTATE 0x10031ca82ull
#define GUEST_KEYSTATE_N 512
#define GUEST_INPUT_KEYBOARD 1
#define GUEST_INPUT_MOUSE 3
#define GUEST_UI_SAVE 2
#define GUEST_FIGHT_FLAG 0x1002f27f8ull
#define GUEST_TITLE_SELECTION 0x1002f40e0ull
#define GUEST_TITLE_MODE 0x1002f40e4ull
#define GUEST_NOW_MENU 0x1002f1f0cull
#define GUEST_CMD_SEL 0x1002a5308ull
#define GUEST_ACTIVE_JOYSTICK 0x1c
#define GUEST_INPUT_TRANSITION 0x1001c15fcull
#define GUEST_FIGHT_OK 0x10003ddf8ull
#define GUEST_FIGHT_UP 0x10003f13cull
#define GUEST_FIGHT_RIGHT 0x10003e668ull
#define GUEST_FIGHT_DOWN 0x10003f28cull
#define GUEST_FIGHT_LEFT 0x10003dff8ull
#define GUEST_FIGHT_CANCEL 0x10003f3ecull
#define FIGHT_NOW_TURN 99
#define FIGHT_NOW_AUTO 100

static SDL_Window *g_window;
static SDL_Renderer *g_renderer;
static SDL_GameController *g_pad;
static int g_logical_w;
static int g_logical_h;
static float g_cursor_x;
static float g_cursor_y;
static int g_cursor_ready;
static int g_finger_down;
static int g_btn_back;
static int g_btn_start;
static int g_menu_item;
static int g_pointer_ui = 1;
static int g_save_ui;
static int g_load_context_done;
static int g_save_dir_down[4];
static int g_save_dir_release[4];
static int g_after_continue;
static int g_title_keys;
static int g_title_dir_dpad[4];
static int g_title_dir_axis[4];
static int g_title_dir_down[4];
static int g_menu_ui;
static int g_menu_keys;
static int g_menu_session;
static int g_menu_open_pending;
static Uint32 g_menu_open_until;
static int g_menu_seen_chrome;
static Uint32 g_menu_gone_at;
static Uint32 g_menu_back_until;
static int g_menu_logged_postopen;
static int g_menu_saw_draw;
static int g_menu_open_direct;
static Uint32 g_menu_reenter_until;
static int g_menu_return_held;
static Uint32 g_menu_return_up_at;
static int g_menu_dir_dpad[4];
static int g_menu_dir_axis[4];
static int g_menu_dir_down[4];
static int g_menu_dir_release[4];
static unsigned g_menu_key_seen;
static int g_fight_ui;
static int g_fight_dir_dpad[4];
static int g_fight_dir_axis[4];
static int g_fight_dir_down[4];
static int g_fight_dir_release[4];
static int g_fight_target_a_up;
static unsigned g_fight_key_seen;
static int g_menu_open_button = -1;
static int g_menu_close_widget;
static int g_menu_close_pending;
static int g_menu_tab_index;
static int g_menu_action_focus = -1;
static int g_menu_action_a_up;
static int g_menu_nested;
static Uint32 g_menu_a_block_until;
static int g_a_down;
static Uint32 g_a_edge_ms;
static unsigned g_a_chatter;
static int g_field_mode_saved;
static int g_field_mode_held;
static unsigned g_field_a_seen;
static unsigned g_finger_seen;
static unsigned g_pad_seen[GUEST_PAD_BUTTONS];
static Uint32 g_cursor_ticks;
static int g_swallow_a_up;

static int movie_is_playing(void);
static int movie_consume_skip(int button, int down);
static void movie_present(SDL_Renderer *renderer);
static void movie_host_tick(void);
static void maybe_force_opening(void);
static Uint32 g_last_present_ms;
static unsigned g_movie_frames;

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
		x = event->motion.x;
		y = event->motion.y;
		break;
	case SDL_MOUSEBUTTONDOWN:
	case SDL_MOUSEBUTTONUP:
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

static Uint32 window_id(void)
{
	return g_window ? SDL_GetWindowID(g_window) : 0;
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

static int guest_data_ok(uintptr_t addr)
{
	return addr >= 0x100294000ull && addr < 0x100380000ull;
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

	if (size[0] <= 0 || size[1] <= 0) {
		origin[0] = 0;
		origin[1] = 0;
		size[0] = lw;
		size[1] = lh;
	}
	if (scale[0] == 0.0f && scale[1] == 0.0f) {
		scale[0] = (float)GAME_W / (float)lw;
		scale[1] = (float)GAME_H / (float)lh;
	}
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
			"sword3-sdl: guest viewport %dx%d scale %.3f,%.3f finger %dx%d\n",
			lw, lh, scale[0], scale[1], finger_size[0],
			finger_size[1]);
	}
}

static void try_init_guest_viewport(void)
{
	if (!guest_data_ok(GUEST_SCREEN))
		return;
	maybe_init_guest_viewport((uint8_t *)(uintptr_t)GUEST_SCREEN);
}

static void guest_clamp_rect(int *x, int *y, int w, int h, int fw, int fh)
{
	if (*x + w > fw)
		*x = fw - w;
	if (*y + h > fh)
		*y = fh - h;
	if (*x < 0)
		*x = 0;
	if (*y < 0)
		*y = 0;
}

/*
 * 0x1001f7b98 clamps every widget to (0,0) when Screen.finger_size is 0.
 * If layout already ran that way, shove the HUD back to the landscape
 * positions the loader would have used with a real 800×600 finger area.
 */
static void maybe_fix_overlay_widgets(uint8_t *screen)
{
	static int done;
	uint8_t *pad;
	int fw;
	int fh;
	int size;
	int gap;
	int portrait;
	int x;
	int y;
	int w;
	int dw;
	float scale;
	volatile int *confirm;
	volatile int *config;
	volatile int *undo;
	volatile int *dpad;
	volatile int *a_btn;

	if (done)
		return;
	if (!guest_data_ok(GUEST_UIGAMEPAD))
		return;
	pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
	fw = ((volatile int *)(screen + GUEST_FINGER_SIZE))[0];
	fh = ((volatile int *)(screen + GUEST_FINGER_SIZE))[1];
	size = *(volatile int *)(pad + 0x340);
	if (fw <= 1 || fh <= 1 || size <= 0)
		return;
	confirm = (volatile int *)(pad + 0x138 + 0x70);
	config = (volatile int *)(pad + 0x1c0 + 0x70);
	undo = (volatile int *)(pad + 0x248 + 0x70);
	if (confirm[0] != 0 || confirm[1] != 0 || config[0] != 0 ||
	    config[1] != 0)
		return;
	done = 1;
	scale = *(volatile float *)(pad + 0x334);
	gap = (int)(scale * 0.2f);
	portrait = *(volatile int *)(pad + 0x344);
	x = portrait ? 0 : gap;
	y = 0;
	guest_clamp_rect(&x, &y, size, size, fw, fh);
	confirm[0] = x;
	confirm[1] = y;
	confirm[2] = size;
	confirm[3] = size;
	x = confirm[0] + gap + size;
	y = 0;
	guest_clamp_rect(&x, &y, size, size, fw, fh);
	config[0] = x;
	config[1] = y;
	config[2] = size;
	config[3] = size;
	x = config[0] + gap + size;
	y = 0;
	guest_clamp_rect(&x, &y, size, size, fw, fh);
	undo[0] = x;
	undo[1] = y;
	undo[2] = size;
	undo[3] = size;
	w = *(volatile int *)(pad + 0x33c);
	dw = *(volatile int *)(pad + 0x338);
	dpad = (volatile int *)(pad + 0x9c);
	a_btn = (volatile int *)(pad + 0xb0 + 0x70);
	if (dpad[0] == 0 && dpad[1] == 0 && dw > 0) {
		x = gap;
		y = (fh / 2) - ((dw + 3) >> 2);
		guest_clamp_rect(&x, &y, dw, dw, fw, fh);
		dpad[0] = x;
		dpad[1] = y;
		dpad[2] = dw;
		dpad[3] = dw;
	}
	if (a_btn[0] == 0 && a_btn[1] == 0 && w > 0) {
		x = fw - gap - w;
		y = (fh / 2) - (w / 3);
		guest_clamp_rect(&x, &y, w, w, fw, fh);
		a_btn[0] = x;
		a_btn[1] = y;
		a_btn[2] = w;
		a_btn[3] = w;
	}
	fprintf(stderr,
		"sword3-sdl: unstacked overlay widgets finger=%dx%d size=%d\n",
		fw, fh, size);
}

static int guest_map_id(void)
{
	if (!guest_data_ok(GUEST_MAP_ID))
		return -1;
	return *(volatile int *)(uintptr_t)GUEST_MAP_ID;
}

static int guest_on_title(void)
{
	int map = guest_map_id();

	/*
	 * Map 0 is the boot/title value. Maps 0x1e+ are playable (the first
	 * field used to be misclassified as title, which kept the pad in
	 * pointer mode so walking never started).
	 */
	if (map < 0)
		return 1;
	return map == 0;
}

static int guest_title_visible(void)
{
	if (!guest_data_ok(GUEST_TITLE_MODE))
		return 0;
	return *(volatile int *)(uintptr_t)GUEST_TITLE_MODE == 1 &&
	       guest_map_id() == 0;
}

static void fill_key(SDL_Event *event, SDL_Scancode scancode, int down);
static void fill_finger(SDL_Event *event, Uint32 type);
static void save_release_directions(void);
static void title_release_directions(void);
static void menu_release_directions(void);
static void fight_release_directions(void);
static void release_guest_walk(void);
static void push_finger_at(float x, float y, Uint32 type);
static int menu_widget_rect(int id, int *x, int *y, int *w, int *h);
static int menu_chrome_drawn(void);
static int menu_widget_drawn(int id);
static int menu_opening(void);
static void menu_axis_event(Uint8 axis, Sint16 value);
static void menu_poll_open_pulse(void);
static int menu_try_close(void);
static int menu_click_open_button(void);
static int menu_click_back(void);
static void menu_open_field(void);
static void guest_keyboard_key(SDL_Scancode scancode, int down);
static void menu_trace_poll(void);

static int guest_save_ui(void)
{
	if (!guest_data_ok(GUEST_UI_FLAGS))
		return 0;
	return (*(volatile int *)(uintptr_t)GUEST_UI_FLAGS & GUEST_UI_SAVE) != 0;
}

static int guest_load_ui(void)
{
	if (g_load_context_done)
		return 0;
	if (!guest_data_ok(GUEST_TITLE_MODE) ||
	    !guest_data_ok(GUEST_MENU_PAGE) ||
	    !guest_data_ok(GUEST_LOAD_ACTIVE))
		return 0;
	return *(volatile uint8_t *)(uintptr_t)GUEST_LOAD_ACTIVE != 0 &&
	       *(volatile int *)(uintptr_t)GUEST_TITLE_MODE == 2 &&
	       *(volatile int *)(uintptr_t)GUEST_MENU_PAGE == 3;
}

static int guest_menu_flag(void)
{
	if (!guest_data_ok(GUEST_IN_MENU_SYSTEM))
		return -1;
	return *(volatile int *)(uintptr_t)GUEST_IN_MENU_SYSTEM;
}

static int menu_drawn(void)
{
	if (!guest_data_ok(GUEST_DRAW_GATE))
		return 0;
	return *(volatile int *)(uintptr_t)GUEST_DRAW_GATE != 0;
}

static int menu_select_busy(void)
{
	if (menu_drawn() || menu_opening())
		return 1;
	return g_menu_session && !g_menu_saw_draw;
}

static int native_system_menu(void)
{
	const char *value;

	value = getenv("SWORD3_NATIVE_MENU");
	return value && value[0] == '1' && value[1] == '\0';
}

static int guest_system_menu(void)
{
	if (guest_load_ui())
		return 0;
	return menu_select_busy();
}

static int guest_read_i32(uintptr_t addr, int fallback)
{
	if (!guest_data_ok(addr))
		return fallback;
	return *(volatile int *)(uintptr_t)addr;
}

static uintptr_t guest_read_ptr(uintptr_t addr)
{
	if (!guest_data_ok(addr + 7))
		return 0;
	return *(volatile uintptr_t *)(uintptr_t)addr;
}

static void menu_log_state(const char *why)
{
	fprintf(stderr,
		"sword3-sdl: menust %s draw=%d layer=%d in=%d page=%d lv=%d sub=%d slot=%d halt=%d cnt=%d obj=%p fex=%p\n",
		why,
		guest_read_i32(GUEST_DRAW_GATE, -1),
		guest_read_i32(GUEST_SYS_LAYER, -1),
		guest_menu_flag(),
		guest_read_i32(GUEST_SYS_PAGE, -1),
		guest_read_i32(GUEST_SYS_LEVEL, -1),
		guest_read_i32(GUEST_SYS_SUB, -1),
		guest_read_i32(GUEST_SYS_SLOT, -1),
		guest_read_i32(GUEST_HALT_INP, -1),
		guest_read_i32(GUEST_SYS_COUNT, -1),
		(void *)guest_read_ptr(GUEST_MENU_OBJ),
		(void *)guest_read_ptr(GUEST_FEXECUTE));
}

static int guest_now_menu(void)
{
	return guest_read_i32(GUEST_NOW_MENU, 0);
}

static int guest_cmd_sel(void)
{
	return guest_read_i32(GUEST_CMD_SEL, 0);
}

static int fight_now_active(int now)
{
	return (now > 0 && now < 0x40) ||
	       now == FIGHT_NOW_TURN ||
	       (now >= FIGHT_NOW_AUTO && now <= FIGHT_NOW_AUTO + 2);
}

static int guest_in_fight(void)
{
	int flag;
	int now;

	/*
	 * inFight is FightFlag bit 1. Other bits stay set on the field, so
	 * flag != 0 would steal the stick. NowMenu covers command / target
	 * / turn / auto while a fight is actually running.
	 */
	flag = guest_read_i32(GUEST_FIGHT_FLAG, 0);
	if (flag & 2)
		return 1;
	now = guest_now_menu();
	return fight_now_active(now);
}

static void fight_poll_ui(void)
{
	int fight_ui;

	if (g_pointer_ui || g_menu_keys || g_title_keys || host_menu_active())
		return;
	fight_ui = guest_in_fight();
	if (fight_ui)
		g_save_ui = 0;
	if (g_fight_ui && !fight_ui) {
		fight_release_directions();
		release_guest_walk();
		host_battle_close();
	}
	g_fight_ui = fight_ui;
	if (g_fight_ui)
		host_battle_poll();
}

static void draw_select_frame(SDL_Renderer *renderer, int x, int y, int w,
			      int h)
{
	SDL_Rect arm[4];
	Uint8 r;
	Uint8 g;
	Uint8 b;
	Uint8 a;
	SDL_BlendMode blend;
	int t = 3;
	int i;

	if (w < 12)
		w = 12;
	if (h < 12)
		h = 12;
	SDL_GetRenderDrawColor(renderer, &r, &g, &b, &a);
	SDL_GetRenderDrawBlendMode(renderer, &blend);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 200);
	arm[0].x = x - 1;
	arm[0].y = y - 1;
	arm[0].w = w + 2;
	arm[0].h = t + 1;
	arm[1].x = x - 1;
	arm[1].y = y + h - t;
	arm[1].w = w + 2;
	arm[1].h = t + 1;
	arm[2].x = x - 1;
	arm[2].y = y - 1;
	arm[2].w = t + 1;
	arm[2].h = h + 2;
	arm[3].x = x + w - t;
	arm[3].y = y - 1;
	arm[3].w = t + 1;
	arm[3].h = h + 2;
	for (i = 0; i < 4; i++)
		SDL_RenderFillRect(renderer, &arm[i]);
	SDL_SetRenderDrawColor(renderer, 255, 220, 64, 255);
	arm[0].x = x;
	arm[0].y = y;
	arm[0].w = w;
	arm[0].h = t;
	arm[1].x = x;
	arm[1].y = y + h - t;
	arm[1].w = w;
	arm[1].h = t;
	arm[2].x = x;
	arm[2].y = y;
	arm[2].w = t;
	arm[2].h = h;
	arm[3].x = x + w - t;
	arm[3].y = y;
	arm[3].w = t;
	arm[3].h = h;
	for (i = 0; i < 4; i++)
		SDL_RenderFillRect(renderer, &arm[i]);
	SDL_SetRenderDrawColor(renderer, r, g, b, a);
	SDL_SetRenderDrawBlendMode(renderer, blend);
}

static void clear_input_slot(uint8_t *slot)
{
	slot[0] = 0;
	slot[8] = 0;
}

static void release_guest_walk(void)
{
	static const SDL_Scancode keys[] = {
		SDL_SCANCODE_UP,
		SDL_SCANCODE_DOWN,
		SDL_SCANCODE_LEFT,
		SDL_SCANCODE_RIGHT,
		SDL_SCANCODE_RETURN,
		SDL_SCANCODE_ESCAPE,
	};
	uint8_t *pad;
	uint8_t *keystate;
	int i;

	if (!guest_data_ok(GUEST_UIGAMEPAD))
		return;
	pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
	for (i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++)
		clear_input_slot(pad + GUEST_KEY_SLOT +
				 keys[i] * GUEST_KEY_STRIDE);
	for (i = 0; i < 4; i++)
		clear_input_slot(pad + GUEST_DPAD_SLOT + i * GUEST_DPAD_STRIDE);
	for (i = 0; i < GUEST_PAD_BUTTONS; i++)
		clear_input_slot(pad + GUEST_PAD_SLOT + i * GUEST_KEY_STRIDE);
	if (guest_data_ok(GUEST_KEYSTATE) &&
	    guest_data_ok(GUEST_KEYSTATE + (uintptr_t)GUEST_KEYSTATE_N - 1)) {
		keystate = (uint8_t *)(uintptr_t)GUEST_KEYSTATE;
		for (i = 0; i < (int)(sizeof(keys) / sizeof(keys[0])); i++)
			keystate[keys[i]] = 0;
	}
}

static void warp_screen_center(void)
{
	if (g_logical_w <= 0 || g_logical_h <= 0)
		return;
	g_cursor_x = (float)g_logical_w * 0.5f;
	g_cursor_y = (float)g_logical_h * 0.5f;
	clamp_cursor();
	g_cursor_ready = 1;
}

static void sync_ui_mode(void)
{
	int on_title = guest_on_title();
	int save_ui = guest_save_ui() || guest_load_ui();
	int title_ui = guest_title_visible() && !save_ui;
	int menu_ui = guest_system_menu();
	int pointer_ui;
	int menu_keys;
	int fight_ui;

	if (!on_title && !save_ui)
		g_after_continue = 0;
	pointer_ui = 0;
	menu_keys = !title_ui && !save_ui && menu_ui;
	fight_ui = !pointer_ui && !menu_keys && guest_in_fight();
	/*
	 * Battle reuses UI_FLAGS bit 1 (GUEST_UI_SAVE). If we keep save_ui
	 * on, D-pad/stick are stolen by the save mapper.
	 */
	if (fight_ui)
		save_ui = 0;
	if ((fight_ui || title_ui) && host_menu_active())
		host_menu_close();
	if (fight_ui)
		host_battle_poll();
	else
		host_battle_close();
	if (pointer_ui == g_pointer_ui && title_ui == g_title_keys &&
	    save_ui == g_save_ui &&
	    menu_ui == g_menu_ui && menu_keys == g_menu_keys &&
	    fight_ui == g_fight_ui)
		return;
	if (pointer_ui && !g_pointer_ui) {
		release_guest_walk();
		g_a_down = 0;
		if (pointer_ui && !title_ui)
			warp_screen_center();
		g_cursor_ticks = 0;
	}
	if (g_pointer_ui && !pointer_ui) {
		if (g_finger_down) {
			SDL_Event event;

			memset(&event, 0, sizeof(event));
			fill_finger(&event, SDL_FINGERUP);
			SDL_PushEvent(&event);
			g_finger_down = 0;
		}
		release_guest_walk();
		g_a_down = 0;
		g_finger_down = 0;
	}
	if (title_ui && !g_title_keys &&
	    guest_data_ok(GUEST_TITLE_SELECTION)) {
		volatile int *selection =
			(volatile int *)(uintptr_t)GUEST_TITLE_SELECTION;

		if (*selection < 0 || *selection > 1)
			*selection = 0;
	}
	if (g_title_keys && !title_ui)
		title_release_directions();
	if (g_save_ui != save_ui) {
		save_release_directions();
		g_a_down = 0;
	}
	if (menu_keys && !g_menu_keys) {
		release_guest_walk();
		menu_release_directions();
		g_a_down = 0;
	}
	if (g_menu_keys && !menu_keys) {
		menu_release_directions();
		release_guest_walk();
		g_a_down = 0;
	}
	if (g_fight_ui && !fight_ui) {
		fight_release_directions();
		release_guest_walk();
		host_battle_close();
	}
	g_save_ui = save_ui;
	g_title_keys = title_ui;
	g_menu_ui = menu_ui;
	g_pointer_ui = pointer_ui;
	g_menu_keys = menu_keys;
	g_fight_ui = fight_ui;
	fprintf(stderr,
		"sword3-sdl: ui pointer=%d menu=%d fight=%d save=%d detected=%d draw=%d in=%d layer=%d syspage=%d syslv=%d page=%d now=%d sel=%d after=%d map=%d flags=%d back=%d tabs=%d cursor=%.0f,%.0f\n",
		pointer_ui, menu_keys, fight_ui, save_ui, menu_ui,
		guest_read_i32(GUEST_DRAW_GATE, -1),
		guest_menu_flag(),
		guest_read_i32(GUEST_SYS_LAYER, -1),
		guest_read_i32(GUEST_SYS_PAGE, -1),
		guest_read_i32(GUEST_SYS_LEVEL, -1),
		guest_data_ok(GUEST_MENU_PAGE)
			? *(volatile int *)(uintptr_t)GUEST_MENU_PAGE
			: -1,
		guest_now_menu(), guest_cmd_sel(), g_after_continue,
		guest_map_id(), guest_data_ok(GUEST_UI_FLAGS)
			? *(volatile int *)(uintptr_t)GUEST_UI_FLAGS
			: -1,
		menu_widget_drawn(GUEST_MENU_BACK),
		menu_chrome_drawn(),
		g_cursor_x, g_cursor_y);
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
	maybe_fix_overlay_widgets(screen);
	if (!g_pointer_ui)
		return;
	if (!g_cursor_ready)
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

static void nudge_cursor(float dx, float dy)
{
	ensure_cursor();
	g_cursor_x += dx;
	g_cursor_y += dy;
	clamp_cursor();
}

static void cursor_norm(float *x, float *y)
{
	float nx = 0.5f;
	float ny = 0.5f;

	ensure_cursor();
	if (g_logical_w > 0 && g_logical_h > 0) {
		nx = g_cursor_x / (float)g_logical_w;
		ny = g_cursor_y / (float)g_logical_h;
	}
	if (nx < 0.0f)
		nx = 0.0f;
	if (nx > 1.0f)
		nx = 1.0f;
	if (ny < 0.0f)
		ny = 0.0f;
	if (ny > 1.0f)
		ny = 1.0f;
	*x = nx;
	*y = ny;
}

static void fill_mouse_button(SDL_Event *event, Uint8 button, int down)
{
	ensure_cursor();
	event->type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	event->button.timestamp = SDL_GetTicks();
	event->button.windowID = window_id();
	event->button.which = 0;
	event->button.button = button;
	event->button.state = down ? SDL_PRESSED : SDL_RELEASED;
	event->button.clicks = 1;
	event->button.x = (Sint32)g_cursor_x;
	event->button.y = (Sint32)g_cursor_y;
}

static void fill_finger(SDL_Event *event, Uint32 type)
{
	float x;
	float y;

	cursor_norm(&x, &y);
	event->type = type;
	event->tfinger.timestamp = SDL_GetTicks();
	event->tfinger.touchId = TOUCH_ID;
	event->tfinger.fingerId = FINGER_ID;
	event->tfinger.x = x;
	event->tfinger.y = y;
	event->tfinger.dx = 0.0f;
	event->tfinger.dy = 0.0f;
	event->tfinger.pressure = (type == SDL_FINGERUP) ? 0.0f : 1.0f;
}

static void set_scancode_state(SDL_Scancode scancode, int down)
{
	uint8_t *guest;
	int n = (int)scancode;

	if (n < 0 || n >= GUEST_KEYSTATE_N)
		return;
	if (guest_data_ok(GUEST_KEYSTATE) &&
	    guest_data_ok(GUEST_KEYSTATE + (uintptr_t)GUEST_KEYSTATE_N - 1)) {
		guest = (uint8_t *)(uintptr_t)GUEST_KEYSTATE;
		guest[n] = down ? 1 : 0;
	}
}

static void fill_key(SDL_Event *event, SDL_Scancode scancode, int down)
{
	memset(event, 0, sizeof(*event));
	event->type = down ? SDL_KEYDOWN : SDL_KEYUP;
	event->key.timestamp = SDL_GetTicks();
	event->key.windowID = window_id();
	event->key.state = down ? SDL_PRESSED : SDL_RELEASED;
	event->key.repeat = 0;
	event->key.keysym.scancode = scancode;
	event->key.keysym.sym = SDL_GetKeyFromScancode(scancode);
	event->key.keysym.mod = 0;
	set_scancode_state(scancode, down);
}

static void emit_finger_motion(void)
{
	SDL_Event event;

	memset(&event, 0, sizeof(event));
	fill_finger(&event, SDL_FINGERMOTION);
	SDL_PushEvent(&event);
}

static void maybe_combo_exit(void)
{
	int start = g_btn_start;
	int back = g_btn_back;

	if (g_pad) {
		start |= SDL_GameControllerGetButton(
			g_pad, SDL_CONTROLLER_BUTTON_START);
		back |= SDL_GameControllerGetButton(g_pad,
						    SDL_CONTROLLER_BUTTON_BACK);
	}
	if (start && back) {
		fprintf(stderr, "sword3-sdl: SELECT+START -> exit\n");
		_exit(0);
	}
}

static void ensure_gamecontroller(void)
{
	int i;
	int n;
	char *mapping;

	if (g_pad)
		return;
	if (SDL_InitSubSystem(SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER) != 0) {
		fprintf(stderr, "sword3-sdl: joystick init failed (%s)\n",
			SDL_GetError());
		return;
	}
	SDL_JoystickEventState(SDL_ENABLE);
	SDL_GameControllerEventState(SDL_ENABLE);
	n = SDL_NumJoysticks();
	fprintf(stderr, "sword3-sdl: %d joysticks\n", n);
	for (i = 0; i < n; i++) {
		fprintf(stderr, "sword3-sdl: js%d '%s' gc=%d\n", i,
			SDL_JoystickNameForIndex(i) ?
				SDL_JoystickNameForIndex(i) :
				"?",
			SDL_IsGameController(i));
		if (!g_pad && SDL_IsGameController(i)) {
			g_pad = SDL_GameControllerOpen(i);
			if (g_pad) {
				mapping = SDL_GameControllerMapping(g_pad);
				fprintf(stderr, "sword3-sdl: pad opened '%s'\n",
					SDL_GameControllerName(g_pad));
				fprintf(stderr, "sword3-sdl: mapping %s\n",
					mapping ? mapping : "(none)");
				SDL_free(mapping);
			}
		}
	}
}

static void apply_cursor_move(void)
{
	Sint16 lx;
	Sint16 ly;
	Sint16 rx;
	Sint16 ry;
	float dx;
	float dy;
	float speed;
	float dt;
	Uint32 now;
	static unsigned move_seen;

	if (!g_pointer_ui || !g_pad || g_logical_w <= 0 || g_logical_h <= 0)
		return;
	now = SDL_GetTicks();
	if (!g_cursor_ticks)
		g_cursor_ticks = now;
	dt = (float)(now - g_cursor_ticks) / 1000.0f;
	g_cursor_ticks = now;
	if (dt <= 0.0f)
		return;
	if (dt > 0.05f)
		dt = 0.05f;
	speed = CURSOR_PX_PER_SEC * (float)g_logical_w / (float)GAME_W;
	dx = 0.0f;
	dy = 0.0f;
	lx = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTX);
	ly = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_LEFTY);
	rx = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTX);
	ry = SDL_GameControllerGetAxis(g_pad, SDL_CONTROLLER_AXIS_RIGHTY);
	if (lx > STICK_DEADZONE || lx < -STICK_DEADZONE)
		dx += ((float)lx / 32767.0f) * speed * dt;
	if (ly > STICK_DEADZONE || ly < -STICK_DEADZONE)
		dy += ((float)ly / 32767.0f) * speed * dt;
	if (rx > STICK_DEADZONE || rx < -STICK_DEADZONE)
		dx += ((float)rx / 32767.0f) * speed * dt;
	if (ry > STICK_DEADZONE || ry < -STICK_DEADZONE)
		dy += ((float)ry / 32767.0f) * speed * dt;
	if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_LEFT))
		dx -= speed * dt;
	if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_RIGHT))
		dx += speed * dt;
	if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_UP))
		dy -= speed * dt;
	if (SDL_GameControllerGetButton(g_pad, SDL_CONTROLLER_BUTTON_DPAD_DOWN))
		dy += speed * dt;
	if (dx == 0.0f && dy == 0.0f)
		return;
	nudge_cursor(dx, dy);
	if (g_finger_down)
		emit_finger_motion();
	if (move_seen < 40) {
		move_seen++;
		fprintf(stderr, "sword3-sdl: cursor %.0f,%.0f\n", g_cursor_x,
			g_cursor_y);
	}
}

static const SDL_Scancode g_save_dir_keys[4] = {
	SDL_SCANCODE_UP,
	SDL_SCANCODE_RIGHT,
	SDL_SCANCODE_DOWN,
	SDL_SCANCODE_LEFT,
};

static void save_set_key_state(int index, int down)
{
	uint8_t *pad;
	uint8_t *slot;
	void (*transition)(void *, void *, int);

	if (index < 0 || index >= 4 || !guest_data_ok(GUEST_UIGAMEPAD))
		return;
	pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
	slot = pad + GUEST_KEY_SLOT +
	       g_save_dir_keys[index] * GUEST_KEY_STRIDE;
	*(volatile int *)(pad + GUEST_INPUT_MODE) = 1;
	transition = (void (*)(void *, void *, int))(uintptr_t)
		GUEST_INPUT_TRANSITION;
	transition(pad, slot, down);
	set_scancode_state(g_save_dir_keys[index], down);
	if (down) {
		slot[8] = 0;
		*(volatile Uint32 *)(slot + 4) = SDL_GetTicks() - 0x209u;
	}
}

static void save_dpad_event(int button, int down)
{
	int index;

	switch (button) {
	case SDL_CONTROLLER_BUTTON_DPAD_UP:
		index = 0;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
		index = 1;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
		index = 2;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
		index = 3;
		break;
	default:
		return;
	}
	if (down) {
		g_save_dir_release[index] = 0;
		if (!g_save_dir_down[index]) {
			g_save_dir_down[index] = 1;
			save_set_key_state(index, 1);
		}
	} else if (g_save_dir_down[index]) {
		g_save_dir_release[index] = 1;
	}
}

static void save_release_directions(void)
{
	int i;

	memset(g_save_dir_release, 0, sizeof(g_save_dir_release));
	for (i = 0; i < 4; i++) {
		if (!g_save_dir_down[i])
			continue;
		g_save_dir_down[i] = 0;
		save_set_key_state(i, 0);
	}
}

static void save_flush_direction_releases(void)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (!g_save_dir_release[i])
			continue;
		g_save_dir_release[i] = 0;
		g_save_dir_down[i] = 0;
		save_set_key_state(i, 0);
	}
}

static void title_set_dir_source(int index, int axis, int down)
{
	volatile int *selection;
	int wanted;
	int value;

	if (index < 0 || index >= 4)
		return;
	if (axis)
		g_title_dir_axis[index] = down;
	else
		g_title_dir_dpad[index] = down;
	wanted = g_title_dir_axis[index] || g_title_dir_dpad[index];
	if (wanted == g_title_dir_down[index])
		return;
	g_title_dir_down[index] = wanted;
	if (!wanted || (index != 0 && index != 2))
		return;
	if (!guest_data_ok(GUEST_TITLE_SELECTION))
		return;
	selection = (volatile int *)(uintptr_t)GUEST_TITLE_SELECTION;
	value = *selection;
	if (value < 0 || value > 1)
		value = 0;
	if (index == 0)
		value = value > 0 ? value - 1 : 0;
	else
		value = value < 1 ? value + 1 : 1;
	*selection = value;
	fprintf(stderr, "sword3-sdl: title selection=%d\n", value);
}

static void title_axis_event(Uint8 axis, Sint16 value)
{
	int old_dir;
	int new_dir;

	if (axis == SDL_CONTROLLER_AXIS_LEFTY) {
		old_dir = g_title_dir_axis[0] ? 0 :
			  (g_title_dir_axis[2] ? 2 : -1);
		new_dir = value < -STICK_DEADZONE ? 0 :
			  (value > STICK_DEADZONE ? 2 : -1);
	} else {
		return;
	}
	if (old_dir == new_dir)
		return;
	if (old_dir >= 0)
		title_set_dir_source(old_dir, 1, 0);
	if (new_dir >= 0)
		title_set_dir_source(new_dir, 1, 1);
}

static void title_dpad_event(int button, int down)
{
	int index;

	switch (button) {
	case SDL_CONTROLLER_BUTTON_DPAD_UP:
		index = 0;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
		index = 1;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
		index = 2;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
		index = 3;
		break;
	default:
		return;
	}
	title_set_dir_source(index, 0, down);
}

static void title_release_directions(void)
{
	memset(g_title_dir_dpad, 0, sizeof(g_title_dir_dpad));
	memset(g_title_dir_axis, 0, sizeof(g_title_dir_axis));
	memset(g_title_dir_down, 0, sizeof(g_title_dir_down));
}

static uint8_t *menu_find_widget(int id)
{
	uint8_t *widget;
	uintptr_t next;
	int i;

	if (!guest_data_ok(GUEST_MENU_WIDGET_ROOT) || id <= 0)
		return NULL;
	next = *(volatile uintptr_t *)(uintptr_t)
		(GUEST_MENU_WIDGET_ROOT + GUEST_WIDGET_NEXT);
	for (i = 0; next && i < 192; i++) {
		widget = (uint8_t *)next;
		if (*(volatile int *)(widget + GUEST_WIDGET_ID) == id)
			return widget;
		next = *(volatile uintptr_t *)(widget + GUEST_WIDGET_NEXT);
	}
	return NULL;
}

static int menu_widget_on_screen(const uint8_t *widget)
{
	int x;
	int y;
	int w;
	int h;

	if (!widget)
		return 0;
	x = *(volatile int *)(widget + GUEST_WIDGET_X);
	y = *(volatile int *)(widget + GUEST_WIDGET_Y);
	h = *(volatile int *)(widget + GUEST_WIDGET_H);
	w = *(volatile int *)(widget + GUEST_WIDGET_W);
	return x >= 0 && y >= 0 && w > 0 && h > 0;
}

static int widget_is_drawn(const uint8_t *widget)
{
	if (!menu_widget_on_screen(widget))
		return 0;
	return *(volatile uint8_t *)(widget + GUEST_WIDGET_SHOW) != 0;
}

static int menu_widget_drawn(int id)
{
	return widget_is_drawn(menu_find_widget(id));
}

static int menu_page_visible(void)
{
	/*
	 * Game hides widgets with x == -1. Field leftover back icon stays
	 * around (592,20), so "menu up" is the tab strip / Tianshu actions.
	 * Do not require the show flag; it is not the hide signal.
	 */
	int y;
	uint8_t *widget;
	static const int ids[] = {
		GUEST_MENU_TAB0, GUEST_MENU_TAB1, GUEST_MENU_TAB2,
		3, 4, 5, 6, 7,
	};
	unsigned i;

	/*
	 * Tactics overlay reuses tab ids 0x2496..0x2498 at the bottom of
	 * the screen. Only the top strip counts as the system menu.
	 */
	if (guest_in_fight())
		return 0;
	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		widget = menu_find_widget(ids[i]);
		if (!menu_widget_on_screen(widget))
			continue;
		y = *(volatile int *)(widget + GUEST_WIDGET_Y);
		if (y >= 0 && y < 160)
			return 1;
	}
	return 0;
}

static int menu_chrome_drawn(void)
{
	return menu_page_visible();
}

static void menu_log_icons(const char *why)
{
	static const int ids[] = {
		GUEST_MENU_BACK, GUEST_MENU_TAB0, GUEST_MENU_TAB1,
		GUEST_MENU_TAB2, 3, 4, 5,
	};
	uint8_t *widget;
	unsigned i;

	fprintf(stderr, "sword3-sdl: icons %s chrome=%d",
		why, menu_chrome_drawn());
	for (i = 0; i < sizeof(ids) / sizeof(ids[0]); i++) {
		widget = menu_find_widget(ids[i]);
		if (!widget) {
			fprintf(stderr, " %d=miss", ids[i]);
			continue;
		}
		fprintf(stderr, " %d=%d,%d,show=%u",
			ids[i],
			*(volatile int *)(widget + GUEST_WIDGET_X),
			*(volatile int *)(widget + GUEST_WIDGET_Y),
			(unsigned)*(volatile uint8_t *)
				(widget + GUEST_WIDGET_SHOW));
	}
	if (guest_data_ok(GUEST_UIGAMEPAD)) {
		uint8_t *pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
		volatile int *cfg = (volatile int *)
			(pad + GUEST_OVERLAY_CONFIG + GUEST_OVERLAY_RECT);

		fprintf(stderr,
			" overlay_cfg=%d,%d %dx%d en=%d hud=%d",
			cfg[0], cfg[1], cfg[2], cfg[3],
			*(volatile int *)(pad + GUEST_OVERLAY_ENABLE),
			*(volatile uint8_t *)(pad + GUEST_OVERLAY_HUD));
	}
	fprintf(stderr, "\n");
	{
		uintptr_t next;
		int n;

		if (!guest_data_ok(GUEST_MENU_WIDGET_ROOT))
			return;
		next = *(volatile uintptr_t *)(uintptr_t)
			(GUEST_MENU_WIDGET_ROOT + GUEST_WIDGET_NEXT);
		fprintf(stderr, "sword3-sdl: right widgets");
		for (n = 0; next && n < 192; n++) {
			int x;
			int y;
			int id;

			widget = (uint8_t *)next;
			id = *(volatile int *)(widget + GUEST_WIDGET_ID);
			x = *(volatile int *)(widget + GUEST_WIDGET_X);
			y = *(volatile int *)(widget + GUEST_WIDGET_Y);
			if (x >= GAME_W / 2 && y >= 0 && y < 160)
				fprintf(stderr, " id=%d,%d,%d", id, x, y);
			next = *(volatile uintptr_t *)
				(widget + GUEST_WIDGET_NEXT);
		}
		fprintf(stderr, "\n");
	}
}

static int menu_opening(void)
{
	return g_menu_open_button >= 0 || g_menu_return_held ||
	       g_menu_open_pending;
}

static int menu_widget_rect(int id, int *x, int *y, int *w, int *h)
{
	uint8_t *widget;

	widget = menu_find_widget(id);
	if (!widget_is_drawn(widget))
		return 0;
	*x = *(volatile int *)(widget + GUEST_WIDGET_X);
	*y = *(volatile int *)(widget + GUEST_WIDGET_Y);
	*h = *(volatile int *)(widget + GUEST_WIDGET_H);
	*w = *(volatile int *)(widget + GUEST_WIDGET_W);
	return 1;
}

static void push_finger_at(float x, float y, Uint32 type)
{
	SDL_Event event;

	memset(&event, 0, sizeof(event));
	event.type = type;
	event.tfinger.timestamp = SDL_GetTicks();
	event.tfinger.touchId = TOUCH_ID;
	event.tfinger.fingerId = FINGER_ID;
	event.tfinger.x = x;
	event.tfinger.y = y;
	event.tfinger.pressure =
		(type == SDL_FINGERUP) ? 0.0f : 1.0f;
	SDL_PushEvent(&event);
}

static int menu_click_widget(int id)
{
	int x;
	int y;
	int w;
	int h;

	if (!menu_widget_rect(id, &x, &y, &w, &h))
		return 0;
	push_finger_at((float)(x + w / 2) / (float)GAME_W,
		       (float)(y + h / 2) / (float)GAME_H,
		       SDL_FINGERDOWN);
	push_finger_at((float)(x + w / 2) / (float)GAME_W,
		       (float)(y + h / 2) / (float)GAME_H,
		       SDL_FINGERUP);
	fprintf(stderr, "sword3-sdl: menu widget id=%d xy=%d,%d\n",
		id, x, y);
	return 1;
}

static int menu_syspage_active(void)
{
	return guest_read_ptr(GUEST_FEXECUTE) == GUEST_FEX_SYSPAGE;
}

static int menu_tianshu_active(void)
{
	/*
	 * Host-driven 存盘/读取 row. After tapping one, click becomes
	 * SysPage 0x10002d018 and those widgets stay on screen; D-pad
	 * must then move save slots, not the parent row.
	 */
	return g_menu_tab_index == 4 && !menu_syspage_active() &&
	       !g_menu_nested;
}

static void menu_click_action(int index)
{
	if (index < 0 || index >= 5 ||
	    !menu_click_widget(index + 3))
		return;
	g_menu_nested = 1;
	g_menu_action_focus = -1;
	fprintf(stderr, "sword3-sdl: Tianshu action=%d id=%d nested\n",
		index, index + 3);
}

static int menu_action_select_rect(int *x, int *y, int *w, int *h)
{
	int gx;
	int gy;
	int gw;
	int gh;
	int lw;
	int lh;

	if (!g_menu_keys || !menu_tianshu_active() ||
	    g_menu_action_focus < 0 ||
	    !menu_widget_rect(g_menu_action_focus + 3,
			      &gx, &gy, &gw, &gh))
		return 0;
	lw = g_logical_w > 0 ? g_logical_w : GAME_W;
	lh = g_logical_h > 0 ? g_logical_h : GAME_H;
	*x = gx * lw / GAME_W;
	*y = gy * lh / GAME_H;
	*w = gw * lw / GAME_W;
	*h = gh * lh / GAME_H;
	return 1;
}

static void menu_switch_tab(int delta)
{
	static const float tab_x[5] = {
		0.378f, 0.509f, 0.641f, 0.772f, 0.902f,
	};
	int index;

	index = (g_menu_tab_index + delta + 5) % 5;
	g_menu_tab_index = index;
	/*
	 * 天书's 存盘/读取/记载/设置/离开 row is selected immediately so
	 * left/right can cycle those buttons without pressing Down first.
	 * Up still returns to the tab strip (focus -1) to leave this page.
	 */
	g_menu_action_focus = index == 4 ? 0 : -1;
	g_menu_nested = 0;
	push_finger_at(tab_x[index], 0.058f, SDL_FINGERDOWN);
	push_finger_at(tab_x[index], 0.058f, SDL_FINGERUP);
	fprintf(stderr, "sword3-sdl: menu tab=%d touch=%.3f,0.058\n",
		index, tab_x[index]);
}

static const SDL_Scancode g_menu_dir_keys[4] = {
	SDL_SCANCODE_UP,
	SDL_SCANCODE_RIGHT,
	SDL_SCANCODE_DOWN,
	SDL_SCANCODE_LEFT,
};

static void guest_keyboard_key(SDL_Scancode scancode, int down)
{
	uint8_t *pad;
	uint8_t *slot;
	void (*transition)(void *, void *, int);

	if (!guest_data_ok(GUEST_UIGAMEPAD)) {
		set_scancode_state(scancode, down);
		return;
	}
	pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
	slot = pad + GUEST_KEY_SLOT + scancode * GUEST_KEY_STRIDE;
	*(volatile int *)(pad + GUEST_INPUT_MODE) = 1;
	transition = (void (*)(void *, void *, int))(uintptr_t)
		GUEST_INPUT_TRANSITION;
	transition(pad, slot, down);
	set_scancode_state(scancode, down);
	if (down) {
		slot[8] = 0;
		*(volatile Uint32 *)(slot + 4) = SDL_GetTicks() - 0x209u;
	}
}

static void host_trace_action_members(int action, const char *why)
{
	uint8_t *pad;
	uint8_t *slot;
	uintptr_t node;
	uintptr_t next;
	uintptr_t slot_addr;
	uintptr_t offset;
	int i;

	if (action < 1 || action > 24 || !guest_data_ok(GUEST_UIGAMEPAD))
		return;
	pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
	node = *(volatile uintptr_t *)(pad + action * GUEST_KEY_STRIDE + 0x80);
	for (i = 0; node && i < 128; i++, node = next) {
		slot_addr = *(volatile uintptr_t *)node;
		next = *(volatile uintptr_t *)(node + sizeof(uintptr_t));
		if (slot_addr < (uintptr_t)pad ||
		    slot_addr + GUEST_KEY_STRIDE >
			    (uintptr_t)pad + 0x2498) {
			fprintf(stderr,
				"sword3-sdl: action-trace %s a%d bad-slot=%p\n",
				why, action, (void *)slot_addr);
			continue;
		}
		slot = (uint8_t *)slot_addr;
		offset = slot_addr - (uintptr_t)pad;
		fprintf(stderr,
			"sword3-sdl: action-trace %s a%d slot=+%#lx "
			"state=%u flag=%u mask=%#x mode=%u\n",
			why, action, (unsigned long)offset,
			(unsigned)slot[0], (unsigned)slot[8],
			(unsigned)*(volatile uint32_t *)(slot + GUEST_SLOT_MASK),
			(unsigned)*(volatile uint32_t *)(slot + 0x14));
	}
}

/*
 * Action 7 is consumed at 0x1000731fc and calls 0x10005bd38, which writes
 * draw_gate=0x80000001 and opens the original menu. Return's stock mask is
 * 0x60. Whenever the game restores that default, remove only action 7,
 * preserve every other binding, then rebuild the action lists.
 */
static void host_unlink_return_menu(void)
{
	static unsigned repairs;
	uint8_t *pad;
	uint32_t *return_mask;
	uint32_t old_return;
	void (*rebind)(void *);

	if (!guest_data_ok(GUEST_UIGAMEPAD))
		return;
	pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
	if (!pad[GUEST_PAD_READY])
		return;
	return_mask = (uint32_t *)(pad + GUEST_KEY_SLOT +
				   SDL_SCANCODE_RETURN * GUEST_KEY_STRIDE +
				   GUEST_SLOT_MASK);
	old_return = *return_mask;
	if (!(old_return & GUEST_ACTION7_MASK))
		return;
	*return_mask &= ~GUEST_ACTION7_MASK;
	rebind = (void (*)(void *))(uintptr_t)GUEST_REBIND_ACTIONS;
	rebind(pad);
	if (repairs++ < 8) {
		fprintf(stderr,
			"sword3-sdl: Return action7 unbound %#x -> %#x "
			"(repair %u)\n",
			(unsigned)old_return, (unsigned)*return_mask, repairs);
	}
}

static void field_talk_key(int down)
{
	uint8_t *pad;

	if (down && guest_data_ok(GUEST_UIGAMEPAD)) {
		pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
		g_field_mode_saved = *(volatile int *)(pad + GUEST_INPUT_MODE);
		g_field_mode_held = 1;
	}
	guest_keyboard_key(SDL_SCANCODE_RETURN, down);
	if (!down && g_field_mode_held && guest_data_ok(GUEST_UIGAMEPAD)) {
		pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
		*(volatile int *)(pad + GUEST_INPUT_MODE) = g_field_mode_saved;
		g_field_mode_held = 0;
	}
}

static void menu_enter_session(void)
{
	if (g_menu_session)
		return;
	g_menu_session = 1;
	g_menu_keys = 1;
	g_menu_ui = 1;
	g_menu_open_pending = 0;
	g_menu_tab_index = 0;
	g_menu_action_focus = -1;
	g_menu_nested = 0;
	fprintf(stderr, "sword3-sdl: menu session start\n");
}

static void menu_leave_session(void)
{
	uint8_t *pad;

	if (g_menu_session || g_menu_open_pending || g_menu_close_pending)
		fprintf(stderr, "sword3-sdl: menu session end\n");
	g_menu_session = 0;
	g_menu_open_pending = 0;
	g_menu_close_pending = 0;
	g_menu_keys = 0;
	g_menu_ui = 0;
	g_menu_return_held = 0;
	g_menu_open_button = -1;
	g_menu_seen_chrome = 0;
	g_menu_saw_draw = 0;
	g_menu_open_direct = 0;
	g_menu_gone_at = 0;
	g_menu_back_until = 0;
	g_menu_reenter_until = SDL_GetTicks() + MENU_BACK_GRACE_MS;
	g_field_mode_held = 0;
	g_menu_nested = 0;
	menu_release_directions();
	release_guest_walk();
	if (guest_data_ok(GUEST_UIGAMEPAD)) {
		pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
		*(volatile int *)(pad + GUEST_INPUT_MODE) = 4;
	}
}

static void menu_begin_open(int button)
{
	g_menu_open_button = button;
	g_menu_return_held = 0;
	g_menu_open_pending = 1;
	g_menu_open_until = SDL_GetTicks() + MENU_OPEN_WAIT_MS;
	g_menu_seen_chrome = 0;
	g_menu_gone_at = 0;
	g_menu_tab_index = 0;
	g_menu_action_focus = -1;
	g_menu_logged_postopen = 0;
	g_menu_saw_draw = 0;
	menu_log_icons("open");
	menu_log_state("open");
	if (!menu_click_open_button())
		fprintf(stderr, "sword3-sdl: SELECT menu button miss\n");
}

static void menu_finish_open(void)
{
	g_menu_open_button = -1;
}

static void menu_park_pointer(void)
{
	uint8_t *pad;

	g_cursor_x = (float)GAME_W / 2.0f;
	g_cursor_y = (float)GAME_H / 2.0f;
	g_cursor_ready = 1;
	if (!guest_data_ok(GUEST_UIGAMEPAD))
		return;
	pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
	*(volatile int *)(pad + GUEST_MOUSE_X) = GAME_W / 2;
	*(volatile int *)(pad + GUEST_MOUSE_Y) = GAME_H / 2;
	*(volatile int *)(pad + GUEST_INPUT_MODE) = 4;
}

static void menu_open_field(void)
{
	/*
	 * Successful field open is draw_gate=1, layer=1, field fex.
	 * 0x1001ccbb0(3) writes 0x80000003. The main draw frame
	 * (0x100023004) treats bit 31 as title/boot: low bits 3 then
	 * call enter_map(0, 0xea60), which is 再续前缘.
	 * Store raw 1 with 0x1001ccb98, same as a tap-open that stuck.
	 */
	menu_park_pointer();
	((void (*)(void *, int))(uintptr_t)GUEST_STORE_DRAW)(
		(void *)(uintptr_t)GUEST_DRAW_OBJ, 1);
	if (guest_data_ok(GUEST_SYS_LAYER))
		*(volatile int *)(uintptr_t)GUEST_SYS_LAYER = GUEST_LAYER_WALK;
	menu_park_pointer();
	fprintf(stderr,
		"sword3-sdl: SELECT -> field open draw=%#x layer=%d fex=%p map=%d title=%d page=%d\n",
		(unsigned)guest_read_i32(GUEST_DRAW_GATE, -1),
		guest_read_i32(GUEST_SYS_LAYER, -1),
		(void *)guest_read_ptr(GUEST_FEXECUTE),
		guest_map_id(),
		guest_read_i32(GUEST_TITLE_MODE, -1),
		guest_read_i32(GUEST_MENU_PAGE, -1));
}

static void menu_poll_open_pulse(void)
{
	Uint32 now = SDL_GetTicks();

	if (g_menu_open_direct) {
		g_menu_open_direct = 0;
		menu_open_field();
	}
	if (g_menu_return_held && g_menu_open_button < 0 &&
	    SDL_TICKS_PASSED(now, g_menu_return_up_at)) {
		g_menu_return_held = 0;
	}
	if (menu_drawn()) {
		g_menu_saw_draw = 1;
		g_menu_gone_at = 0;
		g_menu_open_pending = 0;
		g_menu_close_pending = 0;
		if (!g_menu_session)
			menu_enter_session();
	}
	if (g_menu_session && !g_menu_logged_postopen &&
	    SDL_TICKS_PASSED(now, g_menu_open_until - 600)) {
		g_menu_logged_postopen = 1;
		menu_log_state("postopen");
	}
	if (g_menu_session && g_menu_saw_draw && !menu_drawn()) {
		g_menu_open_pending = 0;
		g_menu_close_pending = 0;
		if (!g_menu_back_until ||
		    SDL_TICKS_PASSED(now, g_menu_back_until)) {
			if (!g_menu_gone_at)
				g_menu_gone_at = now;
			else if (SDL_TICKS_PASSED(now,
						  g_menu_gone_at +
							  MENU_CHROME_GONE_MS)) {
				fprintf(stderr,
					"sword3-sdl: menu closed draw=0 in=%d layer=%d\n",
					guest_menu_flag(),
					guest_read_i32(GUEST_SYS_LAYER, -1));
				menu_log_state("closed");
				menu_leave_session();
			}
		}
	}
	if (g_menu_open_pending && !g_menu_session &&
	    SDL_TICKS_PASSED(now, g_menu_open_until)) {
		fprintf(stderr, "sword3-sdl: menu open timeout\n");
		g_menu_open_pending = 0;
		g_menu_close_pending = 0;
	}
}

static int menu_field_button_rect(int *x, int *y, int *w, int *h)
{
	uint8_t *widget;
	uint8_t *best;
	uintptr_t next;
	int i;
	int id;
	int wx;
	int wy;
	int ww;
	int wh;
	int best_x;

	widget = menu_find_widget(GUEST_MENU_BACK);
	if (menu_widget_on_screen(widget)) {
		wx = *(volatile int *)(widget + GUEST_WIDGET_X);
		wy = *(volatile int *)(widget + GUEST_WIDGET_Y);
		if (wx >= GAME_W / 2 && wy < 160) {
			*x = wx;
			*y = wy;
			*h = *(volatile int *)(widget + GUEST_WIDGET_H);
			*w = *(volatile int *)(widget + GUEST_WIDGET_W);
			return 1;
		}
	}
	best = NULL;
	best_x = -1;
	if (!guest_data_ok(GUEST_MENU_WIDGET_ROOT))
		goto fallback;
	next = *(volatile uintptr_t *)(uintptr_t)
		(GUEST_MENU_WIDGET_ROOT + GUEST_WIDGET_NEXT);
	for (i = 0; next && i < 192; i++) {
		widget = (uint8_t *)next;
		id = *(volatile int *)(widget + GUEST_WIDGET_ID);
		(void)id;
		wx = *(volatile int *)(widget + GUEST_WIDGET_X);
		wy = *(volatile int *)(widget + GUEST_WIDGET_Y);
		wh = *(volatile int *)(widget + GUEST_WIDGET_H);
		ww = *(volatile int *)(widget + GUEST_WIDGET_W);
		if (wx >= GAME_W / 2 && wy >= 0 && wy < 160 &&
		    ww > 0 && wh > 0 && wx > best_x) {
			best = widget;
			best_x = wx;
		}
		next = *(volatile uintptr_t *)(widget + GUEST_WIDGET_NEXT);
	}
	if (best) {
		*x = *(volatile int *)(best + GUEST_WIDGET_X);
		*y = *(volatile int *)(best + GUEST_WIDGET_Y);
		*h = *(volatile int *)(best + GUEST_WIDGET_H);
		*w = *(volatile int *)(best + GUEST_WIDGET_W);
		return 1;
	}
fallback:
	*x = 596;
	*y = 47;
	*w = 40;
	*h = 40;
	return 1;
}

static void menu_set_mouse_at(int x, int y)
{
	uint8_t *pad;

	if (!guest_data_ok(GUEST_UIGAMEPAD))
		return;
	pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
	*(volatile int *)(pad + GUEST_INPUT_MODE) = GUEST_INPUT_MOUSE;
	*(volatile int *)(pad + GUEST_MOUSE_X) = x;
	*(volatile int *)(pad + GUEST_MOUSE_Y) = y;
}

static int menu_click_open_button(void)
{
	int x;
	int y;
	int w;
	int h;

	if (!guest_load_ui() && !guest_on_title()) {
		g_menu_open_direct = 1;
		fprintf(stderr,
			"sword3-sdl: SELECT -> field open pending layer=%d draw=%d\n",
			guest_read_i32(GUEST_SYS_LAYER, -1),
			guest_read_i32(GUEST_DRAW_GATE, -1));
		return 1;
	}
	if (!menu_field_button_rect(&x, &y, &w, &h))
		return 0;
	menu_set_mouse_at(x + w / 2, y + h / 2);
	push_finger_at((float)(x + w / 2) / (float)GAME_W,
		       (float)(y + h / 2) / (float)GAME_H,
		       SDL_FINGERDOWN);
	push_finger_at((float)(x + w / 2) / (float)GAME_W,
		       (float)(y + h / 2) / (float)GAME_H,
		       SDL_FINGERUP);
	fprintf(stderr, "sword3-sdl: SELECT -> menu button xy=%d,%d %dx%d\n",
		x, y, w, h);
	return 1;
}

static int menu_click_back(void)
{
	uint8_t *widget;
	int x;
	int y;
	int w;
	int h;

	widget = menu_find_widget(GUEST_MENU_BACK);
	if (!menu_widget_on_screen(widget))
		return 0;
	x = *(volatile int *)(widget + GUEST_WIDGET_X);
	y = *(volatile int *)(widget + GUEST_WIDGET_Y);
	h = *(volatile int *)(widget + GUEST_WIDGET_H);
	w = *(volatile int *)(widget + GUEST_WIDGET_W);
	push_finger_at((float)(x + w / 2) / (float)GAME_W,
		       (float)(y + h / 2) / (float)GAME_H,
		       SDL_FINGERDOWN);
	push_finger_at((float)(x + w / 2) / (float)GAME_W,
		       (float)(y + h / 2) / (float)GAME_H,
		       SDL_FINGERUP);
	fprintf(stderr, "sword3-sdl: menu back xy=%d,%d\n", x, y);
	menu_log_state("back");
	return 1;
}

/*
 * On-device menu tracer. SSH drops lines into /tmp/sword3-menu-trace;
 * we inject the same tap/key the pad uses, wait a few frames, then
 * snapshot BSS + the widget list. Diffing those snapshots is how we
 * tell globals from heap objects without guessing.
 */
struct menu_trace_region {
	uintptr_t addr;
	unsigned size;
	const char *name;
	uint8_t prev[96];
	int have;
};

static struct menu_trace_region g_trace_regions[] = {
	{ 0x1002a84e0ull, 48, "mode" },
	{ 0x1002a8698ull, 96, "fex" },
	{ 0x1002a99c0ull, 48, "layer" },
	{ 0x1002aa450ull, 80, "sys" },
	{ 0x1002aa7c0ull, 48, "dlg" },
	{ 0x1002ab838ull, 16, "wroot" },
	{ 0x10030f480ull, 24, "draw" },
};

static char g_trace_q[MENU_TRACE_Q][96];
static int g_trace_qn;
static int g_trace_qi;
static Uint32 g_trace_wait_until;
static SDL_Scancode g_trace_key;
static Uint32 g_trace_key_up;

static void menu_trace_widgets(void)
{
	uintptr_t next;
	int n;
	int shown;

	if (!guest_data_ok(GUEST_MENU_WIDGET_ROOT))
		return;
	next = *(volatile uintptr_t *)(uintptr_t)
		(GUEST_MENU_WIDGET_ROOT + GUEST_WIDGET_NEXT);
	shown = 0;
	for (n = 0; next && n < 192; n++) {
		uint8_t *widget = (uint8_t *)next;
		int id = *(volatile int *)(widget + GUEST_WIDGET_ID);
		int x = *(volatile int *)(widget + GUEST_WIDGET_X);
		int y = *(volatile int *)(widget + GUEST_WIDGET_Y);
		int w = *(volatile int *)(widget + GUEST_WIDGET_W);
		int h = *(volatile int *)(widget + GUEST_WIDGET_H);

		if (x >= 0 && y >= 0 && w > 0 && h > 0) {
			fprintf(stderr,
				"sword3-sdl: menu-trace wid ptr=%p id=%d xy=%d,%d %dx%d\n",
				(void *)widget, id, x, y, w, h);
			shown++;
		}
		next = *(volatile uintptr_t *)(widget + GUEST_WIDGET_NEXT);
	}
	fprintf(stderr, "sword3-sdl: menu-trace widgets onscreen=%d scanned=%d\n",
		shown, n);
}

static void menu_trace_snap(const char *why)
{
	unsigned i;
	unsigned off;
	uint8_t now[96];
	int changed;

	fprintf(stderr,
		"sword3-sdl: menu-trace snap %s map=%d session=%d mode=%d layer=%d draw=%d\n",
		why,
		guest_map_id(),
		g_menu_session,
		guest_read_i32(GUEST_SYS_MODE, -1),
		guest_read_i32(GUEST_SYS_LAYER, -1),
		guest_read_i32(GUEST_DRAW_GATE, -1));
	menu_log_state(why);
	fprintf(stderr,
		"sword3-sdl: menu-trace fex click=%p right=%p left=%p ok=%p back=%p\n",
		(void *)guest_read_ptr(GUEST_FEXECUTE),
		(void *)guest_read_ptr(GUEST_FEXECUTE + 8),
		(void *)guest_read_ptr(GUEST_FEXECUTE + 16),
		(void *)guest_read_ptr(GUEST_FEXECUTE + 0x48),
		(void *)guest_read_ptr(GUEST_FEXECUTE + 0x50));
	for (i = 0; i < sizeof(g_trace_regions) / sizeof(g_trace_regions[0]);
	     i++) {
		struct menu_trace_region *r = &g_trace_regions[i];

		if (r->size > sizeof(r->prev) ||
		    !guest_data_ok(r->addr) ||
		    !guest_data_ok(r->addr + r->size - 1))
			continue;
		memcpy(now, (const void *)(uintptr_t)r->addr, r->size);
		if (!r->have) {
			fprintf(stderr,
				"sword3-sdl: menu-trace %s baseline %p %u\n",
				r->name, (void *)r->addr, r->size);
			memcpy(r->prev, now, r->size);
			r->have = 1;
			continue;
		}
		changed = 0;
		for (off = 0; off + 4 <= r->size; off += 4) {
			uint32_t a;
			uint32_t b;

			memcpy(&a, r->prev + off, 4);
			memcpy(&b, now + off, 4);
			if (a == b)
				continue;
			fprintf(stderr,
				"sword3-sdl: menu-trace diff %s+0x%x %08x -> %08x\n",
				r->name, off, a, b);
			changed = 1;
		}
		if (!changed)
			fprintf(stderr,
				"sword3-sdl: menu-trace %s unchanged\n",
				r->name);
		memcpy(r->prev, now, r->size);
	}
	menu_trace_widgets();
}

static void menu_trace_key(SDL_Scancode scancode)
{
	if (g_trace_key) {
		guest_keyboard_key(g_trace_key, 0);
		g_trace_key = 0;
	}
	guest_keyboard_key(scancode, 1);
	g_trace_key = scancode;
	g_trace_key_up = SDL_GetTicks() + 80;
}

static void menu_trace_tap_xy(int x, int y)
{
	float nx = (float)x / (float)GAME_W;
	float ny = (float)y / (float)GAME_H;

	if (guest_data_ok(GUEST_UIGAMEPAD)) {
		uint8_t *pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;

		*(volatile int *)(pad + GUEST_INPUT_MODE) = GUEST_INPUT_MOUSE;
	}
	push_finger_at(nx, ny, SDL_FINGERDOWN);
	push_finger_at(nx, ny, SDL_FINGERUP);
	fprintf(stderr, "sword3-sdl: menu-trace tap %d,%d\n", x, y);
}

static void menu_trace_run(char *line)
{
	char *nl = strchr(line, '\n');
	char why[64];
	int x;
	int y;
	int ms;
	int action;

	if (nl)
		*nl = 0;
	if (line[0] == 0)
		return;
	fprintf(stderr, "sword3-sdl: menu-trace cmd %s\n", line);
	if (sscanf(line, "snap %63s", why) == 1)
		menu_trace_snap(why);
	else if (strcmp(line, "snap") == 0)
		menu_trace_snap("snap");
	else if (strcmp(line, "tap-menu") == 0)
		menu_click_open_button();
	else if (strcmp(line, "tap-back") == 0)
		menu_click_back();
	else if (sscanf(line, "tap %d %d", &x, &y) == 2)
		menu_trace_tap_xy(x, y);
	else if (strcmp(line, "left") == 0)
		menu_trace_key(SDL_SCANCODE_LEFT);
	else if (strcmp(line, "right") == 0)
		menu_trace_key(SDL_SCANCODE_RIGHT);
	else if (strcmp(line, "up") == 0)
		menu_trace_key(SDL_SCANCODE_UP);
	else if (strcmp(line, "down") == 0)
		menu_trace_key(SDL_SCANCODE_DOWN);
	else if (strcmp(line, "a") == 0)
		menu_trace_key(SDL_SCANCODE_RETURN);
	else if (sscanf(line, "action %d", &action) == 1)
		host_trace_action_members(action, "manual");
	else if (strcmp(line, "actions") == 0) {
		host_trace_action_members(5, "manual");
		host_trace_action_members(6, "manual");
		host_trace_action_members(7, "manual");
	}
	else if (sscanf(line, "wait %d", &ms) == 1)
		g_trace_wait_until = SDL_GetTicks() + (Uint32)ms;
	else
		fprintf(stderr, "sword3-sdl: menu-trace unknown %s\n", line);
}

static void menu_trace_ingest(void)
{
	FILE *f;
	char buf[96];

	if (access(MENU_TRACE_PATH, R_OK) != 0)
		return;
	f = fopen(MENU_TRACE_PATH, "r");
	if (!f)
		return;
	while (g_trace_qn < MENU_TRACE_Q && fgets(buf, sizeof(buf), f))
		snprintf(g_trace_q[g_trace_qn++], sizeof(g_trace_q[0]),
			 "%s", buf);
	fclose(f);
	unlink(MENU_TRACE_PATH);
}

static void menu_trace_poll(void)
{
	Uint32 now = SDL_GetTicks();

	menu_trace_ingest();
	if (g_trace_key && SDL_TICKS_PASSED(now, g_trace_key_up)) {
		guest_keyboard_key(g_trace_key, 0);
		g_trace_key = 0;
	}
	if (g_trace_wait_until && !SDL_TICKS_PASSED(now, g_trace_wait_until))
		return;
	g_trace_wait_until = 0;
	if (g_trace_qi >= g_trace_qn) {
		g_trace_qi = 0;
		g_trace_qn = 0;
		return;
	}
	menu_trace_run(g_trace_q[g_trace_qi++]);
}

static void menu_abort_open_pulse(void)
{
	if (g_menu_return_held) {
		guest_keyboard_key(SDL_SCANCODE_RETURN, 0);
		g_menu_return_held = 0;
	}
	g_menu_open_button = -1;
}

static int menu_try_close(void)
{
	menu_abort_open_pulse();
	if (!menu_click_back())
		return 0;
	g_menu_close_pending = 0;
	return 1;
}

static int menu_begin_close(void)
{
	menu_log_icons("close");
	g_menu_close_pending = 1;
	if (g_menu_nested && g_menu_tab_index == 4)
		g_menu_action_focus = 0;
	g_menu_nested = 0;
	if (menu_try_close())
		return 1;
	fprintf(stderr, "sword3-sdl: menu B waiting for back icon\n");
	return 0;
}

static void menu_finish_close(int used_widget)
{
	(void)used_widget;
}

static void menu_set_dir_source(int index, int axis, int down)
{
	int wanted;

	if (index < 0 || index >= 4)
		return;
	if (axis)
		g_menu_dir_axis[index] = down;
	else
		g_menu_dir_dpad[index] = down;
	wanted = g_menu_dir_axis[index] || g_menu_dir_dpad[index];
	if (wanted)
		g_menu_dir_release[index] = 0;
	if (wanted == g_menu_dir_down[index])
		return;
	if (index == 1 || index == 3) {
		if (!wanted) {
			g_menu_dir_down[index] = 0;
			guest_keyboard_key(g_menu_dir_keys[index], 0);
			return;
		}
		g_menu_dir_down[index] = 1;
		/*
		 * Nested pages (save slots, item bag) keep the tab strip
		 * and Tianshu buttons drawn. The game has installed the
		 * SysPage table; send keyboard arrows into that list.
		 */
		if (menu_syspage_active() || g_menu_nested) {
			guest_keyboard_key(g_menu_dir_keys[index], 1);
			menu_log_state(index == 1 ? "right" : "left");
			return;
		}
		if (menu_tianshu_active() && g_menu_action_focus >= 0) {
			g_menu_action_focus =
				(g_menu_action_focus +
				 (index == 1 ? 1 : 4)) % 5;
			return;
		}
		if (menu_widget_on_screen(menu_find_widget(GUEST_MENU_TAB0)) ||
		    menu_widget_on_screen(menu_find_widget(GUEST_MENU_TAB1))) {
			menu_switch_tab(index == 1 ? 1 : -1);
			menu_log_state("tab");
			return;
		}
		guest_keyboard_key(g_menu_dir_keys[index], 1);
		menu_log_state(index == 1 ? "right" : "left");
		return;
	}
	if (menu_tianshu_active()) {
		g_menu_dir_down[index] = wanted;
		if (!wanted)
			return;
		if (index == 2) {
			if (g_menu_action_focus < 0)
				g_menu_action_focus = 0;
		} else if (index == 0) {
			g_menu_action_focus = -1;
		}
		return;
	}
	if (!wanted) {
		g_menu_dir_release[index] = 1;
		return;
	}
	g_menu_dir_down[index] = 1;
	guest_keyboard_key(g_menu_dir_keys[index], 1);
	if (g_menu_key_seen++ < 32)
		fprintf(stderr, "sword3-sdl: menu dir=%d key=%d down\n",
			index, (int)g_menu_dir_keys[index]);
}

static void menu_axis_event(Uint8 axis, Sint16 value)
{
	int old_dir;
	int new_dir;

	if (axis == SDL_CONTROLLER_AXIS_LEFTX) {
		old_dir = g_menu_dir_axis[3] ? 3 :
			  (g_menu_dir_axis[1] ? 1 : -1);
		new_dir = value < -STICK_DEADZONE ? 3 :
			  (value > STICK_DEADZONE ? 1 : -1);
	} else if (axis == SDL_CONTROLLER_AXIS_LEFTY) {
		old_dir = g_menu_dir_axis[0] ? 0 :
			  (g_menu_dir_axis[2] ? 2 : -1);
		new_dir = value < -STICK_DEADZONE ? 0 :
			  (value > STICK_DEADZONE ? 2 : -1);
	} else {
		return;
	}
	if (old_dir == new_dir)
		return;
	if (old_dir >= 0)
		menu_set_dir_source(old_dir, 1, 0);
	if (new_dir >= 0)
		menu_set_dir_source(new_dir, 1, 1);
}

static void menu_dpad_event(int button, int down)
{
	int index;

	switch (button) {
	case SDL_CONTROLLER_BUTTON_DPAD_UP:
		index = 0;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
		index = 1;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
		index = 2;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
		index = 3;
		break;
	default:
		return;
	}
	menu_set_dir_source(index, 0, down);
}

static void menu_release_directions(void)
{
	int i;

	memset(g_menu_dir_dpad, 0, sizeof(g_menu_dir_dpad));
	memset(g_menu_dir_axis, 0, sizeof(g_menu_dir_axis));
	memset(g_menu_dir_release, 0, sizeof(g_menu_dir_release));
	g_menu_action_focus = -1;
	g_menu_action_a_up = 0;
	for (i = 0; i < 4; i++) {
		if (!g_menu_dir_down[i])
			continue;
		g_menu_dir_down[i] = 0;
		guest_keyboard_key(g_menu_dir_keys[i], 0);
	}
}

static void menu_flush_direction_releases(void)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (!g_menu_dir_release[i])
			continue;
		g_menu_dir_release[i] = 0;
		g_menu_dir_down[i] = 0;
		guest_keyboard_key(g_menu_dir_keys[i], 0);
		if (g_menu_key_seen++ < 32)
			fprintf(stderr, "sword3-sdl: menu dir=%d key=%d up\n",
				i, (int)g_menu_dir_keys[i]);
	}
}

static void fight_direction_edge(int index)
{
	static const uintptr_t handlers[4] = {
		GUEST_FIGHT_UP,
		GUEST_FIGHT_RIGHT,
		GUEST_FIGHT_DOWN,
		GUEST_FIGHT_LEFT,
	};

	if (index < 0 || index >= 4)
		return;
	if (guest_data_ok(GUEST_UIGAMEPAD))
		*(volatile int *)(uintptr_t)
			(GUEST_UIGAMEPAD + GUEST_INPUT_MODE) =
				GUEST_INPUT_KEYBOARD;
	((void (*)(void))handlers[index])();
}

static void fight_set_dir_source(int index, int axis, int down)
{
	int wanted;

	if (index < 0 || index >= 4)
		return;
	if (axis)
		g_fight_dir_axis[index] = down;
	else
		g_fight_dir_dpad[index] = down;
	wanted = g_fight_dir_axis[index] || g_fight_dir_dpad[index];
	if (wanted)
		g_fight_dir_release[index] = 0;
	if (wanted == g_fight_dir_down[index])
		return;
	if (!wanted) {
		g_fight_dir_release[index] = 1;
		return;
	}
	g_fight_dir_down[index] = wanted;
	fight_direction_edge(index);
	if (g_fight_key_seen++ < 48)
		fprintf(stderr, "sword3-sdl: fight native dir=%d %s\n",
			index, wanted ? "down" : "up");
}

static void fight_axis_event(Uint8 axis, Sint16 value)
{
	int old_dir;
	int new_dir;

	if (axis == SDL_CONTROLLER_AXIS_LEFTX) {
		old_dir = g_fight_dir_axis[3] ? 3 :
			  (g_fight_dir_axis[1] ? 1 : -1);
		new_dir = value < -STICK_DEADZONE ? 3 :
			  (value > STICK_DEADZONE ? 1 : -1);
	} else if (axis == SDL_CONTROLLER_AXIS_LEFTY) {
		old_dir = g_fight_dir_axis[0] ? 0 :
			  (g_fight_dir_axis[2] ? 2 : -1);
		new_dir = value < -STICK_DEADZONE ? 0 :
			  (value > STICK_DEADZONE ? 2 : -1);
	} else {
		return;
	}
	if (old_dir == new_dir)
		return;
	if (old_dir >= 0)
		fight_set_dir_source(old_dir, 1, 0);
	if (new_dir >= 0)
		fight_set_dir_source(new_dir, 1, 1);
}

static void fight_dpad_event(int button, int down)
{
	int index;

	switch (button) {
	case SDL_CONTROLLER_BUTTON_DPAD_UP:
		index = 0;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
		index = 1;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
		index = 2;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
		index = 3;
		break;
	default:
		return;
	}
	fight_set_dir_source(index, 0, down);
}

static void fight_release_directions(void)
{
	int i;

	memset(g_fight_dir_dpad, 0, sizeof(g_fight_dir_dpad));
	memset(g_fight_dir_axis, 0, sizeof(g_fight_dir_axis));
	memset(g_fight_dir_release, 0, sizeof(g_fight_dir_release));
	for (i = 0; i < 4; i++) {
		if (!g_fight_dir_down[i])
			continue;
		g_fight_dir_down[i] = 0;
	}
	g_fight_target_a_up = 0;
}

static void fight_flush_direction_releases(void)
{
	int i;

	for (i = 0; i < 4; i++) {
		if (!g_fight_dir_release[i])
			continue;
		g_fight_dir_release[i] = 0;
		g_fight_dir_down[i] = 0;
	}
}

static void fight_confirm_target(void)
{
	uint8_t *pad;
	uint8_t *slot;
	void (*transition)(void *, void *, int);

	if (!guest_data_ok(GUEST_UIGAMEPAD))
		return;
	pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
	slot = pad + GUEST_KEY_SLOT +
	       SDL_SCANCODE_RETURN * GUEST_KEY_STRIDE;
	*(volatile int *)(pad + GUEST_INPUT_MODE) = 1;
	transition = (void (*)(void *, void *, int))(uintptr_t)
		GUEST_INPUT_TRANSITION;
	if (slot[0] != 1)
		transition(pad, slot, 1);
	transition(pad, slot, 0);
	set_scancode_state(SDL_SCANCODE_RETURN, 0);
	((void (*)(int))(uintptr_t)GUEST_FIGHT_OK)(0);
}

static void host_menu_run_pending(void)
{
	int action;
	int slot;
	void (*reset_keys)(void *);

	slot = 0;
	action = host_menu_take_pending(&slot);
	if (action < 0)
		return;
	if (slot < 0)
		slot = 0;
	if (guest_data_ok(GUEST_SAVE_INDEX))
		*(volatile int *)(uintptr_t)GUEST_SAVE_INDEX = slot;
	if (action == 0) {
		((void (*)(int))(uintptr_t)GUEST_SAVE_FILE)(slot);
		fprintf(stderr, "sword3-sdl: host menu -> SaveFileACT slot=%d\n",
			slot);
		return;
	}
	if (action == 1) {
		if (guest_data_ok(GUEST_UIGAMEPAD)) {
			reset_keys = (void (*)(void *))(uintptr_t)
				GUEST_RESET_KEYS;
			reset_keys((void *)(uintptr_t)GUEST_UIGAMEPAD);
		}
		((void (*)(int))(uintptr_t)GUEST_LOAD_GAME)(slot);
		fprintf(stderr, "sword3-sdl: host menu -> LoadGame slot=%d\n",
			slot);
	}
}

static void apply_pad_pointer(void)
{
	host_unlink_return_menu();
	host_menu_run_pending();
	sync_ui_mode();
	menu_poll_open_pulse();
	menu_trace_poll();
	ensure_cursor();
	apply_cursor_move();
	sync_game_pointer();
}

static int accept_a_edge(int down)
{
	Uint32 now = SDL_GetTicks();

	if (down) {
		if (g_a_down)
			return 0;
		if (g_a_edge_ms != 0 && now - g_a_edge_ms < A_CHATTER_MS)
			return 0;
		g_a_down = 1;
		g_a_edge_ms = now;
		return 1;
	}
	if (!g_a_down)
		return 0;
	g_a_down = 0;
	return 1;
}

static const char *pad_button_name(int button)
{
	switch (button) {
	case SDL_CONTROLLER_BUTTON_A:
		return "A";
	case SDL_CONTROLLER_BUTTON_B:
		return "B";
	case SDL_CONTROLLER_BUTTON_X:
		return "X";
	case SDL_CONTROLLER_BUTTON_Y:
		return "Y";
	case SDL_CONTROLLER_BUTTON_BACK:
		return "SELECT";
	case SDL_CONTROLLER_BUTTON_START:
		return "START";
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
		return "L1";
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
		return "R1";
	case SDL_CONTROLLER_BUTTON_LEFTSTICK:
		return "L3";
	case SDL_CONTROLLER_BUTTON_RIGHTSTICK:
		return "R3";
	case SDL_CONTROLLER_BUTTON_GUIDE:
		return "GUIDE";
	default:
		return "?";
	}
}

static void prepare_guest_controller(SDL_JoystickID which)
{
	uint8_t *pad;
	int active;
	int needed;

	if (which < 0 || which >= INT_MAX)
		return;
	if (!guest_data_ok(GUEST_UIGAMEPAD))
		return;
	pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
	active = *(volatile int *)(pad + GUEST_ACTIVE_JOYSTICK);
	needed = (int)which + 1;
	if (active >= needed)
		return;
	/*
	 * UIGamePad's controller-axis handler rejects events whose instance id
	 * is above this scalar. The embedded iOS SDL backend sees no Linux
	 * devices, while the host SDL event carries a valid instance id.
	 * Populate only that scalar; controller objects remain host-owned.
	 */
	*(volatile int *)(pad + GUEST_ACTIVE_JOYSTICK) = needed;
	fprintf(stderr, "sword3-sdl: guest controller instance=%d\n",
		(int)which);
}

static void log_pad_button(int button, int down)
{
	unsigned cap;

	if (button < 0 || button >= GUEST_PAD_BUTTONS)
		return;
	if (button == SDL_CONTROLLER_BUTTON_A ||
	    button == SDL_CONTROLLER_BUTTON_B ||
	    button == SDL_CONTROLLER_BUTTON_BACK ||
	    button == SDL_CONTROLLER_BUTTON_START)
		cap = 8;
	else
		cap = 40;
	if (g_pad_seen[button] >= cap)
		return;
	g_pad_seen[button]++;
	fprintf(stderr, "sword3-sdl: pad %s (%d) %s pointer=%d fight=%d menu=%d\n",
		pad_button_name(button), button, down ? "down" : "up",
		g_pointer_ui, g_fight_ui, g_menu_keys);
}

static int rewrite_event(SDL_Event *event)
{
	int down;
	int button;

	if (!event)
		return 0;
	scale_pointer_event(event);
	if (event->type == SDL_CONTROLLERAXISMOTION ||
	    event->type == SDL_CONTROLLERBUTTONDOWN ||
	    event->type == SDL_CONTROLLERBUTTONUP)
		fight_poll_ui();
	if (event->type == SDL_JOYAXISMOTION ||
	    event->type == SDL_JOYHATMOTION ||
	    event->type == SDL_JOYBALLMOTION ||
	    event->type == SDL_JOYBUTTONDOWN ||
	    event->type == SDL_JOYBUTTONUP) {
		event->type = SDL_FIRSTEVENT;
		return 0;
	}
	if (event->type == SDL_CONTROLLERAXISMOTION) {
		if (host_menu_active()) {
			host_menu_axis(event->caxis.axis, event->caxis.value);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (host_battle_owns_pad()) {
			host_battle_axis(event->caxis.axis, event->caxis.value);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_fight_ui) {
			fight_axis_event(event->caxis.axis, event->caxis.value);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_save_ui) {
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_title_keys) {
			title_axis_event(event->caxis.axis, event->caxis.value);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_menu_keys) {
			menu_axis_event(event->caxis.axis, event->caxis.value);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (menu_opening()) {
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_pointer_ui) {
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		prepare_guest_controller(event->caxis.which);
		return 1;
	}
	if (event->type == SDL_MOUSEMOTION) {
		g_cursor_x = (float)event->motion.x;
		g_cursor_y = (float)event->motion.y;
		g_cursor_ready = 1;
		clamp_cursor();
		return 1;
	}
	if (event->type == SDL_MOUSEBUTTONDOWN ||
	    event->type == SDL_MOUSEBUTTONUP) {
		g_cursor_x = (float)event->button.x;
		g_cursor_y = (float)event->button.y;
		g_cursor_ready = 1;
		clamp_cursor();
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
	if (event->type != SDL_CONTROLLERBUTTONDOWN &&
	    event->type != SDL_CONTROLLERBUTTONUP)
		return 1;

	down = event->type == SDL_CONTROLLERBUTTONDOWN;
	button = event->cbutton.button;
	log_pad_button(button, down);
	if (movie_consume_skip(button, down)) {
		event->type = SDL_FIRSTEVENT;
		return 0;
	}
	if (button == SDL_CONTROLLER_BUTTON_BACK) {
		g_btn_back = down;
		maybe_combo_exit();
		if (g_save_ui) {
			if (down) {
				g_after_continue = 0;
				g_load_context_done = 1;
			}
			fill_key(event, SDL_SCANCODE_ESCAPE, down);
			return 1;
		}
		if (host_menu_active()) {
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_menu_open_button == SDL_CONTROLLER_BUTTON_BACK) {
			if (!down) {
				menu_finish_open();
				fprintf(stderr,
					"sword3-sdl: SELECT menu button up\n");
			}
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (menu_drawn()) {
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (!g_title_keys && !g_pointer_ui && !g_fight_ui && down) {
			if (g_menu_session || g_menu_open_pending ||
			    g_menu_close_pending)
				menu_leave_session();
			if (native_system_menu()) {
				menu_begin_open(SDL_CONTROLLER_BUTTON_BACK);
				fprintf(stderr,
					"sword3-sdl: SELECT -> native menu\n");
			} else {
				release_guest_walk();
				host_menu_open();
				fprintf(stderr,
					"sword3-sdl: SELECT -> host menu\n");
			}
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_fight_ui || host_battle_owns_pad()) {
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (down && guest_on_title() && !g_after_continue)
			warp_menu_item(0);
		fill_key(event, SDL_SCANCODE_ESCAPE, down);
		return 1;
	}
	if (button == SDL_CONTROLLER_BUTTON_START) {
		g_btn_start = down;
		maybe_combo_exit();
		event->type = SDL_FIRSTEVENT;
		return 0;
	}
	if (button == SDL_CONTROLLER_BUTTON_B) {
		if (!down)
			host_battle_button(button, 0);
		if (host_battle_owns_pad()) {
			if (down)
				host_battle_button(button, down);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_fight_ui) {
			if (!down) {
				event->type = SDL_FIRSTEVENT;
				return 0;
			}
			((void (*)(void))(uintptr_t)GUEST_FIGHT_CANCEL)();
			if (g_fight_key_seen++ < 48)
				fprintf(stderr,
					"sword3-sdl: fight B -> native cancel now=%d\n",
					guest_now_menu());
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_save_ui) {
			if (down) {
				g_after_continue = 0;
				g_load_context_done = 1;
			}
			fill_key(event, SDL_SCANCODE_ESCAPE, down);
			return 1;
		}
		if (g_title_keys) {
			fill_key(event, SDL_SCANCODE_ESCAPE, down);
			return 1;
		}
		if (host_menu_active()) {
			host_menu_button(button, down);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (menu_drawn()) {
			if (down) {
				g_menu_back_until = SDL_GetTicks() +
						    MENU_BACK_GRACE_MS;
				g_menu_gone_at = 0;
				g_menu_close_widget = menu_begin_close();
			}
			else
				menu_finish_close(g_menu_close_widget);
			if (g_menu_key_seen++ < 32)
				fprintf(stderr,
					"sword3-sdl: menu B -> %s %s\n",
					g_menu_close_widget ? "back icon" :
							     "wait icon",
					down ? "down" : "up");
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (!down && g_menu_close_pending)
			g_menu_close_pending = 0;
		if (g_menu_session && !menu_drawn()) {
			if (down)
				menu_leave_session();
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (menu_opening()) {
			if (down)
				menu_leave_session();
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_pointer_ui) {
			if (g_save_ui && down)
				g_after_continue = 0;
			fill_mouse_button(event, SDL_BUTTON_RIGHT, down);
			return 1;
		}
		if (g_menu_key_seen < 8 && down)
			fprintf(stderr, "sword3-sdl: field B swallowed\n");
		event->type = SDL_FIRSTEVENT;
		return 0;
	}
	if (button == SDL_CONTROLLER_BUTTON_A) {
		if (host_menu_active()) {
			host_menu_button(button, down);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (!down)
			host_battle_button(button, 0);
		if (host_battle_owns_pad()) {
			if (down)
				host_battle_button(button, down);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_menu_keys && menu_drawn()) {
			Uint32 now = SDL_GetTicks();

			if (!down && g_menu_action_a_up) {
				g_menu_action_a_up = 0;
				event->type = SDL_FIRSTEVENT;
				return 0;
			}
			if (down && menu_tianshu_active() &&
			    g_menu_action_focus >= 0) {
				menu_click_action(g_menu_action_focus);
				g_menu_action_a_up = 1;
				g_menu_a_block_until = now + 350;
				event->type = SDL_FIRSTEVENT;
				return 0;
			}
			if (down &&
			    !SDL_TICKS_PASSED(now, g_menu_a_block_until)) {
				event->type = SDL_FIRSTEVENT;
				return 0;
			}
			if (!accept_a_edge(down)) {
				event->type = SDL_FIRSTEVENT;
				return 0;
			}
			guest_keyboard_key(SDL_SCANCODE_RETURN, down);
			if (!down)
				g_menu_a_block_until = now + 350;
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (!down && g_fight_target_a_up) {
			g_fight_target_a_up = 0;
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_fight_ui) {
			if (down) {
				if (guest_now_menu() == 3)
					fight_confirm_target();
				g_fight_target_a_up = 1;
				event->type = SDL_FIRSTEVENT;
				if (g_fight_key_seen++ < 48)
					fprintf(stderr,
						"sword3-sdl: fight native confirm now=%d\n",
						guest_now_menu());
				return 0;
			}
			fill_key(event, SDL_SCANCODE_RETURN, down);
			if (g_fight_key_seen++ < 48)
				fprintf(stderr,
					"sword3-sdl: fight A -> Return %s\n",
					down ? "down" : "up");
			return 1;
		}
		if (!g_pointer_ui && !g_title_keys && !g_save_ui) {
			if (!accept_a_edge(down)) {
				if (!down && g_field_mode_held)
					field_talk_key(0);
				event->type = SDL_FIRSTEVENT;
				return 0;
			}
			field_talk_key(down);
			if (g_field_a_seen < 16) {
				g_field_a_seen++;
				fprintf(stderr,
					"sword3-sdl: field A -> Return %s\n",
					down ? "down" : "up");
			}
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (!g_pointer_ui) {
			if (!accept_a_edge(down)) {
				event->type = SDL_FIRSTEVENT;
				return 0;
			}
			fill_key(event, SDL_SCANCODE_RETURN, down);
			if (g_title_keys && !down) {
				g_after_continue = 1;
				g_load_context_done =
					guest_read_i32(GUEST_TITLE_SELECTION,
						       0) != 0;
			} else if (g_save_ui && !down) {
				g_load_context_done = 1;
			}
			return 1;
		}
		if (!accept_a_edge(down)) {
			if (g_a_chatter < 8) {
				g_a_chatter++;
				fprintf(stderr,
					"sword3-sdl: swallow A chatter %s\n",
					down ? "down" : "up");
			}
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		ensure_cursor();
		g_finger_down = down;
		fill_finger(event, down ? SDL_FINGERDOWN : SDL_FINGERUP);
		if (!down && guest_on_title() && !g_after_continue)
			g_after_continue = 1;
		if (g_finger_seen < 16) {
			g_finger_seen++;
			fprintf(stderr,
				"sword3-sdl: finger %s at %.3f,%.3f (cursor %.0f,%.0f)\n",
				down ? "down" : "up", event->tfinger.x,
				event->tfinger.y, g_cursor_x, g_cursor_y);
		}
		return 1;
	}
	if (button == SDL_CONTROLLER_BUTTON_DPAD_UP ||
	    button == SDL_CONTROLLER_BUTTON_DPAD_DOWN ||
	    button == SDL_CONTROLLER_BUTTON_DPAD_LEFT ||
	    button == SDL_CONTROLLER_BUTTON_DPAD_RIGHT) {
		if (host_menu_active()) {
			host_menu_button(button, down);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (host_battle_owns_pad()) {
			host_battle_button(button, down);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_fight_ui) {
			fight_dpad_event(button, down);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_save_ui) {
			save_dpad_event(button, down);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_title_keys) {
			title_dpad_event(button, down);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_menu_keys) {
			menu_dpad_event(button, down);
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (menu_opening()) {
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_pointer_ui) {
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		prepare_guest_controller(event->cbutton.which);
		return 1;
	}
	if (button == SDL_CONTROLLER_BUTTON_LEFTSTICK) {
		if (g_save_ui || g_title_keys || g_fight_ui ||
		    g_pointer_ui) {
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (g_menu_open_button == SDL_CONTROLLER_BUTTON_LEFTSTICK) {
			if (!down) {
				menu_finish_open();
				fprintf(stderr,
					"sword3-sdl: L3 menu button up\n");
			}
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (menu_drawn() || menu_opening() || g_menu_session) {
			event->type = SDL_FIRSTEVENT;
			return 0;
		}
		if (down) {
			if (host_menu_active())
				host_menu_close();
			if (g_menu_session || g_menu_open_pending ||
			    g_menu_close_pending)
				menu_leave_session();
			release_guest_walk();
			menu_begin_open(SDL_CONTROLLER_BUTTON_LEFTSTICK);
			fprintf(stderr, "sword3-sdl: L3 -> native menu\n");
		}
		event->type = SDL_FIRSTEVENT;
		return 0;
	}
	if (host_menu_active()) {
		host_menu_button(button, down);
		event->type = SDL_FIRSTEVENT;
		return 0;
	}
	if (host_battle_owns_pad()) {
		host_battle_button(button, down);
		event->type = SDL_FIRSTEVENT;
		return 0;
	}
	/* X/Y/L1/R1/R3/Guide are not game actions on this handheld. */
	event->type = SDL_FIRSTEVENT;
	return 0;
}

static int next_translated_event(SDL_Event *event, int timeout)
{
	Uint32 start = SDL_GetTicks();
	int result;
	int slice;
	int blocking = timeout < 0;

	ensure_gamecontroller();
	for (;;) {
		apply_pad_pointer();
		movie_host_tick();
		maybe_force_opening();
		if (timeout == 0)
			slice = 0;
		else if (blocking)
			slice = 16;
		else {
			int left = timeout - (int)(SDL_GetTicks() - start);

			if (left <= 0) {
				sync_game_pointer();
				return 0;
			}
			slice = left > 16 ? 16 : left;
		}
		result = SDL_WaitEventTimeout(event, slice);
		if (result <= 0) {
			if (timeout == 0 ||
			    (!blocking &&
			     (int)(SDL_GetTicks() - start) >= timeout)) {
				sync_game_pointer();
				return 0;
			}
			continue;
		}
		if (rewrite_event(event) && event->type != SDL_FIRSTEVENT) {
			sync_game_pointer();
			return 1;
		}
	}
}

int sword3_ret0(void)
{
	return 0;
}

int sword3_SDL_InitSubSystem(Uint32 flags)
{
	int result = SDL_InitSubSystem(flags);
	fprintf(stderr, "sword3-sdl: SDL_InitSubSystem(0x%x) -> %d (%s)\n",
		flags, result, result == 0 ? "ok" : SDL_GetError());
	if (result == 0 &&
	    (flags & (SDL_INIT_JOYSTICK | SDL_INIT_GAMECONTROLLER)))
		ensure_gamecontroller();
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
	fprintf(stderr, "sword3-sdl: SDL_VideoQuit\n");
	SDL_VideoQuit();
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
	try_init_guest_viewport();
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
	try_init_guest_viewport();
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
	try_init_guest_viewport();
	owner_register(KIND_RENDERER, renderer);
	return renderer;
}

void sword3_SDL_DestroyRenderer(SDL_Renderer *renderer)
{
	owner_unregister("SDL_DestroyRenderer", KIND_RENDERER, renderer);
	if (g_renderer == renderer)
		g_renderer = NULL;
	SDL_DestroyRenderer(renderer);
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

static int battle_skip_dst(int x, int y, int w, int h)
{
	int lw;
	int lh;

	if (w <= 0 || h <= 0)
		return 0;
	if (!host_battle_active())
		return 0;
	lw = g_logical_w > 0 ? g_logical_w : GAME_W;
	lh = g_logical_h > 0 ? g_logical_h : GAME_H;
	if (host_battle_skip_blit(x * GAME_W / lw, y * GAME_H / lh,
				  w * GAME_W / lw, h * GAME_H / lh))
		return 1;
	if ((lw != GAME_W || lh != GAME_H) &&
	    host_battle_skip_blit(x, y, w, h))
		return 1;
	return 0;
}

int sword3_SDL_RenderClear(SDL_Renderer *renderer)
{
	static unsigned seen;

	owner_check("SDL_RenderClear", KIND_RENDERER, renderer);
	if (SDL_GetRenderTarget(renderer) == NULL)
		fight_poll_ui();
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
	owner_check("SDL_RenderCopy", KIND_TEXTURE, texture);
	if (dstrect &&
	    battle_skip_dst(dstrect->x, dstrect->y, dstrect->w, dstrect->h))
		return 0;
	return SDL_RenderCopy(renderer, texture, srcrect, dstrect);
}

int sword3_SDL_RenderCopyF(SDL_Renderer *renderer, SDL_Texture *texture,
			   const SDL_Rect *srcrect, const SDL_FRect *dstrect)
{
	static unsigned seen;
	int result;

	owner_check("SDL_RenderCopyF", KIND_RENDERER, renderer);
	owner_check("SDL_RenderCopyF", KIND_TEXTURE, texture);
	if (dstrect &&
	    battle_skip_dst((int)dstrect->x, (int)dstrect->y,
			    (int)(dstrect->w + 0.5f),
			    (int)(dstrect->h + 0.5f)))
		return 0;
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
	owner_check("SDL_RenderCopyEx", KIND_TEXTURE, texture);
	if (dstrect &&
	    battle_skip_dst(dstrect->x, dstrect->y, dstrect->w, dstrect->h))
		return 0;
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
	owner_check("SDL_RenderCopyExF", KIND_TEXTURE, texture);
	if (dstrect &&
	    battle_skip_dst((int)dstrect->x, (int)dstrect->y,
			    (int)(dstrect->w + 0.5f),
			    (int)(dstrect->h + 0.5f)))
		return 0;
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
	SDL_Rect arm_h;
	SDL_Rect arm_v;
	Uint8 r;
	Uint8 g;
	Uint8 b;
	Uint8 a;
	SDL_BlendMode blend;
	int x;
	int y;
	int bw;
	int bh;

	owner_check("SDL_RenderPresent", KIND_RENDERER, renderer);
	ensure_gamecontroller();
	apply_pad_pointer();
	seen++;
	if (seen <= 8 || (seen % 120) == 0)
		fprintf(stderr, "sword3-sdl: RenderPresent #%u\n", seen);
	maybe_combo_exit();
	g_last_present_ms = SDL_GetTicks();
	if (movie_is_playing() && SDL_GetRenderTarget(renderer) == NULL) {
		movie_present(renderer);
	} else if (host_menu_active() &&
		   SDL_GetRenderTarget(renderer) == NULL) {
		host_menu_draw(renderer, g_logical_w, g_logical_h);
	} else if (host_battle_active() &&
		   SDL_GetRenderTarget(renderer) == NULL) {
		host_battle_draw(renderer, g_logical_w, g_logical_h);
	} else if (g_menu_keys && SDL_GetRenderTarget(renderer) == NULL &&
		   menu_action_select_rect(&x, &y, &bw, &bh)) {
		draw_select_frame(renderer, x, y, bw, bh);
	} else if (g_pointer_ui && g_cursor_ready && g_logical_w > 0 &&
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
	save_flush_direction_releases();
	menu_flush_direction_releases();
	fight_flush_direction_releases();
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
	owner_check("SDL_SetRenderTarget", KIND_TEXTURE, texture);
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
	return texture;
}

void sword3_SDL_DestroyTexture(SDL_Texture *texture)
{
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
	fprintf(stderr,
		"sword3-sdl: CreateTextureFromSurface %dx%d -> %p%s%s\n",
		surface ? surface->w : 0, surface ? surface->h : 0, texture,
		texture ? "" : " ", texture ? "" : SDL_GetError());
	owner_register(KIND_TEXTURE, texture);
	return texture;
}

int sword3_SDL_UpdateTexture(SDL_Texture *texture, const SDL_Rect *rect,
			     const void *pixels, int pitch)
{
	owner_check("SDL_UpdateTexture", KIND_TEXTURE, texture);
	return SDL_UpdateTexture(texture, rect, pixels, pitch);
}

int sword3_SDL_SetTextureBlendMode(SDL_Texture *texture,
				   SDL_BlendMode blendMode)
{
	owner_check("SDL_SetTextureBlendMode", KIND_TEXTURE, texture);
	return SDL_SetTextureBlendMode(texture, blendMode);
}

int sword3_SDL_LockTexture(SDL_Texture *texture, const SDL_Rect *rect,
			   void **pixels, int *pitch)
{
	owner_check("SDL_LockTexture", KIND_TEXTURE, texture);
	return SDL_LockTexture(texture, rect, pixels, pitch);
}

void sword3_SDL_UnlockTexture(SDL_Texture *texture)
{
	owner_check("SDL_UnlockTexture", KIND_TEXTURE, texture);
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

	ensure_gamecontroller();
	if (action != SDL_ADDEVENT)
		apply_pad_pointer();
	n = SDL_PeepEvents(events, numevents, action, minType, maxType);
	if (action == SDL_ADDEVENT || n <= 0 || !events)
		return n;
	for (i = 0; i < n; i++)
		rewrite_event(&events[i]);
	return n;
}

void sword3_SDL_PumpEvents(void)
{
	ensure_gamecontroller();
	SDL_PumpEvents();
	apply_pad_pointer();
	movie_host_tick();
	maybe_force_opening();
}

int sword3_SDL_PollEvent(SDL_Event *event)
{
	return next_translated_event(event, 0);
}

int sword3_SDL_WaitEventTimeout(SDL_Event *event, int timeout)
{
	return next_translated_event(event, timeout);
}

SDL_bool sword3_SDL_IsGameController(int joystick_index)
{
	static unsigned seen;
	SDL_Joystick *js;
	SDL_JoystickID id;
	SDL_bool known = SDL_FALSE;

	if (joystick_index >= 0 && joystick_index < SDL_NumJoysticks() &&
	    SDL_IsGameController(joystick_index))
		known = SDL_TRUE;
	else if (g_pad) {
		js = SDL_GameControllerGetJoystick(g_pad);
		if (js) {
			id = SDL_JoystickInstanceID(js);
			if (joystick_index == (int)id)
				known = SDL_TRUE;
		}
	}
	if (seen < 8) {
		seen++;
		fprintf(stderr, "sword3-sdl: IsGameController(%d) -> %d pad=%p\n",
			joystick_index, (int)known, (void *)g_pad);
	}
	return known;
}

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

static Mix_Music *g_music;
static int g_mixer_ready;

static int usable_music_file(const char *path)
{
	struct stat st;

	return path && path[0] && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int join_music_path(char *buf, size_t cap, const char *a, const char *b,
			   const char *c)
{
	int n;

	if (!buf || !a || !a[0] || !b)
		return -1;
	if (c)
		n = snprintf(buf, cap, "%s/%s/%s", a, b, c);
	else
		n = snprintf(buf, cap, "%s/%s", a, b);
	return (n > 0 && (size_t)n < cap) ? 0 : -1;
}

static const char *resolve_music_path(const char *path, char *buf, size_t cap)
{
	const char *roots[3];
	const char *base;
	size_t i;

	if (usable_music_file(path))
		return path;
	base = strrchr(path, '/');
	base = base ? base + 1 : path;
	roots[0] = getenv("SWORD3_BUNDLE_DIR");
	roots[1] = getenv("SWORD3_DATA_DIR");
	roots[2] = getenv("TMPDIR");
	for (i = 0; i < 3; ++i) {
		if (!roots[i] || roots[i][0] != '/')
			continue;
		if (join_music_path(buf, cap, roots[i], path, NULL) == 0 &&
		    usable_music_file(buf))
			return buf;
		if (join_music_path(buf, cap, roots[i], base, NULL) == 0 &&
		    usable_music_file(buf))
			return buf;
		if (join_music_path(buf, cap, roots[i], "MusicFile", base) == 0 &&
		    usable_music_file(buf))
			return buf;
	}
	return path;
}

static int ensure_host_mixer(void)
{
	int freq = 0;
	int channels = 0;
	Uint16 format = 0;
	int codecs;

	if (g_mixer_ready)
		return 0;
	SDL_ClearError();
	codecs = Mix_Init(MIX_INIT_MP3 | MIX_INIT_OGG);
	fprintf(stderr, "sword3-sdl: Mix_Init -> 0x%x%s%s\n", codecs,
		(codecs & MIX_INIT_MP3) ? " mp3" : "",
		(codecs & MIX_INIT_OGG) ? " ogg" : "");
	if (Mix_QuerySpec(&freq, &format, &channels) == 0) {
		if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) != 0) {
			fprintf(stderr, "sword3-sdl: Mix_OpenAudio failed: %s\n",
				Mix_GetError());
			return -1;
		}
	}
	Mix_VolumeMusic(MIX_MAX_VOLUME);
	Mix_Volume(-1, MIX_MAX_VOLUME);
	g_mixer_ready = 1;
	fprintf(stderr, "sword3-sdl: host mixer opened for music\n");
	return 0;
}

static int play_loaded_music(const char *label, int loops)
{
	int mix_loops = loops <= 0 ? -1 : loops;

	if (!g_music)
		return -1;
	if (Mix_PlayMusic(g_music, mix_loops) != 0) {
		fprintf(stderr, "sword3-sdl: Mix_PlayMusic(%s) failed: %s\n",
			label, Mix_GetError());
		Mix_FreeMusic(g_music);
		g_music = NULL;
		return -1;
	}
	fprintf(stderr, "sword3-sdl: playing music %s loops=%d\n", label,
		mix_loops);
	return 0;
}

int sword3_host_play_music_file(const char *path, int loops)
{
	char resolved[4096];
	const char *use;

	if (!path || !path[0])
		return -1;
	if (ensure_host_mixer() != 0)
		return -1;
	use = resolve_music_path(path, resolved, sizeof(resolved));
	Mix_HaltMusic();
	if (g_music) {
		Mix_FreeMusic(g_music);
		g_music = NULL;
	}
	g_music = Mix_LoadMUS(use);
	if (!g_music) {
		fprintf(stderr, "sword3-sdl: Mix_LoadMUS(%s) failed: %s\n", use,
			Mix_GetError());
		return -1;
	}
	return play_loaded_music(use, loops);
}

int sword3_host_play_music_data(const void *data, size_t size, int loops)
{
	SDL_RWops *rw;

	if (!data || size == 0 || size > (size_t)INT_MAX)
		return -1;
	if (ensure_host_mixer() != 0)
		return -1;
	Mix_HaltMusic();
	if (g_music) {
		Mix_FreeMusic(g_music);
		g_music = NULL;
	}
	rw = SDL_RWFromConstMem(data, (int)size);
	if (!rw) {
		fprintf(stderr, "sword3-sdl: RWFromConstMem failed: %s\n",
			SDL_GetError());
		return -1;
	}
	g_music = Mix_LoadMUS_RW(rw, 1);
	if (!g_music) {
		fprintf(stderr, "sword3-sdl: Mix_LoadMUS_RW failed: %s\n",
			Mix_GetError());
		return -1;
	}
	return play_loaded_music("memory", loops);
}

void sword3_host_stop_music(void)
{
	Mix_HaltMusic();
	if (g_music) {
		Mix_FreeMusic(g_music);
		g_music = NULL;
	}
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
	int have_frame;
	int active;
	int tex_w;
	int tex_h;
} g_movie;

static int movie_is_playing(void)
{
	return g_movie.active;
}

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
	audio->stream = SDL_NewAudioStream(sdl_fmt, channels,
					   frame->sample_rate > 0
						   ? frame->sample_rate
						   : audio->codec->sample_rate,
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
			fprintf(stderr, "sword3-sdl: movie audio decode failed\n");
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
	static int closing;

	if (closing)
		return;
	if (!g_movie.active && !g_movie.video)
		return;
	closing = 1;
	g_movie.active = 0;
	g_movie.done = NULL;
	g_movie.have_frame = 0;
	g_movie_frames = 0;
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
	closing = 0;
}

static int movie_ensure_texture(SDL_Renderer *renderer, int width, int height)
{
	if (g_movie.texture && g_movie.tex_w == width && g_movie.tex_h == height)
		return 0;
	if (g_movie.texture) {
		owner_unregister("movie", KIND_TEXTURE, g_movie.texture);
		SDL_DestroyTexture(g_movie.texture);
		g_movie.texture = NULL;
	}
	g_movie.texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
					    SDL_TEXTUREACCESS_STREAMING, width,
					    height);
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

static void movie_present(SDL_Renderer *renderer)
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
				decoded < 0 ? sword3_video_error(g_movie.video)
					    : "eof");
			movie_close(1);
			return;
		}
		g_movie.have_frame = 1;
		if (movie_ensure_texture(renderer, g_movie.frame.width,
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
		g_movie_frames++;
		if (g_movie_frames == 1)
			fprintf(stderr,
				"sword3-sdl: movie first frame %dx%d\n",
				g_movie.frame.width, g_movie.frame.height);
		else if ((g_movie_frames % 30) == 0)
			fprintf(stderr, "sword3-sdl: movie frame %u t=%.2f\n",
				g_movie_frames, g_movie.frame.pts_seconds);
	}
	if (g_movie.texture)
		SDL_RenderCopy(renderer, g_movie.texture, NULL, NULL);
}

static void movie_pump_skip_events(void)
{
	SDL_Event event;

	ensure_gamecontroller();
	SDL_PumpEvents();
	while (SDL_PeepEvents(&event, 1, SDL_GETEVENT, SDL_FIRSTEVENT,
			      SDL_LASTEVENT) > 0) {
		if (event.type == SDL_QUIT) {
			movie_close(1);
			return;
		}
		if (event.type == SDL_CONTROLLERBUTTONDOWN ||
		    event.type == SDL_CONTROLLERBUTTONUP) {
			movie_consume_skip(event.cbutton.button,
					  event.type == SDL_CONTROLLERBUTTONDOWN);
			continue;
		}
		if (event.type == SDL_FINGERDOWN ||
		    event.type == SDL_MOUSEBUTTONDOWN)
			movie_consume_skip(SDL_CONTROLLER_BUTTON_A, 1);
	}
}

static void movie_host_tick(void)
{
	Uint32 now;

	if (!movie_is_playing() || !g_renderer)
		return;
	now = SDL_GetTicks();
	if (g_last_present_ms && now - g_last_present_ms < 16)
		return;
	SDL_SetRenderTarget(g_renderer, NULL);
	SDL_SetRenderDrawColor(g_renderer, 0, 0, 0, 255);
	SDL_RenderClear(g_renderer);
	movie_present(g_renderer);
	if (!movie_is_playing())
		return;
	SDL_RenderPresent(g_renderer);
	g_last_present_ms = now;
}

static void maybe_force_opening(void)
{
	static int tried;
	char path[PATH_MAX];
	size_t n;

	if (tried)
		return;
	if (!getenv("SWORD3_FORCE_OPENING"))
		return;
	if (!g_renderer || SDL_GetTicks() < 8000)
		return;
	tried = 1;
	if (getcwd(path, sizeof(path)) == NULL)
		return;
	n = strlen(path);
	if (n + 20 >= sizeof(path))
		return;
	memcpy(path + n, "/Video/opening.mp4", 19);
	fprintf(stderr, "sword3-sdl: FORCE_OPENING %s\n", path);
	sword3_host_play_video_file(path, NULL);
}

static void movie_run_until_done(void)
{
	fprintf(stderr, "sword3-sdl: movie blocking present loop\n");
	while (g_movie.active) {
		movie_pump_skip_events();
		if (!g_movie.active)
			break;
		g_last_present_ms = 0;
		movie_host_tick();
		SDL_Delay(8);
	}
}

static int movie_consume_skip(int button, int down)
{
	if (g_swallow_a_up && button == SDL_CONTROLLER_BUTTON_A) {
		if (!down)
			g_swallow_a_up = 0;
		return 1;
	}
	if (!g_movie.active)
		return 0;
	if (!down)
		return 0;
	if (button != SDL_CONTROLLER_BUTTON_A &&
	    button != SDL_CONTROLLER_BUTTON_START)
		return 0;
	fprintf(stderr, "sword3-sdl: skipping movie\n");
	if (button == SDL_CONTROLLER_BUTTON_A)
		g_swallow_a_up = 1;
	movie_close(1);
	return 1;
}

int sword3_host_video_playing(void)
{
	return g_movie.active;
}

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
	sword3_host_stop_music();
	g_movie.done = done;
	g_movie.start_ms = SDL_GetTicks();
	g_movie.have_frame = 0;
	g_movie.active = 1;
	fprintf(stderr, "sword3-sdl: playing video %s\n", path);
	movie_run_until_done();
	return 0;
}

void sword3_host_stop_video(void)
{
	if (!g_movie.active)
		return;
	fprintf(stderr, "sword3-sdl: stopping video\n");
	movie_close(1);
}
