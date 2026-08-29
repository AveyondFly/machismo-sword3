#include "host_menu.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#define HOST_MENU_DEADZONE 14000
#define HOST_MENU_TABS 5
#define HOST_MENU_ACTIONS 5
#define HOST_MENU_SLOTS 10
#define HOST_MENU_SLOT_COLS 2
#define HOST_MENU_FONT "/usr/share/fonts/TTF/DejaVuSansMono.ttf"
#define HOST_MENU_FONT_SLOTS 8
#define HOST_MENU_TEXT_CACHE 160
#define HOST_MENU_NATIVE_W 960
#define HOST_MENU_NATIVE_H 720
#define HOST_MENU_GUEST_LO 0x100294000ull
#define HOST_MENU_GUEST_HI 0x100380000ull
#define HOST_MENU_PARTY 0x1002ab1b8ull
#define HOST_MENU_PARTY_STRIDE 0x68
#define HOST_MENU_PARTY_N 4
#define HOST_MENU_FILL_CHAR 0x10001e4c0ull
#define HOST_MENU_LEVEL_MAX 0x100005f40ull
#define HOST_MENU_LUA_BIND 0x10030aef0ull
#define HOST_MENU_OFF_HP 0x04
#define HOST_MENU_OFF_MP 0x08
#define HOST_MENU_OFF_SP 0x0c
#define HOST_MENU_OFF_HPMAX 0x10
#define HOST_MENU_OFF_MPMAX 0x14
#define HOST_MENU_OFF_SPMAX 0x18
#define HOST_MENU_OFF_STR 0x1c
#define HOST_MENU_OFF_STA 0x1e
#define HOST_MENU_OFF_WIS 0x20
#define HOST_MENU_OFF_AGI 0x26
#define HOST_MENU_OFF_EXP 0x00
#define HOST_MENU_OFF_LVUP 0x2c
#define HOST_MENU_OFF_ATK 0x32
#define HOST_MENU_OFF_DEF 0x34
#define HOST_MENU_OFF_LEVEL 0x38
#define HOST_MENU_OFF_ATTR 0x3a
#define HOST_MENU_OFF_NAME 0x58
#define HOST_MENU_OFF_ACT 0x64
#define HOST_MENU_RESIST_N 9
#define HOST_MENU_TSW_ROOT 0x100319e58ull
#define HOST_MENU_TSW_GET 0x100204a64ull
#define HOST_MENU_TSW_TSWP 0x1001a7144ull
#define HOST_MENU_TSW_HGA3 0x1001a730cull
#define HOST_MENU_TSW_FREE 0x10023fb50ull
#define HOST_MENU_TSW_KEY 0x100319dd8ull
#define HOST_MENU_TSW_MAGIC 0x100319dc0ull
#define HOST_MENU_ITEM_REPO 0x1002ab4d8ull
#define HOST_MENU_EQUIP_REPO 0x1002ab628ull
#define HOST_MENU_SKILL_REPO 0x1002ab608ull
#define HOST_MENU_SKEXP 0x1002ab358ull
#define HOST_MENU_SKEXP_STRIDE 0x60
#define HOST_MENU_ITEM_NODE 0x130
#define HOST_MENU_BAG_N 48
#define HOST_MENU_SKILL_N 32
#define HOST_MENU_EQUIP_N 8
#define HOST_MENU_OFF_NEXT 0x10
#define HOST_MENU_OFF_TEMP 0x20
#define HOST_MENU_OFF_COUNT 0x28
#define HOST_MENU_OFF_COUNT_NEW 0x30
#define HOST_MENU_OFF_INAME 0x38
#define HOST_MENU_OFF_IT 0x58
#define HOST_MENU_OFF_PLACE 0x72
#define HOST_MENU_OFF_HELP 0x110
#define HOST_MENU_OFF_INFO 0x118
#define HOST_MENU_ITEM_APPLY 0x100081130ull
#define HOST_MENU_ITEM_DTOR 0x1000817b0ull
#define HOST_MENU_ITEM_FREE 0x10023f7ccull
#define HOST_MENU_ITEM_SOUND 0x1001c076cull
#define HOST_MENU_LUA_HAS 0x1001c7088ull
#define HOST_MENU_LUA_INT 0x1001c6f70ull
#define HOST_MENU_LUA_NUM 0x1001c7040ull
#define HOST_MENU_LUA_STR 0x1001c7168ull
#define HOST_MENU_LUA_DB 0x1001c0108ull
#define HOST_MENU_FN_BOOK 0x100274071ull
#define HOST_MENU_BOOK_BUILD 0x100056318ull
#define HOST_MENU_BOOK_N 0x1002f3634ull
#define HOST_MENU_BOOK_IDS 0x1002f3620ull
#define HOST_MENU_STR_ITEMTEMP 0x100272904ull
#define HOST_MENU_STR_NAME 0x10027290dull
#define HOST_MENU_STR_LEVEL 0x100272a23ull
#define HOST_MENU_STR_HP 0x100272a43ull
#define HOST_MENU_STR_ATK 0x100272a29ull
#define HOST_MENU_STR_DEF 0x100272a2dull
#define HOST_MENU_STR_SPD 0x100272a31ull
#define HOST_MENU_STR_HELP 0x10027297dull
#define HOST_MENU_BOOK_SRC 0x1002a85d8ull
#define HOST_MENU_BOOK_SRC_N 0x1002a85d0ull
#define HOST_MENU_BESTIARY_N 128
#define HOST_MENU_ITEM_USE_BITS 0xe
#define HOST_MENU_ITEM_NO_THROW 5
#define HOST_MENU_ITEM_NO_CONSUME 7
#define HOST_MENU_WIDGET_HAS 0x1001b7014ull
#define HOST_MENU_WIDGET_BITS 0x10030f500ull
#define HOST_MENU_W_PARTY0 0x1e
#define HOST_MENU_EQUIP_SWAP 0x100080f64ull
#define HOST_MENU_STR_ADDATK 0x100272b10ull
#define HOST_MENU_STR_ADDDEF 0x100272b17ull
#define HOST_MENU_STR_ADDSPD 0x100272b1eull
#define HOST_MENU_STR_ADDHP 0x100272b70ull
#define HOST_MENU_STR_ADDMP 0x100272b76ull
#define HOST_MENU_STR_ADDSP 0x100272b7cull
#define HOST_MENU_STR_CHAR 0x100273e0aull
#define HOST_MENU_STR_ACT 0x100272a1aull

enum host_menu_tab {
	HOST_MENU_TAB_ITEM = 0,
	HOST_MENU_TAB_EQUIP,
	HOST_MENU_TAB_SKILL,
	HOST_MENU_TAB_STATUS,
	HOST_MENU_TAB_BOOK
};

enum host_menu_layer {
	HOST_MENU_LAYER_TABS = 0,
	HOST_MENU_LAYER_INNER,
	HOST_MENU_LAYER_SLOTS,
	HOST_MENU_LAYER_STUB,
	HOST_MENU_LAYER_BESTIARY,
	HOST_MENU_LAYER_PICK,
	HOST_MENU_LAYER_ASK,
	HOST_MENU_LAYER_EQUIP
};

static int g_open;
static int g_tab;
static int g_layer;
static int g_book_focus;
static int g_item_focus;
static int g_item_cat;
static int g_item_sel;
static int g_equip_focus;
static int g_equip_sel;
static int g_stub;
static int g_slot_mode;
static int g_slot_focus;
static int g_pending = -1;
static int g_pending_slot;
static int g_dir_down[4];
static int g_dir_axis[4];
static int g_dir_held[4];
static int g_ttf_ready;
static struct {
	int pt;
	TTF_Font *font;
} g_fonts[HOST_MENU_FONT_SLOTS];
static struct {
	SDL_Texture *tex;
	SDL_Renderer *renderer;
	char text[80];
	int pt;
	Uint32 rgba;
	int w;
	int h;
} g_text[HOST_MENU_TEXT_CACHE];
static int g_text_clock;
static SDL_Renderer *g_text_renderer;
static SDL_Renderer *g_asset_renderer;
static SDL_Texture *g_tex_paper;
static SDL_Texture *g_tex_dark;
static SDL_Texture *g_tex_back;
static SDL_Texture *g_tex_book[HOST_MENU_ACTIONS];
static SDL_Texture *g_tex_face[HOST_MENU_PARTY_N];
static int g_face_act[HOST_MENU_PARTY_N];

struct host_menu_actor {
	int used;
	int level;
	int level_max;
	int hp;
	int hp_max;
	int mp;
	int mp_max;
	int sp;
	int sp_max;
	int str;
	int sta;
	int wis;
	int agi;
	int atk;
	int def;
	int exp;
	int exp_need;
	int sk_exp;
	int sk_next;
	int attr[HOST_MENU_RESIST_N];
	int act;
	char name[32];
};

struct host_menu_item {
	int used;
	int count;
	int count_new;
	int temp;
	int flags;
	int place;
	uintptr_t node;
	char name[32];
	char help[48];
	char info[48];
};

static struct host_menu_actor g_party[HOST_MENU_PARTY_N];
static struct host_menu_item g_bag[HOST_MENU_BAG_N];
static struct host_menu_item g_equip[HOST_MENU_EQUIP_N];
static struct host_menu_item g_skills[HOST_MENU_SKILL_N];
static int g_party_i;
static int g_party_logged;
static int g_pick_logged;
static int g_bag_n;
static int g_skill_n;
static int g_skill_focus;
static int g_inv_logged;
static char g_item_note[64];
static uintptr_t g_act_node;
static int g_act_flags;
static int g_act_from_new;
static char g_act_name[32];
static int g_bestiary_n;
static int g_bestiary_sel;
static int g_bestiary_id[HOST_MENU_BESTIARY_N];
static char g_bestiary_name[HOST_MENU_BESTIARY_N][32];
static int g_bestiary_lv;
static int g_bestiary_hp;
static int g_bestiary_atk;
static int g_bestiary_def;
static int g_bestiary_spd;
static char g_bestiary_help[80];

static const char *g_book_files[HOST_MENU_ACTIONS] = {
	"book_save.png",
	"book_load.png",
	"book_log.png",
	"book_opt.png",
	"book_leave.png",
};

/* Native 960x720 天书 buttons, measured from grim. */
static const int g_book_nx[HOST_MENU_ACTIONS] = {
	302, 417, 531, 645, 759
};
#define HOST_MENU_BOOK_NY 76
#define HOST_MENU_BOOK_NW 104
#define HOST_MENU_BOOK_NH 142
#define HOST_MENU_SPLIT_X 282
#define HOST_MENU_ITEM_ACT 3
#define HOST_MENU_ITEM_CAT 7
#define HOST_MENU_STAT_N 7
#define HOST_MENU_ITEM_ACT_NY 78
#define HOST_MENU_ITEM_ACT_NW 122
#define HOST_MENU_ITEM_ACT_NH 64
#define HOST_MENU_ITEM_CAT_NY 154
#define HOST_MENU_ITEM_CAT_NW 52
#define HOST_MENU_ITEM_CAT_NH 72
#define HOST_MENU_ITEM_WELL_NY 240

static void host_menu_enter_inner(void);
static void host_menu_refresh_party(void);
static void host_menu_refresh_inv(void);
static void host_menu_fill_party(void);
static void host_menu_item_act(void);
static void host_menu_bestiary_open(void);
static void host_menu_equip_open(void);
static void host_menu_equip_commit(void);

static const char *g_tab_text[HOST_MENU_TABS] = {
	"物品", "装备", "奇术", "状态", "天书"
};

static const char *g_book_text[HOST_MENU_ACTIONS] = {
	"存盘", "读取", "记载", "设置", "离开"
};

static const char *g_item_act[] = { "使用", "整理", "丢弃" };
static const char *g_item_cat_text[] = {
	"新品", "恢复", "辅助", "法宝", "装备", "活物", "其他"
};
static const char *g_equip_slot[] = {
	"武器", "头部", "身体", "手部", "足部", "饰品", "法宝", "护驾"
};
static const char *g_stat_row[] = {
	"等级", "经验", "力量", "耐力", "智慧", "敏捷", "绝招经验"
};
static const char *g_resist[] = {
	"火", "冰", "风", "土", "毒", "光", "暗", "电", "物"
};
static const char *g_combat[] = { "攻击", "防御", "敏捷" };
static const int g_item_act_nx[HOST_MENU_ITEM_ACT] = { 320, 464, 606 };
static const int g_item_cat_nx[HOST_MENU_ITEM_CAT] = {
	320, 384, 447, 510, 573, 636, 695
};
/* Original PropResort masks at 0x1002a50c0. Tab 0 is Count_New. */
static const uint32_t g_item_cat_mask[HOST_MENU_ITEM_CAT] = {
	0x00008000u, 0x00001003u, 0x0000001cu, 0x08000040u,
	0x00000700u, 0x00000800u, 0x00000020u
};
/* Original slot masks at 0x1002a50dc, indexed by the 8 equip slots. */
static const uint32_t g_equip_mask[HOST_MENU_EQUIP_N] = {
	0x00000100u, 0x10000200u, 0x40000200u, 0x20000200u,
	0x80000200u, 0x00000400u, 0x00000400u, 0x00000040u
};
static const uintptr_t g_equip_stat_field[] = {
	HOST_MENU_STR_ADDATK, HOST_MENU_STR_ADDDEF, HOST_MENU_STR_ADDSPD,
	HOST_MENU_STR_ADDHP, HOST_MENU_STR_ADDMP, HOST_MENU_STR_ADDSP
};
static const char *g_equip_stat_name[] = {
	"攻击", "防御", "敏捷", "生命", "真气", "精力"
};
#define HOST_MENU_EQUIP_STAT_N 6

static const char *g_stub_text[HOST_MENU_ACTIONS] = {
	"存盘（尚未接入）",
	"读取（尚未接入）",
	"记载（尚未接入）",
	"设置（尚未接入）",
	"存盘（尚未接入）",
};

static const SDL_Color g_ink_title = { 237, 221, 172, 255 };
static const SDL_Color g_ink_body = { 237, 221, 172, 255 };
static const SDL_Color g_ink_hint = { 168, 148, 96, 255 };
static const SDL_Color g_ink_gold = { 232, 196, 96, 255 };
/* Original item-list unusable ink at 0x100032740. */
static const SDL_Color g_ink_dim = { 111, 104, 79, 255 };
static const SDL_Color g_ink_paper = { 72, 48, 24, 255 };
static const SDL_Color g_ink_tab_on = { 72, 42, 16, 255 };
static const SDL_Color g_ink_up = { 120, 196, 96, 255 };
static const SDL_Color g_ink_hp = { 196, 72, 48, 255 };
static const SDL_Color g_ink_mp = { 48, 168, 72, 255 };
static const SDL_Color g_ink_sp = { 64, 128, 208, 255 };

int host_menu_active(void)
{
	return g_open;
}

void host_menu_close(void)
{
	if (!g_open)
		return;
	g_open = 0;
	g_layer = HOST_MENU_LAYER_TABS;
	g_stub = -1;
	g_slot_mode = 0;
	g_slot_focus = 0;
	g_bestiary_n = 0;
	g_bestiary_sel = 0;
	g_act_node = 0;
	g_act_flags = 0;
	g_act_from_new = 0;
	g_act_name[0] = 0;
	memset(g_dir_down, 0, sizeof(g_dir_down));
	memset(g_dir_axis, 0, sizeof(g_dir_axis));
	memset(g_dir_held, 0, sizeof(g_dir_held));
	fprintf(stderr, "sword3-sdl: host menu close\n");
}

void host_menu_open(void)
{
	g_open = 1;
	g_tab = HOST_MENU_TAB_ITEM;
	g_layer = HOST_MENU_LAYER_TABS;
	g_book_focus = 0;
	g_item_focus = 0;
	g_item_cat = 0;
	g_item_sel = 0;
	g_item_note[0] = 0;
	g_act_node = 0;
	g_act_flags = 0;
	g_act_from_new = 0;
	g_act_name[0] = 0;
	g_equip_focus = 0;
	g_equip_sel = 0;
	g_stub = -1;
	g_slot_mode = 0;
	g_slot_focus = 0;
	g_party_i = 0;
	g_party_logged = 0;
	g_pick_logged = 0;
	g_inv_logged = 0;
	g_skill_focus = 0;
	g_bag_n = 0;
	g_skill_n = 0;
	g_bestiary_n = 0;
	g_bestiary_sel = 0;
	memset(g_party, 0, sizeof(g_party));
	memset(g_bag, 0, sizeof(g_bag));
	memset(g_equip, 0, sizeof(g_equip));
	memset(g_skills, 0, sizeof(g_skills));
	memset(g_dir_down, 0, sizeof(g_dir_down));
	memset(g_dir_axis, 0, sizeof(g_dir_axis));
	memset(g_dir_held, 0, sizeof(g_dir_held));
	host_menu_fill_party();
	fprintf(stderr, "sword3-sdl: host menu open\n");
}

void host_menu_open_book(void)
{
	host_menu_open();
	g_tab = HOST_MENU_TAB_BOOK;
}

int host_menu_take_pending(int *slot)
{
	int action;

	action = g_pending;
	g_pending = -1;
	if (slot)
		*slot = g_pending_slot;
	return action;
}

