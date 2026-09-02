#include "host_cheat.h"
#include "host_font.h"

#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>

#include <SDL2/SDL_ttf.h>

#define CHEAT_GAME_W 640
#define CHEAT_GAME_H 480
#define CHEAT_PANEL_W 360
#define CHEAT_PANEL_H 344
#define CHEAT_ROW 26
#define CHEAT_TEXT_CACHE 48
#define CHEAT_MONEY_MAX 99999999
#define CHEAT_GUEST_LO 0x100294000ull
#define CHEAT_GUEST_HI 0x100380000ull
#define CHEAT_GOLD 0x10030f63cull
#define CHEAT_LUA_BIND 0x10030aef0ull
#define CHEAT_LUA_SET 0x1001c78f0ull
#define CHEAT_STR_PLAYER 0x100273eadull
#define CHEAT_STR_GOLDS 0x100273eb8ull
#define CHEAT_SETHP 0x10007b04cull
#define CHEAT_GETHP 0x10007e4f8ull
#define CHEAT_GETMP 0x100080098ull
#define CHEAT_HUD_HP 0x1002f2178ull
#define CHEAT_HUD_MP 0x1002f21c8ull
#define CHEAT_HUD_DIRTY 0x1002f40b4ull
#define CHEAT_ACTOR 0x1002ac4d8ull
#define CHEAT_ACTOR_STRIDE 0x39b8u
#define CHEAT_ACTOR_N 10
#define CHEAT_ACTOR_HUMAN 0x3450u
#define CHEAT_ACTOR_GHOST 0x3453u
#define CHEAT_ACTOR_KEEPER 0x346du
#define CHEAT_ACTOR_NPC 0x346eu
#define CHEAT_ACTOR_DATA 0x18u
#define CHEAT_ACTOR_SPIRIT 0x30u
#define CHEAT_ACTOR_SPIRIT_HP 0x302cu
#define CHEAT_MSROLE 0x1002d0608ull
#define CHEAT_MSROLE_STRIDE 0x3490u
#define CHEAT_MSROLE_N 20
#define CHEAT_PARTY 0x1002ab1b8ull
#define CHEAT_PARTY_STRIDE 0x68u
#define CHEAT_PARTY_N 4
#define CHEAT_OFF_HP 0x04u
#define CHEAT_OFF_MP 0x08u
#define CHEAT_OFF_SP 0x0cu
#define CHEAT_OFF_HPMAX 0x10u
#define CHEAT_OFF_MPMAX 0x14u
#define CHEAT_OFF_SPMAX 0x18u
#define CHEAT_OFF_EXP 0x00u
#define CHEAT_OFF_LVUP 0x2cu
#define CHEAT_OFF_LEVEL 0x38u
#define CHEAT_MAX_MAN 0x1002f1ed0ull
#define CHEAT_MAN_ID 0x1002a53c8ull
#define CHEAT_FIGHT_FLAG 0x1002f27f8ull
#define CHEAT_GET_LEVEL_MAX 0x100005f40ull
#define CHEAT_CAL_LEVEL 0x10006a084ull
#define CHEAT_HIT_DAMAGE1 0x10007e62cull
#define CHEAT_HIT_DAMAGE3 0x10007cb24ull
#define CHEAT_RANDOM 0x1001b9784ull
#define CHEAT_CATCH_ROLL0_RA 0x10000bad4ull
#define CHEAT_CATCH_ROLL1_RA 0x10000bb1cull
#define CHEAT_CATCH_ROLL2_RA 0x10000bb6cull
#define CHEAT_STEAL_ROLL_RA 0x10000dc78ull
#define CHEAT_CAL_LIFE 0x10007b398ull
#define CHEAT_LOAD_BATTLE 0x100046188ull
#define CHEAT_LOAD_BATTLE_PLAYERMOVE_RA 0x100073d1cull
#define CHEAT_LOAD_BATTLE_PLAYERMOVE_SKIP 0x100073d4cull
#define CHEAT_UIGAMEPAD 0x100304e28ull
#define CHEAT_CLICK_SLOT 0x2d8u
#define CHEAT_CLICK_STRIDE 0x18u
#define CHEAT_CLICK_N 3
#define CHEAT_RESULT_TIMER 0x1002f1f64ull
#define CHEAT_RESULT_GATE 0x1002f3f98ull

enum cheat_item {
	CHEAT_MONEY = 0,
	CHEAT_HP,
	CHEAT_NOENC,
	CHEAT_LVUP,
	CHEAT_OHKO,
	CHEAT_CATCH,
	CHEAT_STEAL,
	CHEAT_CLOSE,
	CHEAT_N
};

