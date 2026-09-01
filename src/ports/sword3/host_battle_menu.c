#include "host_battle_menu.h"
#include "host_font.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <SDL2/SDL_ttf.h>

#define BATTLE_DEADZONE 14000
#define BATTLE_GAME_W 640
#define BATTLE_GAME_H 480
#define BATTLE_CMD_N 5
#define BATTLE_MAGIC_CAT 3
#define BATTLE_ITEM_CAT 4
#define BATTLE_ENTRY_MAX 32
#define BATTLE_TEXT_CACHE 96
#define BATTLE_GUEST_LO 0x100294000ull
#define BATTLE_GUEST_HI 0x100380000ull

#define GUEST_FIGHT_FLAG 0x1002f27f8ull
#define GUEST_NOW_MENU 0x1002f1f0cull
#define GUEST_CMD_SEL 0x1002a5308ull
#define GUEST_CMD_BUTTONS 0x1002ab900ull
#define GUEST_CMD_EXTRA 0x1002abe40ull
#define GUEST_CMD_TACTICS 0x1002ac380ull
#define GUEST_CMD_RETREAT 0x1002ac2c0ull
#define GUEST_CMD_ENABLE 0x1002f3568ull
#define GUEST_CMD_FILL 0x10003d9f4ull
#define GUEST_EXTRA_CAN 0x10007ec44ull
#define GUEST_EXTRA_TYPE 0x1002f2a00ull
#define GUEST_CMD_STRIDE 0xc0u
#define GUEST_CMD_IMG 8
#define GUEST_CMD_X 0x20
#define GUEST_CMD_Y 0x24
#define GUEST_CMD_H 0x94
#define GUEST_CMD_W 0x98
#define GUEST_BTN_SHOW 0x9f
#define GUEST_BTN_NAME 0x50
#define GUEST_IMG_W 0x6c
#define GUEST_IMG_H 0x70
#define GUEST_CMD_EXTRA_N 4
#define BATTLE_SLOT_N 9
#define BATTLE_PACK_MAX 9
#define BATTLE_COLS 3
#define BATTLE_FB_X0 190
#define BATTLE_FB_Y0 60
#define BATTLE_FB_W 90
#define BATTLE_FB_H 90
#define BATTLE_HIDE_MAX (BATTLE_CMD_N + GUEST_CMD_EXTRA_N + 2)
#define SLOT_TACTICS 8
#define SLOT_FLEE 9
#define GUEST_PLAYER_ID 0x1002f1ff8ull
#define GUEST_CANT_RETREAT 0x1002f27fcull
#define GUEST_FLEE_FLAG 0x1002f3591ull
#define GUEST_FLEE_BLOCK 0x10007c03cull
#define GUEST_BATTLE_INPUT_CLICK 0x10003cbf8ull
#define GUEST_FLEE_STEP 0x10000aab8ull
#define GUEST_IMAGE_GET 0x1001ea094ull
#define ACTOR_OFF_CURRENT_IMAGE_ID 0x46c
#define ACTOR_OFF_CURRENT_IMAGE_FRAME 0x470
#define ACTOR_OFF_CURRENT_IMAGE 0x2e98
#define ACTOR_OFF_FLEE_IMAGE_ID 0x694
#define ACTOR_OFF_FLEE_IMAGE_FRAME 0x698
#define GUEST_UIGAMEPAD 0x100304e28ull
#define GUEST_INPUT_MODE 0x23d4
#define GUEST_INPUT_KEYBOARD 1
#define GUEST_CMD_SELECT_OK 0x100040c54ull
#define GUEST_CMD_DRAW 0x1001a8b84ull
#define GUEST_CMD_MENU_DRAW 0x10006658cull
#define GUEST_FIGHT_CANCEL 0x10003f3ecull
#define GUEST_SOUND 0x1001c076cull
#define GUEST_OK_GATE 0x1002f2e48ull
#define GUEST_OK_CLICK 0x1002f2e68ull
#define GUEST_SPELL_ID 0x1002f1f54ull
#define GUEST_SPELL_CAT 0x1002f1f5cull
#define GUEST_SPELL_IDX 0x1002a5318ull
#define GUEST_SPECIAL_IDX 0x1002a5320ull
#define GUEST_ITEM_PAGE 0x1002a5330ull
#define GUEST_ITEM_SEL 0x1002a531cull
#define GUEST_ITEM_ALT 0x1002f1f4cull
#define GUEST_ITEM_ARG 0x1002f1f58ull
#define GUEST_NOW_MAGIC 2
#define GUEST_NOW_TARGET 3
#define GUEST_NOW_ITEM 4
#define GUEST_NOW_SPECIAL 8
#define GUEST_DIR_A 0x1002f2758ull
#define GUEST_DIR_AUX 0x1002f275cull
#define GUEST_MSG_HEAD 0x1002eaae0ull
#define GUEST_TGT_F18 0x1002f1f18ull
#define GUEST_TGT_ECC 0x1002f1eccul
#define GUEST_TGT_ED8 0x1002f1ed8ull
#define GUEST_TGT_DCC 0x1002f2dccull
#define GUEST_TGT_SEL 0x1002a530cull
#define GUEST_TGT_IDX 0x1002a5310ull
#define GUEST_SKIP_AUTO 0x1002f2e69ull
#define GUEST_WIDE_UI 0x1002f2e5bull
#define GUEST_UNIT_BASE 0x1002d0608ull
#define GUEST_UNIT_STRIDE 0x3490u
#define GUEST_MARK_TARGET 0x100079e04ull
#define GUEST_ALLY_FLAG 0x1002ac488ull
#define BATTLE_SLOT_MAX 18
#define BATTLE_ENEMY_ID 8

#define PARTY_N 4
#define SKILL_REPO 0x1002ab608ull
#define ITEM_REPO 0x1002ab4d8ull
#define ITEM_NODE 0x130
#define OFF_NEXT 0x10
#define OFF_TEMP 0x20
#define OFF_COUNT 0x28
#define OFF_COUNT_NEW 0x30
#define OFF_INAME 0x38
#define OFF_IT 0x58
#define OFF_PLACE 0x72
#define OFF_SKTYPE 0x90
#define OFF_HELP 0x110
#define OFF_INFO 0x118
#define GUEST_ACTOR_BASE 0x1002ac4d8ull
#define GUEST_ACTOR_STRIDE 0x39b8u
#define GUEST_ACTOR_SKILLS 0x3928u
#define GUEST_ACTOR_BAG 0x3938u
#define GUEST_TIMEBAR_MAX 0x1002a52d4ull
#define ACTOR_OFF_SKDATA 0x20
#define ACTOR_OFF_FLAGS 0x304c
#define ACTOR_OFF_CANUSE 0x304d
#define ACTOR_OFF_MAGICBAR 0x33a6
#define ACTOR_OFF_CANCAST 0x345e
#define LUA_BIND 0x10030aef0ull
#define LUA_INT 0x1001c6f70ull
#define LUA_STR 0x1001c7168ull
#define LUA_DB 0x1001c0108ull
#define STR_ITEMTEMP 0x100272904ull
#define STR_NAME 0x10027290dull
#define STR_SKILLTYPE 0x100272ca0ull
#define STR_CANOBSOLT 0x10027328aull
#define STR_CANOBSOLT2 0x100274743ull
#define BATTLE_LIST_ROWS 8
#define ITEM_USE_BITS 0x5

enum battle_layer {
	BATTLE_OFF = 0,
	BATTLE_IDLE,
	BATTLE_COMMAND,
	BATTLE_MAGIC,
	BATTLE_ITEM,
	BATTLE_SPECIAL,
	BATTLE_TARGET
};

enum battle_cmd_id {
	CMD_ATTACK = 0,
	CMD_MAGIC,
	CMD_ITEM,
	CMD_SPECIAL,
	CMD_DEFENCE
};

struct battle_cmd {
	int live;
	int fallback;
	int x;
	int y;
	int w;
	int h;
};

struct battle_pack {
	int slot;
	int grey;
	char name[32];
};

struct battle_entry {
	int used;
	int temp;
	int count;
	int cat;
	int flags;
	int place;
	int sktype;
	char name[32];
	char help[48];
	char info[48];
};

static int g_layer;
static int g_cmd_focus;
static int g_cat;
static int g_list;
static int g_actor = -1;
static int g_cmd_n;
static int g_entry_n;
static int g_dir_down[4];
static int g_dir_axis[4];
static int g_a_up;
static int g_b_up;
static int g_ok_cmd;
static unsigned g_seen;
static int g_hide_n = 6;
static struct battle_cmd g_hide[BATTLE_HIDE_MAX] = {
	{ 1, 1, 178, 44, 114, 138 },
	{ 1, 1, 268, 44, 114, 138 },
	{ 1, 1, 358, 44, 114, 138 },
	{ 1, 1, 178, 134, 114, 138 },
	{ 1, 1, 268, 134, 114, 138 },
	{ 1, 1, 358, 134, 114, 138 },
};
static struct battle_entry g_entry[BATTLE_ENTRY_MAX];
static struct battle_pack g_pack[BATTLE_PACK_MAX];
static int g_pack_n;
static const char *g_slot_text[BATTLE_CMD_N] = {
	"攻击", "奇术", "物品", "绝招", "防御"
};
static const char *g_magic_cat[BATTLE_MAGIC_CAT] = {
	"攻击", "辅助", "恢复"
};
static const char *g_item_cat[BATTLE_ITEM_CAT] = {
	"恢复", "辅助", "法宝", "其他"
};
/* Native BattleEnv_MemberItemType pages: 0=0x10 1=0xc 2=0x1001 3=0x800. */
static const uint32_t g_item_mask[BATTLE_ITEM_CAT] = {
	0x00001001u, 0x0000000cu, 0x00000800u, 0x00000010u
};
static const int g_item_page[BATTLE_ITEM_CAT] = { 2, 1, 3, 0 };
static const SDL_Color g_ink_body = { 237, 221, 172, 255 };
static const SDL_Color g_ink_hint = { 168, 148, 96, 255 };
static const SDL_Color g_ink_gold = { 232, 196, 96, 255 };
static const SDL_Color g_ink_off = { 108, 96, 72, 255 };
static struct {
	SDL_Texture *tex;
	SDL_Renderer *renderer;
	char text[80];
	int pt;
	Uint32 rgba;
	int w;
	int h;
} g_text[BATTLE_TEXT_CACHE];
static int g_text_clock;

static int battle_native_ui_enabled(void)
{
	static int cached = -1;
	const char *value;

	if (cached >= 0)
		return cached;
	value = getenv("SWORD3_NATIVE_BATTLE");
	cached = !value || strcmp(value, "0") != 0;
	return cached;
}

static int battle_guest_ok(uintptr_t addr, size_t n)
{
	return addr >= BATTLE_GUEST_LO && addr + n - 1 < BATTLE_GUEST_HI;
}

