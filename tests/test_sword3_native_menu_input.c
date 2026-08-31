#include "ports/sword3/native_menu_input.h"

#include <assert.h>

static void test_tab_navigation_and_activation(void)
{
	Sword3NativeMenuInput input;

	sword3_native_menu_input_reset(&input, 5, 0);
	assert(input.focus == SWORD3_NATIVE_MENU_FOCUS_TABS);
	assert(input.tab == 0);
	assert(input.tab_count == 5);

	assert(sword3_native_menu_input_move_tab(&input, 1) == 1);
	assert(input.tab == 1);
	assert(sword3_native_menu_input_move_tab(&input, -1) == 1);
	assert(input.tab == 0);
	assert(sword3_native_menu_input_move_tab(&input, -1) == 1);
	assert(input.tab == 4);
	assert(sword3_native_menu_input_move_tab(&input, 1) == 1);
	assert(input.tab == 0);

	assert(sword3_native_menu_input_activate(&input) == 0);
	assert(input.focus == SWORD3_NATIVE_MENU_FOCUS_CONTENT);
	assert(sword3_native_menu_input_move_tab(&input, 1) == 0);
	assert(input.tab == 0);
	assert(sword3_native_menu_input_activate(&input) == -1);
}

static void test_back_ownership(void)
{
	Sword3NativeMenuInput input;

	sword3_native_menu_input_reset(&input, 5, 0);
	assert(sword3_native_menu_input_activate(&input) == 0);
	assert(sword3_native_menu_input_back(&input, 3) ==
	       SWORD3_NATIVE_MENU_BACK_WITHIN_CONTENT);
	assert(input.focus == SWORD3_NATIVE_MENU_FOCUS_CONTENT);
	assert(sword3_native_menu_input_back(&input, 2) ==
	       SWORD3_NATIVE_MENU_BACK_TO_TABS);
	assert(input.focus == SWORD3_NATIVE_MENU_FOCUS_TABS);
	assert(sword3_native_menu_input_back(&input, 1) ==
	       SWORD3_NATIVE_MENU_BACK_CLOSE);
}

static void test_dynamic_sixth_tab(void)
{
	Sword3NativeMenuInput input;

	sword3_native_menu_input_reset(&input, 6, 4);
	assert(input.tab_count == 6);
	assert(input.tab == 4);
	assert(sword3_native_menu_input_move_tab(&input, 1) == 1);
	assert(input.tab == 5);
	assert(sword3_native_menu_input_move_tab(&input, 1) == 1);
	assert(input.tab == 0);
	assert(sword3_native_menu_input_move_tab(&input, -1) == 1);
	assert(input.tab == 5);
	assert(sword3_native_menu_input_activate(&input) == 5);
}

static void test_item_action_focus(void)
{
	Sword3NativeMenuInput input;

	sword3_native_menu_input_reset(&input, 6, 0);
	assert(sword3_native_menu_input_move_item_action(&input, 1) == 0);
	assert(sword3_native_menu_input_activate(&input) == 0);
	assert(sword3_native_menu_input_move_item_action(&input, 1) == 1);
	assert(input.item_action == 1);
	assert(sword3_native_menu_input_move_item_action(&input, 1) == 1);
	assert(input.item_action == 2);
	assert(sword3_native_menu_input_move_item_action(&input, 1) == 1);
	assert(input.item_action == 0);
	assert(sword3_native_menu_input_move_item_action(&input, -1) == 1);
	assert(input.item_action == 2);

	sword3_native_menu_input_reset(&input, 6, 1);
	assert(sword3_native_menu_input_activate(&input) == 1);
	assert(sword3_native_menu_input_move_item_action(&input, 1) == 0);
}

int main(void)
{
	test_tab_navigation_and_activation();
	test_back_ownership();
	test_dynamic_sixth_tab();
	test_item_action_focus();
	return 0;
}