static int g_open;
static int g_sel;
static int g_noenc;
static int g_lvup;
static int g_ohko;
static int g_catch;
static int g_steal;
static int g_lvup_applied;
static int g_lvup_was_fight;
static int g_hooks_ready;
static void (*g_cal_level_orig)(void);
static int (*g_hit1_orig)(void *atk, void *def, int flag);
static int (*g_hit3_orig)(void *atk, void *def, short *a, short *b, int flag);
static int (*g_random_orig)(int limit);
static void (*g_load_battle_orig)(void *script) __attribute__((used));
static char g_status[64];
static Uint32 g_status_until;
static struct {
	SDL_Texture *tex;
	SDL_Renderer *renderer;
	char text[80];
	int pt;
	Uint32 rgba;
	int w;
	int h;
} g_text[CHEAT_TEXT_CACHE];
static int g_text_clock;
static SDL_Renderer *g_text_renderer;

static int cheat_sx(int x, int logical_w)
{
	return x * logical_w / CHEAT_GAME_W;
}

static int cheat_sy(int y, int logical_h)
{
	return y * logical_h / CHEAT_GAME_H;
}

static int cheat_pt(int logical_h)
{
	int pt;

	pt = 18 * logical_h / CHEAT_GAME_H;
	if (pt < 12)
		pt = 12;
	if (pt > 28)
		pt = 28;
	return pt;
}

static int cheat_guest_ok(uintptr_t addr, size_t n)
{
	return addr >= CHEAT_GUEST_LO && addr + n - 1 < CHEAT_GUEST_HI;
}

static int cheat_heap_ok(uintptr_t addr, size_t n)
{
	if (addr < 0x10000ull || addr > 0x00007fffffffffffull)
		return 0;
	if (n == 0)
		return 1;
	if (addr + n - 1 < addr)
		return 0;
	return addr + n - 1 <= 0x00007fffffffffffull;
}

static int cheat_mem_ok(uintptr_t addr, size_t n)
{
	return cheat_guest_ok(addr, n) || cheat_heap_ok(addr, n);
}

static int cheat_i32(uintptr_t addr, int fallback)
{
	if (!cheat_mem_ok(addr, 4))
		return fallback;
	return *(volatile int *)(uintptr_t)addr;
}

static void cheat_set_i32(uintptr_t addr, int value)
{
	if (!cheat_mem_ok(addr, 4))
		return;
	*(volatile int *)(uintptr_t)addr = value;
}

static uintptr_t cheat_ptr(uintptr_t addr)
{
	if (!cheat_mem_ok(addr, sizeof(uintptr_t)))
		return 0;
	return *(volatile uintptr_t *)(uintptr_t)addr;
}

static int cheat_u8(uintptr_t addr)
{
	if (!cheat_mem_ok(addr, 1))
		return 0;
	return (int)*(volatile uint8_t *)(uintptr_t)addr;
}

static int cheat_u16(uintptr_t addr)
{
	if (!cheat_mem_ok(addr, 2))
		return 0;
	return (int)*(volatile uint16_t *)(uintptr_t)addr;
}

static int cheat_lua_ready(void)
{
	return cheat_guest_ok(CHEAT_LUA_BIND, 8) &&
	       cheat_ptr(CHEAT_LUA_BIND) != 0;
}

static void cheat_set_status(const char *s)
{
	snprintf(g_status, sizeof(g_status), "%s", s);
	g_status_until = SDL_GetTicks() + 1500;
}

static int cheat_gold(void)
{
	if (!cheat_guest_ok(CHEAT_GOLD, 4))
		return -1;
	return cheat_i32(CHEAT_GOLD, -1);
}

static void cheat_apply_money(void)
{
	if (!cheat_lua_ready() || !cheat_guest_ok(CHEAT_GOLD, 4)) {
		cheat_set_status("找不到金钱");
		return;
	}
	cheat_set_i32(CHEAT_GOLD, CHEAT_MONEY_MAX);
	((void (*)(void *, const void *, const void *, int))(
		uintptr_t)CHEAT_LUA_SET)((void *)(uintptr_t)CHEAT_LUA_BIND,
					 (const void *)(uintptr_t)CHEAT_STR_PLAYER,
					 (const void *)(uintptr_t)CHEAT_STR_GOLDS,
					 CHEAT_MONEY_MAX);
	cheat_set_status("金钱已拉满");
}

