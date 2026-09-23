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
#define NP_SCENE_STATE 0x1001d000cull
#define NP_SCENE_REQUEST 0x1001d0010ull
#define NP_MAP_OBJECT_COUNT 0x1001d0528ull
#define NP_MAP_OBJECTS 0x1001f8810ull
#define NP_CURRENT_OBJECT_ID 0x1001f3f30ull
#define NP_CURRENT_OBJECT_INDEX 0x1001f3f34ull
#define NP_SCRIPT_DATA 0x1001f5928ull
#define NP_FIND_MAP_OBJECT 0x10000fc94ull
#define NP_MAP_ACTOR_SLOTS 0x10022c878ull
#define NP_CLEAR_MAP_ACTOR 0x1000119a4ull
#define NP_CLEAR_MAP_COLLISION 0x10000ede8ull
#define NP_UNLINK_MAP_OBJECT 0x100013dc0ull
#define NP_MONEY_MAX 60000
#define NP_LIST_LIMIT 32
#define NP_MAP_OBJECT_SIZE 0x68
#define NP_MAP_ACTOR_SLOT_SIZE 0x21c
#define NP_MAP_ACTOR_SLOT_COUNT 72

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
static unsigned g_encounters_suppressed;

static int (*g_reward_orig)(void);
static int (*g_damage_orig)(void *source, void *target);
static void (*g_script_step_orig)(void *script);
static void (*g_scene_transition_orig)(int requested);

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

static int newpal_hide_encounter_object(int battle, int event_id, int index)
{
	static unsigned samples;
	int count = *(volatile int *)(uintptr_t)NP_MAP_OBJECT_COUNT;
	uint8_t *object;
	uint32_t flags;
	uint16_t object_id;
	int slot;

	if (index <= 0 || index >= count || count > 256)
		return 0;
	object = (uint8_t *)(uintptr_t)NP_MAP_OBJECTS +
		(size_t)index * NP_MAP_OBJECT_SIZE;
	object_id = *(uint16_t *)(object + 0x2c);
	flags = *(uint32_t *)(object + 0x18);
	if (samples++ < 32)
		fprintf(stderr,
			"sword3-sdl: NewPAL trainer battle=%d event=%d index=%d object=%u flags=0x%x\n",
			battle, event_id, index, object_id, flags);
	/* Match the active event and the map's visible monster object class. */
	if (event_id != object_id || object_id < 5000 || object_id >= 6000 ||
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
	fprintf(stderr,
		"sword3-sdl: NewPAL trainer removed map monster=%u index=%d\n",
		object_id, index);
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
			if (object_id < 5000 || object_id >= 6000)
				continue;
			object_index = -1;
			if (!((int (*)(int, int *))(uintptr_t)
			      NP_FIND_MAP_OBJECT)(object_id, &object_index))
				return 0;
			return newpal_hide_encounter_object(
				battle, object_id, object_index);
		}
		break;
	}
	return 0;
}

static void newpal_script_step_hook(void *script)
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

	memcpy(entry_script, script, sizeof(entry_script));
	g_script_step_orig(script);
	state_after = *(volatile int *)(uintptr_t)NP_SCENE_STATE;
	request_after = *(volatile int *)(uintptr_t)NP_SCENE_REQUEST;
	if (g_no_encounter && state_before == 0 && request_after > 0) {
		int event = *(volatile int *)(uintptr_t)NP_CURRENT_OBJECT_ID;
		int index = *(volatile int *)(uintptr_t)NP_CURRENT_OBJECT_INDEX;

		if (g_encounters_suppressed < 32)
			fprintf(stderr,
				"sword3-sdl: NewPAL trainer encounter offset=%u script=%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d entry=%d/%d exit=%d/%d\n",
				entry_offset,
				entry_script[0], entry_script[1], entry_script[2],
				entry_script[3], entry_script[4], entry_script[5],
				entry_script[6], entry_script[7], entry_script[8],
				entry_script[9], entry_script[10], entry_script[11],
				entry_event, entry_index, event, index);
		if (!newpal_hide_script_encounter_object(request_after,
							entry_offset) &&
		    !newpal_hide_encounter_object(request_after, event, index) &&
		    (entry_event != event || entry_index != index))
			newpal_hide_encounter_object(request_after, entry_event,
					      entry_index);
		*(volatile int *)(uintptr_t)NP_SCENE_REQUEST = -1;
		g_encounters_suppressed++;
		if (g_encounters_suppressed <= 32 ||
		    !(g_encounters_suppressed & (g_encounters_suppressed - 1)))
			fprintf(stderr,
				"sword3-sdl: NewPAL trainer suppressed battle request=%d count=%u\n",
				request_after, g_encounters_suppressed);
		return;
	}
	if (g_no_encounter && state_before == 0 &&
	    (state_after != state_before || request_after != request_before) &&
	    samples++ < 32) {
		const int *words = script;

		fprintf(stderr,
			"sword3-sdl: NewPAL trainer scene request=%d->%d state=%d->%d script=%d,%d,%d,%d suppressed=%u\n",
			request_before, request_after, state_before, state_after,
			words[0], words[7], words[8], words[11],
			g_encounters_suppressed);
	}
}

