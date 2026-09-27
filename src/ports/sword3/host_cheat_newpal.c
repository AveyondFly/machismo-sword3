#include "host_cheat_newpal.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

/* These addresses belong only to the SHA-256 pinned NewPAL executable. */
#define NP_MONEY 0x1001d09fcull
#define NP_PARTY_LIST 0x1002400c0ull
#define NP_LIST_ITEM 0x10007a1fcull
#define NP_ROLE_PROPERTY 0x10002eb2cull
#define NP_ROLE_BASE_PROPERTY 0x10002e3c0ull
#define NP_ROLE_ADJUST 0x10004599cull
#define NP_REWARD 0x10004d0b8ull
#define NP_DAMAGE 0x100031328ull
#define NP_SCRIPT_STEP 0x1000694b4ull
#define NP_SCENE_TRANSITION 0x100009160ull
#define NP_SCENE_MODE_SETTER 0x10002d360ull
#define NP_SCENE_STATE 0x1001d000cull
#define NP_SCENE_REQUEST 0x1001d0010ull
#define NP_MAP_OBJECT_COUNT 0x1001d0528ull
#define NP_MAP_OBJECTS 0x1001f8810ull
#define NP_CURRENT_OBJECT_ID 0x1001f3f30ull
#define NP_CURRENT_OBJECT_INDEX 0x1001f3f34ull
#define NP_SCRIPT_DATA 0x1001f5928ull
#define NP_FIELD_SCRIPT 0x10022c810ull
#define NP_FIND_MAP_OBJECT 0x10000fc94ull
#define NP_MAP_ACTOR_SLOTS 0x10022c878ull
#define NP_CLEAR_MAP_ACTOR 0x1000119a4ull
#define NP_CLEAR_MAP_COLLISION 0x10000ede8ull
#define NP_UNLINK_MAP_OBJECT 0x100013dc0ull
#define NP_MONEY_MAX 60000
/* Item objects keep their count as property 20. The inventory array is only
 * rebuilt from them when saving, so update both. */
#define NP_ITEM_LIST 0x10023a0f0ull
#define NP_ITEM_PROPERTY 0x100034088ull
#define NP_INVENTORY_COUNT 0x100248c04ull
#define NP_INVENTORY 0x100248c08ull
#define NP_INVENTORY_ENTRY_SIZE 0x14
#define NP_INVENTORY_LIMIT 300
#define NP_ITEM_COUNT 20
#define NP_ITEM_PUPPET 3902
#define NP_ITEM_COUNT_MAX 99
/* The old woman's hut: each bedside trigger shows its later text once the
 * story script swaps in the next event, the same swap done here. */
#define NP_OBJECT_EVENT 0x26
#define NP_LINGER_OBJECT 2
#define NP_LINGER_ASLEEP 1153
#define NP_LINGER_SMILING 1196
#define NP_YUERU_OBJECT 3
#define NP_YUERU_COLD 1197
#define NP_YUERU_WARM 1198
/* Script opcodes 0x19 and 0x15/0x16 set and test story flags. The ending
 * checks the flag each wake branch sets, not the swapped event. */
#define NP_FLAG_TEST 0x100011438ull
#define NP_FLAG_SET 0x100011264ull
#define NP_FLAG_CLEAR 0x10001121cull
#define NP_LINGER_FLAG 1602
#define NP_YUERU_FLAG 1601
#define NP_LIST_LIMIT 32
#define NP_MAP_OBJECT_SIZE 0x68
/* The object array ends where NP_FIELD_SCRIPT begins. */
#define NP_MAP_OBJECT_CAPACITY 2048
#define NP_MAP_ACTOR_SLOT_SIZE 0x21c
#define NP_MAP_ACTOR_SLOT_COUNT 72
#define NP_OP_BATTLE 0x309
/* Clears the requested field step when the tile map blocks it. */
#define NP_FIELD_COLLISION 0x100073d60ull
/* Party followers path-find to a point on the leader's trail. */
#define NP_FOLLOWER_UPDATE 0x1000235ccull
#define NP_PARTY_COUNT 0x1001f6b48ull
#define NP_PARTY_OBJECTS 0x1001f6b4cull
#define NP_FOLLOWER_PATHS 0x1001f5a68ull
#define NP_FOLLOWER_PATH_SIZE 0x21c
#define NP_PATH_STEP_INDEX 0x02
#define NP_PATH_WAITING 0x8000
#define NP_PATH_DELTA 0x16
#define NP_PATH_STEPS 0x1c
#define NP_TRAIL_X 0x1001f6b6cull
#define NP_TRAIL_Y 0x1001f6becull
#define NP_STEP_DX 0x100180a6cull
#define NP_STEP_DY 0x100180a8cull
#define NP_ACTOR_SPEED 0x08
#define NP_ACTOR_FACING 0x34
#define NP_WALK_SPEED 8
#define NP_PACK_LOOKUP 0x100041564ull
#define NP_PACK_CLOSE 0x100041880ull
#define NP_PACK_HANDLES 0x10023e6e0ull
#define NP_PACK_HANDLE_COUNT 250

enum newpal_property {
	NP_LEVEL_CAP = 4,
	NP_LEVEL = 5,
	NP_EXP_NEXT = 10,
	NP_EXP = 11,
	NP_HP_MAX = 16,
	NP_HP = 17,
	NP_MP_MAX = 22,
	NP_MP = 23
};

static int g_install_attempted;
static int g_level_up;
static int g_one_hit;
static int g_no_encounter;
static int g_noclip;
static unsigned g_encounters_suppressed;
static unsigned g_encounters_bypassed;
static unsigned g_map_monsters_removed;
static unsigned g_monster_input_kept;
static unsigned g_active_monster_input;