static int battle_heap_ok(uintptr_t addr, size_t n)
{
	if (addr < 0x10000ull || addr > 0x00007fffffffffffull)
		return 0;
	if (n == 0)
		return 1;
	if (addr + n - 1 < addr)
		return 0;
	return addr + n - 1 <= 0x00007fffffffffffull;
}

static int battle_mem_ok(uintptr_t addr, size_t n)
{
	return battle_guest_ok(addr, n) || battle_heap_ok(addr, n);
}

static int battle_i32(uintptr_t addr, int fallback)
{
	if (!battle_mem_ok(addr, 4))
		return fallback;
	return *(volatile int *)(uintptr_t)addr;
}

static uintptr_t battle_ptr(uintptr_t addr)
{
	if (!battle_mem_ok(addr, sizeof(uintptr_t)))
		return 0;
	return *(volatile uintptr_t *)(uintptr_t)addr;
}

static unsigned battle_u16(uintptr_t addr)
{
	if (!battle_mem_ok(addr, 2))
		return 0;
	return (unsigned)*(volatile uint16_t *)(uintptr_t)addr;
}

static unsigned battle_u8(uintptr_t addr)
{
	if (!battle_mem_ok(addr, 1))
		return 0;
	return (unsigned)*(volatile uint8_t *)(uintptr_t)addr;
}

static void battle_set_i32(uintptr_t addr, int value)
{
	if (!battle_mem_ok(addr, 4))
		return;
	*(volatile int *)(uintptr_t)addr = value;
}

static void battle_set_u8(uintptr_t addr, int value)
{
	if (!battle_mem_ok(addr, 1))
		return;
	*(volatile uint8_t *)(uintptr_t)addr = (uint8_t)value;
}

static void battle_set_u16(uintptr_t addr, unsigned value)
{
	if (!battle_mem_ok(addr, 2))
		return;
	*(volatile uint16_t *)(uintptr_t)addr = (uint16_t)value;
}

static void battle_set_ptr(uintptr_t addr, uintptr_t value)
{
	if (!battle_mem_ok(addr, sizeof(uintptr_t)))
		return;
	*(volatile uintptr_t *)(uintptr_t)addr = value;
}

static int battle_copy_name(char *dst, size_t dstn, uintptr_t ptr)
{
	size_t i;
	const unsigned char *s;

	if (!dst || dstn == 0)
		return 0;
	dst[0] = 0;
	if (ptr < 0x1000ull || ptr > 0x00007fffffffffffull)
		return 0;
	s = (const unsigned char *)(uintptr_t)ptr;
	for (i = 0; i + 1 < dstn && s[i]; i++)
		dst[i] = (char)s[i];
	dst[i] = 0;
	return i > 0;
}

static int battle_in_fight(void)
{
	int flag;
	int now;

	flag = battle_i32(GUEST_FIGHT_FLAG, 0);
	if (flag & 2)
		return 1;
	now = battle_i32(GUEST_NOW_MENU, 0);
	return (now > 0 && now < 0x40) || now == 99 ||
	       (now >= 100 && now <= 102);
}

static int battle_now(void)
{
	return battle_i32(GUEST_NOW_MENU, 0);
}

static int battle_msg_up(void)
{
	return battle_ptr(GUEST_MSG_HEAD) != 0;
}


static int battle_player_slot(void)
{
	int id;

	id = battle_i32(GUEST_PLAYER_ID, 0);
	if (id >= 8 && id < 8 + PARTY_N)
		return id - 8;
	return -1;
}

static int battle_lua_ready(void)
{
	return battle_guest_ok(LUA_BIND, 8) && battle_ptr(LUA_BIND) != 0;
}

static int battle_lua_int(int temp, uintptr_t field)
{
	if (temp < 1 || !battle_lua_ready())
		return 0;
	return ((int (*)(void *, const void *, int, const void *))(
			uintptr_t)LUA_INT)(
		(void *)(uintptr_t)LUA_BIND,
		(const void *)(uintptr_t)STR_ITEMTEMP, temp,
		(const void *)(uintptr_t)field);
}

static int battle_lua_text(int temp, uintptr_t field, char *dst, size_t dstn)
{
	const char *raw;
	const char *shown;

	if (!dst || dstn == 0)
		return 0;
	dst[0] = 0;
	if (temp < 1 || !battle_lua_ready())
		return 0;
	raw = ((const char *(*)(void *, const void *, int, const void *))(
			uintptr_t)LUA_STR)(
		(void *)(uintptr_t)LUA_BIND,
		(const void *)(uintptr_t)STR_ITEMTEMP, temp,
		(const void *)(uintptr_t)field);
	if (!raw)
		return 0;
	shown = ((const char *(*)(const void *))(uintptr_t)LUA_DB)(raw);
	return battle_copy_name(dst, dstn, (uintptr_t)shown);
}

static void battle_sound(int id)
{
	((void (*)(int))(uintptr_t)GUEST_SOUND)(id);
}

static void battle_set_keyboard(void)
{
	uint8_t *pad;

	if (!battle_guest_ok(GUEST_UIGAMEPAD + GUEST_INPUT_MODE, 4))
		return;
	pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
	*(volatile int *)(pad + GUEST_INPUT_MODE) = GUEST_INPUT_KEYBOARD;
}

static uintptr_t battle_actor_ptr(void);

/*
 * Some converted role data has no flee-animation image. The native action
 * runner assumes ImageGet never returns NULL and crashes at 0x10000acd4.
 * Reuse the actor's current image in that case; only the visual changes,
 * while the native escape roll and round transition remain untouched.
 */
static int battle_flee_image_ready(uintptr_t actor)
{
	uintptr_t image;
	int flee_id;
	int current_id;
	unsigned flee_frame;
	unsigned current_frame;

	flee_id = battle_i32(actor + ACTOR_OFF_FLEE_IMAGE_ID, 0);
	flee_frame = battle_u16(actor + ACTOR_OFF_FLEE_IMAGE_FRAME);
	image = ((uintptr_t(*)(int, unsigned, int))(uintptr_t)GUEST_IMAGE_GET)(
		flee_id, flee_frame, 1);
	if (image)
		return 1;
	current_id = battle_i32(actor + ACTOR_OFF_CURRENT_IMAGE_ID, 0);
	current_frame = battle_u16(actor + ACTOR_OFF_CURRENT_IMAGE_FRAME);
	image = ((uintptr_t(*)(int, unsigned, int))(uintptr_t)GUEST_IMAGE_GET)(
		current_id, current_frame, 1);
	fprintf(stderr,
		"sword3-sdl: battle flee image missing id=%d/%u fallback=%d/%u ptr=%p current=%p\n",
		flee_id, flee_frame, current_id, current_frame, (void *)image,
		(void *)battle_ptr(actor + ACTOR_OFF_CURRENT_IMAGE));
	if (!image)
		return 0;
	battle_set_i32(actor + ACTOR_OFF_FLEE_IMAGE_ID, current_id);
	battle_set_u16(actor + ACTOR_OFF_FLEE_IMAGE_FRAME, current_frame);
	return 1;
}

/*
 * The native retreat widget sets this click latch and then runs the complete
 * battle input handler. That handler queues action 0x11; the normal battle
 * loop performs the escape roll and advances the round after a failed roll.
 */
int host_battle_flee(void)
{
	uintptr_t actor;

	actor = battle_actor_ptr();
	if (!actor || battle_player_slot() < 0)
		return 0;
	if (!battle_flee_image_ready(actor)) {
		fprintf(stderr,
			"sword3-sdl: battle flee aborted: no usable actor image\n");
		return 0;
	}
	battle_set_u8(GUEST_FLEE_FLAG, 1);
	fprintf(stderr, "sword3-sdl: battle host act cmd=逃跑 native\n");
	((void (*)(void))(uintptr_t)GUEST_BATTLE_INPUT_CLICK)();
	battle_set_u8(GUEST_FLEE_FLAG, 0);
	return 1;
}

static void battle_native_ok(void)
{
	battle_set_keyboard();
	battle_set_u8(GUEST_OK_GATE, 0);
	battle_set_u8(GUEST_OK_CLICK, 0);
	battle_set_i32(GUEST_DIR_A, 0);
	battle_set_u8(GUEST_OK_GATE, 1);
	battle_set_u8(GUEST_OK_CLICK, 1);
	battle_set_i32(GUEST_DIR_A, 1);
	((void (*)(void))(uintptr_t)GUEST_CMD_SELECT_OK)();
	battle_set_u8(GUEST_OK_CLICK, 0);
	battle_set_i32(GUEST_DIR_A, 0);
}

/* Magic/item/special confirm: NowMenu 2/4/8 aborts if DIR_A is set. */
static void battle_native_ok_entry(void)
{
	battle_set_keyboard();
	battle_set_i32(GUEST_DIR_A, 0);
	battle_set_i32(GUEST_DIR_AUX, 0);
	battle_set_u8(GUEST_OK_GATE, 1);
	battle_set_u8(GUEST_OK_CLICK, 1);
	((void (*)(void))(uintptr_t)GUEST_CMD_SELECT_OK)();
	battle_set_u8(GUEST_OK_CLICK, 0);
}

static uintptr_t battle_unit_ptr(int index)
{
	if (index < 0 || index >= BATTLE_ENEMY_ID)
		return 0;
	return GUEST_UNIT_BASE + (uintptr_t)index * GUEST_UNIT_STRIDE;
}

static void battle_mark_unit(int index, int on)
{
	uintptr_t unit;

	unit = battle_unit_ptr(index);
	if (!unit || !battle_mem_ok(unit + 0x345c, 1))
		return;
	((void (*)(void *, int))(uintptr_t)GUEST_MARK_TARGET)((void *)unit,
							     on ? 1 : 0);
}

static void battle_mark_actor(int slot, int on)
{
	uintptr_t actor;

	if (slot < 0 || slot >= PARTY_N)
		return;
	actor = GUEST_ACTOR_BASE + (uintptr_t)slot * GUEST_ACTOR_STRIDE;
	if (!battle_mem_ok(actor + 0x345c, 1))
		return;
	((void (*)(void *, int))(uintptr_t)GUEST_MARK_TARGET)((void *)actor,
							     on ? 1 : 0);
}

static void battle_set_ally_cursor(int on)
{
	int id;
	uintptr_t addr;

	id = battle_i32(GUEST_PLAYER_ID, 0);
	addr = GUEST_ALLY_FLAG + ((uintptr_t)id << 3) - 0x40;
	battle_set_u8(addr, on ? 1 : 0);
}

/*
 * 攻击 copies dcc slot IDs <= 7 into ed8. Special/magic confirm also sets
 * a per-player flag that makes Left/Right cycle the party instead of
 * enemies; clear that so the stick behaves like 攻击.
 */
