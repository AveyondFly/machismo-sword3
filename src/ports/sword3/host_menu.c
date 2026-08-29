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
#define HOST_MENU_RESIST_N 9
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
#define HOST_MENU_OFF_HELP 0x110
#define HOST_MENU_OFF_INFO 0x118

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
	HOST_MENU_LAYER_STUB
};

static int g_open;
static int g_tab;
static int g_layer;
static int g_book_focus;
static int g_item_focus;
static int g_equip_focus;
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
	char name[32];
};

struct host_menu_item {
	int used;
	int count;
	int count_new;
	int temp;
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
static int g_bag_n;
static int g_skill_n;
static int g_skill_focus;
static int g_inv_logged;

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

static const char *g_tab_text[HOST_MENU_TABS] = {
	"物品", "装备", "奇术", "状态", "天书"
};

static const char *g_book_text[HOST_MENU_ACTIONS] = {
	"存盘", "读取", "记载", "设置", "离开"
};

static const char *g_item_act[] = { "使用", "整理", "丢弃" };
static const char *g_item_cat[] = {
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
static const SDL_Color g_ink_paper = { 72, 48, 24, 255 };
static const SDL_Color g_ink_tab_on = { 72, 42, 16, 255 };
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
	g_equip_focus = 0;
	g_stub = -1;
	g_slot_mode = 0;
	g_slot_focus = 0;
	g_party_i = 0;
	g_party_logged = 0;
	g_inv_logged = 0;
	g_skill_focus = 0;
	g_bag_n = 0;
	g_skill_n = 0;
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
		a->used = a->hp_max > 0 || a->level > 0 || a->name[0];
	}
	if (!g_party_logged && g_party[0].used) {
		g_party_logged = 1;
		fprintf(stderr,
			"sword3-sdl: party0 name=%s lv=%d/%d hp=%d/%d exp=%d/%d sk=%d/%d\n",
			g_party[0].name[0] ? g_party[0].name : "-",
			g_party[0].level, g_party[0].level_max, g_party[0].hp,
			g_party[0].hp_max, g_party[0].exp, g_party[0].exp_need,
			g_party[0].sk_exp, g_party[0].sk_next);
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
			"sword3-sdl: inv bag=%d skill=%d equip0=%s\n", g_bag_n,
			g_skill_n,
			g_equip[0].name[0] ? g_equip[0].name : "-");
	}
}

static void host_menu_enter_inner(void)
{
	g_layer = HOST_MENU_LAYER_INNER;
	if (g_tab == HOST_MENU_TAB_ITEM)
		g_item_focus = 0;
	else if (g_tab == HOST_MENU_TAB_EQUIP)
		g_equip_focus = 0;
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
}

static void host_menu_move_equip(int delta)
{
	g_equip_focus = (g_equip_focus + delta + HOST_MENU_EQUIP_N) %
			HOST_MENU_EQUIP_N;
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

	if (g_layer == HOST_MENU_LAYER_STUB)
		return;

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
			host_menu_move_item(index == 1 ? 1 : -1);
		return;
	}
	if (g_tab == HOST_MENU_TAB_EQUIP) {
		if (index == 2)
			host_menu_move_equip(1);
		else if (index == 0)
			host_menu_move_equip(-1);
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
	if (g_layer == HOST_MENU_LAYER_STUB)
		return;
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
	    g_layer == HOST_MENU_LAYER_SLOTS) {
		g_layer = HOST_MENU_LAYER_INNER;
		g_stub = -1;
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
		if (down && g_layer != HOST_MENU_LAYER_SLOTS &&
		    g_layer != HOST_MENU_LAYER_STUB)
			host_menu_move_tab(-1);
		return 1;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
		if (down && g_layer != HOST_MENU_LAYER_SLOTS &&
		    g_layer != HOST_MENU_LAYER_STUB)
			host_menu_move_tab(1);
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

static void host_menu_scroll(SDL_Renderer *renderer, SDL_Rect well)
{
	SDL_Rect rail;
	SDL_Rect thumb;
	SDL_Rect cap;

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
	thumb = rail;
	thumb.x += 1;
	thumb.w -= 2;
	thumb.h = rail.h / 5;
	if (thumb.h < 12)
		thumb.h = 12;
	thumb.y += 6;
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

	portrait.x = card.x + host_sx(8, logical_w);
	portrait.y = card.y + host_sy(10, logical_h);
	portrait.w = host_sx(118, logical_w);
	portrait.h = host_sy(140, logical_h);
	host_menu_fill(renderer, portrait, 32, 22, 12, 255);
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
	int i;
	int on;
	int rh;
	int rows;
	char line[48];

	(void)pt;
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
		host_menu_plaque(renderer, row, 0);
		host_menu_text_center(renderer, g_item_cat[i],
				      row.x + row.w / 2, row.y + row.h / 2,
				      pt_small, g_ink_body);
	}
	well.x = host_sx(318, logical_w);
	well.y = host_sy(HOST_MENU_ITEM_WELL_NY, logical_h);
	well.w = host_sx(548, logical_w);
	well.h = logical_h - well.y - host_sy(16, logical_h);
	host_menu_well(renderer, well);
	host_menu_scroll(renderer, well);
	if (g_bag_n <= 0) {
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
	if (rows > g_bag_n)
		rows = g_bag_n;
	for (i = 0; i < rows; i++) {
		if (!g_bag[i].used)
			continue;
		if (g_bag[i].count + g_bag[i].count_new > 1)
			snprintf(line, sizeof(line), "%s  x%d",
				 g_bag[i].name[0] ? g_bag[i].name : "—",
				 g_bag[i].count + g_bag[i].count_new);
		else
			snprintf(line, sizeof(line), "%s",
				 g_bag[i].name[0] ? g_bag[i].name : "—");
		host_menu_text_left(renderer, line,
				    well.x + host_sx(16, logical_w),
				    well.y + host_sy(8, logical_h) + i * rh,
				    pt_small, g_ink_body);
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
		int rh;
		int rows;
		int i;
		char line[48];

		host_menu_well(renderer, well);
		if (g_bag_n <= 0) {
			host_menu_text_center(renderer, "没有物品",
					      well.x + well.w / 2,
					      well.y + well.h / 2, pt,
					      g_ink_hint);
		} else {
			rh = host_sy(26, logical_h);
			if (rh < 16)
				rh = 16;
			rows = (well.h - host_sy(12, logical_h)) / rh;
			if (rows > g_bag_n)
				rows = g_bag_n;
			for (i = 0; i < rows; i++) {
				if (!g_bag[i].name[0])
					continue;
				if (g_bag[i].count + g_bag[i].count_new > 1)
					snprintf(line, sizeof(line), "%s x%d",
						 g_bag[i].name,
						 g_bag[i].count +
							 g_bag[i].count_new);
				else
					snprintf(line, sizeof(line), "%s",
						 g_bag[i].name);
				host_menu_text_left(renderer, line,
						    well.x +
							    host_sx(8, logical_w),
						    well.y +
							    host_sy(6, logical_h) +
							    i * rh,
						    pt_small, g_ink_body);
			}
		}
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

	if (g_layer != HOST_MENU_LAYER_SLOTS &&
	    g_layer != HOST_MENU_LAYER_STUB) {
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