static int (*g_reward_orig)(void);
static int (*g_damage_orig)(void *source, void *target);
static int (*g_script_step_orig)(void *script);
static void (*g_scene_transition_orig)(int requested);
static void (*g_scene_mode_orig)(int mode);
static void (*g_field_collision_orig)(void *object, int *dx, int *dy);
static void (*g_follower_update_orig)(int index);

/* Walking through walls reaches monsters and penalty zones the player would
 * otherwise route around, so it always skips their battles too. */
static int newpal_skip_encounters(void)
{
	return g_no_encounter || g_noclip;
}

static int newpal_list_count(void *list)
{
	int count;

	if (!list)
		return 0;
	count = *(int *)((uint8_t *)list + 8);
	return count > 0 && count <= NP_LIST_LIMIT ? count : 0;
}

static void *newpal_list_role(void *list, int index)
{
	void *node = ((void *(*)(void *, int))(uintptr_t)NP_LIST_ITEM)(
		list, index);
	return node ? *(void **)((uint8_t *)node + 8) : NULL;
}

static int *newpal_property(void *role, int property)
{
	if (!role)
		return NULL;
	return ((int *(*)(void *, int))(uintptr_t)NP_ROLE_PROPERTY)(
		role, property);
}

static int *newpal_base_property(void *role, int property)
{
	if (!role)
		return NULL;
	return ((int *(*)(void *, int))(uintptr_t)NP_ROLE_BASE_PROPERTY)(
		role, property);
}

static int newpal_fill_property(void *role, int current_property,
			       int max_property)
{
	int *current = newpal_property(role, current_property);
	int *maximum = newpal_property(role, max_property);
	int before;
	int limit;

	if (!current || !maximum || *maximum < 1 || *maximum > 999999)
		return 0;
	before = *current;
	limit = *maximum;
	if (before < limit) {
		((int (*)(int, int, int))(uintptr_t)NP_ROLE_ADJUST)(
			*(int *)role, current_property, limit - before);
		current = newpal_property(role, current_property);
		/* The engine normally updates every copy; keep this one in sync. */
		if (current && *current < limit)
			*current = limit;
	}
	fprintf(stderr,
		"sword3-sdl: NewPAL trainer role=%d property=%d %d->%d max=%d\n",
		*(int *)role, current_property, before,
		current ? *current : before, limit);
	return current && *current == limit;
}

static void *newpal_party(void)
{
	void *list = *(void **)(uintptr_t)NP_PARTY_LIST;
	int count = newpal_list_count(list);
	int i;

	for (i = 0; i < count; i++) {
		void *role = newpal_list_role(list, i);
		int id;

		if (!role)
			continue;
		id = *(int *)role;
		if (id >= 2000 && id < 3000)
			return list;
	}
	return NULL;
}

int host_cheat_newpal_money(void)
{
	if (!newpal_party())
		return 0;
	*(volatile int *)(uintptr_t)NP_MONEY = NP_MONEY_MAX;
	fprintf(stderr, "sword3-sdl: NewPAL trainer money=%d\n",
		NP_MONEY_MAX);
	return 1;
}

int host_cheat_newpal_heal(void)
{
	void *party = newpal_party();
	int count = newpal_list_count(party);
	int filled = 0;
	int i;

	for (i = 0; i < count; i++) {
		void *role = newpal_list_role(party, i);

		if (!role || *(int *)role < 2000 || *(int *)role >= 3000)
			continue;
		if (!newpal_fill_property(role, NP_HP, NP_HP_MAX))
			continue;
		newpal_fill_property(role, NP_MP, NP_MP_MAX);
		filled++;
	}
	if (filled)
		fprintf(stderr, "sword3-sdl: NewPAL trainer filled %d party members\n",
			filled);
	return filled;
}

static void *newpal_find_item(int id)
{
	void *list = *(void **)(uintptr_t)NP_ITEM_LIST;
	int count;
	int i;

	if (!list)
		return NULL;
	count = *(int *)((uint8_t *)list + 8);
	if (count <= 0 || count > NP_INVENTORY_LIMIT)
		return NULL;
	for (i = 0; i < count; i++) {
		void *item = newpal_list_role(list, i);

		if (item && *(int *)item == id)
			return item;
	}
	return NULL;
}

int host_cheat_newpal_has_puppet(void)
{
	return newpal_find_item(NP_ITEM_PUPPET) != NULL;
}

int host_cheat_newpal_fill_puppet(void)
{
	void *item = newpal_find_item(NP_ITEM_PUPPET);
	int count = *(volatile int *)(uintptr_t)NP_INVENTORY_COUNT;
	int *value;
	int before;
	int i;

	if (!item)
		return 0;
	value = ((int *(*)(void *, int))(uintptr_t)NP_ITEM_PROPERTY)(
		*(void **)((uint8_t *)item + 0x70), NP_ITEM_COUNT);
	if (!value)
		return 0;
	before = *value;
	*value = NP_ITEM_COUNT_MAX;
	for (i = 0; i < count && i < NP_INVENTORY_LIMIT; i++) {
		int *entry = (int *)((uint8_t *)(uintptr_t)NP_INVENTORY +
				     (size_t)i * NP_INVENTORY_ENTRY_SIZE);

		if (entry[0] == NP_ITEM_PUPPET)
			entry[1] = NP_ITEM_COUNT_MAX;
	}
	fprintf(stderr, "sword3-sdl: NewPAL trainer puppet count %d->%d\n",
		before, NP_ITEM_COUNT_MAX);
	return 1;
}