static void battle_retarget_enemy(void)
{
	int i;
	int n;
	int id;
	int ecc;
	int old;
	int first;
	int dcc[BATTLE_SLOT_MAX];

	old = battle_i32(GUEST_TGT_F18, 0);
	for (i = 0; i < BATTLE_SLOT_MAX; i++)
		dcc[i] = -1;
	ecc = battle_i32(GUEST_TGT_ECC, 0);
	n = 0;
	first = -1;
	for (i = 0; i < BATTLE_SLOT_MAX; i++) {
		id = battle_i32(GUEST_TGT_DCC + (uintptr_t)i * 4, -1);
		dcc[i] = id;
		if (id < 0 || id >= BATTLE_ENEMY_ID)
			continue;
		if (first < 0)
			first = id;
		if (n < 8)
			battle_set_i32(GUEST_TGT_ED8 + (uintptr_t)n * 4, id);
		n++;
		if (ecc >= 1 && n >= ecc)
			break;
	}
	for (i = n; i < 8; i++)
		battle_set_i32(GUEST_TGT_ED8 + (uintptr_t)i * 4, 0);
	fprintf(stderr,
		"sword3-sdl: battle dcc=%d,%d,%d,%d,%d,%d ecc=%d ed0=%d old=%d n=%d first=%d ally=%d\n",
		dcc[0], dcc[1], dcc[2], dcc[3], dcc[4], dcc[5], ecc,
		battle_i32(0x1002f1ed0ull, 0), old, n, first,
		battle_i32(GUEST_ALLY_FLAG, -1) & 0xff);
	if (first < 0) {
		fprintf(stderr, "sword3-sdl: battle retarget none\n");
		return;
	}
	battle_set_ally_cursor(0);
	battle_set_u8(GUEST_WIDE_UI, 0);
	battle_set_u8(GUEST_SKIP_AUTO, 0);
	battle_set_u8(GUEST_OK_CLICK, 1);
	for (i = 0; i < PARTY_N; i++)
		battle_mark_actor(i, 0);
	if (old != first && old >= 0 && old < BATTLE_ENEMY_ID)
		battle_mark_unit(old, 0);
	for (i = 0; i < BATTLE_ENEMY_ID; i++) {
		if (i != first)
			battle_mark_unit(i, 0);
	}
	battle_set_i32(GUEST_TGT_IDX, 1);
	battle_set_i32(GUEST_TGT_F18, first);
	battle_set_i32(GUEST_TGT_SEL, first + 1);
	battle_mark_unit(first, 1);
	fprintf(stderr,
		"sword3-sdl: battle retarget enemy=%d n=%d ally=%d cmd=%d\n",
		first, n, battle_i32(GUEST_ALLY_FLAG, -1) & 0xff,
		battle_i32(GUEST_CMD_SEL, -1));
}

static void battle_native_cancel(void)
{
	battle_set_keyboard();
	((void (*)(void))(uintptr_t)GUEST_FIGHT_CANCEL)();
}

static int battle_read_obj_rect(uintptr_t obj, struct battle_cmd *out)
{
	uint8_t *btn;
	uintptr_t img;
	int x;
	int y;
	int w;
	int h;
	int iw;
	int ih;

	if (!battle_guest_ok(obj + GUEST_CMD_STRIDE - 1, 1))
		return 0;
	btn = (uint8_t *)obj;
	x = *(volatile int *)(btn + GUEST_CMD_X);
	if (x == -1)
		return 0;
	img = *(volatile uintptr_t *)(btn + GUEST_CMD_IMG);
	if (!img)
		return 0;
	y = *(volatile int *)(btn + GUEST_CMD_Y);
	w = *(volatile int *)(btn + GUEST_CMD_W);
	h = *(volatile int *)(btn + GUEST_CMD_H);
	if (battle_mem_ok(img + GUEST_IMG_H, 4)) {
		iw = *(volatile int *)((uint8_t *)img + GUEST_IMG_W);
		ih = *(volatile int *)((uint8_t *)img + GUEST_IMG_H);
		if (iw >= 8 && iw <= 400 && ih >= 8 && ih <= 400) {
			w = iw;
			h = ih;
		}
	}
	if (w < 8 || h < 8)
		return 0;
	if (w > 400)
		w = 96;
	if (h > 400)
		h = 96;
	out->live = 1;
	out->fallback = 0;
	out->x = x;
	out->y = y;
	out->w = w;
	out->h = h;
	return 1;
}

static int battle_is_native_cmd_obj(uintptr_t obj)
{
	uintptr_t lo;
	uintptr_t hi;

	lo = GUEST_CMD_BUTTONS;
	hi = GUEST_CMD_BUTTONS + (uintptr_t)BATTLE_CMD_N * GUEST_CMD_STRIDE;
	if (obj >= lo && obj < hi)
		return 1;
	lo = GUEST_CMD_EXTRA;
	hi = GUEST_CMD_EXTRA + (uintptr_t)GUEST_CMD_EXTRA_N * GUEST_CMD_STRIDE;
	if (obj >= lo && obj < hi)
		return 1;
	return obj == GUEST_CMD_TACTICS;
}