static int cheat_fill_actor(uintptr_t actor, int slot)
{
	uintptr_t data;
	int cur;
	int max;
	short mp_max;
	short mp_cur;

	if (!cheat_guest_ok(actor, CHEAT_ACTOR_STRIDE))
		return 0;
	if (cheat_u8(actor + CHEAT_ACTOR_HUMAN)) {
		data = cheat_ptr(actor + CHEAT_ACTOR_DATA);
		if (!data)
			return 0;
		((void (*)(void *, int, int, int))(uintptr_t)CHEAT_SETHP)(
			(void *)actor, -1, -1, -1);
		cur = 0;
		max = 0;
		((void (*)(void *, int *, int *))(uintptr_t)CHEAT_GETHP)(
			(void *)actor, &cur, &max);
		if (cheat_guest_ok(CHEAT_HUD_HP + (uintptr_t)slot * 4, 4))
			cheat_set_i32(CHEAT_HUD_HP + (uintptr_t)slot * 4, cur);
		mp_max = 0;
		mp_cur = 0;
		((void (*)(void *, short *, short *))(uintptr_t)CHEAT_GETMP)(
			(void *)actor, &mp_max, &mp_cur);
		if (cheat_guest_ok(CHEAT_HUD_MP + (uintptr_t)slot * 4, 4))
			cheat_set_i32(CHEAT_HUD_MP + (uintptr_t)slot * 4,
				      mp_cur);
		return 1;
	}
	data = cheat_ptr(actor + CHEAT_ACTOR_SPIRIT);
	if (!data || !cheat_heap_ok(data, 0x74))
		return 0;
	max = cheat_i32(data + 0x50, 0);
	cheat_set_i32(data + 0x70, max);
	cheat_set_i32(actor + CHEAT_ACTOR_SPIRIT_HP, max);
	return 1;
}

static int cheat_fill_party(void)
{
	int i;
	int n = 0;
	uintptr_t rec;
	int hp_max;
	int mp_max;
	int sp_max;

	if (!cheat_guest_ok(CHEAT_PARTY, CHEAT_PARTY_STRIDE * CHEAT_PARTY_N))
		return 0;
	for (i = 0; i < CHEAT_PARTY_N; i++) {
		rec = CHEAT_PARTY + (uintptr_t)i * CHEAT_PARTY_STRIDE;
		hp_max = cheat_i32(rec + CHEAT_OFF_HPMAX, 0);
		mp_max = cheat_i32(rec + CHEAT_OFF_MPMAX, 0);
		sp_max = cheat_i32(rec + CHEAT_OFF_SPMAX, 0);
		if (hp_max <= 0)
			continue;
		cheat_set_i32(rec + CHEAT_OFF_HP, hp_max);
		if (mp_max > 0)
			cheat_set_i32(rec + CHEAT_OFF_MP, mp_max);
		if (sp_max > 0)
			cheat_set_i32(rec + CHEAT_OFF_SP, sp_max);
		n++;
	}
	return n;
}

static void cheat_apply_hp(void)
{
	int i;
	int n = 0;

	for (i = 0; i < CHEAT_ACTOR_N; i++)
		n += cheat_fill_actor(CHEAT_ACTOR +
					      (uintptr_t)i * CHEAT_ACTOR_STRIDE,
				      i);
	n += cheat_fill_party();
	if (n && cheat_guest_ok(CHEAT_HUD_DIRTY, 4))
		cheat_set_i32(CHEAT_HUD_DIRTY, 0x1c);
	if (n)
		cheat_set_status("全员已满血");
	else
		cheat_set_status("队伍是空的");
}

static void cheat_open(void)
{
	g_open = 1;
	g_sel = 0;
	g_status[0] = 0;
	fprintf(stderr, "sword3-sdl: cheat open\n");
}

static void cheat_close(void)
{
	if (!g_open)
		return;
	g_open = 0;
	g_status[0] = 0;
	fprintf(stderr, "sword3-sdl: cheat close\n");
}

static void *cheat_make_tramp(uintptr_t func)
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