static uint16_t *newpal_wake_event(int who, int include_awake)
{
	int count = *(volatile int *)(uintptr_t)NP_MAP_OBJECT_COUNT;
	int object_id = who == NP_WAKE_YUERU ? NP_YUERU_OBJECT : NP_LINGER_OBJECT;
	int asleep = who == NP_WAKE_YUERU ? NP_YUERU_COLD : NP_LINGER_ASLEEP;
	int awake = who == NP_WAKE_YUERU ? NP_YUERU_WARM : NP_LINGER_SMILING;
	int i;

	if (count <= 0 || count > NP_MAP_OBJECT_CAPACITY)
		return NULL;
	for (i = 1; i < count; i++) {
		uint8_t *object = (uint8_t *)(uintptr_t)NP_MAP_OBJECTS +
			(size_t)i * NP_MAP_OBJECT_SIZE;
		uint16_t *event = (uint16_t *)(object + NP_OBJECT_EVENT);

		if (*(uint16_t *)(object + 0x2c) == object_id &&
		    (*event == asleep || (include_awake && *event == awake)))
			return event;
	}
	return NULL;
}

int host_cheat_newpal_wake_scene(void)
{
	return newpal_wake_event(NP_WAKE_LINGER, 1) ||
	       newpal_wake_event(NP_WAKE_YUERU, 1);
}

static int newpal_wake_flag(int who)
{
	return who == NP_WAKE_YUERU ? NP_YUERU_FLAG : NP_LINGER_FLAG;
}

static int newpal_flag_set(int flag)
{
	return ((int (*)(int))(uintptr_t)NP_FLAG_TEST)(flag) & 1;
}

int host_cheat_newpal_can_wake(int who)
{
	return newpal_wake_event(who, 0) ||
	       (newpal_wake_event(who, 1) &&
		!newpal_flag_set(newpal_wake_flag(who)));
}

int host_cheat_newpal_wake(int who)
{
	uint16_t *event;

	if (!host_cheat_newpal_can_wake(who))
		return 0;
	event = newpal_wake_event(who, 0);
	if (event)
		*event = who == NP_WAKE_YUERU ? NP_YUERU_WARM :
			NP_LINGER_SMILING;
	((void (*)(int))(uintptr_t)NP_FLAG_SET)(newpal_wake_flag(who));
	fprintf(stderr, "sword3-sdl: NewPAL trainer wake %s flag=%d set=%d\n",
		who == NP_WAKE_YUERU ? "yueru" : "linger",
		newpal_wake_flag(who), newpal_flag_set(newpal_wake_flag(who)));
	return 1;
}

int host_cheat_newpal_in_game(void)
{
	return newpal_party() != NULL;
}

int host_cheat_newpal_ending(void)
{
	int linger = newpal_flag_set(NP_LINGER_FLAG);
	int yueru = newpal_flag_set(NP_YUERU_FLAG);

	if (linger && yueru)
		return -1;
	return linger ? NP_ENDING_LINGER : yueru ? NP_ENDING_YUERU :
		NP_ENDING_NORMAL;
}

/* Only one wake flag may be set, so each ending clears the other one. */
int host_cheat_newpal_set_ending(int ending)
{
	void (*set)(int) = (void (*)(int))(uintptr_t)NP_FLAG_SET;
	void (*clear)(int) = (void (*)(int))(uintptr_t)NP_FLAG_CLEAR;

	if (!host_cheat_newpal_in_game())
		return 0;
	(ending == NP_ENDING_LINGER ? set : clear)(NP_LINGER_FLAG);
	(ending == NP_ENDING_YUERU ? set : clear)(NP_YUERU_FLAG);
	fprintf(stderr,
		"sword3-sdl: NewPAL trainer ending=%d linger=%d yueru=%d\n",
		ending, newpal_flag_set(NP_LINGER_FLAG),
		newpal_flag_set(NP_YUERU_FLAG));
	return 1;
}

static void newpal_apply_level_up(void)
{
	void *party = newpal_party();
	int count = newpal_list_count(party);
	int i;

	for (i = 0; i < count; i++) {
		void *role = newpal_list_role(party, i);
		int *level_cap = newpal_base_property(role, NP_LEVEL_CAP);
		int *level = newpal_property(role, NP_LEVEL);
		int *exp_next = newpal_property(role, NP_EXP_NEXT);
		int *exp = newpal_property(role, NP_EXP);

		if (!role || *(int *)role < 2000 || *(int *)role >= 3000 ||
		    !level_cap || !level || !exp_next || !exp)
			continue;
		fprintf(stderr,
			"sword3-sdl: NewPAL trainer reward role=%d level=%d cap=%d exp=%d/%d\n",
			*(int *)role, *level, *level_cap, *exp, *exp_next);
		if (*level < 1 || *level >= 99 || *level_cap < 1 ||
		    *exp < 0 || *exp_next <= 0 || *exp_next > 9999999)
			continue;
		/* The reward screen checks this raw cap before the EXP threshold. */
		if (*level_cap < 99) {
			fprintf(stderr,
				"sword3-sdl: NewPAL trainer level cap role=%d %d->99\n",
				*(int *)role, *level_cap);
			*level_cap = 99;
		}
		if (*exp >= *exp_next)
			continue;
		fprintf(stderr,
			"sword3-sdl: NewPAL trainer level-up role=%d level=%d exp=%d->%d\n",
			*(int *)role, *level, *exp, *exp_next);
		*exp = *exp_next;
	}
}

static int newpal_reward_hook(void)
{
	int result = g_reward_orig();

	/* The original function transfers battle EXP into property 11 first. */
	if (g_level_up)
		fprintf(stderr,
			"sword3-sdl: NewPAL trainer reward step result=%d\n",
			result);
	if (result && g_level_up)
		newpal_apply_level_up();
	return result;
}

