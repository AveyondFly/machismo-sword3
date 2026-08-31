#ifndef SWORD3_NATIVE_MENU_INPUT_H
#define SWORD3_NATIVE_MENU_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

#define SWORD3_NATIVE_MENU_MAX_TABS 6

typedef enum Sword3NativeMenuFocus {
	SWORD3_NATIVE_MENU_FOCUS_TABS = 0,
	SWORD3_NATIVE_MENU_FOCUS_CONTENT
} Sword3NativeMenuFocus;

typedef enum Sword3NativeMenuBack {
	SWORD3_NATIVE_MENU_BACK_WITHIN_CONTENT = 0,
	SWORD3_NATIVE_MENU_BACK_TO_TABS,
	SWORD3_NATIVE_MENU_BACK_CLOSE
} Sword3NativeMenuBack;

typedef struct Sword3NativeMenuInput {
	Sword3NativeMenuFocus focus;
	int tab;
	int tab_count;
	int item_action;
} Sword3NativeMenuInput;

/* Start a native-menu session with a validated dynamic tab count/index. */
void sword3_native_menu_input_reset(Sword3NativeMenuInput *input,
				    int tab_count, int tab);

/*
 * Move the top-level focus without activating the native page. Returns one
 * when the selected tab changed and zero while content owns the directions.
 */
int sword3_native_menu_input_move_tab(Sword3NativeMenuInput *input, int delta);

/*
 * Enter the selected native page. Returns its zero-based tab index, or -1
 * when the input is already owned by page content.
 */
int sword3_native_menu_input_activate(Sword3NativeMenuInput *input);

/* Move the host-owned 使用/整理/丢弃 focus on the item page. */
int sword3_native_menu_input_move_item_action(Sword3NativeMenuInput *input,
					      int delta);

/*
 * Content consumes the first Back by returning focus to the tab strip. Back
 * from the tab strip closes the native menu.
 */
Sword3NativeMenuBack sword3_native_menu_input_back(
	Sword3NativeMenuInput *input, int native_layer);

#ifdef __cplusplus
}
#endif

#endif