static int host_menu_slot_used(int slot)
{
	const char *dir;
	char path[PATH_MAX];
	struct stat st;

	if (slot < 0 || slot >= HOST_MENU_SLOTS)
		return 0;
	dir = getenv("SWORD3_DATA_DIR");
	if (!dir || dir[0] != '/')
		dir = "/tmp/sword3/documents";
	if (snprintf(path, sizeof(path), "%s/%d.sav", dir, slot) >=
	    (int)sizeof(path))
		return 0;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int host_menu_guest_ok(uintptr_t addr, size_t n)
{
	return addr >= HOST_MENU_GUEST_LO &&
	       addr + n - 1 < HOST_MENU_GUEST_HI;
}

static int host_menu_guest_i32(uintptr_t addr)
{
	if (!host_menu_guest_ok(addr, 4))
		return 0;
	return *(volatile int *)(uintptr_t)addr;
}

static int host_menu_guest_i16(uintptr_t addr)
{
	if (!host_menu_guest_ok(addr, 2))
		return 0;
	return (int)*(volatile int16_t *)(uintptr_t)addr;
}

static uintptr_t host_menu_guest_ptr(uintptr_t addr)
{
	if (!host_menu_guest_ok(addr, sizeof(uintptr_t)))
		return 0;
	return *(volatile uintptr_t *)(uintptr_t)addr;
}

static int host_menu_heap_ok(uintptr_t addr, size_t n)
{
	if (addr < 0x10000ull || addr > 0x00007fffffffffffull)
		return 0;
	if (n == 0)
		return 1;
	if (addr + n - 1 < addr)
		return 0;
	return addr + n - 1 <= 0x00007fffffffffffull;
}

static int host_menu_mem_ok(uintptr_t addr, size_t n)
{
	return host_menu_guest_ok(addr, n) || host_menu_heap_ok(addr, n);
}

static int host_menu_mem_i32(uintptr_t addr)
{
	if (!host_menu_mem_ok(addr, 4))
		return 0;
	return *(volatile int *)(uintptr_t)addr;
}

static uintptr_t host_menu_mem_ptr(uintptr_t addr)
{
	if (!host_menu_mem_ok(addr, sizeof(uintptr_t)))
		return 0;
	return *(volatile uintptr_t *)(uintptr_t)addr;
}

static unsigned host_menu_mem_u16(uintptr_t addr)
{
	if (!host_menu_mem_ok(addr, 2))
		return 0;
	return (unsigned)*(volatile uint16_t *)(uintptr_t)addr;
}

static void host_menu_mem_set_i32(uintptr_t addr, int value)
{
	if (!host_menu_mem_ok(addr, 4))
		return;
	*(volatile int *)(uintptr_t)addr = value;
}

static void host_menu_mem_set_ptr(uintptr_t addr, uintptr_t value)
{
	if (!host_menu_mem_ok(addr, sizeof(uintptr_t)))
		return;
	*(volatile uintptr_t *)(uintptr_t)addr = value;
}

static int host_menu_copy_name(char *dst, size_t dstn, uintptr_t ptr)
{
	size_t i;
	const unsigned char *s;

	if (!dst || dstn == 0)
		return 0;
	dst[0] = 0;
	if (ptr < 0x1000ull || ptr > 0x00007fffffffffffull)
		return 0;
	s = (const unsigned char *)(uintptr_t)ptr;
	for (i = 0; i + 1 < dstn; i++) {
		unsigned char c = s[i];

		if (c == 0)
			break;
		if (c < 0x20)
			return 0;
		dst[i] = (char)c;
	}
	dst[i] = 0;
	return i > 0;
}

static int host_menu_lua_ready(void)
{
	return host_menu_guest_ok(HOST_MENU_LUA_BIND, 8) &&
	       host_menu_guest_ptr(HOST_MENU_LUA_BIND) != 0;
}

static int host_menu_party_any(void)
{
	int i;

	for (i = 0; i < HOST_MENU_PARTY_N; i++) {
		if (g_party[i].used)
			return 1;
	}
	return 0;
}

static void host_menu_fill_party(void)
{
	int i;
	int lvmax;
	int filled = 0;

	host_menu_refresh_party();
	/*
	 * The C cache at 0x1002ab1b8 is the live party block (save/load and
	 * field HP writes). Filling from Lua NewGameChar would overwrite it,
	 * so only refill when the cache still looks empty.
	 */
	if (!host_menu_party_any() && host_menu_lua_ready()) {
		for (i = 0; i < HOST_MENU_PARTY_N; i++)
			((void (*)(void *, int))(uintptr_t)HOST_MENU_FILL_CHAR)(
				(void *)(uintptr_t)(HOST_MENU_PARTY +
						    (uintptr_t)i *
							    HOST_MENU_PARTY_STRIDE),
				i + 1);
		filled = 1;
		host_menu_refresh_party();
	}
	if (host_menu_lua_ready()) {
		for (i = 0; i < HOST_MENU_PARTY_N; i++) {
			if (!g_party[i].used)
				continue;
			lvmax = ((int (*)(int))(uintptr_t)HOST_MENU_LEVEL_MAX)(
				i + 1);
			if (lvmax < 1 || lvmax > 999)
				lvmax = 0;
			g_party[i].level_max = lvmax;
		}
	}
	fprintf(stderr, "sword3-sdl: party fill lua=%d filled=%d used=%d\n",
		host_menu_lua_ready(), filled, host_menu_party_any());
	host_menu_refresh_inv();
}

static void host_menu_refresh_party(void)
{
	int i;
	int j;
	int lvmax;
	int sk_exp;
	int sk_next;
	uintptr_t rec;
	uintptr_t name;
	struct host_menu_actor *a;

	if (!host_menu_guest_ok(HOST_MENU_PARTY,
				HOST_MENU_PARTY_STRIDE * HOST_MENU_PARTY_N))
		return;
	for (i = 0; i < HOST_MENU_PARTY_N; i++) {
		rec = HOST_MENU_PARTY + (uintptr_t)i * HOST_MENU_PARTY_STRIDE;
		a = &g_party[i];
		lvmax = a->level_max;
		sk_exp = a->sk_exp;
		sk_next = a->sk_next;
		memset(a, 0, sizeof(*a));
		a->level_max = lvmax;
		a->sk_exp = sk_exp;
		a->sk_next = sk_next;
		a->hp_max = host_menu_guest_i32(rec + HOST_MENU_OFF_HPMAX);
		a->mp_max = host_menu_guest_i32(rec + HOST_MENU_OFF_MPMAX);
		a->sp_max = host_menu_guest_i32(rec + HOST_MENU_OFF_SPMAX);
		a->hp = host_menu_guest_i32(rec + HOST_MENU_OFF_HP);
		a->mp = host_menu_guest_i32(rec + HOST_MENU_OFF_MP);
		a->sp = host_menu_guest_i32(rec + HOST_MENU_OFF_SP);
		a->str = host_menu_guest_i16(rec + HOST_MENU_OFF_STR);
		a->sta = host_menu_guest_i16(rec + HOST_MENU_OFF_STA);
		a->wis = host_menu_guest_i16(rec + HOST_MENU_OFF_WIS);
		a->agi = host_menu_guest_i16(rec + HOST_MENU_OFF_AGI);
		a->atk = host_menu_guest_i16(rec + HOST_MENU_OFF_ATK);
		a->def = host_menu_guest_i16(rec + HOST_MENU_OFF_DEF);
		a->exp = host_menu_guest_i32(rec + HOST_MENU_OFF_EXP);
		a->exp_need = host_menu_guest_i32(rec + HOST_MENU_OFF_LVUP);
		a->level = host_menu_guest_i16(rec + HOST_MENU_OFF_LEVEL);
		for (j = 0; j < HOST_MENU_RESIST_N; j++) {
			if (host_menu_guest_ok(rec + HOST_MENU_OFF_ATTR + j, 1))
				a->attr[j] = *(volatile int8_t *)(uintptr_t)
						     (rec + HOST_MENU_OFF_ATTR +
						      j);
		}
		name = host_menu_guest_ptr(rec + HOST_MENU_OFF_NAME);
		host_menu_copy_name(a->name, sizeof(a->name), name);
		a->act = host_menu_guest_i32(rec + HOST_MENU_OFF_ACT);
		if (a->act <= 0 && host_menu_lua_ready()) {
			a->act = ((int (*)(void *, const void *, int,
					   const void *))(
					  uintptr_t)HOST_MENU_LUA_INT)(
				(void *)(uintptr_t)HOST_MENU_LUA_BIND,
				(const void *)(uintptr_t)HOST_MENU_STR_CHAR,
				i + 1,
				(const void *)(uintptr_t)HOST_MENU_STR_ACT);
			if (a->act > 0)
				host_menu_mem_set_i32(rec + HOST_MENU_OFF_ACT,
						      a->act);
		}
		a->used = a->hp_max > 0 || a->level > 0 || a->name[0];
	}
	if (!g_party_logged && g_party[0].used) {
		g_party_logged = 1;
		fprintf(stderr,
			"sword3-sdl: party0 name=%s lv=%d/%d hp=%d/%d exp=%d/%d sk=%d/%d act=%d\n",
			g_party[0].name[0] ? g_party[0].name : "-",
			g_party[0].level, g_party[0].level_max, g_party[0].hp,
			g_party[0].hp_max, g_party[0].exp, g_party[0].exp_need,
			g_party[0].sk_exp, g_party[0].sk_next, g_party[0].act);
	}
}

static const struct host_menu_actor *host_menu_actor(void)
{
	int i;

	if (g_party_i >= 0 && g_party_i < HOST_MENU_PARTY_N &&
	    g_party[g_party_i].used)
		return &g_party[g_party_i];
	for (i = 0; i < HOST_MENU_PARTY_N; i++) {
		if (g_party[i].used)
			return &g_party[i];
	}
	return NULL;
}

static int host_menu_load_item(struct host_menu_item *it, uintptr_t node)
{
	uintptr_t help;
	uintptr_t info;
	int temp;

	memset(it, 0, sizeof(*it));
	if (!host_menu_heap_ok(node, HOST_MENU_ITEM_NODE))
		return 0;
	temp = host_menu_mem_i32(node + HOST_MENU_OFF_TEMP);
	if (temp < 1)
		return 0;
	it->temp = temp;
	it->count = host_menu_mem_i32(node + HOST_MENU_OFF_COUNT);
	it->count_new = host_menu_mem_i32(node + HOST_MENU_OFF_COUNT_NEW);
	it->flags = host_menu_mem_i32(node + HOST_MENU_OFF_IT);
	it->place = (int)host_menu_mem_u16(node + HOST_MENU_OFF_PLACE);
	it->node = node;
	if (it->count < 0)
		it->count = 0;
	if (it->count_new < 0)
		it->count_new = 0;
	host_menu_copy_name(it->name, sizeof(it->name),
			    node + HOST_MENU_OFF_INAME);
	help = host_menu_mem_ptr(node + HOST_MENU_OFF_HELP);
	if (help)
		host_menu_copy_name(it->help, sizeof(it->help), help);
	info = host_menu_mem_ptr(node + HOST_MENU_OFF_INFO);
	if (info)
		host_menu_copy_name(it->info, sizeof(it->info), info);
	it->used = it->name[0] || temp > 0;
	return it->used;
}

static int host_menu_walk_list(struct host_menu_item *dst, int max,
			      uintptr_t head)
{
	int n = 0;
	int seen_n = 0;
	int guard = 0;
	int i;
	uintptr_t node;
	uintptr_t seen[HOST_MENU_BAG_N];

	node = head;
	while (node && n < max && guard < max + 8) {
		guard++;
		for (i = 0; i < seen_n; i++) {
			if (seen[i] == node)
				return n;
		}
		if (seen_n < HOST_MENU_BAG_N)
			seen[seen_n++] = node;
		if (host_menu_load_item(&dst[n], node))
			n++;
		node = host_menu_mem_ptr(node + HOST_MENU_OFF_NEXT);
	}
	return n;
}

static void host_menu_refresh_inv(void)
{
	int i;
	int idx;
	uintptr_t rec;
	uintptr_t head;
	uintptr_t node;
	struct host_menu_actor *a;

	memset(g_bag, 0, sizeof(g_bag));
	memset(g_equip, 0, sizeof(g_equip));
	memset(g_skills, 0, sizeof(g_skills));
	g_bag_n = 0;
	g_skill_n = 0;
	if (host_menu_guest_ok(HOST_MENU_ITEM_REPO + HOST_MENU_OFF_NEXT, 8)) {
		head = host_menu_guest_ptr(HOST_MENU_ITEM_REPO +
					  HOST_MENU_OFF_NEXT);
		g_bag_n = host_menu_walk_list(g_bag, HOST_MENU_BAG_N, head);
	}
	if (host_menu_guest_ok(HOST_MENU_EQUIP_REPO,
			       (size_t)HOST_MENU_EQUIP_N * 8 *
				       HOST_MENU_PARTY_N)) {
		for (i = 0; i < HOST_MENU_EQUIP_N; i++) {
			idx = i + g_party_i * 16;
			node = host_menu_guest_ptr(HOST_MENU_EQUIP_REPO +
						  (uintptr_t)idx * 8);
			if (node)
				host_menu_load_item(&g_equip[i], node);
		}
	}
	if (host_menu_guest_ok(HOST_MENU_SKILL_REPO +
				      (uintptr_t)g_party_i * 8,
			      8)) {
		rec = host_menu_guest_ptr(HOST_MENU_SKILL_REPO +
					 (uintptr_t)g_party_i * 8);
		if (rec)
			head = host_menu_mem_ptr(rec + HOST_MENU_OFF_NEXT);
		else
			head = 0;
		g_skill_n = host_menu_walk_list(g_skills, HOST_MENU_SKILL_N,
						head);
	}
	if (g_skill_focus >= g_skill_n)
		g_skill_focus = g_skill_n > 0 ? g_skill_n - 1 : 0;
	for (i = 0; i < HOST_MENU_PARTY_N; i++) {
		a = &g_party[i];
		rec = HOST_MENU_SKEXP + (uintptr_t)i * HOST_MENU_SKEXP_STRIDE;
		if (!host_menu_guest_ok(rec, 16))
			continue;
		a->sk_exp = host_menu_guest_i32(rec + 4);
		a->sk_next = host_menu_guest_i32(rec + 8);
	}
	if (!g_inv_logged && (g_bag_n || g_skill_n || g_equip[0].used)) {
		g_inv_logged = 1;
		fprintf(stderr,
			"sword3-sdl: inv bag=%d skill=%d equip0=%s flags0=%08x\n",
			g_bag_n, g_skill_n,
			g_equip[0].name[0] ? g_equip[0].name : "-",
			(unsigned)g_bag[0].flags);
	}
}

static int host_menu_item_in_cat(const struct host_menu_item *it, int cat)
{
	uint32_t flags;
	uint32_t mask;
	uint32_t special;

	if (!it || !it->used)
		return 0;
	if (cat == 0)
		return it->count_new > 0;
	if (cat < 0 || cat >= HOST_MENU_ITEM_CAT || it->count <= 0)
		return 0;
	flags = (uint32_t)it->flags;
	mask = g_item_cat_mask[cat];
	if (cat == 3 || cat == 6) {
		if (flags & mask)
			return 1;
		return cat == 6 && flags == 0;
	}
	if ((flags & mask) == 0)
		return 0;
	special = g_item_cat_mask[3] | g_item_cat_mask[6];
	return (flags & special) == 0;
}

static int host_menu_item_view(int *out, int max)
{
	int i;
	int n = 0;

	if (!out || max <= 0)
		return 0;
	for (i = 0; i < g_bag_n && n < max; i++) {
		if (host_menu_item_in_cat(&g_bag[i], g_item_cat))
			out[n++] = i;
	}
	return n;
}

static void host_menu_clamp_item_sel(void)
{
	int view[HOST_MENU_BAG_N];
	int n;

	if (g_item_cat < 0 || g_item_cat >= HOST_MENU_ITEM_CAT)
		g_item_cat = 0;
	n = host_menu_item_view(view, HOST_MENU_BAG_N);
	if (n <= 0)
		g_item_sel = 0;
	else if (g_item_sel >= n)
		g_item_sel = n - 1;
	else if (g_item_sel < 0)
		g_item_sel = 0;
}

static struct host_menu_item *host_menu_item_current(void)
{
	int view[HOST_MENU_BAG_N];
	int n;

	host_menu_clamp_item_sel();
	n = host_menu_item_view(view, HOST_MENU_BAG_N);
	if (n <= 0 || g_item_sel < 0 || g_item_sel >= n)
		return NULL;
	return &g_bag[view[g_item_sel]];
}

static int host_menu_item_usable(const struct host_menu_item *it)
{
	return it && (it->place & HOST_MENU_ITEM_USE_BITS) != 0;
}

static int host_menu_item_droppable(const struct host_menu_item *it)
{
	return it &&
	       ((unsigned)it->flags & (1u << HOST_MENU_ITEM_NO_THROW)) == 0;
}

static int host_menu_item_dimmed(const struct host_menu_item *it)
{
	if (!it)
		return 0;
	if ((it->place & 0xf) == 0)
		return 1;
	if (g_item_focus == 0 && !host_menu_item_usable(it))
		return 1;
	if (g_item_focus == 2 && !host_menu_item_droppable(it))
		return 1;
	return 0;
}

static void host_menu_item_sound(int id)
{
	((void (*)(int))(uintptr_t)HOST_MENU_ITEM_SOUND)(id);
}

static void host_menu_item_say(const char *text, int sound)
{
	if (text && text[0])
		snprintf(g_item_note, sizeof(g_item_note), "%s", text);
	else
		g_item_note[0] = 0;
	if (sound)
		host_menu_item_sound(sound);
}

static int host_menu_item_has_fn(int temp, uintptr_t fn)
{
	if (temp < 1 || !host_menu_lua_ready())
		return 0;
	return ((int (*)(void *, const void *, int, const void *))(
			uintptr_t)HOST_MENU_LUA_HAS)(
		(void *)(uintptr_t)HOST_MENU_LUA_BIND,
		(const void *)(uintptr_t)HOST_MENU_STR_ITEMTEMP, temp,
		(const void *)(uintptr_t)fn);
}

static int host_menu_lua_int(uintptr_t fn, int temp, uintptr_t field)
{
	if (temp < 1 || !host_menu_lua_ready())
		return 0;
	return ((int (*)(void *, const void *, int, const void *))(
			uintptr_t)fn)(
		(void *)(uintptr_t)HOST_MENU_LUA_BIND,
		(const void *)(uintptr_t)HOST_MENU_STR_ITEMTEMP, temp,
		(const void *)(uintptr_t)field);
}

static int host_menu_lua_text(int temp, uintptr_t field, char *dst, size_t dstn)
{
	const char *raw;
	const char *shown;

	if (!dst || dstn == 0)
		return 0;
	dst[0] = 0;
	if (temp < 1 || !host_menu_lua_ready())
		return 0;
	raw = ((const char *(*)(void *, const void *, int, const void *))(
			uintptr_t)HOST_MENU_LUA_STR)(
		(void *)(uintptr_t)HOST_MENU_LUA_BIND,
		(const void *)(uintptr_t)HOST_MENU_STR_ITEMTEMP, temp,
		(const void *)(uintptr_t)field);
	if (!raw)
		return 0;
	shown = ((const char *(*)(const void *))(uintptr_t)HOST_MENU_LUA_DB)(
		raw);
	return host_menu_copy_name(dst, dstn, (uintptr_t)shown);
}

static void host_menu_bestiary_load_sel(void)
{
	int temp;

	g_bestiary_lv = 0;
	g_bestiary_hp = 0;
	g_bestiary_atk = 0;
	g_bestiary_def = 0;
	g_bestiary_spd = 0;
	g_bestiary_help[0] = 0;
	if (g_bestiary_n <= 0 || g_bestiary_sel < 0 ||
	    g_bestiary_sel >= g_bestiary_n)
		return;
	temp = g_bestiary_id[g_bestiary_sel];
	g_bestiary_lv = host_menu_lua_int(HOST_MENU_LUA_INT, temp,
					 HOST_MENU_STR_LEVEL);
	g_bestiary_hp = host_menu_lua_int(HOST_MENU_LUA_NUM, temp,
					 HOST_MENU_STR_HP);
	g_bestiary_atk = host_menu_lua_int(HOST_MENU_LUA_NUM, temp,
					  HOST_MENU_STR_ATK);
	g_bestiary_def = host_menu_lua_int(HOST_MENU_LUA_NUM, temp,
					  HOST_MENU_STR_DEF);
	g_bestiary_spd = host_menu_lua_int(HOST_MENU_LUA_NUM, temp,
					  HOST_MENU_STR_SPD);
	host_menu_lua_text(temp, HOST_MENU_STR_HELP, g_bestiary_help,
			   sizeof(g_bestiary_help));
}

static void host_menu_bestiary_open(void)
{
	uintptr_t ids;
	uintptr_t src;
	int n;
	int src_n;
	int i;
	int temp;

	g_bestiary_n = 0;
	g_bestiary_sel = 0;
	if (!host_menu_lua_ready()) {
		host_menu_item_say("无法打开", 0x8c);
		return;
	}
	src_n = host_menu_guest_i32(HOST_MENU_BOOK_SRC_N);
	src = host_menu_guest_ptr(HOST_MENU_BOOK_SRC);
	if (src_n > 0 && !src) {
		host_menu_item_say("无法打开", 0x8c);
		return;
	}
	((void (*)(int))(uintptr_t)HOST_MENU_BOOK_BUILD)(0);
	if (!host_menu_guest_ok(HOST_MENU_BOOK_N, 4)) {
		host_menu_item_say("无法打开", 0x8c);
		return;
	}
	n = host_menu_mem_i32(HOST_MENU_BOOK_N);
	ids = host_menu_mem_ptr(HOST_MENU_BOOK_IDS);
	if (n < 0)
		n = 0;
	if (n > HOST_MENU_BESTIARY_N)
		n = HOST_MENU_BESTIARY_N;
	if (n > 0 && !ids) {
		host_menu_item_say("无法打开", 0x8c);
		return;
	}
	for (i = 0; i < n; i++) {
		temp = host_menu_mem_i32(ids + (uintptr_t)i * 4);
		g_bestiary_id[i] = temp;
		if (!host_menu_lua_text(temp, HOST_MENU_STR_NAME,
					g_bestiary_name[i],
					sizeof(g_bestiary_name[i])))
			snprintf(g_bestiary_name[i], sizeof(g_bestiary_name[i]),
				 "#%d", temp);
	}
	g_bestiary_n = n;
	g_layer = HOST_MENU_LAYER_BESTIARY;
	host_menu_bestiary_load_sel();
	g_item_note[0] = 0;
	fprintf(stderr, "sword3-sdl: bestiary open n=%d\n", n);
}

static void host_menu_move_bestiary(int delta)
{
	if (g_bestiary_n <= 0)
		return;
	g_bestiary_sel = (g_bestiary_sel + delta % g_bestiary_n +
			  g_bestiary_n) %
			 g_bestiary_n;
	host_menu_bestiary_load_sel();
}

static void host_menu_item_unlink(uintptr_t target)
{
	uintptr_t prev;
	uintptr_t node;
	uintptr_t next;
	int guard = 0;

	if (!target || !host_menu_heap_ok(target, HOST_MENU_ITEM_NODE))
		return;
	if (!host_menu_guest_ok(HOST_MENU_ITEM_REPO + HOST_MENU_OFF_NEXT, 8))
		return;
	prev = HOST_MENU_ITEM_REPO;
	node = host_menu_guest_ptr(prev + HOST_MENU_OFF_NEXT);
	while (node && guard < HOST_MENU_BAG_N + 8) {
		guard++;
		next = host_menu_mem_ptr(node + HOST_MENU_OFF_NEXT);
		if (node == target) {
			host_menu_mem_set_ptr(prev + HOST_MENU_OFF_NEXT, next);
			((void (*)(void *))(uintptr_t)HOST_MENU_ITEM_DTOR)(
				(void *)(uintptr_t)target);
			((void (*)(void *))(uintptr_t)HOST_MENU_ITEM_FREE)(
				(void *)(uintptr_t)target);
			return;
		}
		prev = node;
		node = next;
	}
}

static void host_menu_item_consume(uintptr_t node, int from_new)
{
	int count;
	int count_new;

	if (!node || !host_menu_heap_ok(node, HOST_MENU_ITEM_NODE))
		return;
	count = host_menu_mem_i32(node + HOST_MENU_OFF_COUNT);
	count_new = host_menu_mem_i32(node + HOST_MENU_OFF_COUNT_NEW);
	if (from_new) {
		count_new -= 1;
		if (count_new <= 0) {
			count += count_new;
			count_new = 0;
		}
	} else {
		count -= 1;
		if (count <= 0)
			count = 0;
	}
	if (count < 0)
		count = 0;
	if (count_new < 0)
		count_new = 0;
	host_menu_mem_set_i32(node + HOST_MENU_OFF_COUNT, count);
	host_menu_mem_set_i32(node + HOST_MENU_OFF_COUNT_NEW, count_new);
	if (count <= 0 && count_new <= 0)
		host_menu_item_unlink(node);
}

static int host_menu_item_special(int temp)
{
	static const uintptr_t fns[] = {
		0x100274061ull, 0x100274082ull, 0x100274092ull,
		0x1002740a3ull, 0x1002740aeull
	};
	size_t i;
	int found;

	if (temp < 1 || !host_menu_lua_ready())
		return 0;
	for (i = 0; i < sizeof(fns) / sizeof(fns[0]); i++) {
		found = ((int (*)(void *, const void *, int, const void *))(
				 uintptr_t)HOST_MENU_LUA_HAS)(
			(void *)(uintptr_t)HOST_MENU_LUA_BIND,
			(const void *)(uintptr_t)0x100272904ull, temp,
			(const void *)(uintptr_t)fns[i]);
		if (found)
			return 1;
	}
	return 0;
}

static int host_menu_widget(int id)
{
	uintptr_t bits;

	if (id < 0)
		return 0;
	if (!host_menu_guest_ok(HOST_MENU_WIDGET_BITS, 8))
		return 0;
	bits = host_menu_guest_ptr(HOST_MENU_WIDGET_BITS);
	if (!bits || !host_menu_mem_ok(bits, (size_t)id / 8 + 1))
		return 0;
	return ((int (*)(int))(uintptr_t)HOST_MENU_WIDGET_HAS)(id);
}

static int host_menu_party_widget_any(void)
{
	int i;

	for (i = 0; i < HOST_MENU_PARTY_N; i++) {
		if (host_menu_widget(HOST_MENU_W_PARTY0 + i))
			return 1;
	}
	return 0;
}

static int host_menu_in_team(int i)
{
	if (i < 0 || i >= HOST_MENU_PARTY_N || !g_party[i].used)
		return 0;
	if (host_menu_party_widget_any())
		return host_menu_widget(HOST_MENU_W_PARTY0 + i);
	return i == 0;
}

static int host_menu_party_team_n(void)
{
	int i;
	int n = 0;

	for (i = 0; i < HOST_MENU_PARTY_N; i++) {
		if (host_menu_in_team(i))
			n++;
	}
	return n;
}

static void host_menu_move_party(int delta)
{
	int i;
	int guard = 0;
	int step;

	if (delta == 0 || host_menu_party_team_n() <= 0)
		return;
	step = delta > 0 ? 1 : -1;
	i = g_party_i;
	do {
		i = (i + step + HOST_MENU_PARTY_N) % HOST_MENU_PARTY_N;
		guard++;
	} while (guard < HOST_MENU_PARTY_N && !host_menu_in_team(i));
	if (host_menu_in_team(i))
		g_party_i = i;
}

static uintptr_t host_menu_item_party(void)
{
	int i;

	if (!host_menu_in_team(g_party_i)) {
		for (i = 0; i < HOST_MENU_PARTY_N; i++) {
			if (host_menu_in_team(i)) {
				g_party_i = i;
				break;
			}
		}
	}
	if (!host_menu_in_team(g_party_i))
		return 0;
	return HOST_MENU_PARTY +
	       (uintptr_t)g_party_i * HOST_MENU_PARTY_STRIDE;
}

static unsigned host_menu_char_place(int i)
{
	if (i < 0 || i >= HOST_MENU_PARTY_N)
		return 0;
	return 0x8000u >> i;
}

static int host_menu_item_fits_slot(const struct host_menu_item *it, int slot)
{
	uint32_t mask;
	unsigned allow;

	if (!it || !it->used || slot < 0 || slot >= HOST_MENU_EQUIP_N)
		return 0;
	if (it->count + it->count_new <= 0)
		return 0;
	mask = g_equip_mask[slot];
	if (((uint32_t)it->flags & mask) != mask)
		return 0;
	allow = host_menu_char_place(g_party_i);
	return allow != 0 && ((unsigned)it->place & allow) != 0;
}

static int host_menu_equip_view(int *out, int max)
{
	int i;
	int n = 0;

	if (!out || max <= 0)
		return 0;
	for (i = 0; i < g_bag_n && n < max; i++) {
		if (host_menu_item_fits_slot(&g_bag[i], g_equip_focus))
			out[n++] = i;
	}
	return n;
}

static void host_menu_clamp_equip_sel(void)
{
	int view[HOST_MENU_BAG_N];
	int n;

	n = host_menu_equip_view(view, HOST_MENU_BAG_N);
	if (n <= 0)
		g_equip_sel = 0;
	else if (g_equip_sel >= n)
		g_equip_sel = n - 1;
}

static struct host_menu_item *host_menu_equip_current(void)
{
	int view[HOST_MENU_BAG_N];
	int n;

	host_menu_clamp_equip_sel();
	n = host_menu_equip_view(view, HOST_MENU_BAG_N);
	if (n <= 0 || g_equip_sel < 0 || g_equip_sel >= n)
		return NULL;
	return &g_bag[view[g_equip_sel]];
}

static int host_menu_item_add(int temp, uintptr_t field)
{
	return host_menu_lua_int(HOST_MENU_LUA_NUM, temp, field);
}

static void host_menu_move_equip_sel(int delta)
{
	int view[HOST_MENU_BAG_N];
	int n;

	n = host_menu_equip_view(view, HOST_MENU_BAG_N);
	if (n <= 0)
		return;
	g_equip_sel = (g_equip_sel + delta % n + n) % n;
}

static void host_menu_equip_open(void)
{
	int view[HOST_MENU_BAG_N];
	int n;

	g_item_note[0] = 0;
	host_menu_item_party();
	if (!host_menu_in_team(g_party_i)) {
		host_menu_item_say("无法装备", 0x8c);
		return;
	}
	n = host_menu_equip_view(view, HOST_MENU_BAG_N);
	g_equip_sel = 0;
	if (n <= 0) {
		host_menu_item_say("没有可装备的物品", 0x8c);
		return;
	}
	g_layer = HOST_MENU_LAYER_EQUIP;
	fprintf(stderr, "sword3-sdl: equip pick slot=%d n=%d party=%d\n",
		g_equip_focus, n, g_party_i);
}

static int host_menu_equip_swap(uintptr_t bag_node, int slot)
{
	uintptr_t rec;
	uintptr_t worn;

	if (!bag_node || !host_menu_heap_ok(bag_node, HOST_MENU_ITEM_NODE))
		return 0;
	if (slot < 0 || slot >= HOST_MENU_EQUIP_N || !host_menu_in_team(g_party_i))
		return 0;
	rec = HOST_MENU_EQUIP_REPO +
	      ((uintptr_t)slot + (uintptr_t)g_party_i * 16u) * 8u;
	if (!host_menu_guest_ok(rec, 8))
		return 0;
	worn = host_menu_guest_ptr(rec);
	if (!worn)
		return 0;
	return ((int (*)(void *, void *, int, int))(uintptr_t)
			HOST_MENU_EQUIP_SWAP)(
		       (void *)(uintptr_t)worn, (void *)(uintptr_t)bag_node,
		       g_party_i + 1, slot + 1) != 0;
}

static void host_menu_equip_commit(void)
{
	struct host_menu_item *it;
	uintptr_t node;

	it = host_menu_equip_current();
	if (!it || !it->node) {
		host_menu_item_say("没有物品", 0x8c);
		return;
	}
	node = it->node;
	if (!host_menu_equip_swap(node, g_equip_focus)) {
		host_menu_item_say("无法装备", 0x8c);
		return;
	}
	host_menu_refresh_party();
	host_menu_refresh_inv();
	host_menu_item_say("装备成功", 0x2e);
	fprintf(stderr, "sword3-sdl: equip swap slot=%d party=%d node=%llx\n",
		g_equip_focus, g_party_i, (unsigned long long)node);
	g_layer = HOST_MENU_LAYER_INNER;
}

static void host_menu_item_hold(const struct host_menu_item *it, int from_new)
{
	g_act_node = it ? it->node : 0;
	g_act_flags = it ? it->flags : 0;
	g_act_from_new = from_new;
	g_act_name[0] = 0;
	if (it && it->name[0])
		snprintf(g_act_name, sizeof(g_act_name), "%s", it->name);
}

static void host_menu_item_clear_hold(void)
{
	g_act_node = 0;
	g_act_flags = 0;
	g_act_from_new = 0;
	g_act_name[0] = 0;
}

static int host_menu_layer_page(void)
{
	return g_layer == HOST_MENU_LAYER_SLOTS ||
	       g_layer == HOST_MENU_LAYER_STUB ||
	       g_layer == HOST_MENU_LAYER_BESTIARY ||
	       g_layer == HOST_MENU_LAYER_PICK ||
	       g_layer == HOST_MENU_LAYER_ASK ||
	       g_layer == HOST_MENU_LAYER_EQUIP;
}

static void host_menu_item_sort(void)
{
	uintptr_t node;
	int count;
	int count_new;
	int n = 0;

	if (!host_menu_guest_ok(HOST_MENU_ITEM_REPO + HOST_MENU_OFF_NEXT, 8))
		return;
	node = host_menu_guest_ptr(HOST_MENU_ITEM_REPO + HOST_MENU_OFF_NEXT);
	while (node && n < HOST_MENU_BAG_N + 8) {
		n++;
		if (!host_menu_heap_ok(node, HOST_MENU_ITEM_NODE))
			break;
		count = host_menu_mem_i32(node + HOST_MENU_OFF_COUNT);
		count_new = host_menu_mem_i32(node + HOST_MENU_OFF_COUNT_NEW);
		if (count_new) {
			if (count < 0)
				count = 0;
			host_menu_mem_set_i32(node + HOST_MENU_OFF_COUNT,
					      count + count_new);
			host_menu_mem_set_i32(node + HOST_MENU_OFF_COUNT_NEW,
					      0);
		}
		node = host_menu_mem_ptr(node + HOST_MENU_OFF_NEXT);
	}
	host_menu_item_say("整理完成", 0x2d);
	fprintf(stderr, "sword3-sdl: item sort\n");
}

static void host_menu_item_use(struct host_menu_item *it)
{
	if (!it || !it->node) {
		host_menu_item_say("没有物品", 0x8c);
		return;
	}
	if (!host_menu_item_usable(it)) {
		host_menu_item_say("无法使用", 0x8c);
		return;
	}
	if (host_menu_item_has_fn(it->temp, HOST_MENU_FN_BOOK)) {
		host_menu_bestiary_open();
		return;
	}
	if (host_menu_item_special(it->temp)) {
		host_menu_item_say("此物品暂不能在此使用", 0x8c);
		return;
	}
	if (host_menu_party_team_n() <= 0) {
		host_menu_item_say("无法使用", 0x8c);
		return;
	}
	host_menu_item_hold(it, 1);
	host_menu_item_party();
	g_layer = HOST_MENU_LAYER_PICK;
	g_item_note[0] = 0;
	if (!g_pick_logged) {
		g_pick_logged = 1;
		fprintf(stderr,
			"sword3-sdl: item pick widgets=%d%d%d%d team=%d party=%d\n",
			host_menu_widget(HOST_MENU_W_PARTY0),
			host_menu_widget(HOST_MENU_W_PARTY0 + 1),
			host_menu_widget(HOST_MENU_W_PARTY0 + 2),
			host_menu_widget(HOST_MENU_W_PARTY0 + 3),
			host_menu_party_team_n(), g_party_i);
	}
	fprintf(stderr, "sword3-sdl: item pick temp=%d party=%d\n", it->temp,
		g_party_i);
}

static void host_menu_item_use_commit(void)
{
	uintptr_t party;
	uintptr_t userdata;

	if (!g_act_node || !host_menu_heap_ok(g_act_node, HOST_MENU_ITEM_NODE)) {
		host_menu_item_say("没有物品", 0x8c);
		host_menu_item_clear_hold();
		g_layer = HOST_MENU_LAYER_INNER;
		return;
	}
	party = host_menu_item_party();
	if (!party) {
		host_menu_item_say("无法使用", 0x8c);
		host_menu_item_clear_hold();
		g_layer = HOST_MENU_LAYER_INNER;
		return;
	}
	userdata = g_act_node + HOST_MENU_OFF_INAME;
	((void (*)(void *, void *))(uintptr_t)HOST_MENU_ITEM_APPLY)(
		(void *)(uintptr_t)party, (void *)(uintptr_t)userdata);
	if (((unsigned)g_act_flags & (1u << HOST_MENU_ITEM_NO_CONSUME)) == 0)
		host_menu_item_consume(g_act_node, 1);
	host_menu_refresh_party();
	host_menu_refresh_inv();
	host_menu_item_say("使用成功", 0x8b);
	fprintf(stderr, "sword3-sdl: item use party=%d node=%llx\n", g_party_i,
		(unsigned long long)g_act_node);
	host_menu_item_clear_hold();
	g_layer = HOST_MENU_LAYER_INNER;
}

static void host_menu_item_drop(struct host_menu_item *it)
{
	if (!it || !it->node) {
		host_menu_item_say("没有物品", 0x8c);
		return;
	}
	if (!host_menu_item_droppable(it)) {
		host_menu_item_say("无法丢弃", 0xb8);
		return;
	}
	host_menu_item_hold(it, g_item_cat == 0);
	g_layer = HOST_MENU_LAYER_ASK;
	g_item_note[0] = 0;
	fprintf(stderr, "sword3-sdl: item drop ask temp=%d\n", it->temp);
}

static void host_menu_item_drop_commit(void)
{
	if (!g_act_node || !host_menu_heap_ok(g_act_node, HOST_MENU_ITEM_NODE)) {
		host_menu_item_say("没有物品", 0x8c);
		host_menu_item_clear_hold();
		g_layer = HOST_MENU_LAYER_INNER;
		return;
	}
	host_menu_item_consume(g_act_node, g_act_from_new);
	host_menu_refresh_inv();
	host_menu_item_say("已丢弃", 0xb8);
	fprintf(stderr, "sword3-sdl: item drop node=%llx\n",
		(unsigned long long)g_act_node);
	host_menu_item_clear_hold();
	g_layer = HOST_MENU_LAYER_INNER;
}

static void host_menu_item_act(void)
{
	struct host_menu_item *it;

	g_item_note[0] = 0;
	if (g_item_focus == 1) {
		host_menu_item_sort();
		return;
	}
	it = host_menu_item_current();
	if (g_item_focus == 2)
		host_menu_item_drop(it);
	else
		host_menu_item_use(it);
}

static void host_menu_enter_inner(void)
{
	g_layer = HOST_MENU_LAYER_INNER;
	if (g_tab == HOST_MENU_TAB_ITEM)
		g_item_focus = 0;
	else if (g_tab == HOST_MENU_TAB_EQUIP) {
		g_equip_focus = 0;
		host_menu_item_party();
	}
	else if (g_tab == HOST_MENU_TAB_SKILL)
		g_skill_focus = 0;
	else if (g_tab == HOST_MENU_TAB_BOOK)
		g_book_focus = 0;
	fprintf(stderr, "sword3-sdl: host menu enter tab=%d\n", g_tab);
}

static void host_menu_enter_slots(int mode)
{
	g_tab = HOST_MENU_TAB_BOOK;
	g_layer = HOST_MENU_LAYER_SLOTS;
	g_slot_mode = mode;
	g_slot_focus = 0;
	fprintf(stderr, "sword3-sdl: host menu slots mode=%d\n", mode);
}

static void host_menu_move_slot(int dx, int dy)
{
	int col;
	int row;

	col = g_slot_focus % HOST_MENU_SLOT_COLS;
	row = g_slot_focus / HOST_MENU_SLOT_COLS;
	col = (col + dx + HOST_MENU_SLOT_COLS) % HOST_MENU_SLOT_COLS;
	row = (row + dy + HOST_MENU_SLOTS / HOST_MENU_SLOT_COLS) %
	      (HOST_MENU_SLOTS / HOST_MENU_SLOT_COLS);
	g_slot_focus = row * HOST_MENU_SLOT_COLS + col;
}

static void host_menu_move_tab(int delta)
{
	g_tab = (g_tab + delta + HOST_MENU_TABS) % HOST_MENU_TABS;
	g_layer = HOST_MENU_LAYER_TABS;
	g_stub = -1;
}

static void host_menu_move_book(int delta)
{
	g_book_focus = (g_book_focus + delta + HOST_MENU_ACTIONS) %
		       HOST_MENU_ACTIONS;
}

static void host_menu_move_item(int delta)
{
	g_item_focus = (g_item_focus + delta + HOST_MENU_ITEM_ACT) %
		       HOST_MENU_ITEM_ACT;
	g_item_note[0] = 0;
}

static void host_menu_move_item_cat(int delta)
{
	g_item_cat = (g_item_cat + delta + HOST_MENU_ITEM_CAT) %
		     HOST_MENU_ITEM_CAT;
	g_item_sel = 0;
	g_item_note[0] = 0;
}

static void host_menu_move_item_sel(int delta)
{
	int view[HOST_MENU_BAG_N];
	int n;

	n = host_menu_item_view(view, HOST_MENU_BAG_N);
	if (n <= 0)
		return;
	g_item_sel = (g_item_sel + delta % n + n) % n;
	g_item_note[0] = 0;
}

static void host_menu_move_equip(int delta)
{
	g_equip_focus = (g_equip_focus + delta + HOST_MENU_EQUIP_N) %
			HOST_MENU_EQUIP_N;
	g_item_note[0] = 0;
}

static void host_menu_move_skill(int delta)
{
	if (g_skill_n <= 0)
		return;
	g_skill_focus = (g_skill_focus + delta + g_skill_n) % g_skill_n;
}

static void host_menu_dir(int index, int down)
{
	if (index < 0 || index >= 4)
		return;
	if (!down) {
		g_dir_held[index] = 0;
		return;
	}
	if (g_dir_held[index])
		return;
	g_dir_held[index] = 1;

	if (g_layer == HOST_MENU_LAYER_STUB || g_layer == HOST_MENU_LAYER_ASK)
		return;
	if (g_layer == HOST_MENU_LAYER_EQUIP) {
		if (index == 2)
			host_menu_move_equip_sel(1);
		else if (index == 0)
			host_menu_move_equip_sel(-1);
		else if (index == 1)
			host_menu_move_party(1);
		else if (index == 3)
			host_menu_move_party(-1);
		host_menu_clamp_equip_sel();
		return;
	}
	if (g_layer == HOST_MENU_LAYER_PICK) {
		if (index == 2 || index == 1)
			host_menu_move_party(1);
		else if (index == 0 || index == 3)
			host_menu_move_party(-1);
		return;
	}
	if (g_layer == HOST_MENU_LAYER_BESTIARY) {
		if (index == 2)
			host_menu_move_bestiary(1);
		else if (index == 0)
			host_menu_move_bestiary(-1);
		return;
	}

	if (g_layer == HOST_MENU_LAYER_SLOTS) {
		if (index == 1)
			host_menu_move_slot(1, 0);
		else if (index == 3)
			host_menu_move_slot(-1, 0);
		else if (index == 2)
			host_menu_move_slot(0, 1);
		else if (index == 0)
			host_menu_move_slot(0, -1);
		return;
	}

	if (g_layer == HOST_MENU_LAYER_TABS) {
		if (index == 1 || index == 3)
			host_menu_move_tab(index == 1 ? 1 : -1);
		return;
	}

	if (g_tab == HOST_MENU_TAB_BOOK) {
		if (index == 1 || index == 3)
			host_menu_move_book(index == 1 ? 1 : -1);
		return;
	}
	if (g_tab == HOST_MENU_TAB_ITEM) {
		if (index == 1 || index == 3)
			host_menu_move_item_cat(index == 1 ? 1 : -1);
		else if (index == 2)
			host_menu_move_item_sel(1);
		else if (index == 0)
			host_menu_move_item_sel(-1);
		return;
	}
	if (g_tab == HOST_MENU_TAB_EQUIP) {
		if (index == 2)
			host_menu_move_equip(1);
		else if (index == 0)
			host_menu_move_equip(-1);
		else if (index == 1)
			host_menu_move_party(1);
		else if (index == 3)
			host_menu_move_party(-1);
		return;
	}
	if (g_tab == HOST_MENU_TAB_SKILL) {
		if (index == 2)
			host_menu_move_skill(1);
		else if (index == 0)
			host_menu_move_skill(-1);
	}
}

static void host_menu_set_dir(int index, int axis, int down)
{
	int wanted;

	if (index < 0 || index >= 4)
		return;
	if (axis)
		g_dir_axis[index] = down;
	else
		g_dir_down[index] = down;
	wanted = g_dir_axis[index] || g_dir_down[index];
	host_menu_dir(index, wanted);
}

static void host_menu_confirm(void)
{
	if (g_layer == HOST_MENU_LAYER_STUB ||
	    g_layer == HOST_MENU_LAYER_BESTIARY)
		return;
	if (g_layer == HOST_MENU_LAYER_PICK) {
		host_menu_item_use_commit();
		return;
	}
	if (g_layer == HOST_MENU_LAYER_EQUIP) {
		host_menu_equip_commit();
		return;
	}
	if (g_layer == HOST_MENU_LAYER_ASK) {
		host_menu_item_drop_commit();
		return;
	}
	if (g_layer == HOST_MENU_LAYER_TABS) {
		host_menu_enter_inner();
		return;
	}
	if (g_layer == HOST_MENU_LAYER_SLOTS) {
		if (g_slot_mode == 1 && !host_menu_slot_used(g_slot_focus))
			return;
		g_pending_slot = g_slot_focus;
		g_pending = g_slot_mode;
		fprintf(stderr,
			"sword3-sdl: host menu slot pending kind=%d slot=%d\n",
			g_pending, g_pending_slot);
		if (g_slot_mode == 1)
			host_menu_close();
		return;
	}
	if (g_layer == HOST_MENU_LAYER_INNER &&
	    g_tab == HOST_MENU_TAB_ITEM) {
		host_menu_item_act();
		return;
	}
	if (g_layer == HOST_MENU_LAYER_INNER &&
	    g_tab == HOST_MENU_TAB_EQUIP) {
		host_menu_equip_open();
		return;
	}
	if (g_tab != HOST_MENU_TAB_BOOK)
		return;
	if (g_book_focus == 4) {
		host_menu_close();
		return;
	}
	if (g_book_focus == 0 || g_book_focus == 1) {
		host_menu_enter_slots(g_book_focus);
		return;
	}
	g_stub = g_book_focus;
	g_layer = HOST_MENU_LAYER_STUB;
	fprintf(stderr, "sword3-sdl: host menu stub action=%d\n", g_stub);
}

static void host_menu_back(void)
{
	if (g_layer == HOST_MENU_LAYER_STUB ||
	    g_layer == HOST_MENU_LAYER_SLOTS ||
	    g_layer == HOST_MENU_LAYER_BESTIARY ||
	    g_layer == HOST_MENU_LAYER_PICK ||
	    g_layer == HOST_MENU_LAYER_ASK ||
	    g_layer == HOST_MENU_LAYER_EQUIP) {
		g_layer = HOST_MENU_LAYER_INNER;
		g_stub = -1;
		host_menu_item_clear_hold();
		return;
	}
	if (g_layer == HOST_MENU_LAYER_INNER) {
		g_layer = HOST_MENU_LAYER_TABS;
		return;
	}
	host_menu_close();
}

int host_menu_button(int button, int down)
{
	int index;

	if (!g_open)
		return 0;
	switch (button) {
	case SDL_CONTROLLER_BUTTON_A:
		if (down)
			host_menu_confirm();
		return 1;
	case SDL_CONTROLLER_BUTTON_B:
		if (down)
			host_menu_back();
		return 1;
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
		if (down && !host_menu_layer_page()) {
			if (g_layer == HOST_MENU_LAYER_INNER &&
			    g_tab == HOST_MENU_TAB_ITEM)
				host_menu_move_item(-1);
			else
				host_menu_move_tab(-1);
		}
		return 1;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
		if (down && !host_menu_layer_page()) {
			if (g_layer == HOST_MENU_LAYER_INNER &&
			    g_tab == HOST_MENU_TAB_ITEM)
				host_menu_move_item(1);
			else
				host_menu_move_tab(1);
		}
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
	host_menu_set_dir(index, 0, down);
	return 1;
}

int host_menu_axis(Uint8 axis, Sint16 value)
{
	int old_dir;
	int new_dir;

	if (!g_open)
		return 0;
	if (axis == SDL_CONTROLLER_AXIS_LEFTX) {
		old_dir = g_dir_axis[3] ? 3 : (g_dir_axis[1] ? 1 : -1);
		new_dir = value < -HOST_MENU_DEADZONE ? 3 :
			  (value > HOST_MENU_DEADZONE ? 1 : -1);
	} else if (axis == SDL_CONTROLLER_AXIS_LEFTY) {
		old_dir = g_dir_axis[0] ? 0 : (g_dir_axis[2] ? 2 : -1);
		new_dir = value < -HOST_MENU_DEADZONE ? 0 :
			  (value > HOST_MENU_DEADZONE ? 2 : -1);
	} else {
		return 1;
	}
	if (old_dir == new_dir)
		return 1;
	if (old_dir >= 0)
		host_menu_set_dir(old_dir, 1, 0);
	if (new_dir >= 0)
		host_menu_set_dir(new_dir, 1, 1);
	return 1;
}

static TTF_Font *host_menu_font(int pt)
{
	int i;
	int empty;
	TTF_Font *font;

	if (pt < 11)
		pt = 11;
	if (pt > 72)
		pt = 72;
	if (!g_ttf_ready) {
		if (TTF_Init() != 0) {
			fprintf(stderr, "sword3-sdl: TTF_Init failed: %s\n",
				TTF_GetError());
			g_ttf_ready = -1;
			return NULL;
		}
		g_ttf_ready = 1;
	}
	if (g_ttf_ready < 0)
		return NULL;
	for (i = 0; i < HOST_MENU_FONT_SLOTS; i++) {
		if (g_fonts[i].font && g_fonts[i].pt == pt)
			return g_fonts[i].font;
	}
	empty = -1;
	for (i = 0; i < HOST_MENU_FONT_SLOTS; i++) {
		if (!g_fonts[i].font) {
			empty = i;
			break;
		}
	}
	if (empty < 0) {
		empty = 0;
		TTF_CloseFont(g_fonts[0].font);
		g_fonts[0].font = NULL;
	}
	font = TTF_OpenFont(HOST_MENU_FONT, pt);
	if (!font) {
		fprintf(stderr, "sword3-sdl: TTF_OpenFont %s pt=%d failed: %s\n",
			HOST_MENU_FONT, pt, TTF_GetError());
		return NULL;
	}
	g_fonts[empty].pt = pt;
	g_fonts[empty].font = font;
	if (empty == 0)
		fprintf(stderr, "sword3-sdl: host menu font %s\n", HOST_MENU_FONT);
	return font;
}

static Uint32 host_menu_rgba(SDL_Color c)
{
	return ((Uint32)c.r << 24) | ((Uint32)c.g << 16) |
	       ((Uint32)c.b << 8) | (Uint32)c.a;
}

static void host_menu_text_flush(SDL_Renderer *renderer)
{
	int i;

	for (i = 0; i < HOST_MENU_TEXT_CACHE; i++) {
		if (!g_text[i].tex)
			continue;
		if (renderer && g_text[i].renderer != renderer)
			continue;
		SDL_DestroyTexture(g_text[i].tex);
		g_text[i].tex = NULL;
		g_text[i].text[0] = '\0';
	}
}

static int host_menu_text_size(SDL_Renderer *renderer, const char *s, int pt,
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
	rgba = host_menu_rgba(color);
	for (i = 0; i < HOST_MENU_TEXT_CACHE; i++) {
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
	font = host_menu_font(pt);
	if (!font)
		return 0;
	surf = TTF_RenderUTF8_Blended(font, s, color);
	if (!surf)
		return 0;
	tex = SDL_CreateTextureFromSurface(renderer, surf);
	if (tex)
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	if (w)
		*w = surf->w;
	if (h)
		*h = surf->h;
	if (!tex) {
		SDL_FreeSurface(surf);
		return 0;
	}
	slot = -1;
	for (i = 0; i < HOST_MENU_TEXT_CACHE; i++) {
		if (!g_text[i].tex) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		slot = (g_text_clock++) % HOST_MENU_TEXT_CACHE;
		if (g_text[slot].tex)
			SDL_DestroyTexture(g_text[slot].tex);
	}
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

static void host_menu_text_center(SDL_Renderer *renderer, const char *s, int cx,
				 int cy, int pt, SDL_Color color)
{
	SDL_Texture *tex;
	SDL_Rect dst;
	int w;
	int h;

	if (!host_menu_text_size(renderer, s, pt, color, &w, &h, &tex) || !tex)
		return;
	dst.x = cx - w / 2;
	dst.y = cy - h / 2;
	dst.w = w;
	dst.h = h;
	SDL_RenderCopy(renderer, tex, NULL, &dst);
}

static void host_menu_text_left(SDL_Renderer *renderer, const char *s, int x,
			       int y, int pt, SDL_Color color)
{
	SDL_Texture *tex;
	SDL_Rect dst;
	int w;
	int h;

	if (!host_menu_text_size(renderer, s, pt, color, &w, &h, &tex) || !tex)
		return;
	dst.x = x;
	dst.y = y;
	dst.w = w;
	dst.h = h;
	SDL_RenderCopy(renderer, tex, NULL, &dst);
}

static void host_menu_fill(SDL_Renderer *renderer, SDL_Rect rect, Uint8 r,
			   Uint8 g, Uint8 b, Uint8 a)
{
	SDL_SetRenderDrawColor(renderer, r, g, b, a);
	SDL_RenderFillRect(renderer, &rect);
}

static void host_menu_frame(SDL_Renderer *renderer, SDL_Rect rect, int thick,
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

static int host_sx(int x, int logical_w)
{
	return x * logical_w / HOST_MENU_NATIVE_W;
}

static int host_sy(int y, int logical_h)
{
	return y * logical_h / HOST_MENU_NATIVE_H;
}

static SDL_Surface *host_menu_open_surf(const char *name)
{
	char path[PATH_MAX];
	char exe[PATH_MAX];
	const char *data;
	const char *bundle;
	ssize_t n;
	char *slash;
	SDL_Surface *surf;

	if (!name || !name[0])
		return NULL;

	if (snprintf(path, sizeof(path), "assets/host_menu/%s", name) <
	    (int)sizeof(path)) {
		surf = IMG_Load(path);
		if (surf)
			return surf;
	}

	data = getenv("SWORD3_DATA_DIR");
	if (data && data[0] == '/' &&
	    snprintf(path, sizeof(path), "%s/../assets/host_menu/%s", data,
		     name) < (int)sizeof(path)) {
		surf = IMG_Load(path);
		if (surf)
			return surf;
	}

	n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		exe[n] = 0;
		slash = strrchr(exe, '/');
		if (slash) {
			*slash = 0;
			if (snprintf(path, sizeof(path),
				     "%s/assets/host_menu/%s", exe, name) <
			    (int)sizeof(path)) {
				surf = IMG_Load(path);
				if (surf)
					return surf;
			}
		}
	}

	bundle = getenv("SWORD3_BUNDLE_DIR");
	if (bundle && bundle[0] == '/' &&
	    snprintf(path, sizeof(path), "%s/../../../assets/host_menu/%s",
		     bundle, name) < (int)sizeof(path)) {
		surf = IMG_Load(path);
		if (surf)
			return surf;
	}

	fprintf(stderr, "sword3-sdl: host menu missing %s (%s)\n", name,
		IMG_GetError());
	return NULL;
}

static SDL_Texture *host_menu_load_tex(SDL_Renderer *renderer, const char *name)
{
	SDL_Surface *surf;
	SDL_Texture *tex;

	if (!renderer || !name)
		return NULL;
	surf = host_menu_open_surf(name);
	if (!surf)
		return NULL;
	tex = SDL_CreateTextureFromSurface(renderer, surf);
	SDL_FreeSurface(surf);
	if (!tex)
		return NULL;
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	return tex;
}

static void host_menu_assets_flush(void)
{
	int i;

	if (g_tex_paper)
		SDL_DestroyTexture(g_tex_paper);
	if (g_tex_dark)
		SDL_DestroyTexture(g_tex_dark);
	if (g_tex_back)
		SDL_DestroyTexture(g_tex_back);
	g_tex_paper = NULL;
	g_tex_dark = NULL;
	g_tex_back = NULL;
	for (i = 0; i < HOST_MENU_ACTIONS; i++) {
		if (g_tex_book[i])
			SDL_DestroyTexture(g_tex_book[i]);
		g_tex_book[i] = NULL;
	}
	for (i = 0; i < HOST_MENU_PARTY_N; i++) {
		if (g_tex_face[i])
			SDL_DestroyTexture(g_tex_face[i]);
		g_tex_face[i] = NULL;
		g_face_act[i] = 0;
	}
	g_asset_renderer = NULL;
}

static void host_menu_assets_ensure(SDL_Renderer *renderer)
{
	int i;
	int ok;

	if (g_asset_renderer == renderer)
		return;
	host_menu_assets_flush();
	g_asset_renderer = renderer;
	g_tex_paper = host_menu_load_tex(renderer, "paper.jpg");
	g_tex_dark = host_menu_load_tex(renderer, "dark.jpg");
	g_tex_back = host_menu_load_tex(renderer, "back.png");
	ok = 0;
	for (i = 0; i < HOST_MENU_ACTIONS; i++) {
		g_tex_book[i] = host_menu_load_tex(renderer, g_book_files[i]);
		if (g_tex_book[i])
			ok = 1;
	}
	if (g_tex_paper || g_tex_dark || ok)
		fprintf(stderr, "sword3-sdl: host menu chrome loaded\n");
}

static void host_menu_blit(SDL_Renderer *renderer, SDL_Texture *tex,
			  SDL_Rect dst)
{
	if (!tex)
		return;
	SDL_RenderCopy(renderer, tex, NULL, &dst);
}

static void host_menu_blit_contain(SDL_Renderer *renderer, SDL_Texture *tex,
				  SDL_Rect box)
{
	int tw;
	int th;
	SDL_Rect dst;

	if (!tex || box.w <= 0 || box.h <= 0)
		return;
	if (SDL_QueryTexture(tex, NULL, NULL, &tw, &th) != 0 || tw <= 0 ||
	    th <= 0)
		return;
	if ((long)tw * box.h < (long)th * box.w) {
		dst.h = box.h;
		dst.w = (int)((long)tw * box.h / th);
	} else {
		dst.w = box.w;
		dst.h = (int)((long)th * box.w / tw);
	}
	dst.x = box.x + (box.w - dst.w) / 2;
	dst.y = box.y + (box.h - dst.h) / 2;
	SDL_RenderCopy(renderer, tex, NULL, &dst);
}

static uintptr_t host_menu_tsw_obj(int act)
{
	uintptr_t table;
	uintptr_t buckets;
	uintptr_t node;
	int n;

	if (act <= 0)
		return 0;
	if (!host_menu_guest_ok(HOST_MENU_TSW_ROOT + 0x400, 8))
		return 0;
	table = host_menu_guest_ptr(HOST_MENU_TSW_ROOT + 0x400);
	if (!table || !host_menu_mem_ok(table + 8, 8))
		return 0;
	buckets = host_menu_mem_ptr(table + 8);
	if (!buckets ||
	    !host_menu_mem_ok(buckets + (uintptr_t)(act & 15) * 8, 8))
		return 0;
	node = host_menu_mem_ptr(buckets + (uintptr_t)(act & 15) * 8);
	for (n = 0; node && n < 128; n++) {
		if (!host_menu_mem_ok(node, 0x88))
			break;
		node = host_menu_mem_ptr(node + 0x80);
		if (!node)
			break;
		if (!host_menu_mem_ok(node, 0x88))
			break;
		if (host_menu_mem_i32(node + 0x28) == act)
			return node;
	}
	return 0;
}

static SDL_Surface *host_menu_surface_from_png(const void *data, int size)
{
	const unsigned char *p;
	SDL_RWops *rw;

	if (!data || size < 16)
		return NULL;
	p = (const unsigned char *)data;
	if (p[1] != 'P' || p[2] != 'N' || p[3] != 'G')
		return NULL;
	rw = SDL_RWFromConstMem(data, size);
	if (!rw)
		return NULL;
	return IMG_Load_RW(rw, 1);
}

static SDL_Surface *host_menu_expand_index(const uint8_t *src, int w, int h,
					   int pitch, const uint16_t *pal)
{
	SDL_Surface *copy;
	int x;
	int y;
	uint16_t key;
	uint32_t *dst;

	if (!src || !pal || w < 1 || h < 1 || pitch < w)
		return NULL;
	copy = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
					      SDL_PIXELFORMAT_ARGB8888);
	if (!copy)
		return NULL;
	key = 0;
	if (host_menu_guest_ok(HOST_MENU_TSW_KEY, 2))
		key = (uint16_t)host_menu_mem_u16(HOST_MENU_TSW_KEY);
	if (SDL_LockSurface(copy) != 0) {
		SDL_FreeSurface(copy);
		return NULL;
	}
	for (y = 0; y < h; y++) {
		dst = (uint32_t *)((uint8_t *)copy->pixels + y * copy->pitch);
		for (x = 0; x < w; x++) {
			uint16_t c = pal[src[y * pitch + x]];
			uint8_t r = (uint8_t)(((c >> 10) & 31) * 255 / 31);
			uint8_t g = (uint8_t)(((c >> 5) & 31) * 255 / 31);
			uint8_t b = (uint8_t)((c & 31) * 255 / 31);
			uint8_t a = (c == key) ? 0 : 255;

			dst[x] = SDL_MapRGBA(copy->format, r, g, b, a);
		}
	}
	SDL_UnlockSurface(copy);
	SDL_SetSurfaceBlendMode(copy, SDL_BLENDMODE_BLEND);
	return copy;
}

static SDL_Surface *host_menu_expand_rgb555(const uint16_t *src, int w, int h,
					    int pitch)
{
	SDL_Surface *copy;
	int x;
	int y;
	uint16_t key;
	uint32_t *dst;

	if (!src || w < 1 || h < 1 || pitch < w)
		return NULL;
	copy = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
					      SDL_PIXELFORMAT_ARGB8888);
	if (!copy)
		return NULL;
	key = 0;
	if (host_menu_guest_ok(HOST_MENU_TSW_KEY, 2))
		key = (uint16_t)host_menu_mem_u16(HOST_MENU_TSW_KEY);
	if (SDL_LockSurface(copy) != 0) {
		SDL_FreeSurface(copy);
		return NULL;
	}
	for (y = 0; y < h; y++) {
		dst = (uint32_t *)((uint8_t *)copy->pixels + y * copy->pitch);
		for (x = 0; x < w; x++) {
			uint16_t c = src[y * pitch + x];
			uint8_t r = (uint8_t)(((c >> 10) & 31) * 255 / 31);
			uint8_t g = (uint8_t)(((c >> 5) & 31) * 255 / 31);
			uint8_t b = (uint8_t)((c & 31) * 255 / 31);
			uint8_t a = (c == key) ? 0 : 255;

			dst[x] = SDL_MapRGBA(copy->format, r, g, b, a);
		}
	}
	SDL_UnlockSurface(copy);
	SDL_SetSurfaceBlendMode(copy, SDL_BLENDMODE_BLEND);
	return copy;
}

static uint16_t host_menu_u16le(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t host_menu_map555(const SDL_PixelFormat *fmt, uint16_t c,
				 uint16_t key)
{
	uint8_t r = (uint8_t)(((c >> 10) & 31) * 255 / 31);
	uint8_t g = (uint8_t)(((c >> 5) & 31) * 255 / 31);
	uint8_t b = (uint8_t)((c & 31) * 255 / 31);
	uint8_t a = (c == key) ? 0 : 255;

	return SDL_MapRGBA(fmt, r, g, b, a);
}

static int host_menu_rle_rows(SDL_Surface *copy, int w, int h, int bpp,
			      const uint16_t *pal, uint16_t key,
			      const uint8_t *src, const uint8_t *end)
{
	int y;
	int painted;
	const uint8_t *p;

	painted = 0;
	p = src;
	for (y = 0; y < h && p + 2 <= end; y++) {
		uint16_t row;
		uint16_t row_bytes;
		const uint8_t *row_end;
		uint32_t *dst;
		int x;

		row = host_menu_u16le(p);
		row_bytes = (uint16_t)(row & 0x7fff);
		if (row_bytes == 0)
			break;
		if (row_bytes < 4 || p + row_bytes > end)
			break;
		row_end = p + row_bytes;
		p += 2;
		dst = (uint32_t *)((uint8_t *)copy->pixels + y * copy->pitch);
		x = 0;
		while (p + 2 <= row_end && x < w) {
			uint16_t cmd;
			int n;
			int i;

			cmd = host_menu_u16le(p);
			p += 2;
			if (cmd == 0)
				break;
			n = (int)(cmd & 0x3fff);
			if (n <= 0)
				break;
			if (cmd & 0xc000) {
				x += n;
				continue;
			}
			for (i = 0; i < n && x < w; i++, x++) {
				uint16_t c;

				if (bpp == 8) {
					if (p >= row_end || !pal)
						break;
					c = pal[*p++];
				} else {
					if (p + 2 > row_end)
						break;
					c = host_menu_u16le(p);
					p += 2;
				}
				dst[x] = host_menu_map555(copy->format, c, key);
				if ((dst[x] >> 24) != 0)
					painted++;
			}
		}
		p = row_end;
	}
	return painted;
}

static SDL_Surface *host_menu_expand_tsw_rle(const uint8_t *src, uint32_t nbytes,
					     int hint_w, int hint_h,
					     const uint16_t *pal)
{
	SDL_Surface *copy;
	uint16_t key;
	uint16_t magic;
	uint16_t w;
	uint16_t h;
	uint16_t bpp;
	const uint8_t *body;
	int painted;

	if (!src || nbytes < 4)
		return NULL;
	key = 0;
	if (host_menu_guest_ok(HOST_MENU_TSW_KEY, 2))
		key = (uint16_t)host_menu_mem_u16(HOST_MENU_TSW_KEY);
	magic = 0;
	if (host_menu_guest_ok(HOST_MENU_TSW_MAGIC, 2))
		magic = (uint16_t)host_menu_mem_u16(HOST_MENU_TSW_MAGIC);
	w = 0;
	h = 0;
	bpp = 8;
	body = src;
	if (nbytes >= 8) {
		uint16_t hw = host_menu_u16le(src + 2);
		uint16_t hh = host_menu_u16le(src + 4);
		uint16_t hb = (uint16_t)(host_menu_u16le(src + 6) & 0x7fff);
		int hdr = 0;

		if ((hb == 8 || hb == 16) && hw >= 1 && hh >= 1 && hw <= 4096 &&
		    hh <= 4096)
			hdr = 1;
		if (magic && host_menu_u16le(src) == magic)
			hdr = 1;
		if (hdr && (hb == 8 || hb == 16) && hw >= 1 && hh >= 1 &&
		    hw <= 4096 && hh <= 4096) {
			w = hw;
			h = hh;
			bpp = hb;
			body = src + 8;
		}
	}
	if (w == 0 || h == 0) {
		if (hint_w < 1 || hint_h < 1 || hint_w > 4096 || hint_h > 4096)
			return NULL;
		w = (uint16_t)hint_w;
		h = (uint16_t)hint_h;
		bpp = pal ? 8 : 16;
		body = src;
	}
	if (bpp == 8 && !pal)
		return NULL;
	copy = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32,
					      SDL_PIXELFORMAT_ARGB8888);
	if (!copy)
		return NULL;
	if (SDL_LockSurface(copy) != 0) {
		SDL_FreeSurface(copy);
		return NULL;
	}
	memset(copy->pixels, 0, (size_t)copy->h * (size_t)copy->pitch);
	painted = host_menu_rle_rows(copy, w, h, bpp, pal, key, body,
				     src + nbytes);
	if (painted <= 0 && body != src) {
		memset(copy->pixels, 0, (size_t)copy->h * (size_t)copy->pitch);
		painted = host_menu_rle_rows(copy, w, h, bpp, pal, key, src,
					     src + nbytes);
	}
	SDL_UnlockSurface(copy);
	if (painted <= 0) {
		SDL_FreeSurface(copy);
		return NULL;
	}
	SDL_SetSurfaceBlendMode(copy, SDL_BLENDMODE_BLEND);
	return copy;
}

static SDL_Surface *host_menu_surface_from_tsw(const void *data, int size)
{
	const unsigned char *p;
	unsigned char dest[64];
	int ok;
	SDL_Surface *surf;
	SDL_Surface *copy;
	void *pixels;
	void *palette;
	uint16_t w;
	uint16_t h;
	uint32_t nbytes;

	if (!data || size < 16)
		return NULL;
	p = (const unsigned char *)data;
	memset(dest, 0, sizeof(dest));
	if (p[0] == 't' && p[1] == 's' && p[2] == 'w' && p[3] == 'p')
		ok = ((int (*)(const void *, int, void *))(
			uintptr_t)HOST_MENU_TSW_TSWP)(data, size, dest);
	else if (p[0] == 'H' && p[1] == 'G' && p[2] == 'A' && p[3] == '3')
		ok = ((int (*)(const void *, int, void *))(
			uintptr_t)HOST_MENU_TSW_HGA3)(data, size, dest);
	else
		return NULL;
	memcpy(&surf, dest + 0x20, sizeof(surf));
	memcpy(&pixels, dest, sizeof(pixels));
	memcpy(&palette, dest + 0x10, sizeof(palette));
	memcpy(&w, dest + 0x18, sizeof(w));
	memcpy(&h, dest + 0x1a, sizeof(h));
	memcpy(&nbytes, dest + 0x1c, sizeof(nbytes));
	copy = NULL;
	if (ok && surf && surf->w > 0 && surf->h > 0 && surf->w <= 4096 &&
	    surf->h <= 4096 && surf->pixels)
		copy = SDL_ConvertSurfaceFormat(surf, SDL_PIXELFORMAT_ARGB8888,
						0);
	if (!copy && ok && pixels && nbytes >= 4) {
		const uint16_t *pal = NULL;
		const uint8_t *raw = (const uint8_t *)pixels;

		if (palette && host_menu_mem_ok((uintptr_t)palette, 512))
			pal = (const uint16_t *)palette;
		fprintf(stderr,
			"sword3-sdl: tswp rle hdr %04x %04x %04x %04x n=%u %ux%u pal=%p flag=%u\n",
			host_menu_u16le(raw),
			nbytes >= 4 ? host_menu_u16le(raw + 2) : 0,
			nbytes >= 6 ? host_menu_u16le(raw + 4) : 0,
			nbytes >= 8 ? host_menu_u16le(raw + 6) : 0, nbytes,
			(unsigned)w, (unsigned)h, palette,
			(unsigned)dest[0x28]);
		copy = host_menu_expand_tsw_rle(raw, nbytes, w, h, pal);
	}
	if (!copy && ok && pixels && w > 0 && h > 0 && w <= 4096 && h <= 4096) {
		if (palette && host_menu_mem_ok((uintptr_t)palette, 512) &&
		    nbytes >= (uint32_t)w * (uint32_t)h)
			copy = host_menu_expand_index(
				(const uint8_t *)pixels, w, h, w,
				(const uint16_t *)palette);
		else if (nbytes >= (uint32_t)w * (uint32_t)h * 2u)
			copy = host_menu_expand_rgb555(
				(const uint16_t *)pixels, w, h, w);
	}
	if (!copy)
		fprintf(stderr,
			"sword3-sdl: tswp decode ok=%d %ux%u nbytes=%u pix=%p pal=%p surf=%p\n",
			ok, (unsigned)w, (unsigned)h, nbytes, pixels, palette,
			(void *)surf);
	if (pixels && pixels != (surf ? surf->pixels : NULL))
		((void (*)(uintptr_t))(uintptr_t)HOST_MENU_TSW_FREE)(
			(uintptr_t)pixels);
	return copy;
}

static SDL_Surface *host_menu_tsw_surface(int act, int frame)
{
	uintptr_t obj;
	uintptr_t sizes;
	uintptr_t data;
	int nframe;
	int size;
	const unsigned char *p;
	SDL_Surface *surf;

	obj = host_menu_tsw_obj(act);
	if (!obj)
		return NULL;
	nframe = host_menu_mem_i32(obj);
	if (frame < 0 || frame >= nframe || nframe > 64)
		return NULL;
	sizes = host_menu_mem_ptr(obj + 0x48);
	if (!sizes || !host_menu_mem_ok(sizes + (uintptr_t)frame * 4, 4))
		return NULL;
	size = host_menu_mem_i32(sizes + (uintptr_t)frame * 4);
	if (size < 16 || size > 8 * 1024 * 1024)
		return NULL;
	data = ((uintptr_t (*)(uintptr_t, int))(uintptr_t)HOST_MENU_TSW_GET)(
		obj, frame);
	if (!data || !host_menu_heap_ok(data, 16))
		return NULL;
	p = (const unsigned char *)(uintptr_t)data;
	surf = host_menu_surface_from_png(p, size);
	if (!surf)
		surf = host_menu_surface_from_tsw(p, size);
	if (!surf)
		fprintf(stderr,
			"sword3-sdl: tsw act=%d frame=%d magic=%02x%02x%02x%02x size=%d\n",
			act, frame, p[0], p[1], p[2], p[3], size);
	((void (*)(uintptr_t))(uintptr_t)HOST_MENU_TSW_FREE)(data);
	return surf;
}

static SDL_Texture *host_menu_tex_from_surf(SDL_Renderer *renderer,
					    SDL_Surface *surf)
{
	SDL_Texture *tex;

	if (!renderer || !surf)
		return NULL;
	tex = SDL_CreateTextureFromSurface(renderer, surf);
	SDL_FreeSurface(surf);
	if (!tex)
		return NULL;
	SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	return tex;
}

static SDL_Texture *host_menu_face_tex(SDL_Renderer *renderer, int slot)
{
	const struct host_menu_actor *a;
	SDL_Surface *surf;
	SDL_Texture *tex;
	int frame;
	int tw;
	int th;

	if (!renderer || slot < 0 || slot >= HOST_MENU_PARTY_N)
		return NULL;
	a = &g_party[slot];
	if (!a->used || a->act <= 0)
		return g_tex_face[slot];
	if (g_face_act[slot] == a->act)
		return g_tex_face[slot];
	if (g_tex_face[slot]) {
		SDL_DestroyTexture(g_tex_face[slot]);
		g_tex_face[slot] = NULL;
	}
	g_face_act[slot] = a->act;
	surf = NULL;
	for (frame = 0; frame < 3 && !surf; frame++)
		surf = host_menu_tsw_surface(a->act, frame);
	tex = host_menu_tex_from_surf(renderer, surf);
	g_tex_face[slot] = tex;
	if (!tex && !host_menu_tsw_obj(a->act)) {
		g_face_act[slot] = 0;
		return NULL;
	}
	tw = 0;
	th = 0;
	if (tex)
		SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
	fprintf(stderr, "sword3-sdl: face slot=%d act=%d %s %dx%d obj=%llx\n",
		slot, a->act, tex ? "ok" : "miss", tw, th,
		(unsigned long long)host_menu_tsw_obj(a->act));
	return tex;
}

static void host_menu_vgrad(SDL_Renderer *renderer, SDL_Rect rect,
			    Uint8 r0, Uint8 g0, Uint8 b0, Uint8 r1, Uint8 g1,
			    Uint8 b1, Uint8 a)
{
	int y;
	int h;
	SDL_Rect line;

	h = rect.h;
	if (h <= 0 || rect.w <= 0)
		return;
	for (y = 0; y < h; y += 2) {
		line.x = rect.x;
		line.y = rect.y + y;
		line.w = rect.w;
		line.h = (y + 2 <= h) ? 2 : 1;
		SDL_SetRenderDrawColor(renderer,
				       (Uint8)((r0 * (h - y) + r1 * y) / h),
				       (Uint8)((g0 * (h - y) + g1 * y) / h),
				       (Uint8)((b0 * (h - y) + b1 * y) / h), a);
		SDL_RenderFillRect(renderer, &line);
	}
}

static void host_menu_plaque(SDL_Renderer *renderer, SDL_Rect box, int selected)
{
	SDL_Rect inner;
	SDL_Rect hi;

	host_menu_fill(renderer, box, 48, 28, 8, 255);
	inner = box;
	inner.x += 1;
	inner.y += 1;
	inner.w -= 2;
	inner.h -= 2;
	if (inner.w <= 0 || inner.h <= 0)
		return;
	if (selected)
		host_menu_vgrad(renderer, inner, 232, 204, 120, 176, 136, 48,
				255);
	else
		host_menu_vgrad(renderer, inner, 132, 108, 64, 72, 56, 32, 255);
	host_menu_frame(renderer, inner, 1, selected ? 237 : 160,
			selected ? 221 : 130, selected ? 120 : 70, 255);
	hi = inner;
	hi.x += 2;
	hi.y += 1;
	hi.w -= 4;
	hi.h = 1;
	host_menu_fill(renderer, hi, 255, 236, 180, selected ? 180 : 80);
}

static void host_menu_bar(SDL_Renderer *renderer, SDL_Rect track, Uint8 r,
			  Uint8 g, Uint8 b, int cur, int max)
{
	SDL_Rect fill;
	int w;

	host_menu_fill(renderer, track, 24, 18, 12, 255);
	host_menu_frame(renderer, track, 1, 80, 56, 24, 255);
	if (max <= 0)
		return;
	if (cur < 0)
		cur = 0;
	if (cur > max)
		cur = max;
	w = (int)((long)track.w * cur / max);
	if (w <= 0)
		return;
	fill = track;
	fill.x += 1;
	fill.y += 1;
	fill.h -= 2;
	fill.w = w - 2;
	if (fill.w < 1)
		fill.w = 1;
	if (fill.w > track.w - 2)
		fill.w = track.w - 2;
	if (fill.h < 1)
		return;
	host_menu_fill(renderer, fill, r, g, b, 230);
}

static void host_menu_scroll(SDL_Renderer *renderer, SDL_Rect well, int start,
			    int vis, int total)
{
	SDL_Rect rail;
	SDL_Rect thumb;
	SDL_Rect cap;
	int span;
	int travel;

	if (well.h < 24 || well.w < 20)
		return;
	rail.w = 6;
	rail.h = well.h - 16;
	rail.x = well.x + well.w - rail.w - 4;
	rail.y = well.y + 8;
	host_menu_fill(renderer, rail, 48, 36, 16, 255);
	host_menu_frame(renderer, rail, 1, 180, 148, 64, 220);
	cap = rail;
	cap.h = 6;
	host_menu_fill(renderer, cap, 212, 176, 88, 255);
	cap.y = rail.y + rail.h - 6;
	host_menu_fill(renderer, cap, 212, 176, 88, 255);
	span = rail.h - 12;
	if (span < 12)
		span = 12;
	thumb = rail;
	thumb.x += 1;
	thumb.w -= 2;
	if (total > vis && vis > 0) {
		thumb.h = span * vis / total;
		if (thumb.h < 12)
			thumb.h = 12;
		if (thumb.h > span)
			thumb.h = span;
		travel = span - thumb.h;
		thumb.y = rail.y + 6 + travel * start / (total - vis);
	} else {
		thumb.h = span / 5;
		if (thumb.h < 12)
			thumb.h = 12;
		thumb.y = rail.y + 6;
	}
	host_menu_fill(renderer, thumb, 196, 164, 72, 255);
}

static void host_menu_well(SDL_Renderer *renderer, SDL_Rect well)
{
	host_menu_fill(renderer, well, 8, 12, 10, 230);
	host_menu_frame(renderer, well, 2, 180, 148, 64, 220);
}

static void host_menu_draw_left(SDL_Renderer *renderer, int logical_w,
			       int logical_h, int pt, int pt_small)
{
	SDL_Rect left;
	SDL_Rect card;
	SDL_Rect portrait;
	SDL_Rect track;
	int i;
	int bar_y;
	int cur[3];
	int maxv[3];
	char name[40];
	char lvline[40];
	char barline[32];
	const struct host_menu_actor *act;
	const char *labs[3] = { "命", "灵", "体" };
	const SDL_Color *ink[3] = { &g_ink_hp, &g_ink_mp, &g_ink_sp };
	Uint8 br[3] = { 176, 48, 48 };
	Uint8 bg[3] = { 48, 140, 64 };
	Uint8 bb[3] = { 48, 96, 176 };

	act = host_menu_actor();
	if (act && act->name[0])
		snprintf(name, sizeof(name), "%s", act->name);
	else
		snprintf(name, sizeof(name), "%s", "—");
	if (act && act->used) {
		if (act->level_max > 0)
			snprintf(lvline, sizeof(lvline), "%d 级 / %d",
				 act->level, act->level_max);
		else
			snprintf(lvline, sizeof(lvline), "%d 级 / —",
				 act->level);
		cur[0] = act->hp;
		cur[1] = act->mp;
		cur[2] = act->sp;
		maxv[0] = act->hp_max;
		maxv[1] = act->mp_max;
		maxv[2] = act->sp_max;
	} else {
		snprintf(lvline, sizeof(lvline), "%s", "— 级 / —");
		cur[0] = cur[1] = cur[2] = 0;
		maxv[0] = maxv[1] = maxv[2] = 0;
	}

	left.x = 0;
	left.y = 0;
	left.w = host_sx(HOST_MENU_SPLIT_X, logical_w);
	left.h = logical_h;
	if (g_tex_paper)
		host_menu_blit(renderer, g_tex_paper, left);
	else
		host_menu_vgrad(renderer, left, 210, 190, 140, 168, 142, 88,
				255);

	card.x = host_sx(8, logical_w);
	card.y = host_sy(10, logical_h);
	card.w = left.w - host_sx(16, logical_w);
	card.h = host_sy(196, logical_h);
	host_menu_frame(renderer, card, 2, 196, 164, 72, 255);
	if (g_layer == HOST_MENU_LAYER_PICK || g_layer == HOST_MENU_LAYER_EQUIP ||
	    (g_layer == HOST_MENU_LAYER_INNER &&
	     g_tab == HOST_MENU_TAB_EQUIP))
		host_menu_frame(renderer, card, 3, g_ink_gold.r, g_ink_gold.g,
				g_ink_gold.b, 255);

	portrait.x = card.x + host_sx(8, logical_w);
	portrait.y = card.y + host_sy(10, logical_h);
	portrait.w = host_sx(118, logical_w);
	portrait.h = host_sy(140, logical_h);
	host_menu_fill(renderer, portrait, 32, 22, 12, 255);
	if (act) {
		SDL_Texture *face;

		face = host_menu_face_tex(renderer, (int)(act - g_party));
		if (face)
			host_menu_blit_contain(renderer, face, portrait);
	}
	host_menu_frame(renderer, portrait, 2, 196, 164, 72, 255);

	host_menu_text_left(renderer, name,
			    portrait.x + portrait.w + host_sx(10, logical_w),
			    portrait.y + host_sy(6, logical_h), pt,
			    g_ink_paper);
	host_menu_text_left(renderer, lvline,
			    portrait.x + portrait.w + host_sx(10, logical_w),
			    portrait.y + host_sy(34, logical_h), pt_small,
			    g_ink_paper);

	bar_y = portrait.y + host_sy(68, logical_h);
	for (i = 0; i < 3; i++) {
		host_menu_text_left(renderer, labs[i],
				    portrait.x + portrait.w +
					    host_sx(8, logical_w),
				    bar_y + i * host_sy(24, logical_h),
				    pt_small, *ink[i]);
		track.x = portrait.x + portrait.w + host_sx(32, logical_w);
		track.y = bar_y + i * host_sy(24, logical_h) + 2;
		track.w = card.x + card.w - track.x - host_sx(8, logical_w);
		track.h = host_sy(16, logical_h);
		if (track.w < 8)
			continue;
		host_menu_bar(renderer, track, br[i], bg[i], bb[i], cur[i],
			      maxv[i]);
		if (maxv[i] > 0)
			snprintf(barline, sizeof(barline), "%d / %d", cur[i],
				 maxv[i]);
		else
			snprintf(barline, sizeof(barline), "%s", "— / —");
		host_menu_text_left(renderer, barline,
				    track.x + host_sx(4, logical_w),
				    track.y - 1, pt_small, g_ink_title);
	}
}

static void host_menu_draw_tabs(SDL_Renderer *renderer, int logical_w,
			       int logical_h, int pt)
{
	SDL_Rect tab;
	int i;
	int x0;
	int y0;
	int tw;
	int th;
	int gap;
	int end;

	x0 = host_sx(290, logical_w);
	y0 = host_sy(16, logical_h);
	th = host_sy(44, logical_h);
	gap = host_sx(4, logical_w);
	end = host_sx(848, logical_w);
	tw = (end - x0 - gap * (HOST_MENU_TABS - 1)) / HOST_MENU_TABS;
	for (i = 0; i < HOST_MENU_TABS; i++) {
		tab.x = x0 + i * (tw + gap);
		tab.y = y0;
		tab.w = tw;
		tab.h = th;
		host_menu_plaque(renderer, tab, g_tab == i);
		host_menu_text_center(renderer, g_tab_text[i],
				      tab.x + tab.w / 2, tab.y + tab.h / 2, pt,
				      g_tab == i ? g_ink_tab_on : g_ink_hint);
		if (g_layer == HOST_MENU_LAYER_TABS && g_tab == i) {
			tab.x -= 3;
			tab.y -= 3;
			tab.w += 6;
			tab.h += 6;
			host_menu_frame(renderer, tab, 3, g_ink_gold.r,
					g_ink_gold.g, g_ink_gold.b, 255);
		}
	}
}

static void host_menu_draw_back(SDL_Renderer *renderer, int logical_w,
			       int logical_h)
{
	SDL_Rect box;

	box.x = host_sx(868, logical_w);
	box.y = host_sy(76, logical_h);
	box.w = host_sx(88, logical_w);
	box.h = host_sy(88, logical_h);
	if (g_tex_back)
		host_menu_blit(renderer, g_tex_back, box);
	else {
		host_menu_plaque(renderer, box, 0);
		host_menu_text_center(renderer, "←", box.x + box.w / 2,
				      box.y + box.h / 2, 22, g_ink_gold);
	}
}

static void host_menu_draw_combat(SDL_Renderer *renderer, int logical_w,
				 int logical_h, int pt_small)
{
	SDL_Rect box;
	int i;
	int x0;
	int y0;
	int vals[3];
	char num[16];
	const struct host_menu_actor *act;

	act = host_menu_actor();
	vals[0] = act ? act->atk : 0;
	vals[1] = act ? act->def : 0;
	vals[2] = act ? act->agi : 0;
	x0 = host_sx(HOST_MENU_SPLIT_X + 12, logical_w);
	y0 = host_sy(68, logical_h);
	box.x = x0;
	box.y = y0;
	box.w = host_sx(88, logical_w);
	box.h = host_sy(34, logical_h);
	host_menu_plaque(renderer, box, 0);
	host_menu_text_center(renderer,
			      (act && act->name[0]) ? act->name : "—",
			      box.x + box.w / 2, box.y + box.h / 2, pt_small,
			      g_ink_body);
	for (i = 0; i < 3; i++) {
		box.x = x0 + host_sx(96, logical_w) +
			i * host_sx(118, logical_w);
		box.w = host_sx(110, logical_w);
		host_menu_plaque(renderer, box, 0);
		host_menu_text_left(renderer, g_combat[i],
				    box.x + host_sx(8, logical_w),
				    box.y + box.h / 2 - pt_small / 2, pt_small,
				    g_ink_body);
		if (act && act->used)
			snprintf(num, sizeof(num), "%d", vals[i]);
		else
			snprintf(num, sizeof(num), "%s", "—");
		host_menu_text_center(renderer, num,
				      box.x + box.w - host_sx(22, logical_w),
				      box.y + box.h / 2, pt_small, g_ink_hint);
	}
}

static void host_menu_draw_items(SDL_Renderer *renderer, int logical_w,
				int logical_h, int pt, int pt_small)
{
	SDL_Rect row;
	SDL_Rect well;
	SDL_Rect hit;
	SDL_Rect desc;
	int i;
	int on;
	int dim;
	int rh;
	int rows;
	int view_n;
	int start;
	int idx;
	int count;
	int view[HOST_MENU_BAG_N];
	char line[48];
	const SDL_Color *ink;
	const struct host_menu_item *it;
	const struct host_menu_item *sel;

	host_menu_clamp_item_sel();
	view_n = host_menu_item_view(view, HOST_MENU_BAG_N);
	for (i = 0; i < HOST_MENU_ITEM_ACT; i++) {
		row.x = host_sx(g_item_act_nx[i], logical_w);
		row.y = host_sy(HOST_MENU_ITEM_ACT_NY, logical_h);
		row.w = host_sx(HOST_MENU_ITEM_ACT_NW, logical_w);
		row.h = host_sy(HOST_MENU_ITEM_ACT_NH, logical_h);
		on = (g_layer == HOST_MENU_LAYER_INNER && g_item_focus == i);
		host_menu_plaque(renderer, row, on);
		host_menu_text_center(renderer, g_item_act[i],
				      row.x + row.w / 2, row.y + row.h / 2,
				      pt_small, on ? g_ink_tab_on : g_ink_body);
		if (on) {
			row.x -= 3;
			row.y -= 3;
			row.w += 6;
			row.h += 6;
			host_menu_frame(renderer, row, 3, g_ink_gold.r,
					g_ink_gold.g, g_ink_gold.b, 255);
		}
	}
	for (i = 0; i < HOST_MENU_ITEM_CAT; i++) {
		row.x = host_sx(g_item_cat_nx[i], logical_w);
		row.y = host_sy(HOST_MENU_ITEM_CAT_NY, logical_h);
		row.w = host_sx(HOST_MENU_ITEM_CAT_NW, logical_w);
		row.h = host_sy(HOST_MENU_ITEM_CAT_NH, logical_h);
		on = (g_layer == HOST_MENU_LAYER_INNER && g_item_cat == i);
		host_menu_plaque(renderer, row, on);
		host_menu_text_center(renderer, g_item_cat_text[i],
				      row.x + row.w / 2, row.y + row.h / 2,
				      pt_small, on ? g_ink_tab_on : g_ink_body);
		if (on) {
			row.x -= 3;
			row.y -= 3;
			row.w += 6;
			row.h += 6;
			host_menu_frame(renderer, row, 3, g_ink_gold.r,
					g_ink_gold.g, g_ink_gold.b, 255);
		}
	}
	well.x = host_sx(318, logical_w);
	well.y = host_sy(HOST_MENU_ITEM_WELL_NY, logical_h);
	well.w = host_sx(548, logical_w);
	well.h = logical_h - well.y - host_sy(16, logical_h);
	host_menu_well(renderer, well);
	sel = NULL;
	if (view_n > 0 && g_item_sel >= 0 && g_item_sel < view_n)
		sel = &g_bag[view[g_item_sel]];
	desc = well;
	if ((g_item_note[0] || (sel && (sel->help[0] || sel->info[0]))) &&
	    well.h > host_sy(56, logical_h)) {
		desc.h = host_sy(40, logical_h);
		desc.y = well.y + well.h - desc.h;
		well.h -= desc.h;
		host_menu_fill(renderer, desc, 16, 12, 8, 230);
		host_menu_text_left(renderer,
				    g_item_note[0] ?
					    g_item_note :
					    (sel->help[0] ? sel->help :
							     sel->info),
				    desc.x + host_sx(12, logical_w),
				    desc.y + desc.h / 2 - pt_small / 2,
				    pt_small,
				    g_item_note[0] ? g_ink_gold : g_ink_hint);
	}
	if (view_n <= 0) {
		host_menu_scroll(renderer, well, 0, 1, 1);
		host_menu_text_center(renderer, "没有物品", well.x + well.w / 2,
				      well.y + well.h / 2, pt, g_ink_hint);
		return;
	}
	rh = host_sy(28, logical_h);
	if (rh < 18)
		rh = 18;
	rows = (well.h - host_sy(12, logical_h)) / rh;
	if (rows < 1)
		rows = 1;
	if (rows > view_n)
		rows = view_n;
	start = g_item_sel - rows / 2;
	if (start < 0)
		start = 0;
	if (start + rows > view_n)
		start = view_n - rows;
	host_menu_scroll(renderer, well, start, rows, view_n);
	for (i = 0; i < rows; i++) {
		idx = view[start + i];
		it = &g_bag[idx];
		count = (g_item_cat == 0) ? it->count_new : it->count;
		if (count > 1)
			snprintf(line, sizeof(line), "%s  x%d",
				 it->name[0] ? it->name : "—", count);
		else
			snprintf(line, sizeof(line), "%s",
				 it->name[0] ? it->name : "—");
		hit.x = well.x + host_sx(8, logical_w);
		hit.y = well.y + host_sy(6, logical_h) + i * rh;
		hit.w = well.w - host_sx(24, logical_w);
		hit.h = rh - 2;
		on = (g_layer == HOST_MENU_LAYER_INNER &&
		      g_item_sel == start + i);
		dim = host_menu_item_dimmed(it);
		if (on)
			host_menu_frame(renderer, hit, 2, g_ink_gold.r,
					g_ink_gold.g, g_ink_gold.b, 255);
		ink = dim ? &g_ink_dim : (on ? &g_ink_gold : &g_ink_body);
		host_menu_text_left(renderer, line,
				    hit.x + host_sx(8, logical_w),
				    hit.y + hit.h / 2 - pt_small / 2, pt_small,
				    *ink);
	}
}

static void host_menu_draw_equip(SDL_Renderer *renderer, int logical_w,
				int logical_h, int pt, int pt_small)
{
	SDL_Rect list;
	SDL_Rect row;
	SDL_Rect well;
	int i;
	int rh;
	int on;

	(void)pt;
	host_menu_draw_combat(renderer, logical_w, logical_h, pt_small);
	list.x = host_sx(HOST_MENU_SPLIT_X + 12, logical_w);
	list.y = host_sy(108, logical_h);
	list.w = host_sx(248, logical_w);
	list.h = host_sy(420, logical_h);
	host_menu_well(renderer, list);
	rh = (list.h - host_sy(16, logical_h)) / HOST_MENU_EQUIP_N;
	for (i = 0; i < HOST_MENU_EQUIP_N; i++) {
		row.x = list.x + host_sx(8, logical_w);
		row.y = list.y + host_sy(8, logical_h) + i * rh;
		row.w = list.w - host_sx(16, logical_w);
		row.h = rh - host_sy(4, logical_h);
		on = (g_layer == HOST_MENU_LAYER_INNER && g_equip_focus == i);
		host_menu_fill(renderer, row, on ? 88 : 28, on ? 68 : 32,
			       on ? 28 : 22, 200);
		if (on)
			host_menu_frame(renderer, row, 2, g_ink_gold.r,
					g_ink_gold.g, g_ink_gold.b, 255);
		host_menu_text_left(renderer, g_equip_slot[i],
				    row.x + host_sx(8, logical_w),
				    row.y + row.h / 2 - pt_small / 2, pt_small,
				    g_ink_body);
		host_menu_text_center(renderer,
				      (g_equip[i].used && g_equip[i].name[0]) ?
					      g_equip[i].name :
					      "无",
				      row.x + row.w - host_sx(24, logical_w),
				      row.y + row.h / 2, pt_small,
				      g_equip[i].used ? g_ink_body :
							g_ink_hint);
	}
	well.x = list.x + list.w + host_sx(8, logical_w);
	well.y = list.y;
	well.w = logical_w - well.x - host_sx(24, logical_w);
	well.h = host_sy(300, logical_h);
	if (well.w > host_sx(40, logical_w)) {
		const struct host_menu_item *worn;
		int y;
		int i;
		int val;
		char line[48];

		host_menu_well(renderer, well);
		worn = &g_equip[g_equip_focus];
		if (worn->used && worn->name[0])
			host_menu_text_left(renderer, worn->name,
					    well.x + host_sx(10, logical_w),
					    well.y + host_sy(8, logical_h),
					    pt_small, g_ink_gold);
		else
			host_menu_text_left(renderer, "此栏没有装备",
					    well.x + host_sx(10, logical_w),
					    well.y + host_sy(8, logical_h),
					    pt_small, g_ink_hint);
		y = well.y + host_sy(36, logical_h);
		for (i = 0; i < HOST_MENU_EQUIP_STAT_N; i++) {
			val = worn->used ?
				      host_menu_item_add(worn->temp,
							 g_equip_stat_field[i]) :
				      0;
			if (i >= 3 && val == 0)
				continue;
			if (val)
				snprintf(line, sizeof(line), "%s  %+d",
					 g_equip_stat_name[i], val);
			else
				snprintf(line, sizeof(line), "%s  —",
					 g_equip_stat_name[i]);
			host_menu_text_left(renderer, line,
					    well.x + host_sx(10, logical_w),
					    y, pt_small, g_ink_body);
			y += host_sy(24, logical_h);
		}
		if (worn->help[0])
			host_menu_text_left(renderer, worn->help,
					    well.x + host_sx(10, logical_w),
					    y + host_sy(8, logical_h),
					    pt_small, g_ink_hint);
		if (g_item_note[0])
			host_menu_text_center(renderer, g_item_note,
					      well.x + well.w / 2,
					      well.y + well.h -
						      host_sy(40, logical_h),
					      pt_small, g_ink_gold);
		host_menu_text_center(renderer, "左右换人  A 更换",
				      well.x + well.w / 2,
				      well.y + well.h - host_sy(18, logical_h),
				      pt_small, g_ink_hint);
	}
}

static void host_menu_draw_skill(SDL_Renderer *renderer, int logical_w,
				int logical_h, int pt, int pt_small)
{
	SDL_Rect list;
	SDL_Rect desc;
	SDL_Rect row;
	int i;
	int rh;
	int on;
	char effect[64];
	char target[64];
	const struct host_menu_item *sk;

	list.x = host_sx(HOST_MENU_SPLIT_X + 16, logical_w);
	list.y = host_sy(78, logical_h);
	list.w = logical_w - list.x - host_sx(24, logical_w);
	list.h = host_sy(500, logical_h);
	if (list.y + list.h > logical_h - host_sy(90, logical_h))
		list.h = logical_h - list.y - host_sy(90, logical_h);
	host_menu_well(renderer, list);
	if (g_skill_n <= 0)
		host_menu_text_center(renderer, "没有奇术", list.x + list.w / 2,
				      list.y + list.h / 2, pt, g_ink_hint);
	else {
		rh = (list.h - host_sy(12, logical_h)) / g_skill_n;
		if (rh > host_sy(36, logical_h))
			rh = host_sy(36, logical_h);
		if (rh < host_sy(22, logical_h))
			rh = host_sy(22, logical_h);
		for (i = 0; i < g_skill_n; i++) {
			row.x = list.x + host_sx(8, logical_w);
			row.y = list.y + host_sy(6, logical_h) + i * rh;
			row.w = list.w - host_sx(16, logical_w);
			row.h = rh - 2;
			on = (g_layer == HOST_MENU_LAYER_INNER &&
			      g_skill_focus == i);
			if (on)
				host_menu_frame(renderer, row, 2, g_ink_gold.r,
						g_ink_gold.g, g_ink_gold.b,
						255);
			host_menu_text_left(renderer,
					    g_skills[i].name[0] ?
						    g_skills[i].name :
						    "—",
					    row.x + host_sx(8, logical_w),
					    row.y + row.h / 2 - pt_small / 2,
					    pt_small,
					    on ? g_ink_gold : g_ink_body);
		}
	}
	desc.x = list.x;
	desc.y = list.y + list.h + host_sy(8, logical_h);
	desc.w = list.w;
	desc.h = logical_h - desc.y - host_sy(12, logical_h);
	if (desc.h > host_sy(36, logical_h)) {
		sk = (g_skill_n > 0 && g_skill_focus >= 0 &&
		      g_skill_focus < g_skill_n) ?
			     &g_skills[g_skill_focus] :
			     NULL;
		if (sk && sk->help[0])
			snprintf(effect, sizeof(effect), "效果  %s", sk->help);
		else
			snprintf(effect, sizeof(effect), "%s", "效果  —");
		if (sk && sk->info[0])
			snprintf(target, sizeof(target), "对象  %s", sk->info);
		else
			snprintf(target, sizeof(target), "%s", "对象  —");
		host_menu_well(renderer, desc);
		host_menu_text_left(renderer, effect,
				    desc.x + host_sx(12, logical_w),
				    desc.y + host_sy(8, logical_h), pt_small,
				    g_ink_hint);
		host_menu_text_left(renderer, target,
				    desc.x + host_sx(12, logical_w) +
					    desc.w / 2,
				    desc.y + host_sy(8, logical_h), pt_small,
				    g_ink_hint);
	}
}

static void host_menu_draw_status(SDL_Renderer *renderer, int logical_w,
				 int logical_h, int pt_small)
{
	SDL_Rect well;
	SDL_Rect row;
	int i;
	int x0;
	int y0;
	int rh;
	int vals[HOST_MENU_STAT_N];
	char num[24];
	char resist[12];
	const struct host_menu_actor *act;

	act = host_menu_actor();
	vals[0] = act ? act->level : 0;
	vals[1] = act ? act->exp : 0;
	vals[2] = act ? act->str : 0;
	vals[3] = act ? act->sta : 0;
	vals[4] = act ? act->wis : 0;
	vals[5] = act ? act->agi : 0;
	vals[6] = 0;

	host_menu_draw_combat(renderer, logical_w, logical_h, pt_small);
	x0 = host_sx(HOST_MENU_SPLIT_X + 20, logical_w);
	y0 = host_sy(112, logical_h);
	for (i = 0; i < HOST_MENU_STAT_N; i++) {
		host_menu_text_left(renderer, g_stat_row[i], x0,
				    y0 + i * host_sy(28, logical_h), pt_small,
				    g_ink_body);
		if (!act || !act->used)
			snprintf(num, sizeof(num), "%s", "—");
		else if (i == 1 && act->exp_need > 0)
			snprintf(num, sizeof(num), "%d / %d", act->exp,
				 act->exp_need);
		else if (i == 6 && (act->sk_exp || act->sk_next))
			snprintf(num, sizeof(num), "%d / %d", act->sk_exp,
				 act->sk_next);
		else if (i == 6)
			snprintf(num, sizeof(num), "%s", "—");
		else
			snprintf(num, sizeof(num), "%d", vals[i]);
		host_menu_text_left(renderer, num,
				    x0 + host_sx(120, logical_w),
				    y0 + i * host_sy(28, logical_h), pt_small,
				    g_ink_hint);
	}
	host_menu_text_left(renderer, "抗力", x0,
			    y0 + host_sy(210, logical_h), pt_small, g_ink_gold);
	for (i = 0; i < HOST_MENU_RESIST_N; i++) {
		host_menu_text_left(renderer, g_resist[i],
				    x0 + (i % 2) * host_sx(140, logical_w),
				    y0 + host_sy(238, logical_h) +
					    (i / 2) * host_sy(26, logical_h),
				    pt_small, g_ink_hint);
		if (!act || !act->used || act->attr[i] == 0)
			snprintf(resist, sizeof(resist), "%s", "----");
		else
			snprintf(resist, sizeof(resist), "%d", act->attr[i]);
		host_menu_text_left(renderer, resist,
				    x0 + host_sx(28, logical_w) +
					    (i % 2) * host_sx(140, logical_w),
				    y0 + host_sy(238, logical_h) +
					    (i / 2) * host_sy(26, logical_h),
				    pt_small, g_ink_hint);
	}
	well.x = x0 + host_sx(300, logical_w);
	well.y = y0;
	well.w = logical_w - well.x - host_sx(24, logical_w);
	well.h = logical_h - y0 - host_sy(16, logical_h);
	if (well.w > host_sx(80, logical_w)) {
		host_menu_well(renderer, well);
		rh = (well.h - host_sy(12, logical_h)) / HOST_MENU_EQUIP_N;
		for (i = 0; i < HOST_MENU_EQUIP_N; i++) {
			row.x = well.x + host_sx(8, logical_w);
			row.y = well.y + host_sy(8, logical_h) + i * rh;
			row.w = well.w - host_sx(16, logical_w);
			row.h = rh - host_sy(2, logical_h);
			host_menu_text_left(renderer, g_equip_slot[i],
					    row.x + host_sx(6, logical_w),
					    row.y + row.h / 2 - pt_small / 2,
					    pt_small, g_ink_body);
			host_menu_text_center(renderer,
					      (g_equip[i].used &&
					       g_equip[i].name[0]) ?
						      g_equip[i].name :
						      "无",
					      row.x + row.w -
						      host_sx(20, logical_w),
					      row.y + row.h / 2, pt_small,
					      g_equip[i].used ? g_ink_body :
								g_ink_hint);
		}
	}
}

static void host_menu_draw_book(SDL_Renderer *renderer, int logical_w,
			       int logical_h, int pt)
{
	SDL_Rect icon;
	int i;
	int selected;

	for (i = 0; i < HOST_MENU_ACTIONS; i++) {
		icon.x = host_sx(g_book_nx[i], logical_w);
		icon.y = host_sy(HOST_MENU_BOOK_NY, logical_h);
		icon.w = host_sx(HOST_MENU_BOOK_NW, logical_w);
		icon.h = host_sy(HOST_MENU_BOOK_NH, logical_h);
		selected = (g_layer == HOST_MENU_LAYER_INNER &&
			    g_book_focus == i);
		if (g_tex_book[i])
			host_menu_blit(renderer, g_tex_book[i], icon);
		else {
			host_menu_plaque(renderer, icon, selected);
			host_menu_text_center(renderer, g_book_text[i],
					      icon.x + icon.w / 2,
					      icon.y + icon.h / 2, pt,
					      g_ink_body);
		}
		if (selected) {
			icon.x -= 3;
			icon.y -= 3;
			icon.w += 6;
			icon.h += 6;
			host_menu_frame(renderer, icon, 3, g_ink_gold.r,
					g_ink_gold.g, g_ink_gold.b, 255);
		}
	}
}

static void host_menu_draw_slots(SDL_Renderer *renderer, SDL_Rect well,
				int logical_h, int pt, int pt_small)
{
	SDL_Rect cell;
	int i;
	int rows = HOST_MENU_SLOTS / HOST_MENU_SLOT_COLS;
	int gap;
	int cell_w;
	int cell_h;
	int col;
	int row;
	int used;
	int selected;
	char line[32];

	gap = well.w / 40;
	if (gap < 6)
		gap = 6;
	host_menu_fill(renderer, well, 8, 12, 10, 230);
	host_menu_frame(renderer, well, 2, 212, 176, 88, 255);
	host_menu_text_center(renderer,
			      g_slot_mode ? "选择读档" : "选择存档",
			      well.x + well.w / 2,
			      well.y + logical_h * 3 / 100, pt, g_ink_title);
	cell_w = (well.w - gap * 3) / HOST_MENU_SLOT_COLS;
	cell_h = (well.h - logical_h * 8 / 100 - gap * (rows + 1)) / rows;
	for (i = 0; i < HOST_MENU_SLOTS; i++) {
		col = i % HOST_MENU_SLOT_COLS;
		row = i / HOST_MENU_SLOT_COLS;
		cell.x = well.x + gap + col * (cell_w + gap);
		cell.y = well.y + logical_h * 6 / 100 + gap +
			 row * (cell_h + gap);
		cell.w = cell_w;
		cell.h = cell_h;
		selected = (g_slot_focus == i);
		used = host_menu_slot_used(i);
		if (selected)
			host_menu_fill(renderer, cell, 88, 68, 28, 250);
		else
			host_menu_fill(renderer, cell, 28, 32, 26, 230);
		host_menu_frame(renderer, cell, selected ? 3 : 1, 212, 176, 88,
				selected ? 255 : 150);
		snprintf(line, sizeof(line), "档位 %d", i + 1);
		host_menu_text_left(renderer, line, cell.x + cell.w / 12,
				    cell.y + cell.h / 2 - pt_small / 2,
				    pt_small, g_ink_body);
		host_menu_text_center(renderer, used ? "已存" : "空",
				      cell.x + cell.w * 3 / 4,
				      cell.y + cell.h / 2, pt_small,
				      g_ink_hint);
	}
}

static void host_menu_draw_bestiary(SDL_Renderer *renderer, SDL_Rect well,
				   int logical_w, int logical_h, int pt,
				   int pt_small)
{
	SDL_Rect list;
	SDL_Rect info;
	SDL_Rect hit;
	char line[48];
	int i;
	int rh;
	int rows;
	int start;
	int on;

	host_menu_fill(renderer, well, 8, 12, 10, 230);
	host_menu_frame(renderer, well, 2, 212, 176, 88, 255);
	host_menu_text_left(renderer, "神魔异事录",
			    well.x + host_sx(12, logical_w),
			    well.y + host_sy(8, logical_h), pt, g_ink_gold);
	list = well;
	list.x += host_sx(8, logical_w);
	list.y += host_sy(36, logical_h);
	list.w = well.w * 11 / 20;
	list.h = well.h - host_sy(48, logical_h);
	info = list;
	info.x = list.x + list.w + host_sx(8, logical_w);
	info.w = well.x + well.w - info.x - host_sx(8, logical_w);
	if (g_bestiary_n <= 0) {
		host_menu_text_center(renderer, "图鉴是空的",
				      well.x + well.w / 2,
				      well.y + well.h / 2, pt, g_ink_hint);
		host_menu_text_center(renderer, "B 返回",
				      well.x + well.w / 2,
				      well.y + well.h - host_sy(22, logical_h),
				      pt_small, g_ink_hint);
		return;
	}
	rh = host_sy(26, logical_h);
	if (rh < 16)
		rh = 16;
	rows = list.h / rh;
	if (rows < 1)
		rows = 1;
	if (rows > g_bestiary_n)
		rows = g_bestiary_n;
	start = g_bestiary_sel - rows / 2;
	if (start < 0)
		start = 0;
	if (start + rows > g_bestiary_n)
		start = g_bestiary_n - rows;
	host_menu_scroll(renderer, list, start, rows, g_bestiary_n);
	for (i = 0; i < rows; i++) {
		on = (g_bestiary_sel == start + i);
		hit.x = list.x;
		hit.y = list.y + i * rh;
		hit.w = list.w - host_sx(16, logical_w);
		hit.h = rh - 2;
		if (on)
			host_menu_frame(renderer, hit, 2, g_ink_gold.r,
					g_ink_gold.g, g_ink_gold.b, 255);
		host_menu_text_left(renderer, g_bestiary_name[start + i],
				    hit.x + host_sx(6, logical_w),
				    hit.y + hit.h / 2 - pt_small / 2, pt_small,
				    on ? g_ink_gold : g_ink_body);
	}
	host_menu_text_left(renderer, g_bestiary_name[g_bestiary_sel],
			    info.x, info.y, pt_small, g_ink_gold);
	snprintf(line, sizeof(line), "等级  %d", g_bestiary_lv);
	host_menu_text_left(renderer, line, info.x,
			    info.y + host_sy(28, logical_h), pt_small,
			    g_ink_body);
	snprintf(line, sizeof(line), "生命  %d", g_bestiary_hp);
	host_menu_text_left(renderer, line, info.x,
			    info.y + host_sy(52, logical_h), pt_small,
			    g_ink_body);
	snprintf(line, sizeof(line), "攻击  %d", g_bestiary_atk);
	host_menu_text_left(renderer, line, info.x,
			    info.y + host_sy(76, logical_h), pt_small,
			    g_ink_body);
	snprintf(line, sizeof(line), "防御  %d", g_bestiary_def);
	host_menu_text_left(renderer, line, info.x,
			    info.y + host_sy(100, logical_h), pt_small,
			    g_ink_body);
	snprintf(line, sizeof(line), "敏捷  %d", g_bestiary_spd);
	host_menu_text_left(renderer, line, info.x,
			    info.y + host_sy(124, logical_h), pt_small,
			    g_ink_body);
	if (g_bestiary_help[0])
		host_menu_text_left(renderer, g_bestiary_help, info.x,
				    info.y + host_sy(152, logical_h), pt_small,
				    g_ink_hint);
	host_menu_text_left(renderer, "B 返回", info.x,
			    well.y + well.h - host_sy(22, logical_h), pt_small,
			    g_ink_hint);
}

static void host_menu_draw_pick(SDL_Renderer *renderer, SDL_Rect well,
			       int logical_w, int logical_h, int pt,
			       int pt_small)
{
	SDL_Rect hit;
	char line[48];
	char title[56];
	int i;
	int n = 0;
	int rh;
	int on;
	const struct host_menu_actor *act;

	host_menu_fill(renderer, well, 8, 12, 10, 230);
	host_menu_frame(renderer, well, 2, 212, 176, 88, 255);
	if (g_act_name[0])
		snprintf(title, sizeof(title), "将「%s」用于", g_act_name);
	else
		snprintf(title, sizeof(title), "%s", "使用给谁");
	host_menu_text_left(renderer, title, well.x + host_sx(12, logical_w),
			    well.y + host_sy(10, logical_h), pt, g_ink_gold);
	rh = host_sy(48, logical_h);
	if (rh < 28)
		rh = 28;
	for (i = 0; i < HOST_MENU_PARTY_N; i++) {
		act = &g_party[i];
		if (!host_menu_in_team(i))
			continue;
		hit.x = well.x + host_sx(12, logical_w);
		hit.y = well.y + host_sy(44, logical_h) + n * rh;
		hit.w = well.w - host_sx(24, logical_w);
		hit.h = rh - host_sy(6, logical_h);
		on = (g_party_i == i);
		if (on)
			host_menu_fill(renderer, hit, 88, 68, 28, 250);
		else
			host_menu_fill(renderer, hit, 28, 32, 26, 230);
		host_menu_frame(renderer, hit, on ? 3 : 1, 212, 176, 88,
				on ? 255 : 150);
		host_menu_text_left(renderer,
				    act->name[0] ? act->name : "—",
				    hit.x + host_sx(10, logical_w),
				    hit.y + host_sy(6, logical_h), pt_small,
				    on ? g_ink_gold : g_ink_body);
		if (act->hp_max > 0)
			snprintf(line, sizeof(line), "命 %d / %d", act->hp,
				 act->hp_max);
		else
			snprintf(line, sizeof(line), "%s", "命 — / —");
		host_menu_text_left(renderer, line,
				    hit.x + host_sx(10, logical_w),
				    hit.y + hit.h / 2, pt_small, g_ink_hint);
		n++;
	}
	host_menu_text_center(renderer, "上下选择  A 使用  B 取消",
			      well.x + well.w / 2,
			      well.y + well.h - host_sy(22, logical_h),
			      pt_small, g_ink_hint);
}

static void host_menu_draw_ask(SDL_Renderer *renderer, SDL_Rect well,
			      int logical_w, int logical_h, int pt,
			      int pt_small)
{
	char line[56];

	(void)logical_w;
	host_menu_fill(renderer, well, 8, 12, 10, 230);
	host_menu_frame(renderer, well, 2, 212, 176, 88, 255);
	host_menu_text_center(renderer, "确定丢弃？", well.x + well.w / 2,
			      well.y + well.h / 2 - host_sy(36, logical_h), pt,
			      g_ink_gold);
	if (g_act_name[0])
		snprintf(line, sizeof(line), "「%s」", g_act_name);
	else
		snprintf(line, sizeof(line), "%s", "这件物品");
	host_menu_text_center(renderer, line, well.x + well.w / 2,
			      well.y + well.h / 2, pt, g_ink_body);
	host_menu_text_center(renderer, "A 确定    B 取消",
			      well.x + well.w / 2,
			      well.y + well.h / 2 + host_sy(40, logical_h),
			      pt_small, g_ink_hint);
}

static void host_menu_draw_equip_pick(SDL_Renderer *renderer, SDL_Rect well,
				     int logical_w, int logical_h, int pt,
				     int pt_small)
{
	SDL_Rect list;
	SDL_Rect cmp;
	SDL_Rect hit;
	char title[56];
	char line[48];
	int view[HOST_MENU_BAG_N];
	int n;
	int i;
	int rh;
	int rows;
	int start;
	int on;
	int worn_temp;
	int cand_temp;
	int oldv;
	int newv;
	int delta;
	int y;
	const struct host_menu_item *it;
	const SDL_Color *ink;

	host_menu_fill(renderer, well, 8, 12, 10, 230);
	host_menu_frame(renderer, well, 2, 212, 176, 88, 255);
	if (g_equip_focus >= 0 && g_equip_focus < HOST_MENU_EQUIP_N)
		snprintf(title, sizeof(title), "更换「%s」",
			 g_equip_slot[g_equip_focus]);
	else
		snprintf(title, sizeof(title), "%s", "更换装备");
	host_menu_text_left(renderer, title, well.x + host_sx(12, logical_w),
			    well.y + host_sy(8, logical_h), pt, g_ink_gold);
	n = host_menu_equip_view(view, HOST_MENU_BAG_N);
	host_menu_clamp_equip_sel();
	list = well;
	list.x += host_sx(8, logical_w);
	list.y += host_sy(40, logical_h);
	list.w = well.w * 11 / 20;
	list.h = well.h - host_sy(70, logical_h);
	cmp = list;
	cmp.x = list.x + list.w + host_sx(8, logical_w);
	cmp.w = well.x + well.w - cmp.x - host_sx(8, logical_w);
	if (n <= 0) {
		host_menu_text_center(renderer, "没有可装备的物品",
				      well.x + well.w / 2,
				      well.y + well.h / 2, pt, g_ink_hint);
		host_menu_text_center(renderer, "B 返回",
				      well.x + well.w / 2,
				      well.y + well.h - host_sy(22, logical_h),
				      pt_small, g_ink_hint);
		return;
	}
	rh = host_sy(26, logical_h);
	if (rh < 16)
		rh = 16;
	rows = list.h / rh;
	if (rows < 1)
		rows = 1;
	if (rows > n)
		rows = n;
	start = g_equip_sel - rows / 2;
	if (start < 0)
		start = 0;
	if (start + rows > n)
		start = n - rows;
	host_menu_scroll(renderer, list, start, rows, n);
	for (i = 0; i < rows; i++) {
		it = &g_bag[view[start + i]];
		on = (g_equip_sel == start + i);
		hit.x = list.x;
		hit.y = list.y + i * rh;
		hit.w = list.w - host_sx(12, logical_w);
		hit.h = rh - 2;
		if (on)
			host_menu_frame(renderer, hit, 2, g_ink_gold.r,
					g_ink_gold.g, g_ink_gold.b, 255);
		if (it->count + it->count_new > 1)
			snprintf(line, sizeof(line), "%s x%d",
				 it->name[0] ? it->name : "—",
				 it->count + it->count_new);
		else
			snprintf(line, sizeof(line), "%s",
				 it->name[0] ? it->name : "—");
		host_menu_text_left(renderer, line,
				    hit.x + host_sx(6, logical_w),
				    hit.y + hit.h / 2 - pt_small / 2, pt_small,
				    on ? g_ink_gold : g_ink_body);
	}
	worn_temp = (g_equip[g_equip_focus].used &&
		     g_equip[g_equip_focus].temp > 0) ?
			    g_equip[g_equip_focus].temp :
			    0;
	it = &g_bag[view[g_equip_sel]];
	cand_temp = it->temp;
	host_menu_text_left(renderer, "已装备 → 待装备", cmp.x, cmp.y,
			    pt_small, g_ink_hint);
	y = cmp.y + host_sy(28, logical_h);
	for (i = 0; i < HOST_MENU_EQUIP_STAT_N; i++) {
		oldv = worn_temp ? host_menu_item_add(worn_temp,
						      g_equip_stat_field[i]) :
				   0;
		newv = cand_temp ? host_menu_item_add(cand_temp,
						      g_equip_stat_field[i]) :
				   0;
		delta = newv - oldv;
		if (i >= 3 && oldv == 0 && newv == 0)
			continue;
		snprintf(line, sizeof(line), "%s  %d → %d",
			 g_equip_stat_name[i], oldv, newv);
		host_menu_text_left(renderer, line, cmp.x, y, pt_small,
				    g_ink_body);
		if (delta > 0) {
			ink = &g_ink_up;
			snprintf(line, sizeof(line), "+%d", delta);
		} else if (delta < 0) {
			ink = &g_ink_hp;
			snprintf(line, sizeof(line), "%d", delta);
		} else {
			ink = &g_ink_hint;
			snprintf(line, sizeof(line), "%s", "0");
		}
		host_menu_text_left(renderer, line,
				    cmp.x + host_sx(150, logical_w), y,
				    pt_small, *ink);
		y += host_sy(24, logical_h);
	}
	if (it->help[0])
		host_menu_text_left(renderer, it->help, cmp.x,
				    y + host_sy(8, logical_h), pt_small,
				    g_ink_hint);
	host_menu_text_center(renderer, "上下选择  左右换人  A 装备  B 取消",
			      well.x + well.w / 2,
			      well.y + well.h - host_sy(22, logical_h),
			      pt_small, g_ink_hint);
}

void host_menu_draw(SDL_Renderer *renderer, int logical_w, int logical_h)
{
	SDL_Rect screen;
	SDL_Rect right;
	SDL_Rect well;
	SDL_Rect div;
	int pt;
	int pt_small;
	Uint8 pr;
	Uint8 pg;
	Uint8 pb;
	Uint8 pa;
	SDL_BlendMode blend;

	if (!g_open || !renderer || logical_w <= 0 || logical_h <= 0)
		return;
	if (SDL_GetRenderTarget(renderer) != NULL)
		return;
	host_menu_refresh_party();
	host_menu_refresh_inv();

	if (g_text_renderer != renderer) {
		host_menu_text_flush(g_text_renderer);
		g_text_renderer = renderer;
	}
	host_menu_assets_ensure(renderer);

	pt = logical_h / 36;
	if (pt < 14)
		pt = 14;
	pt_small = logical_h / 42;
	if (pt_small < 12)
		pt_small = 12;

	SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);
	SDL_GetRenderDrawBlendMode(renderer, &blend);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	screen.x = 0;
	screen.y = 0;
	screen.w = logical_w;
	screen.h = logical_h;
	host_menu_fill(renderer, screen, 12, 10, 8, 255);

	right.x = host_sx(HOST_MENU_SPLIT_X, logical_w);
	right.y = 0;
	right.w = logical_w - right.x;
	right.h = logical_h;
	if (g_tex_dark)
		host_menu_blit(renderer, g_tex_dark, right);
	else
		host_menu_fill(renderer, right, 18, 16, 14, 255);

	host_menu_draw_left(renderer, logical_w, logical_h, pt, pt_small);

	div.x = right.x - 1;
	div.y = 0;
	div.w = 2;
	div.h = logical_h;
	host_menu_fill(renderer, div, 196, 164, 72, 255);
	host_menu_frame(renderer, screen, 2, 196, 164, 72, 255);

	host_menu_draw_tabs(renderer, logical_w, logical_h, pt);
	host_menu_draw_back(renderer, logical_w, logical_h);

	if (!host_menu_layer_page()) {
		if (g_tab == HOST_MENU_TAB_ITEM)
			host_menu_draw_items(renderer, logical_w, logical_h, pt,
					     pt_small);
		else if (g_tab == HOST_MENU_TAB_EQUIP)
			host_menu_draw_equip(renderer, logical_w, logical_h, pt,
					     pt_small);
		else if (g_tab == HOST_MENU_TAB_SKILL)
			host_menu_draw_skill(renderer, logical_w, logical_h, pt,
					     pt_small);
		else if (g_tab == HOST_MENU_TAB_STATUS)
			host_menu_draw_status(renderer, logical_w, logical_h,
					      pt_small);
		else
			host_menu_draw_book(renderer, logical_w, logical_h, pt);
	}

	well.x = host_sx(HOST_MENU_SPLIT_X + 12, logical_w);
	well.y = host_sy(78, logical_h);
	well.w = logical_w - well.x - host_sx(16, logical_w);
	well.h = logical_h - well.y - host_sy(16, logical_h);
	if (g_layer == HOST_MENU_LAYER_SLOTS)
		host_menu_draw_slots(renderer, well, logical_h, pt, pt_small);
	else if (g_layer == HOST_MENU_LAYER_BESTIARY)
		host_menu_draw_bestiary(renderer, well, logical_w, logical_h,
					pt, pt_small);
	else if (g_layer == HOST_MENU_LAYER_PICK)
		host_menu_draw_pick(renderer, well, logical_w, logical_h, pt,
				    pt_small);
	else if (g_layer == HOST_MENU_LAYER_ASK)
		host_menu_draw_ask(renderer, well, logical_w, logical_h, pt,
				   pt_small);
	else if (g_layer == HOST_MENU_LAYER_EQUIP)
		host_menu_draw_equip_pick(renderer, well, logical_w, logical_h,
					  pt, pt_small);
	else if (g_layer == HOST_MENU_LAYER_STUB && g_stub >= 0 &&
		 g_stub < HOST_MENU_ACTIONS) {
		host_menu_fill(renderer, well, 8, 12, 10, 230);
		host_menu_frame(renderer, well, 2, 212, 176, 88, 255);
		host_menu_text_center(renderer, g_stub_text[g_stub],
				      well.x + well.w / 2,
				      well.y + well.h / 2 - logical_h / 20, pt,
				      g_ink_body);
		host_menu_text_center(renderer, "B 返回",
				      well.x + well.w / 2,
				      well.y + well.h / 2 + logical_h / 16,
				      pt_small, g_ink_hint);
	}

	SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
	SDL_SetRenderDrawBlendMode(renderer, blend);
}
