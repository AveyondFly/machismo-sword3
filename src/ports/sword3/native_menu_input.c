#include "native_menu_input.h"

void sword3_native_menu_input_reset(Sword3NativeMenuInput *input,
				    int tab_count, int tab)
{
	if (!input)
		return;
	if (tab_count < 1 || tab_count > SWORD3_NATIVE_MENU_MAX_TABS)
		tab_count = 5;
	if (tab < 0 || tab >= tab_count)
		tab = 0;
	input->focus = SWORD3_NATIVE_MENU_FOCUS_TABS;
	input->tab = tab;
	input->tab_count = tab_count;
	input->item_action = 0;
}

int sword3_native_menu_input_move_tab(Sword3NativeMenuInput *input, int delta)
{
	int tab;

	if (!input || input->focus != SWORD3_NATIVE_MENU_FOCUS_TABS ||
	    input->tab_count < 1 ||
	    input->tab_count > SWORD3_NATIVE_MENU_MAX_TABS || !delta)
		return 0;
	tab = (input->tab + delta) % input->tab_count;
	if (tab < 0)
		tab += input->tab_count;
	if (tab == input->tab)
		return 0;
	input->tab = tab;
	return 1;
}

int sword3_native_menu_input_activate(Sword3NativeMenuInput *input)
{
	if (!input || input->focus != SWORD3_NATIVE_MENU_FOCUS_TABS)
		return -1;
	input->focus = SWORD3_NATIVE_MENU_FOCUS_CONTENT;
	return input->tab;
}

int sword3_native_menu_input_move_item_action(Sword3NativeMenuInput *input,
					      int delta)
{
	int action;

	if (!input || input->focus != SWORD3_NATIVE_MENU_FOCUS_CONTENT ||
	    input->tab != 0 || !delta)
		return 0;
	action = (input->item_action + delta) % 3;
	if (action < 0)
		action += 3;
	if (action == input->item_action)
		return 0;
	input->item_action = action;
	return 1;
}

Sword3NativeMenuBack sword3_native_menu_input_back(
	Sword3NativeMenuInput *input, int native_layer)
{
	if (input && input->focus == SWORD3_NATIVE_MENU_FOCUS_CONTENT) {
		if (native_layer > 2)
			return SWORD3_NATIVE_MENU_BACK_WITHIN_CONTENT;
		input->focus = SWORD3_NATIVE_MENU_FOCUS_TABS;
		return SWORD3_NATIVE_MENU_BACK_TO_TABS;
	}
	return SWORD3_NATIVE_MENU_BACK_CLOSE;
}
