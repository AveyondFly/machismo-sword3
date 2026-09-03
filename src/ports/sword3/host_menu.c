#include "host_menu.h"
#include "host_font.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>

#define HOST_MENU_DEADZONE 14000
#define HOST_MENU_TABS 7
#define HOST_MENU_ACTIONS 5
#define HOST_MENU_SLOTS 10
#define HOST_MENU_SAV_THUMB_W 160
#define HOST_MENU_SAV_THUMB_H 120
#define HOST_MENU_SAV_THUMB_OFF 31
#define HOST_MENU_SAV_THUMB_BYTES \
	(HOST_MENU_SAV_THUMB_W * HOST_MENU_SAV_THUMB_H * 2)
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
#define HOST_MENU_ACT_RESOLVE 0x1001ec79cull
#define HOST_MENU_FRAME_LOAD 0x1001ea094ull
#define HOST_MENU_FACE_QQ 36
#define HOST_MENU_TSW_KEY 0x100319dd8ull
#define HOST_MENU_TSW_MAGIC 0x100319dc0ull
#define HOST_MENU_SCREEN 0x100319450ull
#define HOST_MENU_REFRESH_SNAPSHOT 0x100201568ull
#define HOST_MENU_ITEM_REPO 0x1002ab4d8ull
#define HOST_MENU_EQUIP_REPO 0x1002ab628ull
#define HOST_MENU_SKILL_REPO 0x1002ab608ull
#define HOST_MENU_SKEXP 0x1002ab358ull
#define HOST_MENU_SKEXP_STRIDE 0x60
#define HOST_MENU_ITEM_NODE 0x130
#define HOST_MENU_SKILL_N 32
#define HOST_MENU_EQUIP_N 11
#define HOST_MENU_EQUIP_STRIDE 16
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
#define HOST_MENU_ITEM_SORT 0x100080f34ull
#define HOST_MENU_ITEM_SOUND 0x1001c076cull
#define HOST_MENU_LUA_HAS 0x1001c7088ull
#define HOST_MENU_LUA_INT 0x1001c6f70ull
#define HOST_MENU_LUA_NUM 0x1001c7040ull
#define HOST_MENU_LUA_STR 0x1001c7168ull
#define HOST_MENU_LUA_DB 0x1001c0108ull
#define HOST_MENU_LUA_GETTOP 0x10009648cull
#define HOST_MENU_LUA_GETGLOBAL 0x1000970d4ull
#define HOST_MENU_LUA_PUSHSTRING 0x100096e78ull
#define HOST_MENU_LUA_GETTABLE 0x10009714cull
#define HOST_MENU_LUA_TYPE 0x100096780ull
#define HOST_MENU_LUA_PUSHNIL 0x100096d78ull
#define HOST_MENU_LUA_NEXT 0x100097d00ull
#define HOST_MENU_LUA_TOINTEGER 0x100096a48ull
#define HOST_MENU_LUA_GETFIELD 0x100097180ull
#define HOST_MENU_LUA_TOBOOLEAN 0x100096b20ull
#define HOST_MENU_LUA_SETTOP 0x1000964a8ull
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
#define HOST_MENU_STR_INFO 0x100272986ull
#define HOST_MENU_STR_SAVE_DATA 0x100272b99ull
#define HOST_MENU_STR_QUEST 0x100273e98ull
#define HOST_MENU_STR_ENABLE 0x100273e9eull
#define HOST_MENU_STR_QUEST_INFO 0x100273fd0ull
#define HOST_MENU_STR_MAIN_STORY 0x100273fd5ull
#define HOST_MENU_BOOK_SRC 0x1002a85d8ull
#define HOST_MENU_BOOK_SRC_N 0x1002a85d0ull
#define HOST_MENU_BESTIARY_N 128
#define HOST_MENU_ITEM_META_N 256
#define HOST_MENU_ITEM_USE_BITS 0xe
#define HOST_MENU_ITEM_NO_THROW 5
#define HOST_MENU_ITEM_NO_CONSUME 7
#define HOST_MENU_WIDGET_HAS 0x1001b7014ull
#define HOST_MENU_REFINING_FEATURE 0x49
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
	HOST_MENU_TAB_REFINING,
	HOST_MENU_TAB_BOOK,
	HOST_MENU_TAB_HELP
};

enum host_menu_layer {
	HOST_MENU_LAYER_TABS = 0,
	HOST_MENU_LAYER_INNER,
	HOST_MENU_LAYER_SLOTS,
	HOST_MENU_LAYER_STUB,
	HOST_MENU_LAYER_BESTIARY,
	HOST_MENU_LAYER_PICK,
	HOST_MENU_LAYER_ASK,
	HOST_MENU_LAYER_EXIT_ASK,
	HOST_MENU_LAYER_JOURNAL,
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
static uintptr_t g_face_obj[HOST_MENU_PARTY_N];
static SDL_Texture *g_tex_slot[HOST_MENU_SLOTS];
static int g_slot_have[HOST_MENU_SLOTS];
static unsigned g_slot_play[HOST_MENU_SLOTS];
static time_t g_slot_mtime[HOST_MENU_SLOTS];
static char g_slot_map[HOST_MENU_SLOTS][40];

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
	char help[80];
	char info[64];
};

struct host_menu_item_meta {
	int temp;
	char help[80];
	char info[64];
};

struct host_menu_journal {
	int id;
	int main_story;
	char text[160];
};

static struct host_menu_actor g_party[HOST_MENU_PARTY_N];
static struct host_menu_item *g_bag;
static int g_bag_cap;
static struct host_menu_item g_equip[HOST_MENU_EQUIP_N];
static struct host_menu_item g_skills[HOST_MENU_SKILL_N];
static struct host_menu_item_meta g_item_meta[HOST_MENU_ITEM_META_N];
static int g_item_meta_n;
static struct host_menu_journal *g_journal;
static int g_journal_n;
static int g_journal_cap;
static int g_journal_focus;
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
#define HOST_MENU_BOOK_NW 90
#define HOST_MENU_BOOK_NH 66
#define HOST_MENU_BOOK_PAD 7
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
static void host_menu_draw_equip_pick(SDL_Renderer *renderer, SDL_Rect well,
				      int logical_w, int logical_h, int pt,
				      int pt_small);
static int host_menu_lua_text(int temp, uintptr_t field, char *dst,
			      size_t dstn);
static void host_menu_load_item_meta(struct host_menu_item *it);
static void host_menu_journal_open(void);

static const char *g_tab_text[HOST_MENU_TABS] = {
	"物品", "装备", "奇术", "状态", "炼妖", "天书", "说明"
};

static const char *g_book_text[HOST_MENU_ACTIONS] = {
	"存盘", "读取", "记载", "设置", "离开"
};