static int newpal_damage_hook(void *source, void *target)
{
	static unsigned samples;
	int damage = g_damage_orig(source, target);
	int source_id = source ? *(int *)source : 0;
	int target_id = target ? *(int *)target : 0;
	int *hp;

	if (g_one_hit && samples++ < 16)
		fprintf(stderr,
			"sword3-sdl: NewPAL trainer damage sample source=%d target=%d value=%d\n",
			source_id, target_id, damage);
	if (!g_one_hit || damage >= 0 || source_id < 2000 ||
	    source_id >= 3000 || target_id <= 0 || target_id >= 2000)
		return damage;
	hp = newpal_property(target, NP_HP);
	if (!hp || *hp <= 0 || *hp > 999999)
		return damage;
	damage = *hp > 9999 ? -(*hp * 4) : -99999;
	fprintf(stderr,
		"sword3-sdl: NewPAL trainer one-hit source=%d enemy=%d hp=%d damage=%d\n",
		source_id, target_id, *hp, damage);
	return damage;
}

/* Visible monsters use object ids 5000-5999 on most maps and 4000-4999 on
 * some; the object class flags and battle scripts are the same. */
static int newpal_monster_id(unsigned id)
{
	return id >= 4000 && id < 6000;
}

static int newpal_hide_encounter_object(int event_id, int index)
{
	int count = *(volatile int *)(uintptr_t)NP_MAP_OBJECT_COUNT;
	uint8_t *object;
	uint32_t flags;
	uint16_t object_id;
	int slot;

	if (index <= 0 || index >= count ||
	    count > NP_MAP_OBJECT_CAPACITY)
		return 0;
	object = (uint8_t *)(uintptr_t)NP_MAP_OBJECTS +
		(size_t)index * NP_MAP_OBJECT_SIZE;
	object_id = *(uint16_t *)(object + 0x2c);
	flags = *(uint32_t *)(object + 0x18);
	/* Match the active event and the map's visible monster object class. */
	if (event_id != object_id || !newpal_monster_id(object_id) ||
	    (flags & 0xf000u) != 0xf000u ||
	    (flags & 0x10000000u) ||
	    !*(void **)(object + 0x58))
		return 0;
	for (slot = 0; slot < NP_MAP_ACTOR_SLOT_COUNT; slot++) {
		uint8_t *actor = (uint8_t *)(uintptr_t)NP_MAP_ACTOR_SLOTS +
			(size_t)slot * NP_MAP_ACTOR_SLOT_SIZE;

		if (*(uint16_t *)actor == index)
			((void (*)(void *))(uintptr_t)NP_CLEAR_MAP_ACTOR)(actor);
	}
	*(uint32_t *)(object + 0x18) = flags & 0xffff3fffu;
	((void (*)(void *))(uintptr_t)NP_CLEAR_MAP_COLLISION)(object);
	flags = *(uint32_t *)(object + 0x18);
	*(uint32_t *)(object + 0x18) = flags | 0x10000000u;
	((void (*)(void *, int))(uintptr_t)NP_UNLINK_MAP_OBJECT)(
		object, flags & 3);
	g_map_monsters_removed++;
	return 1;
}

static int newpal_bypass_visible_encounter(void *script, int state)
{
	unsigned object_id;
	unsigned offset;
	int object_index = -1;
	int count;
	uint8_t *object;
	uint32_t flags;
	const uint8_t *data;
	const uint16_t *code;

	if (!newpal_skip_encounters() || state != 0)
		return 0;
	object_id = *(uint16_t *)((uint8_t *)script + 0x2c);
	offset = *(uint16_t *)((uint8_t *)script + 0x28);
	if (!newpal_monster_id(object_id) || offset > 0xffd0)
		return 0;
	data = *(const uint8_t **)(uintptr_t)NP_SCRIPT_DATA;
	if (!data)
		return 0;
	code = (const uint16_t *)(data + offset);
	/* The observed monster scripts enter at this opcode sequence. */
	if (code[0] != 0x43 || (code[1] & 0x7fff) != 0x320 ||
	    code[2] != 0x309 || !code[3] ||
	    code[8] != 0x28 || code[9] != object_id)
		return 0;
	if (!((int (*)(int, int *))(uintptr_t)
	      NP_FIND_MAP_OBJECT)(object_id, &object_index))
		return 0;
	count = *(volatile int *)(uintptr_t)NP_MAP_OBJECT_COUNT;
	if (object_index <= 0 || object_index >= count ||
	    count > NP_MAP_OBJECT_CAPACITY)
		return 0;
	object = (uint8_t *)(uintptr_t)NP_MAP_OBJECTS +
		(size_t)object_index * NP_MAP_OBJECT_SIZE;
	flags = *(uint32_t *)(object + 0x18);
	if (*(uint16_t *)(object + 0x2c) != object_id ||
	    (flags & 0xf000u) != 0xf000u ||
	    (flags & 0x10000000u) || !*(void **)(object + 0x58))
		return 0;
	if (!newpal_hide_encounter_object(object_id, object_index))
		return 0;
	/* The engine must finish this script, but need not enter battle. */
	*(uint16_t *)((uint8_t *)script + 0x28) = offset + 8;
	g_encounters_bypassed++;
	g_encounters_suppressed++;
	return 1;
}

static int newpal_hide_script_encounter_object(int battle,
					       unsigned script_offset)
{
	const uint16_t *code = *(const uint16_t **)(uintptr_t)NP_SCRIPT_DATA;
	unsigned offset;

	if (!code || script_offset > 0xffc0)
		return 0;
	for (offset = script_offset; offset < script_offset + 64;
	     offset += 2) {
		unsigned instruction = offset / 2;
		unsigned next;

		if (code[instruction] != 0x309 ||
		    code[instruction + 1] != battle)
			continue;
		for (next = offset + 4; next < offset + 32; next += 2) {
			unsigned next_instruction = next / 2;
			int object_id;
			int object_index;

			if (code[next_instruction] == 0xffff)
				break;
			if (code[next_instruction] != 0x28)
				continue;
			object_id = code[next_instruction + 1];
			if (!newpal_monster_id(object_id))
				continue;
			object_index = -1;
			if (!((int (*)(int, int *))(uintptr_t)
			      NP_FIND_MAP_OBJECT)(object_id, &object_index))
				return 0;
			return newpal_hide_encounter_object(
				object_id, object_index);
		}
		break;
	}
	return 0;
}