static int cheat_patch_jump(uintptr_t func, void *hook, const uint32_t *expect)
{
	long page;
	uintptr_t aligned;
	uint32_t stub[4];

	if (memcmp((void *)func, expect, 16) != 0) {
		fprintf(stderr,
			"sword3-sdl: cheat hook bytes mismatch at 0x%llx\n",
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
			"sword3-sdl: cheat hook mprotect failed at 0x%llx\n",
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

static int cheat_in_fight(void)
{
	return (cheat_i32(CHEAT_FIGHT_FLAG, 0) & 2) != 0;
}

static int cheat_actor_flag(uintptr_t actor, unsigned off)
{
	return cheat_guest_ok(actor + off, 1) && cheat_u8(actor + off);
}

static int cheat_is_manrole(void *role)
{
	uintptr_t p = (uintptr_t)role;
	int i;

	if (!p)
		return 0;
	for (i = 0; i < CHEAT_ACTOR_N; i++) {
		if (p == CHEAT_ACTOR + (uintptr_t)i * CHEAT_ACTOR_STRIDE)
			return 1;
	}
	return 0;
}

static int cheat_is_msrole(void *role)
{
	uintptr_t p = (uintptr_t)role;
	int i;

	if (!p)
		return 0;
	for (i = 0; i < CHEAT_MSROLE_N; i++) {
		if (p == CHEAT_MSROLE + (uintptr_t)i * CHEAT_MSROLE_STRIDE)
			return 1;
	}
	return 0;
}

static void cheat_force_next_level_exp(void)
{
	int n;
	int i;
	int idx;
	int maxlv;
	int lv;
	int exp;
	int need;
	uintptr_t actor;
	uintptr_t rec;

	n = cheat_i32(CHEAT_MAX_MAN, 0);
	if (n < 0)
		return;
	if (n > CHEAT_ACTOR_N)
		n = CHEAT_ACTOR_N;
	for (i = 0; i < n; i++) {
		actor = CHEAT_ACTOR + (uintptr_t)i * CHEAT_ACTOR_STRIDE;
		if (!cheat_guest_ok(actor, CHEAT_ACTOR_STRIDE))
			continue;
		if (!cheat_actor_flag(actor, CHEAT_ACTOR_HUMAN))
			continue;
		if (cheat_actor_flag(actor, CHEAT_ACTOR_KEEPER) ||
		    cheat_actor_flag(actor, CHEAT_ACTOR_NPC) ||
		    cheat_actor_flag(actor, CHEAT_ACTOR_GHOST))
			continue;
		if (!cheat_guest_ok(CHEAT_MAN_ID + (uintptr_t)i * 4, 4))
			continue;
		idx = cheat_i32(CHEAT_MAN_ID + (uintptr_t)i * 4, -1);
		if (idx < 0 || idx >= CHEAT_PARTY_N)
			continue;
		rec = CHEAT_PARTY + (uintptr_t)idx * CHEAT_PARTY_STRIDE;
		if (!cheat_guest_ok(rec, CHEAT_PARTY_STRIDE))
			continue;
		maxlv = ((int (*)(int))(uintptr_t)CHEAT_GET_LEVEL_MAX)(idx + 1);
		lv = cheat_u16(rec + CHEAT_OFF_LEVEL);
		if (maxlv <= lv)
			continue;
		exp = cheat_i32(rec + CHEAT_OFF_EXP, 0);
		need = cheat_i32(rec + CHEAT_OFF_LVUP, 0);
		if (need > 0 && exp < need)
			cheat_set_i32(rec + CHEAT_OFF_EXP, need);
	}
}

static void cheat_cal_level_hook(void)
{
	if (g_lvup && !g_lvup_applied) {
		cheat_force_next_level_exp();
		g_lvup_applied = 1;
		fprintf(stderr, "sword3-sdl: cheat CalLevel apply next-level exp\n");
	}
	g_cal_level_orig();
}

/*
 * Result and touch state can survive the battle transition. The next fight
 * reuses NowMenu 100 for auto, so clear timer/gate/click state on fight end
 * and again at LoadBattle.
 */
static void cheat_clear_result_latch(void)
{
	uint8_t *pad;
	unsigned i;

	if (cheat_guest_ok(CHEAT_RESULT_TIMER, 4))
		cheat_set_i32(CHEAT_RESULT_TIMER, 0);
	if (cheat_guest_ok(CHEAT_RESULT_GATE, 1))
		*(volatile uint8_t *)(uintptr_t)CHEAT_RESULT_GATE = 0;
	if (!cheat_guest_ok(CHEAT_UIGAMEPAD + CHEAT_CLICK_SLOT,
			    CHEAT_CLICK_STRIDE * CHEAT_CLICK_N))
		return;
	pad = (uint8_t *)(uintptr_t)CHEAT_UIGAMEPAD + CHEAT_CLICK_SLOT;
	for (i = 0; i < CHEAT_CLICK_N; i++)
		memset(pad + i * CHEAT_CLICK_STRIDE, 0, CHEAT_CLICK_STRIDE);
}

void host_cheat_on_fight_end(void)
{
	cheat_clear_result_latch();
}

/*
 * FightFlag bit 2 can stay set across a skipped result screen, so the
 * poll edge never fires and the second fight would refuse to apply.
 * LoadBattle is the real start of a fight (the no-encounter skip does
 * not come through here).
 */
static void cheat_note_new_fight(void) __attribute__((used, noinline));
static void cheat_note_new_fight(void)
{
	if (g_lvup_applied)
		fprintf(stderr,
			"sword3-sdl: cheat CalLevel reset for new fight\n");
	g_lvup_applied = 0;
	g_lvup_was_fight = 1;
	cheat_clear_result_latch();
}

static int cheat_role_dead(void *role)
{
	uintptr_t p = (uintptr_t)role;

	/* ROLE::SetDeath writes 1 at this+0x3457. Do not use +0x3030 bit 31:
	 * the engine sets that before HitDamage as a "taking a hit" lock. */
	return cheat_u8(p + 0x3457u) != 0;
}

static int cheat_onehit_finish(void *atk, void *def, int flag)
{
	static int nlog;

	if (!g_ohko || !atk || !def)
		return 0;
	/* flag!=0 is forecast / setup. Those calls walk every enemy. */
	if (flag)
		return 0;
	if (nlog < 12) {
		fprintf(stderr,
			"sword3-sdl: onehit atk=%p def=%p man=%d/%d slotM=%d/%d slotMs=%d/%d flag=%d\n",
			atk, def,
			cheat_actor_flag((uintptr_t)atk, CHEAT_ACTOR_HUMAN),
			cheat_actor_flag((uintptr_t)def, CHEAT_ACTOR_HUMAN),
			cheat_is_manrole(atk), cheat_is_manrole(def),
			cheat_is_msrole(atk), cheat_is_msrole(def), flag);
		nlog++;
	}
	if (atk == def)
		return 0;
	if (cheat_role_dead(def))
		return 0;
	/* Exclude only when the pointer is a known wrong-side slot.
	 * If identity fails, fall back to the human flag like Android. */
	if (cheat_is_msrole(atk) || cheat_is_manrole(def))
		return 0;
	if (!cheat_actor_flag((uintptr_t)atk, CHEAT_ACTOR_HUMAN) ||
	    cheat_actor_flag((uintptr_t)def, CHEAT_ACTOR_HUMAN))
		return 0;
	((void (*)(void *, int, short, short))(uintptr_t)CHEAT_CAL_LIFE)(
		def, 9999, 0, 0);
	fprintf(stderr, "sword3-sdl: onehit apply def=%p\n", def);
	return 1;
}

static int cheat_hit1_hook(void *atk, void *def, int flag)
{
	int dmg;

	dmg = g_hit1_orig(atk, def, flag);
	if (cheat_onehit_finish(atk, def, flag))
		return 9999;
	return dmg;
}

static int cheat_hit3_hook(void *atk, void *def, short *a, short *b, int flag)
{
	int dmg;

	dmg = g_hit3_orig(atk, def, a, b, flag);
	if (cheat_onehit_finish(atk, def, flag)) {
		if (a && cheat_mem_ok((uintptr_t)a, 2))
			*a = 9999;
		if (b && cheat_mem_ok((uintptr_t)b, 2))
			*b = 9999;
		return 9999;
	}
	return dmg;
}

static int cheat_random_hook(int limit)
{
	uintptr_t caller;
	int result;

	caller = (uintptr_t)__builtin_return_address(0);
	result = g_random_orig(limit);
	if (g_catch && limit == 100 &&
	    (caller == CHEAT_CATCH_ROLL0_RA ||
	     caller == CHEAT_CATCH_ROLL1_RA ||
	     caller == CHEAT_CATCH_ROLL2_RA))
		return 0;
	if (g_steal && limit == 100 && caller == CHEAT_STEAL_ROLL_RA)
		return 0;
	return result;
}

/*
 * iOS has no ChanceOfBattle. Random fights are Lua PlayerMove writing
 * the battle-script pointer, then LoadBattle at 0x100073d18. Skip that
 * call and resume at 0x100073d4c so InBattle is not set. Hand-written
 * so the skip path can ret to a different guest address; GCC ignores
 * naked and will restore the original x30 through a C epilogue.
 */
_Static_assert(CHEAT_LOAD_BATTLE_PLAYERMOVE_RA == 0x100073d1cull,
	       "PlayerMove LoadBattle return address");
_Static_assert(CHEAT_LOAD_BATTLE_PLAYERMOVE_SKIP == 0x100073d4cull,
	       "PlayerMove LoadBattle skip address");
void cheat_load_battle_hook(void *script);

__asm__(
	"	.text\n"
	"	.align	2\n"
	"	.globl	cheat_load_battle_hook\n"
	"	.hidden	cheat_load_battle_hook\n"
	"	.type	cheat_load_battle_hook, %function\n"
	"cheat_load_battle_hook:\n"
	"	adrp	x1, g_noenc\n"
	"	ldr	w1, [x1, :lo12:g_noenc]\n"
	"	cbz	w1, 1f\n"
	"	mov	x1, #0x3d1c\n"
	"	movk	x1, #0x7, lsl #16\n"
	"	movk	x1, #0x1, lsl #32\n"
	"	cmp	x30, x1\n"
	"	b.ne	1f\n"
	"	mov	x30, #0x3d4c\n"
	"	movk	x30, #0x7, lsl #16\n"
	"	movk	x30, #0x1, lsl #32\n"
	"	ret\n"
	"1:\n"
	"	stp	x0, x30, [sp, #-16]!\n"
	"	bl	cheat_note_new_fight\n"
	"	ldp	x0, x30, [sp], #16\n"
	"	adrp	x1, g_load_battle_orig\n"
	"	ldr	x1, [x1, :lo12:g_load_battle_orig]\n"
	"	br	x1\n"
	"	.size	cheat_load_battle_hook, .-cheat_load_battle_hook\n"
);

void host_cheat_install(void)
{
	static const uint32_t cal_expect[4] = {
		0xa9ba6ffcu, 0xa90167fau, 0xa9025ff8u, 0xa90357f6u
	};
	static const uint32_t hit1_expect[4] = {
		0xd10203ffu, 0xa9026ffcu, 0xa90367fau, 0xa9045ff8u
	};
	static const uint32_t hit3_expect[4] = {
		0xd10303ffu, 0xa9066ffcu, 0xa90767fau, 0xa9085ff8u
	};
	static const uint32_t random_expect[4] = {
		0xa9bc5ff8u, 0xa90157f6u, 0xa9024ff4u, 0xa9037bfdu
	};
	static const uint32_t load_expect[4] = {
		0xd10243ffu, 0x6d0223e9u, 0xa9036ffcu, 0xa90467fau
	};

	if (g_hooks_ready)
		return;
	g_cal_level_orig = cheat_make_tramp(CHEAT_CAL_LEVEL);
	g_hit1_orig = cheat_make_tramp(CHEAT_HIT_DAMAGE1);
	g_hit3_orig = cheat_make_tramp(CHEAT_HIT_DAMAGE3);
	g_random_orig = cheat_make_tramp(CHEAT_RANDOM);
	g_load_battle_orig = cheat_make_tramp(CHEAT_LOAD_BATTLE);
	if (!g_cal_level_orig || !g_hit1_orig || !g_hit3_orig ||
	    !g_random_orig || !g_load_battle_orig) {
		fprintf(stderr, "sword3-sdl: cheat tramp mmap failed\n");
		return;
	}
	g_hooks_ready = 1;
	if (cheat_patch_jump(CHEAT_CAL_LEVEL, cheat_cal_level_hook,
			     cal_expect) != 0)
		return;
	if (cheat_patch_jump(CHEAT_HIT_DAMAGE1, cheat_hit1_hook,
			     hit1_expect) != 0)
		return;
	if (cheat_patch_jump(CHEAT_HIT_DAMAGE3, cheat_hit3_hook,
			     hit3_expect) != 0)
		return;
	if (cheat_patch_jump(CHEAT_RANDOM, cheat_random_hook,
			     random_expect) != 0)
		return;
	if (cheat_patch_jump(CHEAT_LOAD_BATTLE, cheat_load_battle_hook,
			     load_expect) != 0)
		return;
	fprintf(stderr,
		"sword3-sdl: cheat CalLevel/HitDamage/Catch/LoadBattle hooks installed\n");
}

void host_cheat_poll(void)
{
	int f;

	f = cheat_in_fight();
	if (f && !g_lvup_was_fight)
		g_lvup_applied = 0;
	g_lvup_was_fight = f;
}

static void cheat_apply(void)
{
	if (g_sel == CHEAT_CLOSE) {
		cheat_close();
		return;
	}
	if (g_sel == CHEAT_MONEY) {
		cheat_apply_money();
		return;
	}
	if (g_sel == CHEAT_HP) {
		cheat_apply_hp();
		return;
	}
	if (g_sel == CHEAT_NOENC) {
		g_noenc = !g_noenc;
		cheat_set_status(g_noenc ? "不遇敌已开" : "不遇敌已关");
		return;
	}
	if (g_sel == CHEAT_LVUP) {
		g_lvup = !g_lvup;
		cheat_set_status(g_lvup ? "战后升级已开" : "战后升级已关");
		return;
	}
	if (g_sel == CHEAT_OHKO) {
		g_ohko = !g_ohko;
		cheat_set_status(g_ohko ? "一击必杀已开" : "一击必杀已关");
		return;
	}
	if (g_sel == CHEAT_CATCH) {
		g_catch = !g_catch;
		cheat_set_status(g_catch ? "抓怪必成已开" : "抓怪必成已关");
		return;
	}
	if (g_sel == CHEAT_STEAL) {
		g_steal = !g_steal;
		cheat_set_status(g_steal ? "偷窃必成已开" : "偷窃必成已关");
		return;
	}
	cheat_set_status("尚未实现");
}

int host_cheat_active(void)
{
	return g_open;
}

int host_cheat_button(int button, int down)
{
	if (button == SDL_CONTROLLER_BUTTON_Y) {
		if (down) {
			if (g_open)
				cheat_close();
			else
				cheat_open();
		}
		return 1;
	}
	if (!g_open)
		return 0;
	if (!down)
		return 1;
	switch (button) {
	case SDL_CONTROLLER_BUTTON_DPAD_UP:
		g_sel = (g_sel + CHEAT_N - 1) % CHEAT_N;
		return 1;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
		g_sel = (g_sel + 1) % CHEAT_N;
		return 1;
	case SDL_CONTROLLER_BUTTON_A:
		cheat_apply();
		return 1;
	case SDL_CONTROLLER_BUTTON_B:
		cheat_close();
		return 1;
	default:
		return 1;
	}
}

static Uint32 cheat_rgba(SDL_Color c)
{
	return ((Uint32)c.r << 24) | ((Uint32)c.g << 16) |
	       ((Uint32)c.b << 8) | (Uint32)c.a;
}

static void cheat_text_flush(SDL_Renderer *renderer)
{
	int i;

	for (i = 0; i < CHEAT_TEXT_CACHE; i++) {
		if (!g_text[i].tex)
			continue;
		if (renderer && g_text[i].renderer != renderer)
			continue;
		SDL_DestroyTexture(g_text[i].tex);
		g_text[i].tex = NULL;
		g_text[i].renderer = NULL;
		g_text[i].text[0] = 0;
	}
}

static int cheat_text_size(SDL_Renderer *renderer, const char *s, int pt,
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
	rgba = cheat_rgba(color);
	for (i = 0; i < CHEAT_TEXT_CACHE; i++) {
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
	font = host_cjk_font(pt);
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
	slot = g_text_clock % CHEAT_TEXT_CACHE;
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

static void cheat_text(SDL_Renderer *renderer, int x, int y, int pt,
		       SDL_Color color, const char *s)
{
	SDL_Texture *tex;
	SDL_Rect dst;
	int w;
	int h;

	if (!cheat_text_size(renderer, s, pt, color, &w, &h, &tex) || !tex)
		return;
	dst.x = x;
	dst.y = y;
	dst.w = w;
	dst.h = h;
	SDL_RenderCopy(renderer, tex, NULL, &dst);
}

static const char *cheat_label(int i, char *buf, size_t n)
{
	switch (i) {
	case CHEAT_MONEY:
		return "金钱最大";
	case CHEAT_HP:
		return "全员满血";
	case CHEAT_NOENC:
		snprintf(buf, n, "不遇敌    %s", g_noenc ? "开" : "关");
		return buf;
	case CHEAT_LVUP:
		snprintf(buf, n, "战后升级  %s", g_lvup ? "开" : "关");
		return buf;
	case CHEAT_OHKO:
		snprintf(buf, n, "一击必杀  %s", g_ohko ? "开" : "关");
		return buf;
	case CHEAT_CATCH:
		snprintf(buf, n, "抓怪必成  %s", g_catch ? "开" : "关");
		return buf;
	case CHEAT_STEAL:
		snprintf(buf, n, "偷窃必成  %s", g_steal ? "开" : "关");
		return buf;
	case CHEAT_CLOSE:
		return "关闭";
	default:
		return "";
	}
}

void host_cheat_draw(SDL_Renderer *renderer, int logical_w, int logical_h)
{
	SDL_BlendMode old_bm;
	Uint8 or_;
	Uint8 og;
	Uint8 ob;
	Uint8 oa;
	SDL_Rect dim;
	SDL_Rect box;
	SDL_Rect hi;
	int pw;
	int ph;
	int px;
	int py;
	int pt;
	int i;
	int iy;
	int row;
	int gold;
	char item[40];
	char gold_line[32];
	const char *label;
	SDL_Color title = {255, 220, 120, 255};
	SDL_Color on = {255, 255, 210, 255};
	SDL_Color off = {210, 200, 180, 255};
	SDL_Color hint = {170, 160, 140, 255};
	SDL_Color ok = {140, 230, 150, 255};

	if (!g_open || !renderer || logical_w <= 0 || logical_h <= 0)
		return;
	if (g_text_renderer != renderer) {
		cheat_text_flush(g_text_renderer);
		g_text_renderer = renderer;
	}
	pt = cheat_pt(logical_h);
	pw = cheat_sx(CHEAT_PANEL_W, logical_w);
	ph = cheat_sy(CHEAT_PANEL_H, logical_h);
	if (ph > logical_h - cheat_sy(8, logical_h))
		ph = logical_h - cheat_sy(8, logical_h);
	px = (logical_w - pw) / 2;
	py = (logical_h - ph) / 2;
	row = cheat_sy(CHEAT_ROW, logical_h);
	gold = cheat_gold();
	if (gold < 0)
		snprintf(gold_line, sizeof(gold_line), "%s", "金钱  --");
	else
		snprintf(gold_line, sizeof(gold_line), "金钱  %d", gold);
	SDL_GetRenderDrawBlendMode(renderer, &old_bm);
	SDL_GetRenderDrawColor(renderer, &or_, &og, &ob, &oa);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	dim.x = 0;
	dim.y = 0;
	dim.w = logical_w;
	dim.h = logical_h;
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 140);
	SDL_RenderFillRect(renderer, &dim);
	box.x = px;
	box.y = py;
	box.w = pw;
	box.h = ph;
	SDL_SetRenderDrawColor(renderer, 28, 22, 16, 230);
	SDL_RenderFillRect(renderer, &box);
	SDL_SetRenderDrawColor(renderer, 210, 170, 70, 255);
	SDL_RenderDrawRect(renderer, &box);
	box.x += 2;
	box.y += 2;
	box.w -= 4;
	box.h -= 4;
	SDL_RenderDrawRect(renderer, &box);
	cheat_text(renderer, px + cheat_sx(24, logical_w),
		   py + cheat_sy(16, logical_h), pt, title, "修改器");
	cheat_text(renderer, px + cheat_sx(100, logical_w),
		   py + cheat_sy(18, logical_h), pt, hint, "mod by kk(k源机)");
	cheat_text(renderer, px + cheat_sx(24, logical_w),
		   py + cheat_sy(48, logical_h), pt, hint, gold_line);
	for (i = 0; i < CHEAT_N; i++) {
		iy = py + cheat_sy(74, logical_h) + i * row;
		label = cheat_label(i, item, sizeof(item));
		if (i == g_sel) {
			hi.x = px + cheat_sx(16, logical_w);
			hi.y = iy - cheat_sy(4, logical_h);
			hi.w = pw - cheat_sx(32, logical_w);
			hi.h = row;
			SDL_SetRenderDrawColor(renderer, 90, 70, 28, 220);
			SDL_RenderFillRect(renderer, &hi);
			cheat_text(renderer, px + cheat_sx(28, logical_w), iy,
				   pt, on, label);
		} else {
			cheat_text(renderer, px + cheat_sx(28, logical_w), iy,
				   pt, off, label);
		}
	}
	cheat_text(renderer, px + cheat_sx(24, logical_w),
		   py + ph - cheat_sy(60, logical_h), pt, hint,
		   "A 确定   B/Y 关闭   上下选择");
	if (g_status[0] && SDL_GetTicks() < g_status_until)
		cheat_text(renderer, px + cheat_sx(24, logical_w),
			   py + ph - cheat_sy(34, logical_h), pt, ok, g_status);
	SDL_SetRenderDrawBlendMode(renderer, old_bm);
	SDL_SetRenderDrawColor(renderer, or_, og, ob, oa);
}