static const char *g_item_act[] = { "使用", "整理", "丢弃" };
static const char *g_item_cat_text[] = {
	"新品", "恢复", "辅助", "法宝", "装备", "活物", "其他"
};
static const char *g_equip_slot[] = {
	"武器", "头部", "身体", "手部", "足部",
	"饰品一", "饰品二", "法宝一", "法宝二", "护驾一", "护驾二"
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
/* Original slot masks at 0x1002a50dc, indexed by all 11 equip slots. */
static const uint32_t g_equip_mask[HOST_MENU_EQUIP_N] = {
	0x00000100u, 0x10000200u, 0x40000200u, 0x20000200u,
	0x80000200u, 0x00000400u, 0x00000400u, 0x00000040u,
	0x00000040u, 0x00000800u, 0x00000800u
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
	"离开（尚未接入）",
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
	free(g_bag);
	g_bag = NULL;
	g_bag_n = 0;
	g_bag_cap = 0;
	free(g_journal);
	g_journal = NULL;
	g_journal_n = 0;
	g_journal_cap = 0;
	g_journal_focus = 0;
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
	((void (*)(void *, float))(uintptr_t)HOST_MENU_REFRESH_SNAPSHOT)(
		(void *)(uintptr_t)HOST_MENU_SCREEN, 0.25f);
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
	free(g_bag);
	g_bag = NULL;
	g_bag_cap = 0;
	free(g_journal);
	g_journal = NULL;
	g_journal_n = 0;
	g_journal_cap = 0;
	g_journal_focus = 0;
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

static int host_menu_slot_path(int slot, char *path, size_t n)
{
	const char *dir;

	if (!path || n == 0 || slot < 0 || slot >= HOST_MENU_SLOTS)
		return 0;
	dir = getenv("SWORD3_DATA_DIR");
	if (!dir || dir[0] != '/')
		dir = "/tmp/sword3/documents";
	return snprintf(path, n, "%s/%d.sav", dir, slot) < (int)n;
}

static int host_menu_slot_used(int slot)
{
	char path[PATH_MAX];
	struct stat st;

	if (!host_menu_slot_path(slot, path, sizeof(path)))
		return 0;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void host_menu_slot_clear(int slot)
{
	if (slot < 0 || slot >= HOST_MENU_SLOTS)
		return;
	if (g_tex_slot[slot]) {
		SDL_DestroyTexture(g_tex_slot[slot]);
		g_tex_slot[slot] = NULL;
	}
	g_slot_have[slot] = 0;
	g_slot_play[slot] = 0;
	g_slot_mtime[slot] = 0;
	g_slot_map[slot][0] = 0;
}

static void host_menu_fmt_play(char *dst, size_t n, unsigned sec)
{
	unsigned h;
	unsigned m;
	unsigned s;

	if (!dst || n == 0)
		return;
	h = sec / 3600u;
	m = (sec / 60u) % 60u;
	s = sec % 60u;
	if (h > 0)
		snprintf(dst, n, "游玩 %u:%02u:%02u", h, m, s);
	else
		snprintf(dst, n, "游玩 %u:%02u", m, s);
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
	/*
	 * AArch64 Linux maps shared-library strings in the upper half of its
	 * 48-bit user address space (0xffff...). StringDB legitimately returns
	 * those pointers for HelpText/InfoText.
	 */
	if (ptr < 0x1000ull || ptr > 0x0000ffffffffffffull)
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
	it->used = it->name[0] || temp > 0;
	host_menu_load_item_meta(it);
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
	uintptr_t seen[HOST_MENU_SKILL_N];

	node = head;
	while (node && n < max && guard < max + 8) {
		guard++;
		for (i = 0; i < seen_n; i++) {
			if (seen[i] == node)
				return n;
		}
		if (seen_n < HOST_MENU_SKILL_N)
			seen[seen_n++] = node;
		if (host_menu_load_item(&dst[n], node))
			n++;
		node = host_menu_mem_ptr(node + HOST_MENU_OFF_NEXT);
	}
	return n;
}

static int host_menu_refresh_bag(uintptr_t head)
{
	struct host_menu_item *items;
	uintptr_t *seen;
	uintptr_t node;
	uintptr_t next;
	int cap;
	int i;
	int n;

	seen = NULL;
	cap = 0;
	n = 0;
	node = head;
	while (node) {
		if (!host_menu_heap_ok(node, HOST_MENU_ITEM_NODE))
			break;
		for (i = 0; i < n; i++) {
			if (seen[i] == node) {
				free(seen);
				return n;
			}
		}
		if (n == cap) {
			int new_cap = cap > 0 ? cap * 2 : 32;
			uintptr_t *grown;

			grown = realloc(seen,
					(size_t)new_cap * sizeof(*seen));
			if (!grown)
				break;
			seen = grown;
			cap = new_cap;
		}
		if (n == g_bag_cap) {
			int new_cap = g_bag_cap > 0 ? g_bag_cap * 2 : 32;

			items = realloc(g_bag,
					(size_t)new_cap * sizeof(*g_bag));
			if (!items)
				break;
			g_bag = items;
			g_bag_cap = new_cap;
		}
		seen[n] = node;
		if (host_menu_load_item(&g_bag[n], node))
			n++;
		else
			break;
		next = host_menu_mem_ptr(node + HOST_MENU_OFF_NEXT);
		node = next;
	}
	free(seen);
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

	memset(g_equip, 0, sizeof(g_equip));
	memset(g_skills, 0, sizeof(g_skills));
	g_bag_n = 0;
	g_skill_n = 0;
	if (host_menu_guest_ok(HOST_MENU_ITEM_REPO + HOST_MENU_OFF_NEXT, 8)) {
		head = host_menu_guest_ptr(HOST_MENU_ITEM_REPO +
					  HOST_MENU_OFF_NEXT);
		g_bag_n = host_menu_refresh_bag(head);
	}
	if (host_menu_guest_ok(HOST_MENU_EQUIP_REPO,
			       (size_t)HOST_MENU_EQUIP_STRIDE * 8 *
				       HOST_MENU_PARTY_N)) {
		for (i = 0; i < HOST_MENU_EQUIP_N; i++) {
			idx = i + g_party_i * HOST_MENU_EQUIP_STRIDE;
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

static int host_menu_item_view_count(void)
{
	int i;
	int n = 0;

	for (i = 0; i < g_bag_n; i++) {
		if (host_menu_item_in_cat(&g_bag[i], g_item_cat))
			n++;
	}
	return n;
}

static struct host_menu_item *host_menu_item_view_at(int at)
{
	int i;

	if (at < 0)
		return NULL;
	for (i = 0; i < g_bag_n; i++) {
		if (!host_menu_item_in_cat(&g_bag[i], g_item_cat))
			continue;
		if (at-- == 0)
			return &g_bag[i];
	}
	return NULL;
}

static void host_menu_clamp_item_sel(void)
{
	int n;

	if (g_item_cat < 0 || g_item_cat >= HOST_MENU_ITEM_CAT)
		g_item_cat = 0;
	n = host_menu_item_view_count();
	if (n <= 0)
		g_item_sel = 0;
	else if (g_item_sel >= n)
		g_item_sel = n - 1;
	else if (g_item_sel < 0)
		g_item_sel = 0;
}

static struct host_menu_item *host_menu_item_current(void)
{
	int n;

	host_menu_clamp_item_sel();
	n = host_menu_item_view_count();
	if (n <= 0 || g_item_sel < 0 || g_item_sel >= n)
		return NULL;
	return host_menu_item_view_at(g_item_sel);
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

static int host_menu_lua_table_text(uintptr_t table, int temp, uintptr_t field,
				    char *dst, size_t dstn)
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
		(const void *)(uintptr_t)table, temp,
		(const void *)(uintptr_t)field);
	if (!raw)
		return 0;
	shown = ((const char *(*)(const void *))(uintptr_t)HOST_MENU_LUA_DB)(
		raw);
	return host_menu_copy_name(dst, dstn, (uintptr_t)shown);
}

static int host_menu_lua_text(int temp, uintptr_t field, char *dst, size_t dstn)
{
	return host_menu_lua_table_text(HOST_MENU_STR_ITEMTEMP, temp, field,
					dst, dstn);
}

static void host_menu_load_item_meta(struct host_menu_item *it)
{
	struct host_menu_item_meta *meta;
	int i;

	if (!it || it->temp < 1 || !host_menu_lua_ready())
		return;
	meta = NULL;
	for (i = 0; i < g_item_meta_n; i++) {
		if (g_item_meta[i].temp == it->temp) {
			meta = &g_item_meta[i];
			break;
		}
	}
	if (!meta && g_item_meta_n < HOST_MENU_ITEM_META_N) {
		meta = &g_item_meta[g_item_meta_n++];
		memset(meta, 0, sizeof(*meta));
		meta->temp = it->temp;
		host_menu_lua_text(it->temp, HOST_MENU_STR_HELP, meta->help,
				   sizeof(meta->help));
		host_menu_lua_text(it->temp, HOST_MENU_STR_INFO, meta->info,
				   sizeof(meta->info));
	}
	if (!meta) {
		host_menu_lua_text(it->temp, HOST_MENU_STR_HELP, it->help,
				   sizeof(it->help));
		host_menu_lua_text(it->temp, HOST_MENU_STR_INFO, it->info,
				   sizeof(it->info));
		return;
	}
	snprintf(it->help, sizeof(it->help), "%s", meta->help);
	snprintf(it->info, sizeof(it->info), "%s", meta->info);
}

static int host_menu_journal_add(int id)
{
	struct host_menu_journal *entries;
	struct host_menu_journal *entry;
	int new_cap;

	if (id <= 0)
		return 0;
	if (g_journal_n == g_journal_cap) {
		new_cap = g_journal_cap > 0 ? g_journal_cap * 2 : 16;
		entries = realloc(g_journal,
				  (size_t)new_cap * sizeof(*g_journal));
		if (!entries)
			return 0;
		g_journal = entries;
		g_journal_cap = new_cap;
	}
	entry = &g_journal[g_journal_n];
	memset(entry, 0, sizeof(*entry));
	entry->id = id;
	if (!host_menu_lua_table_text(
		    HOST_MENU_STR_QUEST, id, HOST_MENU_STR_QUEST_INFO,
		    entry->text, sizeof(entry->text)))
		snprintf(entry->text, sizeof(entry->text), "事件 #%d", id);
	entry->main_story =
		((int (*)(void *, const void *, int, const void *))(
			 uintptr_t)HOST_MENU_LUA_HAS)(
			(void *)(uintptr_t)HOST_MENU_LUA_BIND,
			(const void *)(uintptr_t)HOST_MENU_STR_QUEST, id,
			(const void *)(uintptr_t)HOST_MENU_STR_MAIN_STORY) != 0;
	g_journal_n++;
	return 1;
}

static void host_menu_journal_open(void)
{
	void *lua;
	int (*gettop)(void *);
	int (*next)(void *, int);
	int top;

	free(g_journal);
	g_journal = NULL;
	g_journal_n = 0;
	g_journal_cap = 0;
	g_journal_focus = 0;
	if (!host_menu_lua_ready())
		return;
	lua = (void *)(uintptr_t)host_menu_guest_ptr(HOST_MENU_LUA_BIND);
	if (!lua)
		return;
	gettop = (int (*)(void *))(uintptr_t)HOST_MENU_LUA_GETTOP;
	next = (int (*)(void *, int))(uintptr_t)HOST_MENU_LUA_NEXT;
	top = gettop(lua);
	((void (*)(void *, const char *))(uintptr_t)HOST_MENU_LUA_GETGLOBAL)(
		lua, (const char *)(uintptr_t)HOST_MENU_STR_SAVE_DATA);
	((void (*)(void *, const char *))(uintptr_t)HOST_MENU_LUA_PUSHSTRING)(
		lua, (const char *)(uintptr_t)HOST_MENU_STR_QUEST);
	((void (*)(void *, int))(uintptr_t)HOST_MENU_LUA_GETTABLE)(lua, -2);
	if (((int (*)(void *, int))(uintptr_t)HOST_MENU_LUA_TYPE)(lua, -1) !=
	    0) {
		((void (*)(void *))(uintptr_t)HOST_MENU_LUA_PUSHNIL)(lua);
		while (next(lua, -2)) {
			long long id;
			int enabled;

			id = ((long long (*)(void *, int, int *))(uintptr_t)
				      HOST_MENU_LUA_TOINTEGER)(lua, -2, NULL);
			((void (*)(void *, int, const char *))(uintptr_t)
				 HOST_MENU_LUA_GETFIELD)(
				lua, -1,
				(const char *)(uintptr_t)HOST_MENU_STR_ENABLE);
			enabled = ((int (*)(void *, int))(uintptr_t)
					   HOST_MENU_LUA_TOBOOLEAN)(lua, -1);
			((void (*)(void *, int))(uintptr_t)HOST_MENU_LUA_SETTOP)(
				lua, -2);
			if (enabled && id > 0 && id <= INT_MAX &&
			    !host_menu_journal_add((int)id))
				break;
			((void (*)(void *, int))(uintptr_t)HOST_MENU_LUA_SETTOP)(
				lua, -2);
		}
	}
	((void (*)(void *, int))(uintptr_t)HOST_MENU_LUA_SETTOP)(lua, top);
	g_layer = HOST_MENU_LAYER_JOURNAL;
	fprintf(stderr, "sword3-sdl: host journal open n=%d\n", g_journal_n);
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

static void host_menu_move_journal(int delta)
{
	if (g_journal_n <= 0)
		return;
	g_journal_focus = (g_journal_focus + delta % g_journal_n +
			   g_journal_n) %
			  g_journal_n;
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
	while (node && guard <= g_bag_n) {
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
	int old;
	int step;

	if (delta == 0 || host_menu_party_team_n() <= 0)
		return;
	step = delta > 0 ? 1 : -1;
	old = g_party_i;
	i = g_party_i;
	do {
		i = (i + step + HOST_MENU_PARTY_N) % HOST_MENU_PARTY_N;
		guard++;
	} while (guard < HOST_MENU_PARTY_N && !host_menu_in_team(i));
	if (host_menu_in_team(i)) {
		g_party_i = i;
		if (g_party_i != old) {
			g_skill_focus = 0;
			g_equip_sel = 0;
			g_item_note[0] = 0;
			host_menu_refresh_inv();
		}
	}
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

static int host_menu_equip_view_count(void)
{
	int i;
	int n = 0;

	for (i = 0; i < g_bag_n; i++) {
		if (host_menu_item_fits_slot(&g_bag[i], g_equip_focus))
			n++;
	}
	return n;
}

static struct host_menu_item *host_menu_equip_view_at(int at)
{
	int i;

	if (at < 0)
		return NULL;
	for (i = 0; i < g_bag_n; i++) {
		if (!host_menu_item_fits_slot(&g_bag[i], g_equip_focus))
			continue;
		if (at-- == 0)
			return &g_bag[i];
	}
	return NULL;
}

static void host_menu_clamp_equip_sel(void)
{
	int n;

	n = host_menu_equip_view_count();
	if (n <= 0)
		g_equip_sel = 0;
	else if (g_equip_sel >= n)
		g_equip_sel = n - 1;
}

static struct host_menu_item *host_menu_equip_current(void)
{
	int n;

	host_menu_clamp_equip_sel();
	n = host_menu_equip_view_count();
	if (n <= 0 || g_equip_sel < 0 || g_equip_sel >= n)
		return NULL;
	return host_menu_equip_view_at(g_equip_sel);
}

static int host_menu_item_add(int temp, uintptr_t field)
{
	return host_menu_lua_int(HOST_MENU_LUA_NUM, temp, field);
}

static void host_menu_move_equip_sel(int delta)
{
	int n;

	n = host_menu_equip_view_count();
	if (n <= 0)
		return;
	g_equip_sel = (g_equip_sel + delta % n + n) % n;
}

static void host_menu_equip_open(void)
{
	int n;

	g_item_note[0] = 0;
	host_menu_item_party();
	if (!host_menu_in_team(g_party_i)) {
		host_menu_item_say("无法装备", 0x8c);
		return;
	}
	n = host_menu_equip_view_count();
	g_equip_sel = 0;
	if (n <= 0) {
		host_menu_item_say("没有可装备的物品", 0x8c);
		return;
	}
	g_layer = HOST_MENU_LAYER_EQUIP;
	fprintf(stderr, "sword3-sdl: equip pick slot=%d n=%d party=%d\n",
		g_equip_focus, n, g_party_i);
}

static int host_menu_equip_node_temp(uintptr_t node)
{
	if (!node || !host_menu_heap_ok(node, HOST_MENU_ITEM_NODE))
		return -1;
	return host_menu_mem_i32(node + HOST_MENU_OFF_TEMP);
}

static void host_menu_equip_delta(uintptr_t node, int out[3])
{
	int scratch[HOST_MENU_PARTY_STRIDE / sizeof(int)];

	out[0] = 0;
	out[1] = 0;
	out[2] = 0;
	if (host_menu_equip_node_temp(node) < 1)
		return;
	memset(scratch, 0, sizeof(scratch));
	((void (*)(void *, void *))(uintptr_t)HOST_MENU_ITEM_APPLY)(
		scratch, (void *)(uintptr_t)(node + HOST_MENU_OFF_INAME));
	out[0] = scratch[HOST_MENU_OFF_HPMAX / sizeof(int)];
	out[1] = scratch[HOST_MENU_OFF_MPMAX / sizeof(int)];
	out[2] = scratch[HOST_MENU_OFF_SPMAX / sizeof(int)];
}

static int host_menu_equip_swap(uintptr_t bag_node, int slot)
{
	uintptr_t rec;
	uintptr_t party;
	uintptr_t worn;
	int old_delta[3];
	int new_delta[3];
	int temp;
	int result;
	int i;

	if (!bag_node || !host_menu_heap_ok(bag_node, HOST_MENU_ITEM_NODE))
		return 0;
	if (slot < 0 || slot >= HOST_MENU_EQUIP_N || !host_menu_in_team(g_party_i))
		return 0;
	rec = HOST_MENU_EQUIP_REPO +
	      ((uintptr_t)slot +
	       (uintptr_t)g_party_i * HOST_MENU_EQUIP_STRIDE) * 8u;
	if (!host_menu_guest_ok(rec, 8))
		return 0;
	worn = host_menu_guest_ptr(rec);
	if (!worn)
		return 0;
	temp = host_menu_equip_node_temp(bag_node);
	if (temp < 1)
		return 0;
	host_menu_equip_delta(worn, old_delta);
	host_menu_equip_delta(bag_node, new_delta);
	result = ((int (*)(void *, void *, int, int))(uintptr_t)
			  HOST_MENU_EQUIP_SWAP)(
		(void *)(uintptr_t)worn, (void *)(uintptr_t)bag_node,
		g_party_i + 1, slot + 1);
	worn = host_menu_guest_ptr(rec);
	fprintf(stderr,
		"sword3-sdl: equip transaction slot=%d party=%d result=%d "
		"want=%d got=%d\n",
		slot, g_party_i, result, temp,
		host_menu_equip_node_temp(worn));
	/*
	 * ExchangeEqu's return value is an internal Lua result code. Native
	 * equipment code ignores it and validates the resulting slot instead;
	 * some talisman transactions update the slot while returning zero.
	 */
	if (host_menu_equip_node_temp(worn) != temp)
		return 0;
	party = HOST_MENU_PARTY +
		(uintptr_t)g_party_i * HOST_MENU_PARTY_STRIDE;
	for (i = 0; i < 3; i++) {
		uintptr_t field = party + HOST_MENU_OFF_HPMAX +
				  (uintptr_t)i * sizeof(int);
		int value = host_menu_guest_i32(field);

		host_menu_mem_set_i32(field,
				      value - old_delta[i] + new_delta[i]);
	}
	return 1;
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
	       g_layer == HOST_MENU_LAYER_EXIT_ASK ||
	       g_layer == HOST_MENU_LAYER_JOURNAL;
}

static void host_menu_item_sort(void)
{
	((void (*)(void))(uintptr_t)HOST_MENU_ITEM_SORT)();
	host_menu_refresh_inv();
	host_menu_item_say("整理完成", 0x2d);
	fprintf(stderr, "sword3-sdl: native item repository sort\n");
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
	else if (g_tab == HOST_MENU_TAB_SKILL) {
		g_skill_focus = 0;
		host_menu_item_party();
	}
	else if (g_tab == HOST_MENU_TAB_STATUS)
		host_menu_item_party();
	else if (g_tab == HOST_MENU_TAB_BOOK)
		g_book_focus = 0;
	fprintf(stderr, "sword3-sdl: host menu enter tab=%d\n", g_tab);
}

static void host_menu_move_slot(int dx, int dy)
{
	int delta;

	delta = dy + dx;
	if (delta == 0)
		return;
	g_slot_focus = (g_slot_focus + delta + HOST_MENU_SLOTS) %
		       HOST_MENU_SLOTS;
}

static int host_menu_refining_enabled(void)
{
	return ((int (*)(int))(uintptr_t)HOST_MENU_WIDGET_HAS)(
		       HOST_MENU_REFINING_FEATURE) != 0;
}

static int host_menu_tab_count(void)
{
	return host_menu_refining_enabled() ? HOST_MENU_TABS :
					     HOST_MENU_TABS - 1;
}

static int host_menu_tab_from_visible(int visible)
{
	if (!host_menu_refining_enabled() && visible >= HOST_MENU_TAB_REFINING)
		return visible + 1;
	return visible;
}

static int host_menu_tab_to_visible(int tab)
{
	if (!host_menu_refining_enabled() && tab > HOST_MENU_TAB_REFINING)
		return tab - 1;
	return tab;
}

static void host_menu_move_tab(int delta)
{
	int count;
	int visible;

	count = host_menu_tab_count();
	visible = host_menu_tab_to_visible(g_tab);
	visible = (visible + delta % count + count) % count;
	g_tab = host_menu_tab_from_visible(visible);
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
	int n;

	n = host_menu_item_view_count();
	if (n <= 0)
		return;
	g_item_sel = (g_item_sel + delta % n + n) % n;
	g_item_note[0] = 0;
}

static void host_menu_move_equip(int delta)
{
	g_equip_focus = (g_equip_focus + delta + HOST_MENU_EQUIP_N) %
			HOST_MENU_EQUIP_N;
	g_equip_sel = 0;
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
	if (g_layer == HOST_MENU_LAYER_JOURNAL) {
		if (index == 2)
			host_menu_move_journal(1);
		else if (index == 0)
			host_menu_move_journal(-1);
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
	    g_layer == HOST_MENU_LAYER_BESTIARY ||
	    g_layer == HOST_MENU_LAYER_JOURNAL)
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
	if (g_layer == HOST_MENU_LAYER_EXIT_ASK) {
		g_pending = 6;
		g_pending_slot = 0;
		host_menu_close();
		fprintf(stderr,
			"sword3-sdl: host menu safe exit pending\n");
		return;
	}
	if (g_layer == HOST_MENU_LAYER_TABS) {
		if (g_tab == HOST_MENU_TAB_REFINING &&
		    host_menu_refining_enabled()) {
			g_pending = 3;
			g_pending_slot = 0;
			host_menu_close();
			fprintf(stderr,
				"sword3-sdl: host menu refining pending\n");
			return;
		}
		if (g_tab == HOST_MENU_TAB_HELP)
			return;
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
		g_layer = HOST_MENU_LAYER_EXIT_ASK;
		return;
	}
	if (g_book_focus == 0 || g_book_focus == 1) {
		g_pending = g_book_focus == 0 ? 4 : 5;
		g_pending_slot = 0;
		host_menu_close();
		fprintf(stderr,
			"sword3-sdl: host menu native %s pending\n",
			g_book_focus == 0 ? "save" : "load");
		return;
	}
	if (g_book_focus == 2) {
		host_menu_journal_open();
		return;
	}
	g_stub = g_book_focus;
	g_layer = HOST_MENU_LAYER_STUB;
	fprintf(stderr, "sword3-sdl: host menu stub action=%d\n", g_stub);
}

static void host_menu_back(void)
{
	if (g_layer == HOST_MENU_LAYER_EQUIP) {
		g_layer = HOST_MENU_LAYER_INNER;
		g_stub = -1;
		host_menu_item_clear_hold();
		host_menu_refresh_party();
		host_menu_refresh_inv();
		return;
	}
	if (g_layer == HOST_MENU_LAYER_STUB ||
	    g_layer == HOST_MENU_LAYER_SLOTS ||
	    g_layer == HOST_MENU_LAYER_BESTIARY ||
	    g_layer == HOST_MENU_LAYER_JOURNAL ||
	    g_layer == HOST_MENU_LAYER_PICK ||
	    g_layer == HOST_MENU_LAYER_ASK ||
	    g_layer == HOST_MENU_LAYER_EXIT_ASK) {
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
		if (down &&
		    ((g_layer == HOST_MENU_LAYER_INNER &&
		      (g_tab == HOST_MENU_TAB_EQUIP ||
		       g_tab == HOST_MENU_TAB_SKILL ||
		       g_tab == HOST_MENU_TAB_STATUS)) ||
		     (g_layer == HOST_MENU_LAYER_EQUIP &&
		      g_tab == HOST_MENU_TAB_EQUIP))) {
			host_menu_move_party(-1);
			if (g_layer == HOST_MENU_LAYER_EQUIP)
				host_menu_clamp_equip_sel();
		} else if (down && !host_menu_layer_page()) {
			if (g_layer == HOST_MENU_LAYER_INNER &&
			    g_tab == HOST_MENU_TAB_ITEM)
				host_menu_move_item(-1);
			else
				host_menu_move_tab(-1);
		}
		return 1;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
		if (down &&
		    ((g_layer == HOST_MENU_LAYER_INNER &&
		      (g_tab == HOST_MENU_TAB_EQUIP ||
		       g_tab == HOST_MENU_TAB_SKILL ||
		       g_tab == HOST_MENU_TAB_STATUS)) ||
		     (g_layer == HOST_MENU_LAYER_EQUIP &&
		      g_tab == HOST_MENU_TAB_EQUIP))) {
			host_menu_move_party(1);
			if (g_layer == HOST_MENU_LAYER_EQUIP)
				host_menu_clamp_equip_sel();
		} else if (down && !host_menu_layer_page()) {
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
	return host_cjk_font(pt);
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

static Uint32 host_menu_surf_pixel(SDL_Surface *surf, int x, int y)
{
	int bpp;
	Uint8 *p;

	bpp = surf->format->BytesPerPixel;
	p = (Uint8 *)surf->pixels + y * surf->pitch + x * bpp;
	switch (bpp) {
	case 1:
		return *p;
	case 2:
		return *(Uint16 *)p;
	case 3:
		if (SDL_BYTEORDER == SDL_BIG_ENDIAN)
			return ((Uint32)p[0] << 16) | ((Uint32)p[1] << 8) |
			       p[2];
		return p[0] | ((Uint32)p[1] << 8) | ((Uint32)p[2] << 16);
	case 4:
		return *(Uint32 *)p;
	default:
		return 0;
	}
}

static int host_menu_row_ink(SDL_Surface *surf, int y, int thresh)
{
	int x;
	int n;
	Uint8 r;
	Uint8 g;
	Uint8 b;
	Uint8 a;

	n = 0;
	for (x = 0; x < surf->w; x++) {
		SDL_GetRGBA(host_menu_surf_pixel(surf, x, y), surf->format, &r,
			    &g, &b, &a);
		if ((int)r + (int)g + (int)b >= thresh)
			n++;
	}
	return n;
}

static SDL_Surface *host_menu_crop_plaque(SDL_Surface *src)
{
	SDL_Surface *rgba;
	SDL_Surface *out;
	SDL_Rect clip;
	int y;
	int first;
	int last;
	int half;
	int lim;

	if (!src || src->w < 8 || src->h < 8)
		return NULL;
	rgba = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_ARGB8888, 0);
	if (!rgba)
		return NULL;
	if (SDL_MUSTLOCK(rgba) && SDL_LockSurface(rgba) != 0) {
		SDL_FreeSurface(rgba);
		return NULL;
	}
	half = rgba->w / 2;
	if (half < 8)
		half = 8;
	lim = rgba->h < 96 ? rgba->h : 96;
	first = -1;
	last = -1;
	for (y = 0; y < lim; y++) {
		if (host_menu_row_ink(rgba, y, 160) >= half) {
			if (first < 0)
				first = y;
			last = y;
		}
	}
	if (SDL_MUSTLOCK(rgba))
		SDL_UnlockSurface(rgba);
	if (first < 0 || last < first) {
		SDL_FreeSurface(rgba);
		return NULL;
	}
	clip.x = 0;
	clip.y = first > 2 ? first - 2 : 0;
	clip.w = rgba->w;
	clip.h = last + 4 - clip.y;
	if (clip.h < 8 || clip.y + clip.h > rgba->h)
		clip.h = rgba->h - clip.y;
	if (clip.h >= rgba->h - 4) {
		SDL_FreeSurface(rgba);
		return NULL;
	}
	out = SDL_CreateRGBSurfaceWithFormat(0, clip.w, clip.h, 32,
					     SDL_PIXELFORMAT_ARGB8888);
	if (!out) {
		SDL_FreeSurface(rgba);
		return NULL;
	}
	SDL_SetSurfaceBlendMode(rgba, SDL_BLENDMODE_NONE);
	SDL_BlitSurface(rgba, &clip, out, NULL);
	SDL_FreeSurface(rgba);
	return out;
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

static SDL_Texture *host_menu_load_tex_plaque(SDL_Renderer *renderer,
					     const char *name)
{
	SDL_Surface *surf;
	SDL_Surface *crop;
	SDL_Texture *tex;

	if (!renderer || !name)
		return NULL;
	surf = host_menu_open_surf(name);
	if (!surf)
		return NULL;
	crop = host_menu_crop_plaque(surf);
	if (crop) {
		fprintf(stderr, "sword3-sdl: %s plaque %dx%d -> %dx%d\n", name,
			surf->w, surf->h, crop->w, crop->h);
		SDL_FreeSurface(surf);
		surf = crop;
	}
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
		g_face_obj[i] = 0;
	}
	for (i = 0; i < HOST_MENU_SLOTS; i++)
		host_menu_slot_clear(i);
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
		g_tex_book[i] = host_menu_load_tex_plaque(renderer,
							 g_book_files[i]);
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
	int resolved[0xe0 / sizeof(int)];
	uint8_t *animation;
	uintptr_t obj;
	int frame;
	int tsw;
	int tw;
	int th;

	if (!renderer || slot < 0 || slot >= HOST_MENU_PARTY_N)
		return NULL;
	a = &g_party[slot];
	if (!a->used || a->act <= 0)
		return g_tex_face[slot];
	memset(resolved, 0, sizeof(resolved));
	animation = (uint8_t *)resolved;
	*(int *)(animation + 0x04) = a->act;
	*(int *)(animation + 0x0c) = HOST_MENU_FACE_QQ;
	*(uint16_t *)(animation + 0x38) = 0;
	if (!((int (*)(void *, int))(uintptr_t)HOST_MENU_ACT_RESOLVE)(
		    animation, 0))
		return NULL;
	tsw = *(int *)(animation + 0x54);
	frame = *(uint16_t *)(animation + 0x58);
	((void *(*)(int, int, int))(uintptr_t)HOST_MENU_FRAME_LOAD)(
		tsw, frame, 1);
	obj = host_menu_tsw_obj(tsw);
	if (g_tex_face[slot] && g_face_act[slot] == a->act &&
	    g_face_obj[slot] == obj)
		return g_tex_face[slot];
	if (g_tex_face[slot]) {
		SDL_DestroyTexture(g_tex_face[slot]);
		g_tex_face[slot] = NULL;
	}
	g_face_act[slot] = a->act;
	g_face_obj[slot] = obj;
	surf = host_menu_tsw_surface(tsw, frame);
	tex = host_menu_tex_from_surf(renderer, surf);
	g_tex_face[slot] = tex;
	if (!tex && !obj) {
		g_face_act[slot] = 0;
		g_face_obj[slot] = 0;
		return NULL;
	}
	tw = 0;
	th = 0;
	if (tex)
		SDL_QueryTexture(tex, NULL, NULL, &tw, &th);
	fprintf(stderr,
		"sword3-sdl: face slot=%d act=%d -> tsw=%d frame=%d %s %dx%d obj=%llx\n",
		slot, a->act, tsw, frame, tex ? "ok" : "miss", tw, th,
		(unsigned long long)obj);
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
	SDL_Texture *face;
	const struct host_menu_actor *act;
	const char *labs[3] = { "命", "灵", "体" };
	const SDL_Color *ink[3] = { &g_ink_hp, &g_ink_mp, &g_ink_sp };
	Uint8 br[3] = { 176, 48, 48 };
	Uint8 bg[3] = { 48, 140, 64 };
	Uint8 bb[3] = { 48, 96, 176 };
	int members[HOST_MENU_PARTY_N];
	int member_n;
	int party;
	int i;
	int j;
	int gap;
	int available;
	int card_h;
	int bar_y;
	int face_w;
	int face_h;
	int party_focus;
	int focused;
	int cur[3];
	int maxv[3];
	char name[40];
	char lvline[40];
	char barline[32];

	left.x = 0;
	left.y = 0;
	left.w = host_sx(HOST_MENU_SPLIT_X, logical_w);
	left.h = logical_h;
	if (g_tex_paper)
		host_menu_blit(renderer, g_tex_paper, left);
	else
		host_menu_vgrad(renderer, left, 210, 190, 140, 168, 142, 88,
				255);

	member_n = 0;
	for (i = 0; i < HOST_MENU_PARTY_N; i++) {
		if (host_menu_in_team(i))
			members[member_n++] = i;
	}
	if (member_n == 0) {
		act = host_menu_actor();
		if (act)
			members[member_n++] = (int)(act - g_party);
	}
	gap = host_sy(4, logical_h);
	available = logical_h - host_sy(16, logical_h) -
		    (HOST_MENU_PARTY_N - 1) * gap;
	card_h = available / HOST_MENU_PARTY_N;
	if (card_h > host_sy(196, logical_h))
		card_h = host_sy(196, logical_h);
	party_focus = g_layer == HOST_MENU_LAYER_PICK ||
		      g_layer == HOST_MENU_LAYER_EQUIP ||
		      (g_layer == HOST_MENU_LAYER_INNER &&
		       (g_tab == HOST_MENU_TAB_EQUIP ||
			g_tab == HOST_MENU_TAB_SKILL ||
			g_tab == HOST_MENU_TAB_STATUS));

	for (j = 0; j < member_n; j++) {
		party = members[j];
		act = &g_party[party];
		focused = party_focus && party == g_party_i;
		if (act->name[0])
			snprintf(name, sizeof(name), "%s", act->name);
		else
			snprintf(name, sizeof(name), "%s", "—");
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

		card.x = host_sx(8, logical_w);
		card.y = host_sy(8, logical_h) + j * (card_h + gap);
		card.w = left.w - host_sx(16, logical_w);
		card.h = card_h;
		host_menu_frame(renderer, card, !party_focus || focused ? 3 : 2,
				!party_focus || focused ? g_ink_gold.r : 196,
				!party_focus || focused ? g_ink_gold.g : 164,
				!party_focus || focused ? g_ink_gold.b : 72, 255);

		portrait.x = card.x + host_sx(8, logical_w);
		portrait.y = card.y + host_sy(10, logical_h);
		portrait.w = card.h >= host_sy(190, logical_h) ?
				     host_sx(118, logical_w) :
				     host_sx(96, logical_w);
		portrait.h = card.h - host_sy(20, logical_h);
		if (portrait.h > host_sy(140, logical_h))
			portrait.h = host_sy(140, logical_h);
		host_menu_fill(renderer, portrait, 32, 22, 12, 255);
		face = host_menu_face_tex(renderer, party);
		face_w = 0;
		face_h = 0;
		if (face)
			SDL_QueryTexture(face, NULL, NULL, &face_w, &face_h);
		if (face && (face_w <= 192 || face_h <= 192))
			host_menu_blit_contain(renderer, face, portrait);
		else
			host_menu_text_center(renderer,
					      act->name[0] ? act->name : "—",
					      portrait.x + portrait.w / 2,
					      portrait.y + portrait.h / 2, pt,
					      g_ink_gold);
		host_menu_frame(renderer, portrait, 2, 196, 164, 72, 255);

		host_menu_text_left(
			renderer, name,
			portrait.x + portrait.w + host_sx(10, logical_w),
			portrait.y + host_sy(6, logical_h), pt, g_ink_paper);
		host_menu_text_left(
			renderer, lvline,
			portrait.x + portrait.w + host_sx(10, logical_w),
			portrait.y + host_sy(34, logical_h), pt_small,
			g_ink_paper);

		bar_y = portrait.y + host_sy(68, logical_h);
		for (i = 0; i < 3; i++) {
			host_menu_text_left(
				renderer, labs[i],
				portrait.x + portrait.w +
					host_sx(8, logical_w),
				bar_y + i * host_sy(24, logical_h), pt_small,
				*ink[i]);
			track.x = portrait.x + portrait.w +
				  host_sx(32, logical_w);
			track.y = bar_y + i * host_sy(24, logical_h) + 2;
			track.w = card.x + card.w - track.x -
				  host_sx(8, logical_w);
			track.h = host_sy(16, logical_h);
			if (track.w < 8)
				continue;
			host_menu_bar(renderer, track, br[i], bg[i], bb[i],
				      cur[i], maxv[i]);
			if (maxv[i] > 0)
				snprintf(barline, sizeof(barline), "%d / %d",
					 cur[i], maxv[i]);
			else
				snprintf(barline, sizeof(barline), "%s",
					 "— / —");
			host_menu_text_left(renderer, barline,
					    track.x + host_sx(4, logical_w),
					    track.y - 1, pt_small,
					    g_ink_title);
		}
		if (party_focus && !focused) {
			host_menu_fill(renderer, card, 12, 10, 8, 112);
			host_menu_frame(renderer, card, 2, 196, 164, 72, 180);
		}
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
	int count;
	int tab_i;

	x0 = host_sx(290, logical_w);
	y0 = host_sy(16, logical_h);
	th = host_sy(44, logical_h);
	gap = host_sx(4, logical_w);
	end = host_sx(848, logical_w);
	count = host_menu_tab_count();
	tw = (end - x0 - gap * (count - 1)) / count;
	for (i = 0; i < count; i++) {
		tab_i = host_menu_tab_from_visible(i);
		tab.x = x0 + i * (tw + gap);
		tab.y = y0;
		tab.w = tw;
		tab.h = th;
		host_menu_plaque(renderer, tab, g_tab == tab_i);
		host_menu_text_center(renderer, g_tab_text[tab_i],
				      tab.x + tab.w / 2, tab.y + tab.h / 2, pt,
				      g_tab == tab_i ? g_ink_tab_on :
						      g_ink_hint);
		if (g_layer == HOST_MENU_LAYER_TABS && g_tab == tab_i) {
			host_menu_frame(renderer, tab, 2, g_ink_gold.r,
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
	int count;
	char line[48];
	const SDL_Color *ink;
	const struct host_menu_item *it;
	const struct host_menu_item *sel;

	host_menu_clamp_item_sel();
	view_n = host_menu_item_view_count();
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
		sel = host_menu_item_view_at(g_item_sel);
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
		it = host_menu_item_view_at(start + i);
		if (!it)
			continue;
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

	host_menu_draw_combat(renderer, logical_w, logical_h, pt_small);
	list.x = host_sx(HOST_MENU_SPLIT_X + 12, logical_w);
	list.y = host_sy(108, logical_h);
	list.w = host_sx(358, logical_w);
	list.h = logical_h - list.y - host_sy(92, logical_h);
	host_menu_well(renderer, list);
	rh = (list.h - host_sy(16, logical_h)) / HOST_MENU_EQUIP_N;
	for (i = 0; i < HOST_MENU_EQUIP_N; i++) {
		row.x = list.x + host_sx(8, logical_w);
		row.y = list.y + host_sy(8, logical_h) + i * rh;
		row.w = list.w - host_sx(16, logical_w);
		row.h = rh - host_sy(4, logical_h);
		on = (g_layer == HOST_MENU_LAYER_INNER ||
		      g_layer == HOST_MENU_LAYER_EQUIP) &&
		     g_equip_focus == i;
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
	well.h = list.h;
	if (well.w > host_sx(40, logical_w))
		host_menu_draw_equip_pick(renderer, well, logical_w, logical_h,
					  pt, pt_small);
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
	int usable;
	char effect[112];
	char target[96];
	const struct host_menu_item *sk;
	const SDL_Color *skill_ink;

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
			usable = host_menu_item_usable(&g_skills[i]);
			if (on)
				host_menu_frame(renderer, row, 2, g_ink_gold.r,
						g_ink_gold.g, g_ink_gold.b,
						255);
			if (!usable)
				skill_ink = &g_ink_hp;
			else if (on)
				skill_ink = &g_ink_gold;
			else
				skill_ink = &g_ink_body;
			host_menu_text_left(renderer,
					    g_skills[i].name[0] ?
						    g_skills[i].name :
						    "—",
					    row.x + host_sx(8, logical_w),
					    row.y + row.h / 2 - pt_small / 2,
					    pt_small, *skill_ink);
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
				    desc.x + host_sx(12, logical_w),
				    desc.y + host_sy(34, logical_h), pt_small,
				    g_ink_hint);
		if (sk && !host_menu_item_usable(sk))
			host_menu_text_left(
				renderer, "当前菜单不可使用",
				desc.x + desc.w - host_sx(170, logical_w),
				desc.y + host_sy(34, logical_h), pt_small,
				g_ink_hp);
		host_menu_text_center(renderer, "L1/R1 切换人物",
				      desc.x + desc.w / 2,
				      desc.y + desc.h -
					      host_sy(18, logical_h),
				      pt_small, g_ink_hint);
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
	host_menu_text_center(renderer, "L1/R1 切换人物",
			      x0 + host_sx(140, logical_w),
			      logical_h - host_sy(18, logical_h), pt_small,
			      g_ink_hint);
}

static void host_menu_draw_refining(SDL_Renderer *renderer, int logical_w,
				   int logical_h, int pt, int pt_small)
{
	SDL_Rect well;

	well.x = host_sx(HOST_MENU_SPLIT_X + 16, logical_w);
	well.y = host_sy(78, logical_h);
	well.w = logical_w - well.x - host_sx(24, logical_w);
	well.h = logical_h - well.y - host_sy(16, logical_h);
	host_menu_well(renderer, well);
	host_menu_text_center(renderer, "炼妖",
			      well.x + well.w / 2,
			      well.y + well.h / 2 - host_sy(20, logical_h), pt,
			      g_ink_gold);
	host_menu_text_center(renderer, "A 进入炼妖",
			      well.x + well.w / 2,
			      well.y + well.h / 2 + host_sy(24, logical_h),
			      pt_small, g_ink_hint);
}

static void host_menu_draw_book(SDL_Renderer *renderer, int logical_w,
			       int logical_h, int pt)
{
	SDL_Rect icon;
	int i;
	int selected;
	int tw;
	int th;

	for (i = 0; i < HOST_MENU_ACTIONS; i++) {
		icon.x = host_sx(g_book_nx[i] + HOST_MENU_BOOK_PAD, logical_w);
		icon.y = host_sy(HOST_MENU_BOOK_NY, logical_h);
		icon.w = host_sx(HOST_MENU_BOOK_NW, logical_w);
		icon.h = host_sy(HOST_MENU_BOOK_NH, logical_h);
		selected = (g_layer == HOST_MENU_LAYER_INNER &&
			    g_book_focus == i);
		if (g_tex_book[i] &&
		    SDL_QueryTexture(g_tex_book[i], NULL, NULL, &tw, &th) ==
			    0 &&
		    tw > 0 && th > 0) {
			icon.h = (int)((long)th * icon.w / tw);
			if (icon.h < 1)
				icon.h = 1;
			host_menu_blit(renderer, g_tex_book[i], icon);
		} else if (!g_tex_book[i]) {
			host_menu_plaque(renderer, icon, selected);
			host_menu_text_center(renderer, g_book_text[i],
					      icon.x + icon.w / 2,
					      icon.y + icon.h / 2, pt,
					      g_ink_body);
		}
		if (selected)
			host_menu_frame(renderer, icon, 2, g_ink_gold.r,
					g_ink_gold.g, g_ink_gold.b, 255);
	}
}

static void host_menu_draw_help(SDL_Renderer *renderer, int logical_w,
			       int logical_h, int pt, int pt_small)
{
	static const struct {
		const char *key;
		const char *action;
	} basic[] = {
		{ "方向键 / 左摇杆", "移动、选择" },
		{ "A", "确认、对话" },
		{ "B", "返回、取消" },
		{ "X", "打开适配菜单" },
		{ "SELECT", "打开原生菜单" },
		{ "Y", "打开作弊菜单" },
		{ "L1 / R1", "按页面切换分页、人物或分类" },
	}, cursor[] = {
		{ "按住 R1 + 方向键 / 左摇杆", "移动临时光标" },
		{ "光标显示时按 START", "模拟触摸点击" },
		{ "短按 R1", "保留当前页面的 R1 功能" },
		{ "停止操作 5 秒", "自动隐藏光标" },
		{ "SELECT + START", "安全退出（不额外存档）" },
	};
	SDL_Rect well;
	int key_x;
	int action_x;
	int y;
	int row_h;
	size_t i;

	well.x = host_sx(HOST_MENU_SPLIT_X + 16, logical_w);
	well.y = host_sy(78, logical_h);
	well.w = logical_w - well.x - host_sx(24, logical_w);
	well.h = logical_h - well.y - host_sy(16, logical_h);
	host_menu_well(renderer, well);

	key_x = well.x + host_sx(24, logical_w);
	action_x = well.x + host_sx(270, logical_w);
	row_h = host_sy(34, logical_h);
	if (row_h < pt_small + 5)
		row_h = pt_small + 5;
	y = well.y + host_sy(20, logical_h);
	host_menu_text_left(renderer, "适配版操作说明", key_x, y, pt,
			    g_ink_gold);
	y += host_sy(48, logical_h);
	host_menu_text_left(renderer, "基础操作", key_x, y, pt_small,
			    g_ink_title);
	y += host_sy(34, logical_h);
	for (i = 0; i < sizeof(basic) / sizeof(basic[0]); i++) {
		host_menu_text_left(renderer, basic[i].key, key_x, y, pt_small,
				    g_ink_body);
		host_menu_text_left(renderer, basic[i].action, action_x, y,
				    pt_small, g_ink_hint);
		y += row_h;
	}

	y += host_sy(12, logical_h);
	host_menu_text_left(renderer, "应急触摸光标", key_x, y, pt_small,
			    g_ink_title);
	y += host_sy(34, logical_h);
	for (i = 0; i < sizeof(cursor) / sizeof(cursor[0]); i++) {
		host_menu_text_left(renderer, cursor[i].key, key_x, y, pt_small,
				    g_ink_body);
		host_menu_text_left(renderer, cursor[i].action, action_x, y,
				    pt_small, g_ink_hint);
		y += row_h;
	}

	host_menu_text_center(renderer, "真实触屏仍可直接操作",
			      well.x + well.w / 2,
			      well.y + well.h - host_sy(24, logical_h),
			      pt_small, g_ink_hint);
}

static void host_menu_load_slot(SDL_Renderer *renderer, int slot)
{
	char path[PATH_MAX];
	unsigned char hdr[HOST_MENU_SAV_THUMB_OFF];
	uint16_t *pix;
	uint32_t nmap;
	struct stat st;
	FILE *fp;
	SDL_Surface *surf;
	size_t nread;

	if (slot < 0 || slot >= HOST_MENU_SLOTS)
		return;
	host_menu_slot_clear(slot);
	if (!host_menu_slot_path(slot, path, sizeof(path)))
		return;
	if (stat(path, &st) != 0 || !S_ISREG(st.st_mode))
		return;
	g_slot_mtime[slot] = st.st_mtime;
	fp = fopen(path, "rb");
	if (!fp)
		return;
	nread = fread(hdr, 1, sizeof(hdr), fp);
	if (nread != sizeof(hdr) || memcmp(hdr, "u9SWD3", 6) != 0) {
		fclose(fp);
		return;
	}
	pix = (uint16_t *)malloc(HOST_MENU_SAV_THUMB_BYTES);
	if (!pix) {
		fclose(fp);
		return;
	}
	nread = fread(pix, 1, HOST_MENU_SAV_THUMB_BYTES, fp);
	if (nread == HOST_MENU_SAV_THUMB_BYTES && renderer) {
		surf = host_menu_expand_rgb555(pix, HOST_MENU_SAV_THUMB_W,
					       HOST_MENU_SAV_THUMB_H,
					       HOST_MENU_SAV_THUMB_W);
		g_tex_slot[slot] = host_menu_tex_from_surf(renderer, surf);
	}
	free(pix);
	nmap = 0;
	if (fread(&nmap, 4, 1, fp) == 1 && nmap > 1 && nmap < sizeof(g_slot_map[0])) {
		if (fread(g_slot_map[slot], 1, nmap, fp) == nmap)
			g_slot_map[slot][nmap - 1] = 0;
		else
			g_slot_map[slot][0] = 0;
	}
	if (fread(&g_slot_play[slot], 4, 1, fp) != 1)
		g_slot_play[slot] = 0;
	fclose(fp);
	g_slot_have[slot] = 1;
}

static void host_menu_sync_slots(SDL_Renderer *renderer)
{
	char path[PATH_MAX];
	struct stat st;
	int i;

	for (i = 0; i < HOST_MENU_SLOTS; i++) {
		if (!host_menu_slot_path(i, path, sizeof(path)) ||
		    stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
			host_menu_slot_clear(i);
			continue;
		}
		if (g_slot_mtime[i] == st.st_mtime && g_slot_have[i] &&
		    (!renderer || g_tex_slot[i]))
			continue;
		host_menu_load_slot(renderer, i);
	}
}

static void host_menu_draw_slots(SDL_Renderer *renderer, SDL_Rect well,
				int logical_w, int logical_h, int pt,
				int pt_small)
{
	SDL_Rect cell;
	SDL_Rect thumb;
	SDL_Rect list;
	int i;
	int gap;
	int row_h;
	int vis;
	int start;
	int selected;
	int used;
	int tx;
	char line[48];
	char play[32];

	host_menu_sync_slots(renderer);
	gap = host_sy(6, logical_h);
	if (gap < 4)
		gap = 4;
	host_menu_fill(renderer, well, 8, 12, 10, 230);
	host_menu_frame(renderer, well, 2, 212, 176, 88, 255);
	host_menu_text_center(renderer,
			      g_slot_mode ? "选择读档" : "选择存档",
			      well.x + well.w / 2,
			      well.y + host_sy(8, logical_h), pt, g_ink_title);
	list.x = well.x + host_sx(8, logical_w);
	list.y = well.y + host_sy(36, logical_h);
	list.w = well.w - host_sx(16, logical_w);
	list.h = well.y + well.h - list.y - host_sy(8, logical_h);
	row_h = host_sy(86, logical_h);
	if (row_h < 56)
		row_h = 56;
	vis = list.h / row_h;
	if (vis < 1)
		vis = 1;
	if (vis > HOST_MENU_SLOTS)
		vis = HOST_MENU_SLOTS;
	start = g_slot_focus - vis / 2;
	if (start < 0)
		start = 0;
	if (start + vis > HOST_MENU_SLOTS)
		start = HOST_MENU_SLOTS - vis;
	host_menu_scroll(renderer, list, start, vis, HOST_MENU_SLOTS);
	for (i = 0; i < vis; i++) {
		int slot = start + i;

		cell.x = list.x;
		cell.y = list.y + i * row_h;
		cell.w = list.w - host_sx(16, logical_w);
		cell.h = row_h - gap;
		selected = (g_slot_focus == slot);
		used = g_slot_have[slot] || host_menu_slot_used(slot);
		if (selected)
			host_menu_fill(renderer, cell, 88, 68, 28, 250);
		else
			host_menu_fill(renderer, cell, 28, 32, 26, 230);
		host_menu_frame(renderer, cell, selected ? 2 : 1, 212, 176, 88,
				selected ? 255 : 150);
		thumb.x = cell.x + host_sx(6, logical_w);
		thumb.y = cell.y + host_sy(6, logical_h);
		thumb.h = cell.h - host_sy(12, logical_h);
		thumb.w = thumb.h * HOST_MENU_SAV_THUMB_W /
			  HOST_MENU_SAV_THUMB_H;
		if (thumb.w > host_sx(120, logical_w))
			thumb.w = host_sx(120, logical_w);
		if (g_tex_slot[slot]) {
			host_menu_fill(renderer, thumb, 16, 12, 8, 255);
			host_menu_blit_contain(renderer, g_tex_slot[slot],
					       thumb);
			host_menu_frame(renderer, thumb, 1, 196, 164, 72, 200);
		} else {
			host_menu_fill(renderer, thumb, 24, 18, 12, 255);
			host_menu_frame(renderer, thumb, 1, 80, 64, 40, 180);
			host_menu_text_center(renderer, used ? "—" : "空",
					      thumb.x + thumb.w / 2,
					      thumb.y + thumb.h / 2, pt_small,
					      g_ink_hint);
		}
		tx = thumb.x + thumb.w + host_sx(10, logical_w);
		snprintf(line, sizeof(line), "档位 %d", slot + 1);
		host_menu_text_left(renderer, line, tx,
				    cell.y + host_sy(8, logical_h), pt_small,
				    g_ink_body);
		if (g_slot_have[slot] && g_slot_map[slot][0])
			host_menu_text_left(renderer, g_slot_map[slot], tx,
					    cell.y + host_sy(30, logical_h),
					    pt_small, g_ink_gold);
		else
			host_menu_text_left(renderer, used ? "已存" : "空档", tx,
					    cell.y + host_sy(30, logical_h),
					    pt_small, g_ink_hint);
		if (g_slot_have[slot]) {
			host_menu_fmt_play(play, sizeof(play),
					   g_slot_play[slot]);
			host_menu_text_left(renderer, play, tx,
					    cell.y + host_sy(52, logical_h),
					    pt_small, g_ink_hint);
		}
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

static void host_menu_draw_journal(SDL_Renderer *renderer, SDL_Rect well,
				  int logical_w, int logical_h, int pt,
				  int pt_small)
{
	SDL_Rect list;
	SDL_Rect row;
	SDL_Rect old_clip;
	SDL_bool had_clip;
	int rows;
	int start;
	int rh;
	int i;
	int index;
	int selected;

	host_menu_fill(renderer, well, 8, 12, 10, 230);
	host_menu_frame(renderer, well, 2, 212, 176, 88, 255);
	host_menu_text_left(renderer, "记载",
			    well.x + host_sx(14, logical_w),
			    well.y + host_sy(10, logical_h), pt, g_ink_gold);
	list = well;
	list.x += host_sx(10, logical_w);
	list.y += host_sy(44, logical_h);
	list.w -= host_sx(20, logical_w);
	list.h -= host_sy(76, logical_h);
	if (g_journal_n <= 0) {
		host_menu_text_center(renderer, "当前没有记载",
				      list.x + list.w / 2,
				      list.y + list.h / 2, pt, g_ink_hint);
	} else {
		rh = host_sy(64, logical_h);
		if (rh < 36)
			rh = 36;
		rows = list.h / rh;
		if (rows < 1)
			rows = 1;
		if (rows > g_journal_n)
			rows = g_journal_n;
		start = g_journal_focus - rows / 2;
		if (start < 0)
			start = 0;
		if (start + rows > g_journal_n)
			start = g_journal_n - rows;
		host_menu_scroll(renderer, list, start, rows, g_journal_n);
		had_clip = SDL_RenderIsClipEnabled(renderer);
		SDL_RenderGetClipRect(renderer, &old_clip);
		SDL_RenderSetClipRect(renderer, &list);
		for (i = 0; i < rows; i++) {
			index = start + i;
			selected = index == g_journal_focus;
			row.x = list.x;
			row.y = list.y + i * rh;
			row.w = list.w - host_sx(14, logical_w);
			row.h = rh - host_sy(4, logical_h);
			if (selected)
				host_menu_fill(renderer, row, 88, 68, 28, 180);
			if (selected)
				host_menu_frame(renderer, row, 2, g_ink_gold.r,
						g_ink_gold.g, g_ink_gold.b,
						255);
			if (g_journal[index].main_story)
				host_menu_text_center(
					renderer, "主",
					row.x + host_sx(15, logical_w),
					row.y + row.h / 2, pt_small,
					g_ink_gold);
			host_menu_text_left(
				renderer, g_journal[index].text,
				row.x + host_sx(34, logical_w),
				row.y + row.h / 2 - pt_small / 2, pt_small,
				selected ? g_ink_gold : g_ink_body);
		}
		if (had_clip)
			SDL_RenderSetClipRect(renderer, &old_clip);
		else
			SDL_RenderSetClipRect(renderer, NULL);
	}
	host_menu_text_center(renderer, "上下查看    B 返回",
			      well.x + well.w / 2,
			      well.y + well.h - host_sy(20, logical_h),
			      pt_small, g_ink_hint);
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

static void host_menu_draw_exit_ask(SDL_Renderer *renderer, SDL_Rect well,
				   int logical_w, int logical_h, int pt,
				   int pt_small)
{
	(void)logical_w;
	host_menu_fill(renderer, well, 8, 12, 10, 230);
	host_menu_frame(renderer, well, 2, 212, 176, 88, 255);
	host_menu_text_center(renderer, "确定退出游戏？",
			      well.x + well.w / 2,
			      well.y + well.h / 2 - host_sy(24, logical_h), pt,
			      g_ink_gold);
	host_menu_text_center(renderer, "退出前将完成一次自动存档",
			      well.x + well.w / 2,
			      well.y + well.h / 2 + host_sy(12, logical_h),
			      pt_small, g_ink_body);
	host_menu_text_center(renderer, "A 确定    B 取消",
			      well.x + well.w / 2,
			      well.y + well.h / 2 + host_sy(48, logical_h),
			      pt_small, g_ink_hint);
}

static void host_menu_draw_equip_pick(SDL_Renderer *renderer, SDL_Rect well,
				     int logical_w, int logical_h, int pt,
				     int pt_small)
{
	SDL_Rect list;
	SDL_Rect cmp;
	SDL_Rect hit;
	SDL_Rect old_clip;
	SDL_bool had_clip;
	char line[96];
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
	int x;
	const struct host_menu_item *worn;
	const struct host_menu_item *it;
	const SDL_Color *ink;

	n = host_menu_equip_view_count();
	host_menu_clamp_equip_sel();
	list = well;
	list.h = well.h * 3 / 5;
	cmp = well;
	cmp.y = list.y + list.h + host_sy(8, logical_h);
	cmp.h = well.y + well.h - cmp.y;
	host_menu_well(renderer, list);
	host_menu_well(renderer, cmp);
	host_menu_text_center(renderer, "待更换装备",
			      list.x + list.w / 2,
			      list.y + host_sy(20, logical_h), pt_small,
			      g_ink_gold);

	had_clip = SDL_RenderIsClipEnabled(renderer);
	SDL_RenderGetClipRect(renderer, &old_clip);
	SDL_RenderSetClipRect(renderer, &list);
	if (n <= 0)
		host_menu_text_center(renderer, "没有物品",
				      list.x + list.w / 2,
				      list.y + list.h / 2, pt, g_ink_hint);
	rh = host_sy(26, logical_h);
	if (rh < 16)
		rh = 16;
	rows = (list.h - host_sy(42, logical_h)) / rh;
	if (rows < 1)
		rows = 1;
	if (rows > n)
		rows = n;
	start = g_equip_sel - rows / 2;
	if (start < 0)
		start = 0;
	if (start + rows > n)
		start = n - rows;
	hit = list;
	hit.y += host_sy(36, logical_h);
	hit.h -= host_sy(42, logical_h);
	if (n > 0)
		host_menu_scroll(renderer, hit, start, rows, n);
	for (i = 0; i < rows; i++) {
		it = host_menu_equip_view_at(start + i);
		if (!it)
			continue;
		on = g_layer == HOST_MENU_LAYER_EQUIP &&
		     g_equip_sel == start + i;
		hit.x = list.x;
		hit.y = list.y + host_sy(36, logical_h) + i * rh;
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
	if (had_clip)
		SDL_RenderSetClipRect(renderer, &old_clip);
	else
		SDL_RenderSetClipRect(renderer, NULL);

	worn = &g_equip[g_equip_focus];
	worn_temp = worn->used && worn->temp > 0 ? worn->temp : 0;
	it = n > 0 ? host_menu_equip_view_at(g_equip_sel) : NULL;
	cand_temp = it ? it->temp : 0;
	SDL_RenderSetClipRect(renderer, &cmp);
	host_menu_text_left(renderer,
			    it && it->name[0] ? it->name :
			    (worn->used && worn->name[0] ? worn->name :
							   "没有装备"),
			    cmp.x + host_sx(10, logical_w),
			    cmp.y + host_sy(8, logical_h), pt_small,
			    it ? g_ink_gold : g_ink_hint);
	for (i = 0; i < HOST_MENU_EQUIP_STAT_N; i++) {
		oldv = worn_temp ? host_menu_item_add(worn_temp,
						      g_equip_stat_field[i]) :
				   0;
		newv = cand_temp ?
			       host_menu_item_add(cand_temp,
						  g_equip_stat_field[i]) :
			       oldv;
		delta = newv - oldv;
		x = cmp.x + host_sx(10 + (i / 3) * 130, logical_w);
		y = cmp.y + host_sy(36 + (i % 3) * 24, logical_h);
		if (it)
			snprintf(line, sizeof(line), "%s %d→%d",
				 g_equip_stat_name[i], oldv, newv);
		else if (oldv)
			snprintf(line, sizeof(line), "%s %+d",
				 g_equip_stat_name[i], oldv);
		else
			snprintf(line, sizeof(line), "%s —",
				 g_equip_stat_name[i]);
		if (delta > 0)
			ink = &g_ink_up;
		else if (delta < 0)
			ink = &g_ink_hp;
		else
			ink = &g_ink_body;
		host_menu_text_left(renderer, line, x, y, pt_small, *ink);
	}
	if (it && it->help[0])
		host_menu_text_left(renderer, it->help,
				    cmp.x + host_sx(10, logical_w),
				    cmp.y + host_sy(116, logical_h), pt_small,
				    g_ink_hint);
	else if (worn->help[0])
		host_menu_text_left(renderer, worn->help,
				    cmp.x + host_sx(10, logical_w),
				    cmp.y + host_sy(116, logical_h), pt_small,
				    g_ink_hint);
	if (g_item_note[0])
		host_menu_text_center(renderer, g_item_note,
				      cmp.x + cmp.w / 2,
				      cmp.y + cmp.h -
					      host_sy(38, logical_h),
				      pt_small, g_ink_gold);
	host_menu_text_center(renderer,
			      g_layer == HOST_MENU_LAYER_EQUIP ?
				      "上下选择 A装备 B返回" :
				      "上下选槽 A选择 L1/R1换人",
			      well.x + well.w / 2,
			      well.y + well.h - host_sy(22, logical_h),
			      pt_small, g_ink_hint);
	if (had_clip)
		SDL_RenderSetClipRect(renderer, &old_clip);
	else
		SDL_RenderSetClipRect(renderer, NULL);
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
		else if (g_tab == HOST_MENU_TAB_REFINING)
			host_menu_draw_refining(renderer, logical_w, logical_h,
					       pt, pt_small);
		else if (g_tab == HOST_MENU_TAB_BOOK)
			host_menu_draw_book(renderer, logical_w, logical_h, pt);
		else if (g_tab == HOST_MENU_TAB_HELP)
			host_menu_draw_help(renderer, logical_w, logical_h, pt,
					    pt_small);
	}

	well.x = host_sx(HOST_MENU_SPLIT_X + 12, logical_w);
	well.y = host_sy(78, logical_h);
	well.w = logical_w - well.x - host_sx(16, logical_w);
	well.h = logical_h - well.y - host_sy(16, logical_h);
	if (g_layer == HOST_MENU_LAYER_SLOTS)
		host_menu_draw_slots(renderer, well, logical_w, logical_h, pt,
				     pt_small);
	else if (g_layer == HOST_MENU_LAYER_BESTIARY)
		host_menu_draw_bestiary(renderer, well, logical_w, logical_h,
					pt, pt_small);
	else if (g_layer == HOST_MENU_LAYER_JOURNAL)
		host_menu_draw_journal(renderer, well, logical_w, logical_h,
				      pt, pt_small);
	else if (g_layer == HOST_MENU_LAYER_PICK)
		host_menu_draw_pick(renderer, well, logical_w, logical_h, pt,
				    pt_small);
	else if (g_layer == HOST_MENU_LAYER_ASK)
		host_menu_draw_ask(renderer, well, logical_w, logical_h, pt,
				   pt_small);
	else if (g_layer == HOST_MENU_LAYER_EXIT_ASK)
		host_menu_draw_exit_ask(renderer, well, logical_w, logical_h,
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
