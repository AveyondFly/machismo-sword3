#include "input_context.h"

#include <string.h>

static void add_candidate(uint32_t *candidates, Sword3InputRoute route,
			  int active)
{
	if (active)
		*candidates |= SWORD3_INPUT_ROUTE_BIT(route);
}

static Sword3InputRoute choose_route(uint32_t candidates)
{
	static const Sword3InputRoute priority[] = {
		SWORD3_INPUT_ROUTE_HOST_CHEAT,
		SWORD3_INPUT_ROUTE_CAPTION,
		SWORD3_INPUT_ROUTE_HOST_MENU,
		SWORD3_INPUT_ROUTE_HOST_BATTLE,
		SWORD3_INPUT_ROUTE_BATTLE,
		SWORD3_INPUT_ROUTE_SAVE_LOAD,
		SWORD3_INPUT_ROUTE_SAVE_LIST,
		SWORD3_INPUT_ROUTE_SHOP,
		SWORD3_INPUT_ROUTE_MENU_OPENING,
		SWORD3_INPUT_ROUTE_TITLE,
		SWORD3_INPUT_ROUTE_SYSTEM_MENU,
		SWORD3_INPUT_ROUTE_POINTER,
		SWORD3_INPUT_ROUTE_FIELD,
	};
	size_t i;

	for (i = 0; i < sizeof(priority) / sizeof(priority[0]); i++) {
		if (candidates & SWORD3_INPUT_ROUTE_BIT(priority[i]))
			return priority[i];
	}
	return SWORD3_INPUT_ROUTE_NONE;
}

Sword3InputContext sword3_input_context_resolve(
	const Sword3InputProbe *probe)
{
	Sword3InputContext context;
	uint32_t candidates = 0;

	memset(&context, 0, sizeof(context));
	if (!probe)
		return context;

	add_candidate(&candidates, SWORD3_INPUT_ROUTE_HOST_CHEAT,
		      probe->host_cheat);
	add_candidate(&candidates, SWORD3_INPUT_ROUTE_CAPTION,
		      probe->caption);
	add_candidate(&candidates, SWORD3_INPUT_ROUTE_HOST_MENU,
		      probe->host_menu);
	add_candidate(&candidates, SWORD3_INPUT_ROUTE_HOST_BATTLE,
		      probe->host_battle);
	add_candidate(&candidates, SWORD3_INPUT_ROUTE_BATTLE, probe->battle);
	add_candidate(&candidates, SWORD3_INPUT_ROUTE_SAVE_LOAD,
		      probe->save_load);
	add_candidate(&candidates, SWORD3_INPUT_ROUTE_SAVE_LIST,
		      probe->save_list);
	add_candidate(&candidates, SWORD3_INPUT_ROUTE_SHOP, probe->shop);
	add_candidate(&candidates, SWORD3_INPUT_ROUTE_TITLE, probe->title);
	add_candidate(&candidates, SWORD3_INPUT_ROUTE_SYSTEM_MENU,
		      probe->system_menu);
	add_candidate(&candidates, SWORD3_INPUT_ROUTE_MENU_OPENING,
		      probe->menu_opening);
	add_candidate(&candidates, SWORD3_INPUT_ROUTE_POINTER, probe->pointer);

	/*
	 * Field is the base route when no title or battle scene owns the game.
	 * It intentionally remains a candidate under modal overlays so shadow
	 * logs expose the complete route stack.
	 */
	add_candidate(&candidates, SWORD3_INPUT_ROUTE_FIELD,
		      !probe->title && !probe->battle);

	context.route = choose_route(candidates);
	context.candidates = candidates;
	context.map_id = probe->map_id;
	context.title_mode = probe->title_mode;
	context.menu_page = probe->menu_page;
	context.system_page = probe->system_page;
	context.system_level = probe->system_level;
	context.system_sub = probe->system_sub;
	context.battle_menu = probe->battle_menu;
	return context;
}

static int same_lifecycle(const Sword3InputContext *left,
			  const Sword3InputContext *right)
{
	uint32_t title_routes =
		SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_TITLE) |
		SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_SAVE_LOAD);
	uint32_t save_routes =
		SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_SAVE_LOAD) |
		SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_SAVE_LIST);
	uint32_t battle_routes =
		SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_BATTLE) |
		SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_HOST_BATTLE);
	uint32_t candidates;

	if (left->route != right->route ||
	    left->candidates != right->candidates ||
	    left->map_id != right->map_id)
		return 0;
	candidates = left->candidates;
	if ((candidates & title_routes) &&
	    left->title_mode != right->title_mode)
		return 0;
	if ((candidates & save_routes) &&
	    left->menu_page != right->menu_page)
		return 0;
	if ((candidates & battle_routes) &&
	    left->battle_menu != right->battle_menu)
		return 0;
	return 1;
}

int sword3_input_context_track(Sword3InputContextTracker *tracker,
			       const Sword3InputProbe *probe,
			       Sword3InputContext *context)
{
	Sword3InputContext next;
	int changed;

	if (!tracker || !context)
		return 0;
	next = sword3_input_context_resolve(probe);
	changed = !tracker->initialized ||
		  !same_lifecycle(&tracker->current, &next);
	if (!tracker->initialized) {
		next.generation = 1;
		tracker->initialized = 1;
	} else if (changed) {
		next.generation = tracker->current.generation + 1;
		if (!next.generation)
			next.generation = 1;
	} else {
		next.generation = tracker->current.generation;
	}
	tracker->current = next;
	*context = next;
	return changed;
}

const char *sword3_input_route_name(Sword3InputRoute route)
{
	switch (route) {
	case SWORD3_INPUT_ROUTE_NONE:
		return "none";
	case SWORD3_INPUT_ROUTE_FIELD:
		return "field";
	case SWORD3_INPUT_ROUTE_POINTER:
		return "pointer";
	case SWORD3_INPUT_ROUTE_MENU_OPENING:
		return "menu-opening";
	case SWORD3_INPUT_ROUTE_SYSTEM_MENU:
		return "system-menu";
	case SWORD3_INPUT_ROUTE_TITLE:
		return "title";
	case SWORD3_INPUT_ROUTE_SHOP:
		return "shop";
	case SWORD3_INPUT_ROUTE_SAVE_LIST:
		return "save-list";
	case SWORD3_INPUT_ROUTE_SAVE_LOAD:
		return "save-load";
	case SWORD3_INPUT_ROUTE_BATTLE:
		return "battle";
	case SWORD3_INPUT_ROUTE_HOST_BATTLE:
		return "host-battle";
	case SWORD3_INPUT_ROUTE_HOST_MENU:
		return "host-menu";
	case SWORD3_INPUT_ROUTE_CAPTION:
		return "caption";
	case SWORD3_INPUT_ROUTE_HOST_CHEAT:
		return "host-cheat";
	case SWORD3_INPUT_ROUTE_COUNT:
		break;
	}
	return "invalid";
}