static void *battle_make_tramp(uintptr_t func)
{
	uint32_t *t;
	long page;

	page = sysconf(_SC_PAGESIZE);
	if (page < 4096)
		page = 4096;
	t = mmap(NULL, (size_t)page, PROT_READ | PROT_WRITE | PROT_EXEC,
		 MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (t == MAP_FAILED)
		return NULL;
	memcpy(t, (void *)func, 16);
	t[4] = 0x58000051u;
	t[5] = 0xd61f0220u;
	*(uint64_t *)(t + 6) = func + 16;
	__builtin___clear_cache((char *)t, (char *)t + 32);
	return t;
}

static int battle_patch_jump(uintptr_t func, void *hook, const uint32_t *expect)
{
	long page;
	uintptr_t aligned;
	uint32_t stub[4];

	if (memcmp((void *)func, expect, 16) != 0) {
		fprintf(stderr,
			"sword3-sdl: battle native draw bytes mismatch at 0x%llx\n",
			(unsigned long long)func);
		return -1;
	}
	page = sysconf(_SC_PAGESIZE);
	if (page < 4096)
		page = 4096;
	aligned = func & ~((uintptr_t)page - 1);
	if (mprotect((void *)aligned, (size_t)page * 2,
		     PROT_READ | PROT_WRITE | PROT_EXEC) != 0) {
		fprintf(stderr,
			"sword3-sdl: battle native draw mprotect failed at 0x%llx\n",
			(unsigned long long)func);
		return -1;
	}
	stub[0] = 0x58000050u;
	stub[1] = 0xd61f0200u;
	memcpy(stub + 2, &hook, 8);
	memcpy((void *)func, stub, 16);
	__builtin___clear_cache((char *)func, (char *)func + 16);
	return 0;
}

static int (*g_flee_step_orig)(void *actor, int phase);
static int (*g_cmd_draw_orig)(void *btn, int x, int y);
static void (*g_cmd_menu_orig)(void *a0, void *a1, void *a2, void *a3,
			       void *a4, void *a5, void *a6, void *a7);
static int g_flee_hooked;
static int g_draw_hooked;
static int g_overlay_hooked;
static int g_native_logged;

static int battle_flee_step_hook(void *actor, int phase)
{
	if (!actor || !battle_flee_image_ready((uintptr_t)actor))
		return 0;
	return g_flee_step_orig(actor, phase);
}

static int battle_cmd_draw_hook(void *btn, int x, int y)
{
	if ((uintptr_t)btn == GUEST_CMD_TACTICS) {
		char *label = *(char **)(uintptr_t)
			(GUEST_CMD_TACTICS + GUEST_BTN_NAME);

		if (label && strcmp(label, "逃跑") != 0)
			memcpy(label, "逃跑", sizeof("逃跑"));
	}
	if (host_battle_active() && battle_is_native_cmd_obj((uintptr_t)btn))
		return 0;
	return g_cmd_draw_orig(btn, x, y);
}

static void battle_cmd_menu_hook(void *a0, void *a1, void *a2, void *a3,
				 void *a4, void *a5, void *a6, void *a7)
{
	if (host_battle_active())
		return;
	g_cmd_menu_orig(a0, a1, a2, a3, a4, a5, a6, a7);
}

void host_battle_install(void)
{
	static const uint32_t draw_expect[4] = {
		0xa9bc5ff8u, 0xa90157f6u, 0xa9024ff4u, 0xa9037bfdu
	};
	static const uint32_t menu_expect[4] = {
		0xd101c3ffu, 0xa9016ffcu, 0xa90267fau, 0xa9035ff8u
	};
	static const uint32_t flee_step_expect[4] = {
		0xd10103ffu, 0xa90157f6u, 0xa9024ff4u, 0xa9037bfdu
	};

	if (!g_flee_hooked) {
		g_flee_step_orig = battle_make_tramp(GUEST_FLEE_STEP);
		if (!g_flee_step_orig ||
		    battle_patch_jump(GUEST_FLEE_STEP, battle_flee_step_hook,
				      flee_step_expect) != 0) {
			fprintf(stderr,
				"sword3-sdl: battle flee guard install failed\n");
			return;
		}
		g_flee_hooked = 1;
		fprintf(stderr,
			"sword3-sdl: battle flee image guard installed\n");
	}
	if (!g_draw_hooked) {
		g_cmd_draw_orig = battle_make_tramp(GUEST_CMD_DRAW);
		if (!g_cmd_draw_orig ||
		    battle_patch_jump(GUEST_CMD_DRAW, battle_cmd_draw_hook,
				      draw_expect) != 0) {
			fprintf(stderr,
				"sword3-sdl: battle command draw hook failed\n");
			return;
		}
		g_draw_hooked = 1;
	}
	if (battle_native_ui_enabled()) {
		if (!g_native_logged) {
			g_native_logged = 1;
			fprintf(stderr,
				"sword3-sdl: native battle UI enabled; host overlay disabled\n");
		}
		return;
	}
	if (g_overlay_hooked)
		return;
	g_cmd_menu_orig = battle_make_tramp(GUEST_CMD_MENU_DRAW);
	if (!g_cmd_menu_orig) {
		fprintf(stderr, "sword3-sdl: battle overlay tramp failed\n");
		return;
	}
	if (battle_patch_jump(GUEST_CMD_MENU_DRAW, battle_cmd_menu_hook,
			      menu_expect) != 0)
		return;
	g_overlay_hooked = 1;
	fprintf(stderr,
		"sword3-sdl: legacy host battle overlay enabled\n");
}

static int battle_rects_spread(const struct battle_cmd *r, int n)
{
	int i;
	int j;
	int dx;
	int dy;
	int far_n = 0;

	if (n < 2)
		return 0;
	for (i = 0; i < n; i++) {
		for (j = i + 1; j < n; j++) {
			dx = (r[i].x + r[i].w / 2) - (r[j].x + r[j].w / 2);
			dy = (r[i].y + r[i].h / 2) - (r[j].y + r[j].h / 2);
			if (dx < 0)
				dx = -dx;
			if (dy < 0)
				dy = -dy;
			if (dx + dy >= 80)
				far_n++;
		}
	}
	return far_n >= 3;
}

static uintptr_t battle_actor_ptr(void);
static int battle_special_ready(uintptr_t actor);
static int battle_frozen(void);
static int battle_slot_grey(int slot);
static int battle_flee_grey(void);
static void battle_snap_cmd_focus(void);
static void battle_log_special(void);
static void battle_capture_hide(void);

static uintptr_t battle_slot_obj(int slot)
{
	if (slot < 0)
		return 0;
	if (slot < BATTLE_CMD_N)
		return GUEST_CMD_BUTTONS + (uintptr_t)slot * GUEST_CMD_STRIDE;
	if (slot < SLOT_TACTICS)
		return GUEST_CMD_EXTRA +
		       (uintptr_t)(slot - BATTLE_CMD_N) * GUEST_CMD_STRIDE;
	if (slot == SLOT_TACTICS)
		return GUEST_CMD_TACTICS;
	if (slot == SLOT_FLEE)
		return GUEST_CMD_RETREAT;
	return 0;
}

static int battle_enable_at(int slot)
{
	if (slot < 0 || slot >= BATTLE_SLOT_N)
		return 0;
	if (!battle_guest_ok(GUEST_CMD_ENABLE + (uintptr_t)slot, 1))
		return 0;
	return *(volatile uint8_t *)(uintptr_t)(GUEST_CMD_ENABLE +
						 (uintptr_t)slot) == 1;
}

static int battle_read_caption(uintptr_t obj, char *dst, size_t dstn)
{
	uintptr_t name;

	if (!dst || dstn == 0)
		return 0;
	dst[0] = 0;
	if (!obj)
		return 0;
	name = battle_ptr(obj + GUEST_BTN_NAME);
	return battle_copy_name(dst, dstn, name);
}

static int battle_extra_usable(int slot)
{
	uintptr_t actor;
	unsigned type;

	if (slot < BATTLE_CMD_N || slot >= SLOT_TACTICS)
		return 0;
	type = battle_u16(GUEST_EXTRA_TYPE + (uintptr_t)slot * 2);
	if (type == 0)
		return 0;
	actor = battle_actor_ptr();
	if (!actor)
		return 0;
	return ((int (*)(void *, unsigned))(uintptr_t)GUEST_EXTRA_CAN)(
		(void *)actor, type) != 0;
}

static void battle_pack_add(int slot, int grey, const char *name)
{
	struct battle_pack *it;

	if (g_pack_n < 0 || g_pack_n >= BATTLE_PACK_MAX)
		return;
	it = &g_pack[g_pack_n];
	memset(it, 0, sizeof(*it));
	it->slot = slot;
	it->grey = grey;
	if (name && name[0])
		snprintf(it->name, sizeof(it->name), "%s", name);
	else if (slot >= 0 && slot < BATTLE_CMD_N)
		snprintf(it->name, sizeof(it->name), "%s", g_slot_text[slot]);
	else if (slot == SLOT_FLEE)
		snprintf(it->name, sizeof(it->name), "逃跑");
	else if (slot == SLOT_TACTICS)
		snprintf(it->name, sizeof(it->name), "战术");
	else
		snprintf(it->name, sizeof(it->name), "指令");
	g_pack_n++;
}

static void battle_refresh_commands(void)
{
	static char last[160];
	char line[160];
	char name[32];
	uintptr_t obj;
	int i;
	int n;
	int off;

	memset(g_pack, 0, sizeof(g_pack));
	g_pack_n = 0;
	((void (*)(int))(uintptr_t)GUEST_CMD_FILL)(
		battle_i32(GUEST_PLAYER_ID, 0));
	/*
	 * Native enable[3] is SKData type 0x38, not the 绝招 meter.
	 * Keep 攻击..防御 always, grey 绝招/奇术 as before, then extras.
	 * Native 战术 is hidden; that last cell is 逃跑.
	 */
	for (i = 0; i < BATTLE_CMD_N; i++)
		battle_pack_add(i, battle_slot_grey(i), g_slot_text[i]);
	if (!battle_frozen()) {
		for (i = BATTLE_CMD_N; i < SLOT_TACTICS; i++) {
			if (!battle_enable_at(i) || !battle_extra_usable(i))
				continue;
			obj = battle_slot_obj(i);
			name[0] = 0;
			battle_read_caption(obj, name, sizeof(name));
			battle_pack_add(i, 0, name);
		}
	}
	name[0] = 0;
	battle_read_caption(GUEST_CMD_RETREAT, name, sizeof(name));
	battle_pack_add(SLOT_FLEE, battle_flee_grey(), name);
	g_cmd_n = g_pack_n;
	if (g_cmd_focus < 0 || g_cmd_focus >= g_pack_n)
		g_cmd_focus = 0;
	battle_snap_cmd_focus();
	battle_capture_hide();
	n = 0;
	off = 0;
	off = snprintf(line, sizeof(line), "n=%d", g_pack_n);
	for (i = 0; i < g_pack_n && off < (int)sizeof(line) - 8; i++) {
		off += snprintf(line + off, sizeof(line) - (size_t)off, " %s%s",
				g_pack[i].name, g_pack[i].grey ? "*" : "");
		n++;
	}
	if (n && strcmp(last, line) != 0) {
		snprintf(last, sizeof(last), "%s", line);
		fprintf(stderr, "sword3-sdl: battle pack %s\n", line);
	}
}

static void battle_capture_hide(void)
{
	int i;
	int n = 0;
	struct battle_cmd cmd;
	struct battle_cmd got[BATTLE_HIDE_MAX];

	for (i = 0; i < BATTLE_CMD_N && n < BATTLE_HIDE_MAX; i++) {
		if (!battle_read_obj_rect(GUEST_CMD_BUTTONS +
					  (uintptr_t)i * GUEST_CMD_STRIDE,
					  &cmd))
			continue;
		got[n++] = cmd;
	}
	for (i = 0; i < GUEST_CMD_EXTRA_N && n < BATTLE_HIDE_MAX; i++) {
		if (!battle_read_obj_rect(GUEST_CMD_EXTRA +
					  (uintptr_t)i * GUEST_CMD_STRIDE,
					  &cmd))
			continue;
		got[n++] = cmd;
	}
	if (n < BATTLE_HIDE_MAX &&
	    battle_read_obj_rect(GUEST_CMD_TACTICS, &cmd))
		got[n++] = cmd;
	if (n < BATTLE_HIDE_MAX) {
		if (!battle_read_obj_rect(GUEST_CMD_RETREAT, &cmd) ||
		    cmd.w < 8 || cmd.h < 8) {
			cmd.w = 114;
			cmd.h = 90;
		}
		cmd.x = 320 - cmd.w / 2;
		cmd.y = 384 - cmd.h;
		got[n++] = cmd;
	}
	if (n < 3 || !battle_rects_spread(got, n))
		return;
	g_hide_n = n;
	for (i = 0; i < n; i++) {
		g_hide[i] = got[i];
		g_hide[i].x -= 12;
		g_hide[i].y -= 16;
		g_hide[i].w += 24;
		g_hide[i].h += 48;
	}
}

static int battle_load_node(struct battle_entry *it, uintptr_t node)
{
	uintptr_t help;
	uintptr_t info;
	int temp;

	memset(it, 0, sizeof(*it));
	if (!battle_heap_ok(node, ITEM_NODE))
		return 0;
	temp = battle_i32(node + OFF_TEMP, 0);
	if (temp < 1)
		return 0;
	it->temp = temp;
	it->count = battle_i32(node + OFF_COUNT, 0);
	it->count += battle_i32(node + OFF_COUNT_NEW, 0);
	it->flags = battle_i32(node + OFF_IT, 0);
	it->place = (int)battle_u16(node + OFF_PLACE);
	it->sktype = (int)battle_u16(node + OFF_SKTYPE);
	if (it->count < 0)
		it->count = 0;
	battle_copy_name(it->name, sizeof(it->name), node + OFF_INAME);
	help = battle_ptr(node + OFF_HELP);
	if (help)
		battle_copy_name(it->help, sizeof(it->help), help);
	info = battle_ptr(node + OFF_INFO);
	if (info)
		battle_copy_name(it->info, sizeof(it->info), info);
	if (!it->name[0])
		battle_lua_text(temp, STR_NAME, it->name, sizeof(it->name));
	it->used = it->name[0] || temp > 0;
	return it->used;
}

static int battle_walk_list(struct battle_entry *dst, int max, uintptr_t head)
{
	int n = 0;
	int guard = 0;
	int i;
	uintptr_t node;
	uintptr_t seen[BATTLE_ENTRY_MAX];
	int seen_n = 0;

	node = head;
	while (node && n < max && guard < max + 8) {
		guard++;
		for (i = 0; i < seen_n; i++) {
			if (seen[i] == node)
				return n;
		}
		if (seen_n < BATTLE_ENTRY_MAX)
			seen[seen_n++] = node;
		if (battle_load_node(&dst[n], node))
			n++;
		node = battle_ptr(node + OFF_NEXT);
	}
	return n;
}

static int battle_skill_cat(const struct battle_entry *it)
{
	uint32_t flags;
	int skill_type;

	if (!it)
		return 0;
	flags = (uint32_t)it->flags;
	if (flags & 0x10)
		return 0;
	if (flags & 0xc)
		return 1;
	if (flags & 0x1001)
		return 2;
	skill_type = battle_lua_int(it->temp, STR_SKILLTYPE);
	if (skill_type == 0x10 || skill_type == 0)
		return 0;
	if (skill_type == 0xc || skill_type == 1)
		return 1;
	if (skill_type == 0x1001 || skill_type == 2)
		return 2;
	if (skill_type >= 0 && skill_type <= 2)
		return skill_type;
	return 0;
}

static int battle_item_cat(const struct battle_entry *it)
{
	uint32_t flags;
	int i;

	if (!it)
		return 3;
	flags = (uint32_t)it->flags;
	for (i = 0; i < BATTLE_ITEM_CAT; i++) {
		if (flags & g_item_mask[i])
			return i;
	}
	return 3;
}

static int battle_item_ok(const struct battle_entry *it)
{
	if (!it || !it->used || it->count <= 0)
		return 0;
	if ((it->place & ITEM_USE_BITS) == 0)
		return 0;
	return 1;
}

static int battle_is_obsolt(const struct battle_entry *it)
{
	int v;

	if (!it || it->temp < 1)
		return 0;
	if (it->sktype == 0x1f)
		return 1;
	v = battle_lua_int(it->temp, STR_CANOBSOLT);
	if (v)
		return 1;
	v = battle_lua_int(it->temp, STR_CANOBSOLT2);
	return v != 0;
}

static uintptr_t battle_actor_skills(void);
static uintptr_t battle_skill_repo_head(void);

static void battle_refresh_skills(int obsolt)
{
	struct battle_entry raw[BATTLE_ENTRY_MAX];
	uintptr_t head;
	int n;
	int i;
	int type;
	int keep;

	g_entry_n = 0;
	memset(g_entry, 0, sizeof(g_entry));
	head = battle_actor_skills();
	if (!head)
		head = battle_skill_repo_head();
	n = battle_walk_list(raw, BATTLE_ENTRY_MAX, head);
	for (i = 0; i < n && g_entry_n < BATTLE_ENTRY_MAX; i++) {
		if ((raw[i].place & ITEM_USE_BITS) == 0)
			continue;
		type = battle_lua_int(raw[i].temp, STR_SKILLTYPE);
		raw[i].cat = battle_skill_cat(&raw[i]);
		if (obsolt)
			keep = battle_is_obsolt(&raw[i]);
		else
			keep = !battle_is_obsolt(&raw[i]);
		if (!keep)
			continue;
		if (g_seen < 8)
			fprintf(stderr,
				"sword3-sdl: battle skill temp=%d type=%d sktype=0x%x flags=0x%x place=0x%x cat=%d name=%s\n",
				raw[i].temp, type, raw[i].sktype, raw[i].flags,
				raw[i].place, raw[i].cat, raw[i].name);
		g_entry[g_entry_n++] = raw[i];
	}
}

static uintptr_t battle_actor_ptr(void)
{
	int slot;

	slot = battle_player_slot();
	if (slot < 0)
		return 0;
	return GUEST_ACTOR_BASE + (uintptr_t)slot * GUEST_ACTOR_STRIDE;
}

static int battle_special_ready(uintptr_t actor)
{
	unsigned magic;
	unsigned max;
	unsigned can;

	if (!actor)
		return 0;
	can = battle_u8(actor + ACTOR_OFF_CANCAST);
	if (can)
		return 1;
	max = (unsigned)battle_i32(GUEST_TIMEBAR_MAX, 0);
	if (max > 0xffffu)
		max = 0xffffu;
	magic = battle_u16(actor + ACTOR_OFF_MAGICBAR);
	return max > 0 && magic >= max;
}

static int battle_frozen(void)
{
	uintptr_t actor;

	actor = battle_actor_ptr();
	if (!actor)
		return 0;
	return (battle_u8(actor + ACTOR_OFF_FLAGS) & 2) != 0;
}

static int battle_slot_grey(int slot)
{
	uintptr_t actor;

	if (slot < 0 || slot >= BATTLE_CMD_N)
		return 0;
	actor = battle_actor_ptr();
	if (!actor)
		return slot == CMD_SPECIAL;
	if (battle_frozen())
		return slot != CMD_MAGIC && slot != CMD_DEFENCE;
	if (slot == CMD_MAGIC)
		return (battle_u8(actor + ACTOR_OFF_CANUSE) & 1) != 0;
	if (slot == CMD_SPECIAL)
		return !battle_special_ready(actor);
	return 0;
}

static int battle_flee_grey(void)
{
	uintptr_t actor;

	if (battle_frozen() || battle_u8(GUEST_CANT_RETREAT))
		return 1;
	actor = battle_actor_ptr();
	if (!actor)
		return 1;
	if (battle_u8(actor + ACTOR_OFF_CANUSE) & 8)
		return 1;
	return ((int (*)(void *))(uintptr_t)GUEST_FLEE_BLOCK)((void *)actor) != 0;
}

static int battle_cmd_enabled(int pack)
{
	if (pack < 0 || pack >= g_pack_n)
		return 0;
	return !g_pack[pack].grey;
}

static void battle_snap_cmd_focus(void)
{
	int i;

	if (g_pack_n <= 0) {
		g_cmd_focus = 0;
		return;
	}
	if (g_cmd_focus < 0 || g_cmd_focus >= g_pack_n)
		g_cmd_focus = 0;
	if (battle_cmd_enabled(g_cmd_focus))
		return;
	for (i = 0; i < g_pack_n; i++) {
		if (battle_cmd_enabled(i)) {
			g_cmd_focus = i;
			return;
		}
	}
}

static void battle_log_special(void)
{
	static int last_ready = -1;
	static int last_slot = -99;
	uintptr_t actor;
	uintptr_t sk;
	uintptr_t chardata;
	int ready;
	int slot;

	slot = battle_player_slot();
	ready = battle_special_ready(battle_actor_ptr()) ? 1 : 0;
	if (ready == last_ready && slot == last_slot)
		return;
	last_ready = ready;
	last_slot = slot;
	actor = battle_actor_ptr();
	sk = actor ? battle_ptr(actor + ACTOR_OFF_SKDATA) : 0;
	chardata = actor ? battle_ptr(actor + 0x18) : 0;
	fprintf(stderr,
		"sword3-sdl: battle special ready=%d magic=%u max=%d cancast=%u sp=%d/%d sk=0x%llx type=0x%x freeze=%u\n",
		ready,
		actor ? battle_u16(actor + ACTOR_OFF_MAGICBAR) : 0,
		battle_i32(GUEST_TIMEBAR_MAX, 0),
		actor ? battle_u8(actor + ACTOR_OFF_CANCAST) : 0,
		chardata ? battle_i32(chardata + 0xc, 0) : -1,
		chardata ? battle_i32(chardata + 0x18, 0) : -1,
		(unsigned long long)sk,
		(sk && battle_mem_ok(sk, 4)) ? battle_i32(sk, 0) : -1,
		actor ? (battle_u8(actor + ACTOR_OFF_FLAGS) & 2) : 0);
}

static uintptr_t battle_list_head(uintptr_t rec)
{
	return rec ? battle_ptr(rec + OFF_NEXT) : 0;
}

static uintptr_t battle_skill_repo_head(void)
{
	int slot;

	slot = battle_player_slot();
	if (slot < 0)
		return 0;
	if (!battle_guest_ok(SKILL_REPO + (uintptr_t)slot * 8, 8))
		return 0;
	return battle_list_head(battle_ptr(SKILL_REPO + (uintptr_t)slot * 8));
}

static uintptr_t battle_actor_skills(void)
{
	uintptr_t actor;

	actor = battle_actor_ptr();
	if (!actor || !battle_guest_ok(actor + GUEST_ACTOR_SKILLS, 8))
		return 0;
	return battle_list_head(battle_ptr(actor + GUEST_ACTOR_SKILLS));
}

static uintptr_t battle_actor_bag(void)
{
	uintptr_t actor;

	actor = battle_actor_ptr();
	if (!actor || !battle_guest_ok(actor + GUEST_ACTOR_BAG, 8))
		return 0;
	return battle_list_head(battle_ptr(actor + GUEST_ACTOR_BAG));
}

static void battle_prepare_actor_lists(void)
{
	uintptr_t actor;
	int slot;
	uintptr_t rec;

	actor = battle_actor_ptr();
	slot = battle_player_slot();
	if (!actor || slot < 0)
		return;
	if (!battle_actor_skills()) {
		rec = battle_ptr(SKILL_REPO + (uintptr_t)slot * 8);
		if (rec)
			battle_set_ptr(actor + GUEST_ACTOR_SKILLS, rec);
	}
	if (!battle_actor_bag() && battle_ptr(ITEM_REPO + OFF_NEXT))
		battle_set_ptr(actor + GUEST_ACTOR_BAG, ITEM_REPO);
}

static uint32_t battle_skill_mask(int cat)
{
	if (cat == 1)
		return 0xcu;
	if (cat == 2)
		return 0x1001u;
	return 0x10u;
}

static uint32_t battle_item_page_mask(int page)
{
	if (page == 1)
		return 0xcu;
	if (page == 2)
		return 0x1001u;
	if (page == 3)
		return 0x800u;
	return 0x10u;
}

static int battle_native_skill_index(int temp, int cat, int obsolt)
{
	struct battle_entry raw[BATTLE_ENTRY_MAX];
	uintptr_t head;
	uint32_t mask;
	int n;
	int i;
	int idx;

	head = battle_actor_skills();
	if (!head)
		head = battle_skill_repo_head();
	n = battle_walk_list(raw, BATTLE_ENTRY_MAX, head);
	mask = battle_skill_mask(cat);
	idx = 0;
	for (i = 0; i < n; i++) {
		if ((raw[i].place & ITEM_USE_BITS) == 0)
			continue;
		if (((uint32_t)raw[i].flags & mask) == 0)
			continue;
		if (obsolt) {
			if (raw[i].sktype != 0x1f)
				continue;
		} else if (raw[i].sktype == 0x1f) {
			continue;
		}
		idx++;
		if (raw[i].temp == temp)
			return idx;
	}
	return 0;
}

static int battle_native_item_index(int temp, int page)
{
	struct battle_entry raw[BATTLE_ENTRY_MAX];
	uintptr_t head;
	uint32_t mask;
	int n;
	int i;
	int idx;

	head = battle_actor_bag();
	if (!head && battle_guest_ok(ITEM_REPO + OFF_NEXT, 8))
		head = battle_ptr(ITEM_REPO + OFF_NEXT);
	n = battle_walk_list(raw, BATTLE_ENTRY_MAX, head);
	mask = battle_item_page_mask(page);
	idx = 0;
	for (i = 0; i < n; i++) {
		if ((raw[i].place & ITEM_USE_BITS) == 0)
			continue;
		if (((uint32_t)raw[i].flags & mask) == 0)
			continue;
		if (raw[i].count <= 0)
			continue;
		idx++;
		if (raw[i].temp == temp)
			return idx;
	}
	return 0;
}

static void battle_refresh_items(void)
{
	struct battle_entry raw[BATTLE_ENTRY_MAX];
	uintptr_t head;
	int n;
	int i;
	int keep;

	g_entry_n = 0;
	memset(g_entry, 0, sizeof(g_entry));
	head = battle_actor_bag();
	n = battle_walk_list(raw, BATTLE_ENTRY_MAX, head);
	if (n <= 0 && battle_guest_ok(ITEM_REPO + OFF_NEXT, 8)) {
		head = battle_ptr(ITEM_REPO + OFF_NEXT);
		n = battle_walk_list(raw, BATTLE_ENTRY_MAX, head);
	}
	for (i = 0; i < n && g_entry_n < BATTLE_ENTRY_MAX; i++) {
		keep = battle_item_ok(&raw[i]);
		raw[i].cat = battle_item_cat(&raw[i]);
		if (g_seen < 12)
			fprintf(stderr,
				"sword3-sdl: battle item temp=%d count=%d place=0x%x flags=0x%x cat=%d keep=%d name=%s\n",
				raw[i].temp, raw[i].count, raw[i].place,
				raw[i].flags, raw[i].cat, keep,
				raw[i].name[0] ? raw[i].name : "-");
		if (!keep)
			continue;
		g_entry[g_entry_n++] = raw[i];
	}
}

static int battle_cat_count(void)
{
	if (g_layer == BATTLE_MAGIC)
		return BATTLE_MAGIC_CAT;
	if (g_layer == BATTLE_ITEM)
		return BATTLE_ITEM_CAT;
	return 1;
}

static int battle_view(int *out, int max)
{
	int i;
	int n = 0;
	int want;

	if (!out || max <= 0)
		return 0;
	want = (g_layer == BATTLE_SPECIAL) ? -1 : g_cat;
	for (i = 0; i < g_entry_n && n < max; i++) {
		if (want >= 0 && g_entry[i].cat != want)
			continue;
		out[n++] = i;
	}
	return n;
}

static void battle_clamp_list(void)
{
	int view[BATTLE_ENTRY_MAX];
	int n;
	int cats;

	cats = battle_cat_count();
	if (g_cat < 0 || g_cat >= cats)
		g_cat = 0;
	n = battle_view(view, BATTLE_ENTRY_MAX);
	if (n <= 0)
		g_list = 0;
	else if (g_list >= n)
		g_list = n - 1;
	else if (g_list < 0)
		g_list = 0;
}

static struct battle_entry *battle_current(void)
{
	int view[BATTLE_ENTRY_MAX];
	int n;

	battle_clamp_list();
	n = battle_view(view, BATTLE_ENTRY_MAX);
	if (n <= 0 || g_list < 0 || g_list >= n)
		return NULL;
	return &g_entry[view[g_list]];
}

static const char *battle_layer_name(int layer)
{
	switch (layer) {
	case BATTLE_COMMAND:
		return "command";
	case BATTLE_MAGIC:
		return "magic";
	case BATTLE_ITEM:
		return "item";
	case BATTLE_SPECIAL:
		return "special";
	case BATTLE_TARGET:
		return "target";
	case BATTLE_IDLE:
		return "idle";
	default:
		return "off";
	}
}

static void battle_enter(int layer)
{
	int old;

	old = g_layer;
	g_layer = layer;
	if (layer == BATTLE_MAGIC) {
		if (old != BATTLE_MAGIC) {
			g_cat = 0;
			g_list = 0;
			battle_prepare_actor_lists();
			battle_refresh_skills(0);
		}
	} else if (layer == BATTLE_ITEM) {
		if (old != BATTLE_ITEM) {
			g_cat = 0;
			g_list = 0;
			battle_prepare_actor_lists();
			battle_refresh_items();
		}
	} else if (layer == BATTLE_SPECIAL) {
		if (old != BATTLE_SPECIAL) {
			g_cat = 0;
			g_list = 0;
			battle_prepare_actor_lists();
			battle_refresh_skills(1);
		}
	} else if (layer == BATTLE_COMMAND) {
		battle_refresh_commands();
		battle_log_special();
	}
	battle_clamp_list();
	if (old != layer) {
		g_a_up = 0;
		g_b_up = 0;
		if (layer != BATTLE_COMMAND && layer != BATTLE_TARGET)
			g_ok_cmd = 0;
		memset(g_dir_down, 0, sizeof(g_dir_down));
		memset(g_dir_axis, 0, sizeof(g_dir_axis));
		if (g_seen < 64) {
			g_seen++;
			fprintf(stderr,
				"sword3-sdl: battle host layer=%s now=%d actor=%d cmds=%d entries=%d\n",
				battle_layer_name(layer), battle_now(),
				battle_player_slot(), g_cmd_n, g_entry_n);
		}
	}
}

void host_battle_close(void)
{
	if (g_layer == BATTLE_OFF)
		return;
	g_layer = BATTLE_OFF;
	g_cmd_n = 0;
	g_pack_n = 0;
	g_entry_n = 0;
	g_actor = -1;
	memset(g_dir_down, 0, sizeof(g_dir_down));
	memset(g_dir_axis, 0, sizeof(g_dir_axis));
	g_a_up = 0;
	g_b_up = 0;
	g_ok_cmd = 0;
	fprintf(stderr, "sword3-sdl: battle host close\n");
}

int host_battle_active(void)
{
	if (battle_native_ui_enabled())
		return 0;
	return g_layer == BATTLE_COMMAND || g_layer == BATTLE_MAGIC ||
	       g_layer == BATTLE_ITEM || g_layer == BATTLE_SPECIAL;
}

int host_battle_owns_pad(void)
{
	return host_battle_active();
}

void host_battle_poll(void)
{
	int now;
	int slot;
	int host_sub;

	host_battle_install();
	if (battle_native_ui_enabled()) {
		host_battle_close();
		return;
	}
	if (!battle_in_fight()) {
		host_battle_close();
		return;
	}
	battle_capture_hide();
	now = battle_now();
	if (g_ok_cmd && now == 1) {
		battle_set_i32(GUEST_CMD_SEL, g_ok_cmd);
		battle_native_ok();
		g_ok_cmd = 0;
		now = battle_now();
	}
	slot = battle_player_slot();
	if (slot != g_actor) {
		g_actor = slot;
		if (g_layer == BATTLE_MAGIC)
			battle_refresh_skills(0);
		else if (g_layer == BATTLE_ITEM)
			battle_refresh_items();
		else if (g_layer == BATTLE_SPECIAL)
			battle_refresh_skills(1);
	}
	if (now == GUEST_NOW_TARGET) {
		battle_enter(BATTLE_TARGET);
		return;
	}
	host_sub = (g_layer == BATTLE_MAGIC || g_layer == BATTLE_ITEM ||
		    g_layer == BATTLE_SPECIAL);
	if (host_sub) {
		if (now == 0 || now == 99 || (now >= 100 && now <= 102))
			battle_enter(BATTLE_IDLE);
		return;
	}
	if (now == 1) {
		battle_set_keyboard();
		battle_enter(BATTLE_COMMAND);
		return;
	}
	if (g_layer == BATTLE_COMMAND)
		battle_enter(BATTLE_IDLE);
	else if (g_layer == BATTLE_OFF || g_layer == BATTLE_TARGET)
		g_layer = BATTLE_IDLE;
}

static int battle_pack_step(int from, int dir)
{
	int col;
	int row;
	int next;

	if (g_pack_n <= 1)
		return from;
	if (from < 0 || from >= g_pack_n)
		from = 0;
	col = from % BATTLE_COLS;
	row = from / BATTLE_COLS;
	switch (dir) {
	case 0:
		next = from - BATTLE_COLS;
		if (next < 0) {
			next = col;
			while (next + BATTLE_COLS < g_pack_n)
				next += BATTLE_COLS;
		}
		break;
	case 2:
		next = from + BATTLE_COLS;
		if (next >= g_pack_n)
			next = col;
		break;
	case 1:
		next = from + 1;
		if (next >= g_pack_n || next / BATTLE_COLS != row)
			next = row * BATTLE_COLS;
		break;
	case 3:
		next = from - 1;
		if (next < row * BATTLE_COLS) {
			next = row * BATTLE_COLS + BATTLE_COLS - 1;
			if (next >= g_pack_n)
				next = g_pack_n - 1;
		}
		break;
	default:
		return from;
	}
	return next;
}

static int battle_focus_cmd(int dir)
{
	int start;
	int next;
	int hops;

	if (dir < 0 || dir >= 4 || g_pack_n <= 0)
		return 0;
	if (g_cmd_focus < 0 || g_cmd_focus >= g_pack_n)
		g_cmd_focus = 0;
	start = g_cmd_focus;
	next = start;
	for (hops = 0; hops < g_pack_n; hops++) {
		next = battle_pack_step(next, dir);
		if (next == start)
			break;
		if (battle_cmd_enabled(next)) {
			g_cmd_focus = next;
			battle_sound(0x2e);
			return 1;
		}
	}
	return 0;
}

static void battle_move_cat(int delta)
{
	int cats;
	int start;

	cats = battle_cat_count();
	if (cats <= 1)
		return;
	start = g_cat;
	g_cat += delta;
	if (g_cat < 0)
		g_cat = cats - 1;
	if (g_cat >= cats)
		g_cat = 0;
	g_list = 0;
	battle_clamp_list();
	if (g_cat != start)
		battle_sound(0x2e);
}

static void battle_move_list(int delta)
{
	int view[BATTLE_ENTRY_MAX];
	int n;
	int next;

	n = battle_view(view, BATTLE_ENTRY_MAX);
	if (n <= 0)
		return;
	next = g_list + delta;
	if (next < 0)
		next = n - 1;
	if (next >= n)
		next = 0;
	if (next != g_list) {
		g_list = next;
		battle_sound(0x2e);
	}
}

static void battle_dir(int index)
{
	if (g_layer == BATTLE_COMMAND) {
		battle_focus_cmd(index);
		return;
	}
	if (g_layer == BATTLE_SPECIAL) {
		if (index == 2)
			battle_move_list(1);
		else if (index == 0)
			battle_move_list(-1);
		return;
	}
	if (g_layer == BATTLE_MAGIC || g_layer == BATTLE_ITEM) {
		if (index == 1)
			battle_move_cat(1);
		else if (index == 3)
			battle_move_cat(-1);
		else if (index == 2)
			battle_move_list(1);
		else if (index == 0)
			battle_move_list(-1);
	}
}

static void battle_set_dir(int index, int axis, int down)
{
	int old_wanted;
	int wanted;

	if (index < 0 || index >= 4)
		return;
	old_wanted = g_dir_axis[index] || g_dir_down[index];
	if (axis)
		g_dir_axis[index] = down;
	else
		g_dir_down[index] = down;
	wanted = g_dir_axis[index] || g_dir_down[index];
	if (wanted && !old_wanted)
		battle_dir(index);
}

static void battle_confirm_command(void)
{
	int wanted;
	int slot;

	if (g_cmd_focus < 0 || g_cmd_focus >= g_pack_n)
		return;
	if (!battle_cmd_enabled(g_cmd_focus))
		return;
	slot = g_pack[g_cmd_focus].slot;
	if (slot == CMD_MAGIC) {
		battle_enter(BATTLE_MAGIC);
		return;
	}
	if (slot == CMD_ITEM) {
		battle_enter(BATTLE_ITEM);
		return;
	}
	if (slot == CMD_SPECIAL) {
		battle_enter(BATTLE_SPECIAL);
		return;
	}
	if (slot == SLOT_FLEE) {
		(void)host_battle_flee();
		return;
	}
	wanted = slot + 1;
	fprintf(stderr, "sword3-sdl: battle host act cmd=%s sel=%d\n",
		g_pack[g_cmd_focus].name, wanted);
	if (battle_i32(GUEST_CMD_SEL, -1) == wanted) {
		battle_set_i32(GUEST_CMD_SEL, 0);
		g_ok_cmd = wanted;
		return;
	}
	battle_set_i32(GUEST_CMD_SEL, wanted);
	battle_native_ok();
}

static void battle_confirm_entry(void)
{
	struct battle_entry *it;
	int now_menu;
	int cmd_sel;
	int idx;
	int page;
	int now;
	int retarget;

	it = battle_current();
	if (!it)
		return;
	battle_prepare_actor_lists();
	retarget = (it->flags & 0x10) != 0;
	if (g_layer == BATTLE_MAGIC && it->sktype != 0x1f) {
		now_menu = GUEST_NOW_MAGIC;
		cmd_sel = 2;
		idx = battle_native_skill_index(it->temp, g_cat, 0);
		if (idx <= 0)
			idx = g_list + 1;
		battle_set_i32(GUEST_SPELL_ID, idx);
		battle_set_i32(GUEST_SPELL_CAT, g_cat);
		battle_set_i32(GUEST_SPELL_IDX, idx);
		fprintf(stderr,
			"sword3-sdl: battle host magic temp=%d idx=%d cat=%d flags=0x%x name=%s\n",
			it->temp, idx, g_cat, it->flags, it->name);
	} else if (g_layer == BATTLE_ITEM) {
		now_menu = GUEST_NOW_ITEM;
		cmd_sel = 3;
		page = g_item_page[g_cat];
		idx = battle_native_item_index(it->temp, page);
		if (idx <= 0)
			idx = g_list + 1;
		battle_set_i32(GUEST_ITEM_PAGE, page);
		battle_set_i32(GUEST_ITEM_SEL, idx);
		battle_set_i32(GUEST_ITEM_ALT, idx);
		battle_set_i32(GUEST_ITEM_ARG, idx);
		fprintf(stderr,
			"sword3-sdl: battle host item temp=%d idx=%d page=%d flags=0x%x name=%s\n",
			it->temp, idx, page, it->flags, it->name);
	} else if (g_layer == BATTLE_SPECIAL ||
		   (g_layer == BATTLE_MAGIC && it->sktype == 0x1f)) {
		now_menu = GUEST_NOW_SPECIAL;
		cmd_sel = 4;
		idx = battle_native_skill_index(it->temp, 0, 1);
		if (idx <= 0)
			idx = g_list + 1;
		battle_set_i32(GUEST_SPELL_ID, idx);
		battle_set_i32(GUEST_SPECIAL_IDX, idx);
		fprintf(stderr,
			"sword3-sdl: battle host special temp=%d idx=%d flags=0x%x name=%s\n",
			it->temp, idx, it->flags, it->name);
	} else {
		return;
	}
	battle_set_i32(GUEST_NOW_MENU, now_menu);
	fprintf(stderr,
		"sword3-sdl: battle ok-entry now=%d player=%d dira=%d aux=%d gate=%d click=%d\n",
		now_menu, battle_i32(GUEST_PLAYER_ID, 0),
		battle_i32(GUEST_DIR_A, -1), battle_i32(GUEST_DIR_AUX, -1),
		battle_i32(GUEST_OK_GATE, -1) & 0xff,
		battle_i32(GUEST_OK_CLICK, -1) & 0xff);
	if (retarget)
		battle_set_u8(GUEST_SKIP_AUTO, 1);
	battle_native_ok_entry();
	now = battle_now();
	fprintf(stderr, "sword3-sdl: battle confirm now=%d msg=%d cmd=%d\n", now,
		battle_msg_up(), cmd_sel);
	if (now == GUEST_NOW_TARGET) {
		battle_set_i32(GUEST_CMD_SEL, cmd_sel);
		if (retarget)
			battle_retarget_enemy();
	} else if (now == now_menu)
		battle_set_i32(GUEST_NOW_MENU, 1);
}

static void battle_confirm(void)
{
	if (g_layer == BATTLE_COMMAND)
		battle_confirm_command();
	else if (g_layer == BATTLE_MAGIC || g_layer == BATTLE_ITEM ||
		 g_layer == BATTLE_SPECIAL)
		battle_confirm_entry();
}

static void battle_back(void)
{
	int now;

	if (g_layer != BATTLE_MAGIC && g_layer != BATTLE_ITEM &&
	    g_layer != BATTLE_SPECIAL)
		return;
	now = battle_now();
	if (now == GUEST_NOW_MAGIC || now == GUEST_NOW_ITEM ||
	    now == GUEST_NOW_SPECIAL) {
		fprintf(stderr, "sword3-sdl: battle host B -> native cancel\n");
		battle_native_cancel();
	} else {
		fprintf(stderr, "sword3-sdl: battle host B -> command\n");
	}
	battle_enter(BATTLE_COMMAND);
}

int host_battle_button(int button, int down)
{
	int index;

	if (button == SDL_CONTROLLER_BUTTON_A && !down) {
		g_a_up = 0;
		return host_battle_owns_pad();
	}
	if (button == SDL_CONTROLLER_BUTTON_B && !down) {
		g_b_up = 0;
		return host_battle_owns_pad();
	}
	if (!host_battle_owns_pad())
		return 0;
	if (battle_msg_up())
		return 1;
	switch (button) {
	case SDL_CONTROLLER_BUTTON_A:
		if (g_a_up)
			return 1;
		g_a_up = 1;
		battle_confirm();
		return 1;
	case SDL_CONTROLLER_BUTTON_B:
		if (g_b_up)
			return 1;
		g_b_up = 1;
		battle_back();
		return 1;
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
		if (down)
			battle_move_cat(-1);
		return 1;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
		if (down)
			battle_move_cat(1);
		return 1;
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
		return 1;
	}
	battle_set_dir(index, 0, down);
	return 1;
}

int host_battle_axis(Uint8 axis, Sint16 value)
{
	int old_dir;
	int new_dir;

	if (!host_battle_owns_pad())
		return 0;
	if (battle_msg_up())
		return 1;
	if (axis == SDL_CONTROLLER_AXIS_LEFTX) {
		old_dir = g_dir_axis[3] ? 3 : (g_dir_axis[1] ? 1 : -1);
		new_dir = value < -BATTLE_DEADZONE ? 3 :
			  (value > BATTLE_DEADZONE ? 1 : -1);
	} else if (axis == SDL_CONTROLLER_AXIS_LEFTY) {
		old_dir = g_dir_axis[0] ? 0 : (g_dir_axis[2] ? 2 : -1);
		new_dir = value < -BATTLE_DEADZONE ? 0 :
			  (value > BATTLE_DEADZONE ? 2 : -1);
	} else {
		return 1;
	}
	if (old_dir == new_dir)
		return 1;
	if (old_dir >= 0)
		battle_set_dir(old_dir, 1, 0);
	if (new_dir >= 0)
		battle_set_dir(new_dir, 1, 1);
	return 1;
}

static int battle_sx(int x, int logical_w)
{
	return x * logical_w / BATTLE_GAME_W;
}

static int battle_sy(int y, int logical_h)
{
	return y * logical_h / BATTLE_GAME_H;
}

static TTF_Font *battle_font(int pt)
{
	return host_cjk_font(pt);
}

static Uint32 battle_rgba(SDL_Color c)
{
	return ((Uint32)c.r << 24) | ((Uint32)c.g << 16) |
	       ((Uint32)c.b << 8) | (Uint32)c.a;
}

static int battle_text_size(SDL_Renderer *renderer, const char *s, int pt,
			    SDL_Color color, int *w, int *h,
			    SDL_Texture **tex_out)
{
	TTF_Font *font;
	SDL_Surface *surf;
	SDL_Texture *tex;
	Uint32 rgba;
	int i;
	int slot;

	if (w)
		*w = 0;
	if (h)
		*h = 0;
	if (tex_out)
		*tex_out = NULL;
	if (!renderer || !s || !s[0])
		return 0;
	rgba = battle_rgba(color);
	for (i = 0; i < BATTLE_TEXT_CACHE; i++) {
		if (g_text[i].tex && g_text[i].renderer == renderer &&
		    g_text[i].pt == pt && g_text[i].rgba == rgba &&
		    strcmp(g_text[i].text, s) == 0) {
			if (w)
				*w = g_text[i].w;
			if (h)
				*h = g_text[i].h;
			if (tex_out)
				*tex_out = g_text[i].tex;
			return 1;
		}
	}
	font = battle_font(pt);
	if (!font)
		return 0;
	surf = TTF_RenderUTF8_Blended(font, s, color);
	if (!surf) {
		static unsigned seen;

		if (seen < 4) {
			seen++;
			fprintf(stderr,
				"sword3-sdl: battle TTF_RenderUTF8 '%s' failed: %s\n",
				s, TTF_GetError());
		}
		return 0;
	}
	tex = SDL_CreateTextureFromSurface(renderer, surf);
	if (tex)
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	if (w)
		*w = surf->w;
	if (h)
		*h = surf->h;
	slot = g_text_clock % BATTLE_TEXT_CACHE;
	g_text_clock++;
	if (g_text[slot].tex)
		SDL_DestroyTexture(g_text[slot].tex);
	snprintf(g_text[slot].text, sizeof(g_text[slot].text), "%s", s);
	g_text[slot].tex = tex;
	g_text[slot].renderer = renderer;
	g_text[slot].pt = pt;
	g_text[slot].rgba = rgba;
	g_text[slot].w = surf->w;
	g_text[slot].h = surf->h;
	SDL_FreeSurface(surf);
	if (tex_out)
		*tex_out = tex;
	return 1;
}

static void battle_text_center(SDL_Renderer *renderer, const char *s, int cx,
			       int cy, int pt, SDL_Color color)
{
	SDL_Texture *tex;
	SDL_Rect dst;
	int w;
	int h;

	if (!battle_text_size(renderer, s, pt, color, &w, &h, &tex) || !tex)
		return;
	dst.x = cx - w / 2;
	dst.y = cy - h / 2;
	dst.w = w;
	dst.h = h;
	SDL_RenderCopy(renderer, tex, NULL, &dst);
}

static void battle_text_left(SDL_Renderer *renderer, const char *s, int x,
			     int y, int pt, SDL_Color color)
{
	SDL_Texture *tex;
	SDL_Rect dst;
	int w;
	int h;

	if (!battle_text_size(renderer, s, pt, color, &w, &h, &tex) || !tex)
		return;
	dst.x = x;
	dst.y = y;
	dst.w = w;
	dst.h = h;
	SDL_RenderCopy(renderer, tex, NULL, &dst);
}

static void battle_fill(SDL_Renderer *renderer, SDL_Rect rect, Uint8 r, Uint8 g,
			Uint8 b, Uint8 a)
{
	SDL_SetRenderDrawColor(renderer, r, g, b, a);
	SDL_RenderFillRect(renderer, &rect);
}

static void battle_frame(SDL_Renderer *renderer, SDL_Rect rect, int thick,
			 Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	SDL_Rect arm[4];

	arm[0].x = rect.x;
	arm[0].y = rect.y;
	arm[0].w = rect.w;
	arm[0].h = thick;
	arm[1].x = rect.x;
	arm[1].y = rect.y + rect.h - thick;
	arm[1].w = rect.w;
	arm[1].h = thick;
	arm[2].x = rect.x;
	arm[2].y = rect.y;
	arm[2].w = thick;
	arm[2].h = rect.h;
	arm[3].x = rect.x + rect.w - thick;
	arm[3].y = rect.y;
	arm[3].w = thick;
	arm[3].h = rect.h;
	SDL_SetRenderDrawColor(renderer, r, g, b, a);
	SDL_RenderFillRect(renderer, &arm[0]);
	SDL_RenderFillRect(renderer, &arm[1]);
	SDL_RenderFillRect(renderer, &arm[2]);
	SDL_RenderFillRect(renderer, &arm[3]);
}

static void battle_cmd_cell(int index, int *x, int *y, int *w, int *h)
{
	int col;
	int row;

	if (index < 0)
		index = 0;
	col = index % BATTLE_COLS;
	row = index / BATTLE_COLS;
	*x = BATTLE_FB_X0 + col * BATTLE_FB_W;
	*y = BATTLE_FB_Y0 + row * BATTLE_FB_H;
	*w = BATTLE_FB_W;
	*h = BATTLE_FB_H;
}

static void battle_cluster(int *x, int *y, int *w, int *h)
{
	int i;
	int cx;
	int cy;
	int cw;
	int ch;
	int x0;
	int y0;
	int x1;
	int y1;
	int n;

	n = g_pack_n > 0 ? g_pack_n : BATTLE_CMD_N;
	battle_cmd_cell(0, &x0, &y0, &cw, &ch);
	x1 = x0 + cw;
	y1 = y0 + ch;
	for (i = 1; i < n; i++) {
		battle_cmd_cell(i, &cx, &cy, &cw, &ch);
		if (cx < x0)
			x0 = cx;
		if (cy < y0)
			y0 = cy;
		if (cx + cw > x1)
			x1 = cx + cw;
		if (cy + ch > y1)
			y1 = cy + ch;
	}
	*x = x0;
	*y = y0;
	*w = x1 - x0;
	*h = y1 - y0;
}

static void battle_draw_command(SDL_Renderer *renderer, int logical_w,
				int logical_h)
{
	SDL_Rect btn;
	int i;
	int gx;
	int gy;
	int gw;
	int gh;
	int pt;
	int on;
	int ok;
	int inset;

	for (i = 0; i < g_pack_n; i++) {
		battle_cmd_cell(i, &gx, &gy, &gw, &gh);
		inset = gw / 12;
		if (inset < 4)
			inset = 4;
		btn.x = battle_sx(gx + inset, logical_w);
		btn.y = battle_sy(gy + inset, logical_h);
		btn.w = battle_sx(gw - inset * 2, logical_w);
		btn.h = battle_sy(gh - inset * 2, logical_h);
		on = (g_cmd_focus == i);
		ok = battle_cmd_enabled(i);
		if (!ok) {
			battle_fill(renderer, btn, 10, 8, 6, 255);
			battle_frame(renderer, btn, 2, 72, 60, 40, 255);
		} else {
			battle_fill(renderer, btn, 18, 14, 10, 255);
			battle_frame(renderer, btn, on ? 3 : 2,
				     on ? g_ink_gold.r : 160,
				     on ? g_ink_gold.g : 128,
				     on ? g_ink_gold.b : 64, 255);
		}
		pt = gh / 4;
		if (pt < 16)
			pt = 16;
		if (pt > 24)
			pt = 24;
		battle_text_center(renderer, g_pack[i].name,
				   btn.x + btn.w / 2, btn.y + btn.h / 2, pt,
				   !ok ? g_ink_off :
				   (on ? g_ink_gold : g_ink_body));
	}
}

static void battle_draw_list(SDL_Renderer *renderer, int logical_w,
			     int logical_h, const char *title,
			     const char **cats, int cat_n)
{
	SDL_Rect panel;
	SDL_Rect tab;
	SDL_Rect row;
	SDL_Rect help;
	int view[BATTLE_ENTRY_MAX];
	int n;
	int i;
	int pt;
	int pt_small;
	int tw;
	int rh;
	int on;
	int visible;
	int start;
	int gx;
	int gy;
	int gw;
	int gh;
	char line[80];
	const struct battle_entry *it;
	const char *empty;

	battle_cluster(&gx, &gy, &gw, &gh);
	panel.x = battle_sx(gx, logical_w);
	panel.y = battle_sy(gy, logical_h);
	panel.w = battle_sx(gw, logical_w);
	panel.h = battle_sy(gh, logical_h);
	pt = 18;
	pt_small = 16;
	battle_fill(renderer, panel, 8, 12, 10, 255);
	battle_frame(renderer, panel, 2, 160, 128, 56, 255);
	battle_text_center(renderer, title, panel.x + panel.w / 2,
			   panel.y + battle_sy(22, logical_h), pt, g_ink_gold);
	if (cats && cat_n > 1) {
		tw = (panel.w - battle_sx(24, logical_w)) / cat_n;
		for (i = 0; i < cat_n; i++) {
			tab.x = panel.x + battle_sx(12, logical_w) + i * tw;
			tab.y = panel.y + battle_sy(42, logical_h);
			tab.w = tw - battle_sx(6, logical_w);
			tab.h = battle_sy(28, logical_h);
			on = (g_cat == i);
			if (on)
				battle_frame(renderer, tab, 2, g_ink_gold.r,
					     g_ink_gold.g, g_ink_gold.b, 255);
			battle_text_center(renderer, cats[i],
					   tab.x + tab.w / 2,
					   tab.y + tab.h / 2, pt_small,
					   on ? g_ink_gold : g_ink_hint);
		}
	}
	n = battle_view(view, BATTLE_ENTRY_MAX);
	row.x = panel.x + battle_sx(16, logical_w);
	row.y = panel.y + battle_sy(cats && cat_n > 1 ? 78 : 48, logical_h);
	row.w = panel.w - battle_sx(32, logical_w);
	row.h = panel.h - (row.y - panel.y) - battle_sy(40, logical_h);
	if (n <= 0) {
		empty = (g_layer == BATTLE_ITEM) ? "没有可用物品" :
			(g_layer == BATTLE_SPECIAL) ? "没有绝招" :
						      "没有奇术";
		battle_text_center(renderer, empty, row.x + row.w / 2,
				   row.y + row.h / 3, pt, g_ink_hint);
	} else {
		visible = n > BATTLE_LIST_ROWS ? BATTLE_LIST_ROWS : n;
		start = 0;
		if (n > visible) {
			start = g_list - visible / 2;
			if (start < 0)
				start = 0;
			if (start + visible > n)
				start = n - visible;
		}
		rh = row.h / visible;
		if (rh > battle_sy(26, logical_h))
			rh = battle_sy(26, logical_h);
		if (rh < battle_sy(18, logical_h))
			rh = battle_sy(18, logical_h);
		for (i = 0; i < visible; i++) {
			it = &g_entry[view[start + i]];
			on = (g_list == start + i);
			tab.x = row.x;
			tab.y = row.y + i * rh;
			tab.w = row.w;
			tab.h = rh - 2;
			if (on)
				battle_frame(renderer, tab, 2, g_ink_gold.r,
					     g_ink_gold.g, g_ink_gold.b, 255);
			if (it->count > 1)
				snprintf(line, sizeof(line), "%s  x%d",
					 it->name[0] ? it->name : "—",
					 it->count);
			else
				snprintf(line, sizeof(line), "%s",
					 it->name[0] ? it->name : "—");
			battle_text_left(renderer, line,
					 tab.x + battle_sx(10, logical_w),
					 tab.y + tab.h / 2 - pt_small / 2,
					 pt_small,
					 on ? g_ink_gold : g_ink_body);
		}
	}
	it = battle_current();
	help.x = panel.x + battle_sx(16, logical_w);
	help.y = panel.y + panel.h - battle_sy(56, logical_h);
	help.w = panel.w - battle_sx(32, logical_w);
	help.h = battle_sy(44, logical_h);
	if (it && it->help[0])
		snprintf(line, sizeof(line), "%s", it->help);
	else if (it && it->info[0])
		snprintf(line, sizeof(line), "%s", it->info);
	else if (cats && cat_n > 1)
		snprintf(line, sizeof(line), "%s", "左右/LR 分类   A 确认   B 返回");
	else
		snprintf(line, sizeof(line), "%s", "A 确认   B 返回");
	battle_text_left(renderer, line, help.x,
			 help.y + battle_sy(8, logical_h), pt_small,
			 g_ink_hint);
}

void host_battle_draw(SDL_Renderer *renderer, int logical_w, int logical_h)
{
	if (battle_native_ui_enabled())
		return;
	SDL_BlendMode blend;

	if (!renderer || logical_w <= 0 || logical_h <= 0)
		return;
	if (!host_battle_active())
		return;
	SDL_GetRenderDrawBlendMode(renderer, &blend);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	if (g_layer == BATTLE_COMMAND)
		battle_draw_command(renderer, logical_w, logical_h);
	else if (g_layer == BATTLE_MAGIC)
		battle_draw_list(renderer, logical_w, logical_h, "奇术",
				 g_magic_cat, BATTLE_MAGIC_CAT);
	else if (g_layer == BATTLE_ITEM)
		battle_draw_list(renderer, logical_w, logical_h, "物品",
				 g_item_cat, BATTLE_ITEM_CAT);
	else if (g_layer == BATTLE_SPECIAL)
		battle_draw_list(renderer, logical_w, logical_h, "绝招", NULL,
				 0);
	SDL_SetRenderDrawBlendMode(renderer, blend);
}

int host_battle_skip_blit(int x, int y, int w, int h)
{
	if (battle_native_ui_enabled())
		return 0;
	int i;
	int cx;
	int cy;
	int x0;
	int y0;
	int w0;
	int h0;
	const struct battle_cmd *cmd;

	if (!host_battle_active())
		return 0;
	if (battle_msg_up())
		return 0;
	if (w <= 0 || h <= 0)
		return 0;
	if (w >= 220 && h >= 220)
		return 0;
	cx = x + w / 2;
	cy = y + h / 2;
	if (cy < 56)
		return 0;
	if (g_layer == BATTLE_MAGIC || g_layer == BATTLE_ITEM ||
	    g_layer == BATTLE_SPECIAL) {
		battle_cluster(&x0, &y0, &w0, &h0);
		if (cx >= x0 && cx < x0 + w0 && cy >= y0 && cy < y0 + h0)
			return 1;
	}
	for (i = 0; i < g_hide_n; i++) {
		cmd = &g_hide[i];
		if (cx < cmd->x || cx >= cmd->x + cmd->w)
			continue;
		if (cy < cmd->y || cy >= cmd->y + cmd->h)
			continue;
		return 1;
	}
	return 0;
}
