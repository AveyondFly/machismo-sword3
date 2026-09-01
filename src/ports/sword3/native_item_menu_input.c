#include "native_item_menu_input.h"

#include <stdint.h>
#include <stdio.h>

#include <SDL2/SDL.h>

#define GUEST_DATA_LO 0x100294000ull
#define GUEST_DATA_HI 0x100380000ull

#define GUEST_FEXECUTE 0x1002a86a8ull
#define GUEST_FEX_ITEM 0x10002fffcull
#define GUEST_MENU_SELECTION 0x1002a99d0ull
#define GUEST_MAP_ID 0x1002a99d4ull
#define GUEST_SYS_LAYER 0x1002a99ccull

#define GUEST_ITEM_OK 0x100030a20ull
#define GUEST_ITEM_CATEGORY_PREV 0x1000314a8ull
#define GUEST_ITEM_CATEGORY_NEXT 0x1000315f0ull
#define GUEST_ITEM_CATEGORY 0x1002aa798ull
#define GUEST_ITEM_CONFIRM_SELECTION 0x1002aa78cull
#define GUEST_UIGAMEPAD 0x100304e28ull
#define GUEST_INPUT_MODE 0x23d4
#define GUEST_KEY_SLOT 0x320
#define GUEST_KEY_STRIDE 0x18
#define GUEST_INPUT_TRANSITION 0x1001c15fcull

static int pending_scancode;

static int guest_data_ok(uintptr_t address, size_t size)
{
	return address >= GUEST_DATA_LO && size > 0 &&
	       address + size - 1 >= address &&
	       address + size - 1 < GUEST_DATA_HI;
}

static int guest_i32(uintptr_t address, int fallback)
{
	if (!guest_data_ok(address, sizeof(int)))
		return fallback;
	return *(volatile int *)(uintptr_t)address;
}

static uintptr_t guest_ptr(uintptr_t address)
{
	if (!guest_data_ok(address, sizeof(uintptr_t)))
		return 0;
	return *(volatile uintptr_t *)(uintptr_t)address;
}

static int owner_active(int menu_session, int menu_keys, int content_focus)
{
	int route = guest_i32(GUEST_MAP_ID, -1);

	return menu_session && menu_keys && content_focus &&
	       guest_i32(GUEST_MENU_SELECTION, -1) == 11 &&
	       guest_i32(GUEST_SYS_LAYER, 0) >= 2 &&
	       route >= 30 && route <= 32 &&
	       guest_ptr(GUEST_FEXECUTE) == GUEST_FEX_ITEM;
}

static void set_key_state(int scancode, int down)
{
	uint8_t *pad;
	uint8_t *slot;
	void (*transition)(void *, void *, int);

	if (!guest_data_ok(GUEST_UIGAMEPAD + GUEST_INPUT_MODE,
			   sizeof(int)))
		return;
	pad = (uint8_t *)(uintptr_t)GUEST_UIGAMEPAD;
	slot = pad + GUEST_KEY_SLOT + scancode * GUEST_KEY_STRIDE;
	*(volatile int *)(pad + GUEST_INPUT_MODE) = 1;
	transition = (void (*)(void *, void *, int))(uintptr_t)
		GUEST_INPUT_TRANSITION;
	transition(pad, slot, down);
	if (down) {
		slot[8] = 0;
		*(volatile Uint32 *)(slot + 4) =
			SDL_GetTicks() - 0x209u;
	}
}

int sword3_native_item_menu_active(int menu_session, int menu_keys,
				   int content_focus)
{
	return owner_active(menu_session, menu_keys, content_focus);
}

int sword3_native_item_menu_content_active(int menu_session, int menu_keys,
					   int content_focus)
{
	return owner_active(menu_session, menu_keys, content_focus) &&
	       guest_i32(GUEST_SYS_LAYER, 0) == 2;
}

int sword3_native_item_menu_move_category(int menu_session, int menu_keys,
					  int content_focus, int direction)
{
	uintptr_t callback;
	uintptr_t expected;

	if (!sword3_native_item_menu_content_active(
		    menu_session, menu_keys, content_focus) ||
	    (direction != 1 && direction != 3))
		return 0;
	callback = guest_ptr(GUEST_FEXECUTE +
			     (direction == 1 ? 0x30u : 0x28u));
	expected = direction == 1 ? GUEST_ITEM_CATEGORY_NEXT
				  : GUEST_ITEM_CATEGORY_PREV;
	if (callback != expected)
		return 0;
	((void (*)(void))callback)();
	fprintf(stderr, "sword3-item: category=%d\n",
		guest_i32(GUEST_ITEM_CATEGORY, -1));
	return 1;
}

int sword3_native_item_menu_move_item(int menu_session, int menu_keys,
				      int content_focus, int direction)
{
	int scancode;

	if (!sword3_native_item_menu_content_active(
		    menu_session, menu_keys, content_focus) ||
	    (direction != 0 && direction != 2))
		return 0;
	scancode = direction == 0 ? SDL_SCANCODE_UP : SDL_SCANCODE_DOWN;
	if (pending_scancode)
		set_key_state(pending_scancode, 0);
	set_key_state(scancode, 1);
	pending_scancode = scancode;
	return 1;
}

int sword3_native_item_menu_move_nested(int menu_session, int menu_keys,
					int content_focus, int direction)
{
	uintptr_t callback;

	if (!owner_active(menu_session, menu_keys, content_focus) ||
	    guest_i32(GUEST_SYS_LAYER, 0) <= 2 ||
	    (direction != 1 && direction != 3))
		return 0;
	callback = guest_ptr(GUEST_FEXECUTE +
			     (direction == 1 ? 0x08u : 0x10u));
	if (!callback)
		return 0;
	((void (*)(void))callback)();
	fprintf(stderr, "sword3-item: nested %s selection=%d\n",
		direction == 1 ? "right" : "left",
		guest_i32(GUEST_ITEM_CONFIRM_SELECTION, -1));
	return 1;
}

int sword3_native_item_menu_confirm(int menu_session, int menu_keys,
				    int content_focus, int action)
{
	uintptr_t callback;
	int layer;

	if (!owner_active(menu_session, menu_keys, content_focus))
		return 0;
	callback = guest_ptr(GUEST_FEXECUTE + 0x48);
	if (callback != GUEST_ITEM_OK)
		return 0;
	layer = guest_i32(GUEST_SYS_LAYER, 0);
	if (layer == 2) {
		*(volatile int *)(uintptr_t)GUEST_MAP_ID = 30 + action;
	}
	fprintf(stderr, "sword3-item: action=%d layer=%d\n", action, layer);
	((void (*)(void))callback)();
	return 1;
}

void sword3_native_item_menu_flush(void)
{
	if (!pending_scancode)
		return;
	set_key_state(pending_scancode, 0);
	pending_scancode = 0;
}