/* Penalty-zone triggers start their script with the battle; the observed
 * story battles run dialogue or other steps before theirs. */
#define NP_PENALTY_BATTLE_OFFSET_MAX 8

static int newpal_penalty_battle(const uint16_t *at, int battle,
				 unsigned offset)
{
	return at && offset <= NP_PENALTY_BATTLE_OFFSET_MAX &&
	       at[0] == NP_OP_BATTLE && at[1] == battle;
}

static int newpal_script_step_hook(void *script)
{
	static unsigned samples;
	int entry_event = *(volatile int *)(uintptr_t)NP_CURRENT_OBJECT_ID;
	int entry_index = *(volatile int *)(uintptr_t)NP_CURRENT_OBJECT_INDEX;
	unsigned entry_offset = *(uint16_t *)((uint8_t *)script + 0x28);
	int entry_script[12];
	int state_before = *(volatile int *)(uintptr_t)NP_SCENE_STATE;
	int request_before = *(volatile int *)(uintptr_t)NP_SCENE_REQUEST;
	int state_after;
	int request_after;
	int result;

	newpal_bypass_visible_encounter(script, state_before);
	memcpy(entry_script, script, sizeof(entry_script));
	result = g_script_step_orig(script);
	state_after = *(volatile int *)(uintptr_t)NP_SCENE_STATE;
	request_after = *(volatile int *)(uintptr_t)NP_SCENE_REQUEST;
	if (newpal_skip_encounters() && state_before == 0 &&
	    request_after > 0) {
		int event = *(volatile int *)(uintptr_t)NP_CURRENT_OBJECT_ID;
		int index = *(volatile int *)(uintptr_t)NP_CURRENT_OBJECT_INDEX;
		int hidden;

		if (g_encounters_suppressed < 4)
			fprintf(stderr,
				"sword3-sdl: NewPAL trainer encounter offset=%u script=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d entry=%d/%d exit=%d/%d\n",
				entry_offset,
				entry_script[0], entry_script[1], entry_script[2],
				entry_script[3], entry_script[4], entry_script[5],
				entry_script[6], entry_script[7], entry_script[8],
				entry_script[9], entry_script[10], entry_script[11],
				entry_event, entry_index, event, index);
		hidden = newpal_hide_script_encounter_object(request_after,
							 entry_offset);
		if (!hidden)
			hidden = newpal_hide_encounter_object(event, index);
		if (!hidden && (entry_event != event || entry_index != index))
			hidden = newpal_hide_encounter_object(entry_event,
						       entry_index);
		if (!hidden) {
			const uint16_t *code =
				*(const uint16_t **)(uintptr_t)NP_SCRIPT_DATA;
			const uint16_t *at = code && entry_offset <= 0xffc0 ?
				code + entry_offset / 2 : NULL;

			if (g_noclip &&
			    newpal_penalty_battle(at, request_after,
						  entry_offset)) {
				fprintf(stderr,
					"sword3-sdl: NewPAL trainer noclip suppressed penalty battle request=%d offset=%u source=%u head=%04x %04x %04x %04x\n",
					request_after, entry_offset,
					*(uint16_t *)((uint8_t *)entry_script +
						      0x2c),
					code[0], code[1], code[2], code[3]);
				hidden = 1;
			}
		}
		if (!hidden) {
			const uint16_t *code =
				*(const uint16_t **)(uintptr_t)NP_SCRIPT_DATA;
			const uint16_t *at = code && entry_offset <= 0xffc0 ?
				code + entry_offset / 2 : NULL;

			fprintf(stderr,
				"sword3-sdl: NewPAL trainer preserved scripted battle request=%d offset=%u active=%u source=%u event=%d/%d\n",
				request_after, entry_offset,
				*(uint16_t *)((uint8_t *)script + 0x2c),
				*(uint16_t *)((uint8_t *)entry_script + 0x2c),
				event, index);
			if (at)
				fprintf(stderr,
					"sword3-sdl: NewPAL trainer preserved code %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x %04x head=%04x %04x %04x %04x\n",
					at[0], at[1], at[2], at[3], at[4], at[5],
					at[6], at[7], at[8], at[9], at[10], at[11],
					at[12], at[13], at[14], at[15],
					code[0], code[1], code[2], code[3]);
			return result;
		}
		*(volatile int *)(uintptr_t)NP_SCENE_REQUEST = -1;
		g_encounters_suppressed++;
		if (g_encounters_suppressed <= 4 ||
		    !(g_encounters_suppressed & (g_encounters_suppressed - 1)))
			fprintf(stderr,
				"sword3-sdl: NewPAL trainer suppressed battle request=%d count=%u result=%d offset=%u active=%u\n",
				request_after, g_encounters_suppressed, result,
				*(uint16_t *)((uint8_t *)script + 0x28),
				*(uint16_t *)((uint8_t *)script + 0x2c));
		return result;
	}
	if (newpal_skip_encounters() && state_before == 0 &&
	    (state_after != state_before || request_after != request_before) &&
	    samples++ < 32) {
		const int *words = script;

		fprintf(stderr,
			"sword3-sdl: NewPAL trainer scene request=%d->%d state=%d->%d script=%d,%d,%d,%d suppressed=%u\n",
			request_before, request_after, state_before, state_after,
			words[0], words[7], words[8], words[11],
			g_encounters_suppressed);
	}
	return result;
}

