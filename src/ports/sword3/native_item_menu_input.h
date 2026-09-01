#ifndef SWORD3_NATIVE_ITEM_MENU_INPUT_H
#define SWORD3_NATIVE_ITEM_MENU_INPUT_H

#ifdef __cplusplus
extern "C" {
#endif

int sword3_native_item_menu_active(int menu_session, int menu_keys,
				   int content_focus);
int sword3_native_item_menu_content_active(int menu_session, int menu_keys,
					   int content_focus);
int sword3_native_item_menu_move_category(int menu_session, int menu_keys,
					  int content_focus, int direction);
int sword3_native_item_menu_move_item(int menu_session, int menu_keys,
				      int content_focus, int direction);
int sword3_native_item_menu_move_nested(int menu_session, int menu_keys,
					int content_focus, int direction);
int sword3_native_item_menu_confirm(int menu_session, int menu_keys,
				    int content_focus, int action);
void sword3_native_item_menu_flush(void);

#ifdef __cplusplus
}
#endif

#endif
