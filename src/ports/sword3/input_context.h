#ifndef SWORD3_INPUT_CONTEXT_H
#define SWORD3_INPUT_CONTEXT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum Sword3InputRoute {
	SWORD3_INPUT_ROUTE_NONE = 0,
	SWORD3_INPUT_ROUTE_FIELD,
	SWORD3_INPUT_ROUTE_POINTER,
	SWORD3_INPUT_ROUTE_MENU_OPENING,
	SWORD3_INPUT_ROUTE_SYSTEM_MENU,
	SWORD3_INPUT_ROUTE_TITLE,
	SWORD3_INPUT_ROUTE_SHOP,
	SWORD3_INPUT_ROUTE_SAVE_LIST,
	SWORD3_INPUT_ROUTE_SAVE_LOAD,
	SWORD3_INPUT_ROUTE_BATTLE,
	SWORD3_INPUT_ROUTE_HOST_BATTLE,
	SWORD3_INPUT_ROUTE_HOST_MENU,
	SWORD3_INPUT_ROUTE_CAPTION,
	SWORD3_INPUT_ROUTE_HOST_CHEAT,
	SWORD3_INPUT_ROUTE_COUNT
} Sword3InputRoute;

#define SWORD3_INPUT_ROUTE_BIT(route) (UINT32_C(1) << (unsigned)(route))

/*
 * Read-only facts collected from the guest and host overlays once per input
 * tick.  This type intentionally contains no pointers: the resolver is pure
 * and can be tested without loading the Mach-O.
 */
typedef struct Sword3InputProbe {
	int host_cheat;
	int caption;
	int host_menu;
	int host_battle;
	int battle;
	int save_load;
	int save_list;
	int shop;
	int title;
	int system_menu;
	int menu_opening;
	int pointer;

	/*
	 * Observed guest state. Only fields proven authoritative participate in
	 * generation; system_* remain diagnostic until native touch sub-states
	 * have stable identities.
	 */
	int map_id;
	int title_mode;
	int menu_page;
	int system_page;
	int system_level;
	int system_sub;
	int battle_menu;
} Sword3InputProbe;

typedef struct Sword3InputContext {
	Sword3InputRoute route;
	uint32_t candidates;
	uint64_t generation;

	int map_id;
	int title_mode;
	int menu_page;
	int system_page;
	int system_level;
	int system_sub;
	int battle_menu;
} Sword3InputContext;

typedef struct Sword3InputContextTracker {
	Sword3InputContext current;
	int initialized;
} Sword3InputContextTracker;

/*
 * Resolve one winning route.  Lower layers may remain present in candidates;
 * the route is the highest-priority modal owner.
 */
Sword3InputContext sword3_input_context_resolve(
	const Sword3InputProbe *probe);

/*
 * Update lifecycle generation. Returns one when the lifecycle identity or
 * candidate stack changed, zero when it remained stable.
 */
int sword3_input_context_track(Sword3InputContextTracker *tracker,
			       const Sword3InputProbe *probe,
			       Sword3InputContext *context);

const char *sword3_input_route_name(Sword3InputRoute route);

#ifdef __cplusplus
}
#endif

#endif