static int newpal_active_visible_encounter(void)
{
	const uint8_t *script = (const uint8_t *)(uintptr_t)NP_FIELD_SCRIPT;
	unsigned object_id = *(const uint16_t *)(script + 0x2c);
	unsigned offset = *(const uint16_t *)(script + 0x28);
	int object_index = -1;
	int count;
	const uint8_t *object;
	uint32_t flags;

	if (!newpal_monster_id(object_id) || offset > 26) {
		g_active_monster_input = 0;
		return 0;
	}
	if (g_active_monster_input == object_id)
		return 1;
	if (!((int (*)(int, int *))(uintptr_t)
	      NP_FIND_MAP_OBJECT)(object_id, &object_index))
		return 0;
	count = *(volatile int *)(uintptr_t)NP_MAP_OBJECT_COUNT;
	if (object_index <= 0 || object_index >= count ||
	    count > NP_MAP_OBJECT_CAPACITY)
		return 0;
	object = (const uint8_t *)(uintptr_t)NP_MAP_OBJECTS +
		(size_t)object_index * NP_MAP_OBJECT_SIZE;
	flags = *(const uint32_t *)(object + 0x18);
	if (*(const uint16_t *)(object + 0x2c) != object_id ||
	    (flags & 0xf000u) != 0xf000u ||
	    (flags & 0x10000000u) || !*(void *const *)(object + 0x58))
		return 0;
	g_active_monster_input = object_id;
	return 1;
}

static void newpal_scene_mode_hook(int mode)
{
	if (g_active_monster_input) {
		const uint8_t *script =
			(const uint8_t *)(uintptr_t)NP_FIELD_SCRIPT;

		if (*(const uint16_t *)(script + 0x2c) !=
		    g_active_monster_input ||
		    *(const uint16_t *)(script + 0x28) > 26)
			g_active_monster_input = 0;
	}
	if (newpal_skip_encounters() && mode == 0 &&
	    newpal_active_visible_encounter()) {
		mode = 7;
		g_monster_input_kept++;
	}
	g_scene_mode_orig(mode);
}

static void newpal_scene_transition_hook(int requested)
{
	static unsigned samples;
	int state = *(volatile int *)(uintptr_t)NP_SCENE_STATE;
	uintptr_t caller = (uintptr_t)__builtin_return_address(0);

	if (newpal_skip_encounters() && state == 0 && samples++ < 32)
		fprintf(stderr,
			"sword3-sdl: NewPAL trainer preserved transition request=%d caller=0x%llx suppressed=%u\n",
			requested, (unsigned long long)caller,
			g_encounters_suppressed);
	g_scene_transition_orig(requested);
}

void host_cheat_newpal_level_up(int enabled)
{
	g_level_up = enabled;
	fprintf(stderr, "sword3-sdl: NewPAL trainer level-up=%d\n", enabled);
}

void host_cheat_newpal_one_hit(int enabled)
{
	g_one_hit = enabled;
	fprintf(stderr, "sword3-sdl: NewPAL trainer one-hit=%d\n", enabled);
}

static void newpal_set_encounter_modes(int no_encounter, int noclip)
{
	if ((no_encounter || noclip) && !newpal_skip_encounters()) {
		g_encounters_suppressed = 0;
		g_encounters_bypassed = 0;
		g_map_monsters_removed = 0;
		g_monster_input_kept = 0;
	}
	if (!no_encounter && !noclip)
		g_active_monster_input = 0;
	g_no_encounter = no_encounter;
	g_noclip = noclip;
}

void host_cheat_newpal_no_encounter(int enabled)
{
	fprintf(stderr,
		"sword3-sdl: NewPAL trainer no-encounter=%d suppressed=%u bypassed=%u removed=%u input-kept=%u\n",
		enabled, g_encounters_suppressed,
		g_encounters_bypassed, g_map_monsters_removed,
		g_monster_input_kept);
	newpal_set_encounter_modes(enabled, g_noclip);
}

void host_cheat_newpal_noclip(int enabled)
{
	fprintf(stderr, "sword3-sdl: NewPAL trainer noclip=%d hooked=%d\n",
		enabled, g_field_collision_orig != NULL);
	newpal_set_encounter_modes(g_no_encounter, enabled);
}

static void newpal_field_collision_hook(void *object, int *dx, int *dy)
{
	if (g_noclip)
		return;
	g_field_collision_orig(object, dx, dy);
}

static int newpal_sign(int value)
{
	return (value > 0) - (value < 0);
}

/* With noclip the leader stands inside walls, so the follower's path search
 * keeps failing. Step straight toward its trail point instead, using the
 * same step state the original writes after a successful search. */