static void newpal_scene_transition_hook(int requested)
{
	static unsigned samples;
	int state = *(volatile int *)(uintptr_t)NP_SCENE_STATE;
	uintptr_t caller = (uintptr_t)__builtin_return_address(0);

	if (g_no_encounter && state == 0 && requested > 0) {
		*(volatile int *)(uintptr_t)NP_SCENE_REQUEST = -1;
		g_encounters_suppressed++;
		fprintf(stderr,
			"sword3-sdl: NewPAL trainer suppressed direct battle=%d caller=0x%llx count=%u\n",
			requested, (unsigned long long)caller,
			g_encounters_suppressed);
		return;
	}
	if (g_no_encounter && state == 0 && samples++ < 32)
		fprintf(stderr,
			"sword3-sdl: NewPAL trainer transition request=%d caller=0x%llx suppressed=%u\n",
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

void host_cheat_newpal_no_encounter(int enabled)
{
	fprintf(stderr,
		"sword3-sdl: NewPAL trainer no-encounter=%d suppressed=%u\n",
		enabled, g_encounters_suppressed);
	if (enabled && !g_no_encounter)
		g_encounters_suppressed = 0;
	g_no_encounter = enabled;
}

static void *newpal_trampoline(uintptr_t function)
{
	uint32_t *code;
	long page_size = sysconf(_SC_PAGESIZE);

	if (page_size < 4096)
		page_size = 4096;
	code = mmap(NULL, (size_t)page_size, PROT_READ | PROT_WRITE | PROT_EXEC,
		MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (code == MAP_FAILED)
		return NULL;
	memcpy(code, (const void *)function, 16);
	code[4] = 0x58000051u; /* ldr x17, #8 */
	code[5] = 0xd61f0220u; /* br x17 */
	*(uint64_t *)(code + 6) = function + 16;
	__builtin___clear_cache((char *)code, (char *)code + 32);
	return code;
}

static int newpal_patch_jump(uintptr_t function, void *hook,
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

	if (g_install_attempted)
		return;
	g_install_attempted = 1;
	if (memcmp((const void *)(uintptr_t)NP_REWARD, reward_expected, 16) ||
	    memcmp((const void *)(uintptr_t)NP_DAMAGE, damage_expected, 16) ||
	    memcmp((const void *)(uintptr_t)NP_SCRIPT_STEP,
		   script_expected, 16) ||
	    memcmp((const void *)(uintptr_t)NP_SCENE_TRANSITION,
		   transition_expected, 16)) {
		fprintf(stderr,
			"sword3-sdl: NewPAL trainer executable hook fingerprint mismatch\n");
		return;
	}
	g_reward_orig = newpal_trampoline(NP_REWARD);
	g_damage_orig = newpal_trampoline(NP_DAMAGE);
	g_script_step_orig = newpal_trampoline(NP_SCRIPT_STEP);
	g_scene_transition_orig = newpal_trampoline(NP_SCENE_TRANSITION);
	if (!g_reward_orig || !g_damage_orig || !g_script_step_orig ||
	    !g_scene_transition_orig)
		return;
	if (newpal_patch_jump(NP_REWARD, newpal_reward_hook,
			     reward_expected) != 0)
		return;
	if (newpal_patch_jump(NP_DAMAGE, newpal_damage_hook,
			     damage_expected) != 0)
		return;
	if (newpal_patch_jump(NP_SCRIPT_STEP, newpal_script_step_hook,
			     script_expected) != 0)
		return;
	if (newpal_patch_jump(NP_SCENE_TRANSITION,
			     newpal_scene_transition_hook,
			     transition_expected) != 0)
		return;
	fprintf(stderr, "sword3-sdl: NewPAL trainer runtime hooks installed\n");
}
