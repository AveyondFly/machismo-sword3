#include "ports/sword3/input_context.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>

static void test_default_field(void)
{
	Sword3InputProbe probe = {0};
	Sword3InputContext context = sword3_input_context_resolve(&probe);

	assert(context.route == SWORD3_INPUT_ROUTE_FIELD);
	assert(context.candidates ==
	       SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_FIELD));
}

static void test_modal_priority(void)
{
	Sword3InputProbe probe = {0};
	Sword3InputContext context;

	probe.system_menu = 1;
	context = sword3_input_context_resolve(&probe);
	assert(context.route == SWORD3_INPUT_ROUTE_SYSTEM_MENU);
	assert(context.candidates &
	       SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_FIELD));

	probe.caption = 1;
	context = sword3_input_context_resolve(&probe);
	assert(context.route == SWORD3_INPUT_ROUTE_CAPTION);
	assert(context.candidates &
	       SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_SYSTEM_MENU));

	probe.caption = 0;
	probe.menu_opening = 1;
	context = sword3_input_context_resolve(&probe);
	assert(context.route == SWORD3_INPUT_ROUTE_MENU_OPENING);

	probe.host_cheat = 1;
	context = sword3_input_context_resolve(&probe);
	assert(context.route == SWORD3_INPUT_ROUTE_HOST_CHEAT);
}

static void test_scene_routes(void)
{
	Sword3InputProbe probe = {0};
	Sword3InputContext context;

	probe.title = 1;
	context = sword3_input_context_resolve(&probe);
	assert(context.route == SWORD3_INPUT_ROUTE_TITLE);
	assert(!(context.candidates &
		 SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_FIELD)));

	probe.save_load = 1;
	context = sword3_input_context_resolve(&probe);
	assert(context.route == SWORD3_INPUT_ROUTE_SAVE_LOAD);
	assert(context.candidates &
	       SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_TITLE));
	assert(!(context.candidates &
		 SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_FIELD)));

	memset(&probe, 0, sizeof(probe));
	probe.battle = 1;
	probe.host_battle = 1;
	context = sword3_input_context_resolve(&probe);
	assert(context.route == SWORD3_INPUT_ROUTE_HOST_BATTLE);
	assert(context.candidates &
	       SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_BATTLE));
	assert(!(context.candidates &
		 SWORD3_INPUT_ROUTE_BIT(SWORD3_INPUT_ROUTE_FIELD)));
}

static void test_generation(void)
{
	Sword3InputContextTracker tracker = {0};
	Sword3InputProbe probe = {0};
	Sword3InputContext context;

	assert(sword3_input_context_track(&tracker, &probe, &context) == 1);
	assert(context.generation == 1);
	assert(sword3_input_context_track(&tracker, &probe, &context) == 0);
	assert(context.generation == 1);

	probe.system_menu = 1;
	probe.system_page = 2;
	assert(sword3_input_context_track(&tracker, &probe, &context) == 1);
	assert(context.generation == 2);
	assert(context.route == SWORD3_INPUT_ROUTE_SYSTEM_MENU);

	probe.system_page = 3;
	assert(sword3_input_context_track(&tracker, &probe, &context) == 0);
	assert(context.generation == 2);

	probe.save_load = 1;
	probe.menu_page = 3;
	assert(sword3_input_context_track(&tracker, &probe, &context) == 1);
	assert(context.generation == 3);
	assert(context.route == SWORD3_INPUT_ROUTE_SAVE_LOAD);
	probe.system_level = 7;
	assert(sword3_input_context_track(&tracker, &probe, &context) == 0);
	assert(context.generation == 3);
	probe.menu_page = 4;
	assert(sword3_input_context_track(&tracker, &probe, &context) == 1);
	assert(context.generation == 4);
	assert(sword3_input_context_track(&tracker, &probe, &context) == 0);
	assert(context.generation == 4);
}

static void test_inactive_metadata_does_not_change_generation(void)
{
	Sword3InputContextTracker tracker = {0};
	Sword3InputProbe probe = {0};
	Sword3InputContext context;

	assert(sword3_input_context_track(&tracker, &probe, &context) == 1);
	probe.system_page = 7;
	probe.system_level = 3;
	probe.system_sub = 9;
	probe.battle_menu = 100;
	probe.title_mode = 2;
	assert(sword3_input_context_track(&tracker, &probe, &context) == 0);
	assert(context.generation == 1);

	probe.battle = 1;
	assert(sword3_input_context_track(&tracker, &probe, &context) == 1);
	assert(context.generation == 2);
	probe.battle_menu = 101;
	assert(sword3_input_context_track(&tracker, &probe, &context) == 1);
	assert(context.generation == 3);
}

static void test_route_names(void)
{
	assert(strcmp(sword3_input_route_name(SWORD3_INPUT_ROUTE_CAPTION),
		      "caption") == 0);
	assert(strcmp(sword3_input_route_name(SWORD3_INPUT_ROUTE_FIELD),
		      "field") == 0);
}

int main(void)
{
	test_default_field();
	test_modal_priority();
	test_scene_routes();
	test_generation();
	test_inactive_metadata_does_not_change_generation();
	test_route_names();
	return 0;
}