static void newpal_follower_update_hook(int index)
{
	const int32_t *step_dx = (const int32_t *)(uintptr_t)NP_STEP_DX;
	const int32_t *step_dy = (const int32_t *)(uintptr_t)NP_STEP_DY;
	const int32_t *party = (const int32_t *)(uintptr_t)NP_PARTY_OBJECTS;
	int count = *(volatile int *)(uintptr_t)NP_PARTY_COUNT;
	uint8_t *object;
	uint8_t *actor;
	uint8_t *path;
	uint8_t *leader_actor;
	int x;
	int y;
	int dx;
	int dy;
	int slot;
	int code;

	if (!g_noclip || count < 2 || count > 16) {
		g_follower_update_orig(index);
		return;
	}
	for (slot = 1; slot < count && party[slot] != index; slot++)
		;
	object = (uint8_t *)(uintptr_t)NP_MAP_OBJECTS +
		(size_t)index * NP_MAP_OBJECT_SIZE;
	actor = *(uint8_t **)(object + 0x58);
	if (slot >= count || !actor) {
		g_follower_update_orig(index);
		return;
	}
	x = *(uint16_t *)(object + 8);
	y = *(uint16_t *)(object + 0xa);
	if ((x | y) & 0xf)
		return;
	path = (uint8_t *)(uintptr_t)NP_FOLLOWER_PATHS +
		(size_t)slot * NP_FOLLOWER_PATH_SIZE;
	/* The per-frame party mover only advances a path owned by this object
	 * whose step index is clear; the step list ends with 0xff, which also
	 * makes the original search again once noclip is off. */
	*(uint16_t *)path = (uint16_t)index;
	*(uint32_t *)(path + NP_PATH_DELTA) = 0;
	path[NP_PATH_STEPS + 1] = 0xff;
	dx = newpal_sign(((const int32_t *)(uintptr_t)NP_TRAIL_X)[slot * 2] - x);
	dy = newpal_sign(((const int32_t *)(uintptr_t)NP_TRAIL_Y)[slot * 2] - y);
	if (!dx && !dy) {
		*(uint16_t *)(path + NP_PATH_STEP_INDEX) = NP_PATH_WAITING;
		path[NP_PATH_STEPS] = 0xff;
		leader_actor = *(uint8_t **)((uint8_t *)(uintptr_t)NP_MAP_OBJECTS +
			(size_t)*(volatile int *)(uintptr_t)NP_CURRENT_OBJECT_INDEX *
			NP_MAP_OBJECT_SIZE + 0x58);
		*(int32_t *)(actor + NP_ACTOR_SPEED) = 0;
		if (leader_actor)
			*(int32_t *)(actor + NP_ACTOR_FACING) =
				*(int32_t *)(leader_actor + NP_ACTOR_FACING);
		return;
	}
	for (code = 0; code < 8; code++)
		if (newpal_sign(step_dx[code]) == dx &&
		    newpal_sign(step_dy[code]) == dy)
			break;
	if (code == 8)
		return;
	*(uint16_t *)(path + NP_PATH_STEP_INDEX) = 0;
	path[NP_PATH_STEPS] = (uint8_t)code;
	*(int16_t *)(path + NP_PATH_DELTA) = (int16_t)(step_dx[code] * 2);
	*(int16_t *)(path + NP_PATH_DELTA + 2) = (int16_t)(step_dy[code] * 2);
	*(int32_t *)(actor + NP_ACTOR_SPEED) = NP_WALK_SPEED;
}

static void *newpal_trampoline(uintptr_t function);

/* Opening a pack file that is already open returns the same FILE*, and every
 * opener closes it. Darwin ignores the repeated fclose; glibc aborts with a
 * double free. Keep the handle open until its last opener closes it. */
static uint16_t g_pack_extra_refs[NP_PACK_HANDLE_COUNT];
static void *(*g_pack_lookup_orig)(const char *path, int mode);
static int (*g_pack_close_orig)(void *handle);

static int newpal_pack_index(void *handle)
{
	void *const *handles = (void *const *)(uintptr_t)NP_PACK_HANDLES;
	int i;

	for (i = 0; i < NP_PACK_HANDLE_COUNT; i++)
		if (handles[i] == handle)
			return i;
	return -1;
}

static void *newpal_pack_lookup_hook(const char *path, int mode)
{
	void *handle = g_pack_lookup_orig(path, mode);
	int index = handle ? newpal_pack_index(handle) : -1;

	if (index >= 0)
		g_pack_extra_refs[index]++;
	return handle;
}

static int newpal_pack_close_hook(void *handle)
{
	int index = newpal_pack_index(handle);

	if (index >= 0 && g_pack_extra_refs[index]) {
		g_pack_extra_refs[index]--;
		return 1;
	}
	return g_pack_close_orig(handle);
}

void host_newpal_prepare(void)
{
	static const uint32_t lookup_expected[4] = {
		0xa9bd57f6u, 0xa9014ff4u, 0xa9027bfdu, 0x910083fdu
	};
	static const uint32_t close_expected[4] = {
		0xa9be4ff4u, 0xa9017bfdu, 0x910043fdu, 0xaa0003f3u
	};

	if (memcmp((const void *)(uintptr_t)NP_PACK_LOOKUP,
		   lookup_expected, 16) ||
	    memcmp((const void *)(uintptr_t)NP_PACK_CLOSE,
		   close_expected, 16)) {
		fprintf(stderr,
			"sword3-sdl: NewPAL pack handle hook fingerprint mismatch\n");
		return;
	}
	g_pack_lookup_orig = newpal_trampoline(NP_PACK_LOOKUP);
	g_pack_close_orig = newpal_trampoline(NP_PACK_CLOSE);
	if (!g_pack_lookup_orig || !g_pack_close_orig ||
	    host_newpal_patch_jump(NP_PACK_LOOKUP, newpal_pack_lookup_hook,
				   lookup_expected) != 0 ||
	    host_newpal_patch_jump(NP_PACK_CLOSE, newpal_pack_close_hook,
				   close_expected) != 0) {
		fprintf(stderr, "sword3-sdl: NewPAL pack handle hooks failed\n");
		return;
	}
	fprintf(stderr, "sword3-sdl: NewPAL pack handle refcount installed\n");
}

static void *newpal_trampoline(uintptr_t function)
{
	uint32_t *code;
	uint64_t adrp_page = 0;
	long page_size = sysconf(_SC_PAGESIZE);

	if (page_size < 4096)
		page_size = 4096;
	code = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (code == MAP_FAILED)
		return NULL;
	memcpy(code, (const void *)function, 16);
	/* A copied ADRP must still refer to its original PC page. Put the
	 * resolved page in a literal after the return jump. */
	if ((code[3] & 0x9f000000u) == 0x90000000u) {
		int64_t imm = ((int64_t)((code[3] >> 5) & 0x7ffffu) << 2) |
			((code[3] >> 29) & 3u);

		if (imm & (1 << 20))
			imm -= 1 << 21;
		adrp_page = (uint64_t)(((function + 12) & ~(uintptr_t)0xfff) +
					  imm * 4096);
		code[3] = 0x58000000u | (5u << 5) | (code[3] & 31u);
	}
	code[4] = 0x58000051u; /* ldr x17, #8 */
	code[5] = 0xd61f0220u; /* br x17 */
	*(uint64_t *)(code + 6) = function + 16;
	if (adrp_page)
		*(uint64_t *)(code + 8) = adrp_page;
	__builtin___clear_cache((char *)code, (char *)code + 40);
	return code;
}

int host_newpal_patch_jump(uintptr_t function, void *hook,
			   const uint32_t expected[4])
{
	uint32_t stub[4] = { 0x58000050u, 0xd61f0200u, 0, 0 };
	uintptr_t page;
	long page_size = sysconf(_SC_PAGESIZE);

	if (memcmp((const void *)function, expected, 16) != 0) {
		fprintf(stderr, "sword3-sdl: NewPAL trainer hook mismatch at 0x%llx\n",
			(unsigned long long)function);
		return -1;
	}
	if (page_size < 4096)
		page_size = 4096;
	page = function & ~((uintptr_t)page_size - 1);
	if (mprotect((void *)page, (size_t)page_size * 2,
		     PROT_READ | PROT_WRITE | PROT_EXEC) != 0)
		return -1;
	memcpy(stub + 2, &hook, sizeof(hook));
	memcpy((void *)function, stub, sizeof(stub));
	__builtin___clear_cache((char *)function, (char *)function + 16);
	return 0;
}

void host_cheat_newpal_install(void)
{
	static const uint32_t reward_expected[4] = {
		0xd10183ffu, 0xa90167fau, 0xa9025ff8u, 0xa90357f6u
	};
	static const uint32_t damage_expected[4] = {
		0x6dba23e9u, 0xa90167fau, 0xa9025ff8u, 0xa90357f6u
	};
	static const uint32_t script_expected[4] = {
		0xd10443ffu, 0xa90b6ffcu, 0xa90c67fau, 0xa90d5ff8u
	};
	static const uint32_t transition_expected[4] = {
		0xa9bd57f6u, 0xa9014ff4u, 0xa9027bfdu, 0x910083fdu
	};
	static const uint32_t scene_mode_expected[4] = {
		0xa9be4ff4u, 0xa9017bfdu, 0x910043fdu, 0x90001214u
	};
	static const uint32_t collision_expected[4] = {
		0xd10443ffu, 0xa90d5ff8u, 0xa90e57f6u, 0xa90f4ff4u
	};
	static const uint32_t follower_expected[4] = {
		0xd10303ffu, 0xa9066ffcu, 0xa90767fau, 0xa9085ff8u
	};
	void (*collision_orig)(void *, int *, int *);
	void (*follower_orig)(int);

	if (g_install_attempted)
		return;
	g_install_attempted = 1;
	if (memcmp((const void *)(uintptr_t)NP_REWARD, reward_expected, 16) ||
	    memcmp((const void *)(uintptr_t)NP_DAMAGE, damage_expected, 16) ||
	    memcmp((const void *)(uintptr_t)NP_SCRIPT_STEP,
		   script_expected, 16) ||
	    memcmp((const void *)(uintptr_t)NP_SCENE_TRANSITION,
		   transition_expected, 16) ||
	    memcmp((const void *)(uintptr_t)NP_SCENE_MODE_SETTER,
		   scene_mode_expected, 16) ||
	    memcmp((const void *)(uintptr_t)NP_FIELD_COLLISION,
		   collision_expected, 16) ||
	    memcmp((const void *)(uintptr_t)NP_FOLLOWER_UPDATE,
		   follower_expected, 16)) {
		fprintf(stderr,
			"sword3-sdl: NewPAL trainer executable hook fingerprint mismatch\n");
		return;
	}
	g_reward_orig = newpal_trampoline(NP_REWARD);
	g_damage_orig = newpal_trampoline(NP_DAMAGE);
	g_script_step_orig = newpal_trampoline(NP_SCRIPT_STEP);
	g_scene_transition_orig = newpal_trampoline(NP_SCENE_TRANSITION);
	g_scene_mode_orig = newpal_trampoline(NP_SCENE_MODE_SETTER);
	collision_orig = newpal_trampoline(NP_FIELD_COLLISION);
	follower_orig = newpal_trampoline(NP_FOLLOWER_UPDATE);
	if (!g_reward_orig || !g_damage_orig || !g_script_step_orig ||
	    !g_scene_transition_orig || !g_scene_mode_orig || !collision_orig ||
	    !follower_orig)
		return;
	if (host_newpal_patch_jump(NP_REWARD, newpal_reward_hook,
			     reward_expected) != 0)
		return;
	if (host_newpal_patch_jump(NP_DAMAGE, newpal_damage_hook,
			     damage_expected) != 0)
		return;
	if (host_newpal_patch_jump(NP_SCRIPT_STEP, newpal_script_step_hook,
			     script_expected) != 0)
		return;
	if (host_newpal_patch_jump(NP_SCENE_TRANSITION,
			     newpal_scene_transition_hook,
			     transition_expected) != 0)
		return;
	if (host_newpal_patch_jump(NP_SCENE_MODE_SETTER,
			     newpal_scene_mode_hook,
			     scene_mode_expected) != 0)
		return;
	g_field_collision_orig = collision_orig;
	if (host_newpal_patch_jump(NP_FIELD_COLLISION,
			     newpal_field_collision_hook,
			     collision_expected) != 0) {
		g_field_collision_orig = NULL;
		return;
	}
	g_follower_update_orig = follower_orig;
	if (host_newpal_patch_jump(NP_FOLLOWER_UPDATE,
			     newpal_follower_update_hook,
			     follower_expected) != 0) {
		g_follower_update_orig = NULL;
		return;
	}
	fprintf(stderr, "sword3-sdl: NewPAL trainer runtime hooks installed\n");
}
