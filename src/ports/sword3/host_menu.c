#include "host_menu.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <SDL2/SDL_ttf.h>

#define HOST_MENU_DEADZONE 14000
#define HOST_MENU_TABS 5
#define HOST_MENU_ACTIONS 5
#define HOST_MENU_SLOTS 10
#define HOST_MENU_SLOT_COLS 2
#define HOST_MENU_FONT "/usr/share/fonts/TTF/DejaVuSansMono.ttf"
#define HOST_MENU_FONT_SLOTS 4
#define HOST_MENU_TEXT_CACHE 64

enum host_menu_tab {
	HOST_MENU_TAB_ITEM = 0,
	HOST_MENU_TAB_EQUIP,
	HOST_MENU_TAB_SKILL,
	HOST_MENU_TAB_STATUS,
	HOST_MENU_TAB_BOOK
};

enum host_menu_layer {
	HOST_MENU_LAYER_TABS = 0,
	HOST_MENU_LAYER_BOOK,
	HOST_MENU_LAYER_SLOTS,
	HOST_MENU_LAYER_STUB
};

static int g_open;
static int g_tab;
static int g_layer;
static int g_book_focus;
static int g_stub;
static int g_slot_mode;
static int g_slot_focus;
static int g_pending = -1;
static int g_pending_slot;
static int g_dir_down[4];
static int g_dir_axis[4];
static int g_dir_held[4];
static int g_ttf_ready;
static struct {
	int pt;
	TTF_Font *font;
} g_fonts[HOST_MENU_FONT_SLOTS];
static struct {
	SDL_Texture *tex;
	SDL_Renderer *renderer;
	char text[80];
	int pt;
	Uint32 rgba;
	int w;
	int h;
} g_text[HOST_MENU_TEXT_CACHE];
static int g_text_clock;
static SDL_Renderer *g_text_renderer;

static void host_menu_enter_book(void);

static const char *g_tab_text[HOST_MENU_TABS] = {
	"物品", "装备", "奇术", "状态", "天书"
};

static const char *g_book_text[HOST_MENU_ACTIONS] = {
	"存盘", "读取", "记载", "设置", "离开"
};

static const char *g_stub_text[HOST_MENU_ACTIONS] = {
	"存盘（尚未接入）",
	"读取（尚未接入）",
	"记载（尚未接入）",
	"设置（尚未接入）",
	"存盘（尚未接入）",
};

static const SDL_Color g_ink_title = { 255, 220, 120, 255 };
static const SDL_Color g_ink_body = { 255, 236, 196, 255 };
static const SDL_Color g_ink_hint = { 170, 160, 140, 255 };

int host_menu_active(void)
{
	return g_open;
}

void host_menu_close(void)
{
	if (!g_open)
		return;
	g_open = 0;
	g_layer = HOST_MENU_LAYER_TABS;
	g_stub = -1;
	g_slot_mode = 0;
	g_slot_focus = 0;
	memset(g_dir_down, 0, sizeof(g_dir_down));
	memset(g_dir_axis, 0, sizeof(g_dir_axis));
	memset(g_dir_held, 0, sizeof(g_dir_held));
	fprintf(stderr, "sword3-sdl: host menu close\n");
}

void host_menu_open(void)
{
	g_open = 1;
	g_tab = HOST_MENU_TAB_ITEM;
	g_layer = HOST_MENU_LAYER_TABS;
	g_book_focus = 0;
	g_stub = -1;
	g_slot_mode = 0;
	g_slot_focus = 0;
	memset(g_dir_down, 0, sizeof(g_dir_down));
	memset(g_dir_axis, 0, sizeof(g_dir_axis));
	memset(g_dir_held, 0, sizeof(g_dir_held));
	fprintf(stderr, "sword3-sdl: host menu open\n");
}

void host_menu_open_book(void)
{
	host_menu_open();
	host_menu_enter_book();
}

int host_menu_take_pending(int *slot)
{
	int action;

	action = g_pending;
	g_pending = -1;
	if (slot)
		*slot = g_pending_slot;
	return action;
}

static int host_menu_slot_used(int slot)
{
	const char *dir;
	char path[PATH_MAX];
	struct stat st;

	if (slot < 0 || slot >= HOST_MENU_SLOTS)
		return 0;
	dir = getenv("SWORD3_DATA_DIR");
	if (!dir || dir[0] != '/')
		dir = "/tmp/sword3/documents";
	if (snprintf(path, sizeof(path), "%s/%d.sav", dir, slot) >=
	    (int)sizeof(path))
		return 0;
	return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static void host_menu_enter_book(void)
{
	g_tab = HOST_MENU_TAB_BOOK;
	g_layer = HOST_MENU_LAYER_BOOK;
	g_book_focus = 0;
}

static void host_menu_enter_slots(int mode)
{
	g_tab = HOST_MENU_TAB_BOOK;
	g_layer = HOST_MENU_LAYER_SLOTS;
	g_slot_mode = mode;
	g_slot_focus = 0;
	fprintf(stderr, "sword3-sdl: host menu slots mode=%d\n", mode);
}

static void host_menu_move_slot(int dx, int dy)
{
	int col;
	int row;

	col = g_slot_focus % HOST_MENU_SLOT_COLS;
	row = g_slot_focus / HOST_MENU_SLOT_COLS;
	col = (col + dx + HOST_MENU_SLOT_COLS) % HOST_MENU_SLOT_COLS;
	row = (row + dy + HOST_MENU_SLOTS / HOST_MENU_SLOT_COLS) %
	      (HOST_MENU_SLOTS / HOST_MENU_SLOT_COLS);
	g_slot_focus = row * HOST_MENU_SLOT_COLS + col;
}

static void host_menu_move_tab(int delta)
{
	g_tab = (g_tab + delta + HOST_MENU_TABS) % HOST_MENU_TABS;
	if (g_tab == HOST_MENU_TAB_BOOK)
		host_menu_enter_book();
	else
		g_layer = HOST_MENU_LAYER_TABS;
}

static void host_menu_move_book(int delta)
{
	g_book_focus = (g_book_focus + delta + HOST_MENU_ACTIONS) %
		       HOST_MENU_ACTIONS;
}

static void host_menu_dir(int index, int down)
{
	if (index < 0 || index >= 4)
		return;
	if (!down) {
		g_dir_held[index] = 0;
		return;
	}
	if (g_dir_held[index])
		return;
	g_dir_held[index] = 1;

	if (g_layer == HOST_MENU_LAYER_STUB)
		return;

	if (g_layer == HOST_MENU_LAYER_SLOTS) {
		if (index == 1)
			host_menu_move_slot(1, 0);
		else if (index == 3)
			host_menu_move_slot(-1, 0);
		else if (index == 2)
			host_menu_move_slot(0, 1);
		else if (index == 0)
			host_menu_move_slot(0, -1);
		return;
	}

	if (index == 1 || index == 3) {
		if (g_layer == HOST_MENU_LAYER_BOOK)
			host_menu_move_book(index == 1 ? 1 : -1);
		else
			host_menu_move_tab(index == 1 ? 1 : -1);
		return;
	}
	if (g_tab != HOST_MENU_TAB_BOOK)
		return;
	if (index == 2 && g_layer == HOST_MENU_LAYER_TABS)
		host_menu_enter_book();
	else if (index == 0 && g_layer == HOST_MENU_LAYER_BOOK)
		g_layer = HOST_MENU_LAYER_TABS;
}

static void host_menu_set_dir(int index, int axis, int down)
{
	int wanted;

	if (index < 0 || index >= 4)
		return;
	if (axis)
		g_dir_axis[index] = down;
	else
		g_dir_down[index] = down;
	wanted = g_dir_axis[index] || g_dir_down[index];
	host_menu_dir(index, wanted);
}

static void host_menu_confirm(void)
{
	if (g_layer == HOST_MENU_LAYER_STUB)
		return;
	if (g_layer == HOST_MENU_LAYER_TABS) {
		if (g_tab == HOST_MENU_TAB_BOOK)
			host_menu_enter_book();
		return;
	}
	if (g_layer == HOST_MENU_LAYER_SLOTS) {
		if (g_slot_mode == 1 && !host_menu_slot_used(g_slot_focus))
			return;
		g_pending_slot = g_slot_focus;
		g_pending = g_slot_mode;
		fprintf(stderr,
			"sword3-sdl: host menu slot pending kind=%d slot=%d\n",
			g_pending, g_pending_slot);
		if (g_slot_mode == 1)
			host_menu_close();
		return;
	}
	if (g_book_focus == 4) {
		host_menu_close();
		return;
	}
	if (g_book_focus == 0 || g_book_focus == 1) {
		host_menu_enter_slots(g_book_focus);
		return;
	}
	g_stub = g_book_focus;
	g_layer = HOST_MENU_LAYER_STUB;
	fprintf(stderr, "sword3-sdl: host menu stub action=%d\n", g_stub);
}

static void host_menu_back(void)
{
	if (g_layer == HOST_MENU_LAYER_STUB ||
	    g_layer == HOST_MENU_LAYER_SLOTS) {
		g_layer = HOST_MENU_LAYER_BOOK;
		g_stub = -1;
		return;
	}
	host_menu_close();
}

int host_menu_button(int button, int down)
{
	int index;

	if (!g_open)
		return 0;
	switch (button) {
	case SDL_CONTROLLER_BUTTON_A:
		if (down)
			host_menu_confirm();
		return 1;
	case SDL_CONTROLLER_BUTTON_B:
		if (down)
			host_menu_back();
		return 1;
	case SDL_CONTROLLER_BUTTON_LEFTSHOULDER:
		if (down && g_layer != HOST_MENU_LAYER_SLOTS &&
		    g_layer != HOST_MENU_LAYER_STUB)
			host_menu_move_tab(-1);
		return 1;
	case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER:
		if (down && g_layer != HOST_MENU_LAYER_SLOTS &&
		    g_layer != HOST_MENU_LAYER_STUB)
			host_menu_move_tab(1);
		return 1;
	case SDL_CONTROLLER_BUTTON_DPAD_UP:
		index = 0;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_RIGHT:
		index = 1;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_DOWN:
		index = 2;
		break;
	case SDL_CONTROLLER_BUTTON_DPAD_LEFT:
		index = 3;
		break;
	default:
		return 1;
	}
	host_menu_set_dir(index, 0, down);
	return 1;
}

int host_menu_axis(Uint8 axis, Sint16 value)
{
	int old_dir;
	int new_dir;

	if (!g_open)
		return 0;
	if (axis == SDL_CONTROLLER_AXIS_LEFTX) {
		old_dir = g_dir_axis[3] ? 3 : (g_dir_axis[1] ? 1 : -1);
		new_dir = value < -HOST_MENU_DEADZONE ? 3 :
			  (value > HOST_MENU_DEADZONE ? 1 : -1);
	} else if (axis == SDL_CONTROLLER_AXIS_LEFTY) {
		old_dir = g_dir_axis[0] ? 0 : (g_dir_axis[2] ? 2 : -1);
		new_dir = value < -HOST_MENU_DEADZONE ? 0 :
			  (value > HOST_MENU_DEADZONE ? 2 : -1);
	} else {
		return 1;
	}
	if (old_dir == new_dir)
		return 1;
	if (old_dir >= 0)
		host_menu_set_dir(old_dir, 1, 0);
	if (new_dir >= 0)
		host_menu_set_dir(new_dir, 1, 1);
	return 1;
}

static TTF_Font *host_menu_font(int pt)
{
	int i;
	int empty;
	TTF_Font *font;

	if (pt < 12)
		pt = 12;
	if (pt > 40)
		pt = 40;
	if (!g_ttf_ready) {
		if (TTF_Init() != 0) {
			fprintf(stderr, "sword3-sdl: TTF_Init failed: %s\n",
				TTF_GetError());
			g_ttf_ready = -1;
			return NULL;
		}
		g_ttf_ready = 1;
	}
	if (g_ttf_ready < 0)
		return NULL;
	for (i = 0; i < HOST_MENU_FONT_SLOTS; i++) {
		if (g_fonts[i].font && g_fonts[i].pt == pt)
			return g_fonts[i].font;
	}
	empty = -1;
	for (i = 0; i < HOST_MENU_FONT_SLOTS; i++) {
		if (!g_fonts[i].font) {
			empty = i;
			break;
		}
	}
	if (empty < 0) {
		empty = 0;
		TTF_CloseFont(g_fonts[0].font);
		g_fonts[0].font = NULL;
	}
	font = TTF_OpenFont(HOST_MENU_FONT, pt);
	if (!font) {
		fprintf(stderr, "sword3-sdl: TTF_OpenFont %s pt=%d failed: %s\n",
			HOST_MENU_FONT, pt, TTF_GetError());
		return NULL;
	}
	g_fonts[empty].pt = pt;
	g_fonts[empty].font = font;
	if (empty == 0)
		fprintf(stderr, "sword3-sdl: host menu font %s\n", HOST_MENU_FONT);
	return font;
}

static Uint32 host_menu_rgba(SDL_Color c)
{
	return ((Uint32)c.r << 24) | ((Uint32)c.g << 16) |
	       ((Uint32)c.b << 8) | (Uint32)c.a;
}

static void host_menu_text_flush(SDL_Renderer *renderer)
{
	int i;

	for (i = 0; i < HOST_MENU_TEXT_CACHE; i++) {
		if (!g_text[i].tex)
			continue;
		if (renderer && g_text[i].renderer != renderer)
			continue;
		SDL_DestroyTexture(g_text[i].tex);
		g_text[i].tex = NULL;
		g_text[i].text[0] = '\0';
	}
}

static int host_menu_text_size(SDL_Renderer *renderer, const char *s, int pt,
			      SDL_Color color, int *w, int *h,
			      SDL_Texture **tex_out)
{
	TTF_Font *font;
	SDL_Surface *surf;
	SDL_Texture *tex;
	Uint32 rgba;
	int i;
	int slot;

	if (w)
		*w = 0;
	if (h)
		*h = 0;
	if (tex_out)
		*tex_out = NULL;
	if (!renderer || !s || !s[0])
		return 0;
	rgba = host_menu_rgba(color);
	for (i = 0; i < HOST_MENU_TEXT_CACHE; i++) {
		if (g_text[i].tex && g_text[i].renderer == renderer &&
		    g_text[i].pt == pt && g_text[i].rgba == rgba &&
		    strcmp(g_text[i].text, s) == 0) {
			if (w)
				*w = g_text[i].w;
			if (h)
				*h = g_text[i].h;
			if (tex_out)
				*tex_out = g_text[i].tex;
			return 1;
		}
	}
	font = host_menu_font(pt);
	if (!font)
		return 0;
	surf = TTF_RenderUTF8_Blended(font, s, color);
	if (!surf)
		return 0;
	tex = SDL_CreateTextureFromSurface(renderer, surf);
	if (tex)
		SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
	if (w)
		*w = surf->w;
	if (h)
		*h = surf->h;
	if (!tex) {
		SDL_FreeSurface(surf);
		return 0;
	}
	slot = -1;
	for (i = 0; i < HOST_MENU_TEXT_CACHE; i++) {
		if (!g_text[i].tex) {
			slot = i;
			break;
		}
	}
	if (slot < 0) {
		slot = (g_text_clock++) % HOST_MENU_TEXT_CACHE;
		if (g_text[slot].tex)
			SDL_DestroyTexture(g_text[slot].tex);
	}
	snprintf(g_text[slot].text, sizeof(g_text[slot].text), "%s", s);
	g_text[slot].tex = tex;
	g_text[slot].renderer = renderer;
	g_text[slot].pt = pt;
	g_text[slot].rgba = rgba;
	g_text[slot].w = surf->w;
	g_text[slot].h = surf->h;
	SDL_FreeSurface(surf);
	if (tex_out)
		*tex_out = tex;
	return 1;
}

static void host_menu_text_center(SDL_Renderer *renderer, const char *s, int cx,
				 int cy, int pt, SDL_Color color)
{
	SDL_Texture *tex;
	SDL_Rect dst;
	int w;
	int h;

	if (!host_menu_text_size(renderer, s, pt, color, &w, &h, &tex) || !tex)
		return;
	dst.x = cx - w / 2;
	dst.y = cy - h / 2;
	dst.w = w;
	dst.h = h;
	SDL_RenderCopy(renderer, tex, NULL, &dst);
}

static void host_menu_fill(SDL_Renderer *renderer, SDL_Rect rect, Uint8 r,
			   Uint8 g, Uint8 b, Uint8 a)
{
	SDL_SetRenderDrawColor(renderer, r, g, b, a);
	SDL_RenderFillRect(renderer, &rect);
}

static void host_menu_frame(SDL_Renderer *renderer, SDL_Rect rect, int thick,
			    Uint8 r, Uint8 g, Uint8 b, Uint8 a)
{
	SDL_Rect arm[4];

	arm[0].x = rect.x;
	arm[0].y = rect.y;
	arm[0].w = rect.w;
	arm[0].h = thick;
	arm[1].x = rect.x;
	arm[1].y = rect.y + rect.h - thick;
	arm[1].w = rect.w;
	arm[1].h = thick;
	arm[2].x = rect.x;
	arm[2].y = rect.y;
	arm[2].w = thick;
	arm[2].h = rect.h;
	arm[3].x = rect.x + rect.w - thick;
	arm[3].y = rect.y;
	arm[3].w = thick;
	arm[3].h = rect.h;
	SDL_SetRenderDrawColor(renderer, r, g, b, a);
	SDL_RenderFillRect(renderer, &arm[0]);
	SDL_RenderFillRect(renderer, &arm[1]);
	SDL_RenderFillRect(renderer, &arm[2]);
	SDL_RenderFillRect(renderer, &arm[3]);
}

void host_menu_draw(SDL_Renderer *renderer, int logical_w, int logical_h)
{
	SDL_Rect dim;
	SDL_Rect panel;
	SDL_Rect tab;
	SDL_Rect body;
	SDL_Rect action;
	int i;
	int tab_w;
	int gap;
	int selected;
	int pt;
	int pt_title;
	int pt_small;
	Uint8 pr;
	Uint8 pg;
	Uint8 pb;
	Uint8 pa;
	SDL_BlendMode blend;

	if (!g_open || !renderer || logical_w <= 0 || logical_h <= 0)
		return;
	if (SDL_GetRenderTarget(renderer) != NULL)
		return;

	if (g_text_renderer != renderer) {
		host_menu_text_flush(g_text_renderer);
		g_text_renderer = renderer;
	}

	pt = logical_h / 22;
	if (pt < 14)
		pt = 14;
	pt_title = pt + 4;
	pt_small = pt - 2;
	if (pt_small < 12)
		pt_small = 12;

	SDL_GetRenderDrawColor(renderer, &pr, &pg, &pb, &pa);
	SDL_GetRenderDrawBlendMode(renderer, &blend);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

	dim.x = 0;
	dim.y = 0;
	dim.w = logical_w;
	dim.h = logical_h;
	host_menu_fill(renderer, dim, 0, 0, 0, 150);

	panel.x = logical_w * 6 / 100;
	panel.y = logical_h * 8 / 100;
	panel.w = logical_w * 88 / 100;
	panel.h = logical_h * 82 / 100;
	host_menu_fill(renderer, panel, 16, 24, 36, 236);
	host_menu_frame(renderer, panel, 2, 212, 176, 88, 255);

	host_menu_text_center(renderer, "系统菜单",
			      panel.x + panel.w / 2,
			      panel.y + logical_h * 5 / 100, pt_title,
			      g_ink_title);

	gap = panel.w / 80;
	tab_w = (panel.w - gap * 6) / HOST_MENU_TABS;
	for (i = 0; i < HOST_MENU_TABS; i++) {
		tab.x = panel.x + gap + i * (tab_w + gap);
		tab.y = panel.y + logical_h * 10 / 100;
		tab.w = tab_w;
		tab.h = logical_h * 10 / 100;
		selected = (g_tab == i);
		if (selected)
			host_menu_fill(renderer, tab, 72, 56, 24, 245);
		else
			host_menu_fill(renderer, tab, 28, 40, 54, 230);
		host_menu_frame(renderer, tab, selected ? 3 : 1, 212, 176, 88,
				selected ? 255 : 160);
		if (g_layer == HOST_MENU_LAYER_TABS && selected)
			host_menu_frame(renderer, tab, 3, 255, 220, 64, 255);
		host_menu_text_center(renderer, g_tab_text[i],
				      tab.x + tab.w / 2, tab.y + tab.h / 2, pt,
				      g_ink_body);
	}

	body.x = panel.x + gap;
	body.y = panel.y + logical_h * 22 / 100;
	body.w = panel.w - gap * 2;
	body.h = panel.h - logical_h * 30 / 100;
	host_menu_fill(renderer, body, 10, 16, 24, 220);
	host_menu_frame(renderer, body, 1, 80, 72, 48, 200);

	if (g_layer == HOST_MENU_LAYER_STUB && g_stub >= 0 && g_stub < 4) {
		host_menu_text_center(renderer, g_stub_text[g_stub],
				      body.x + body.w / 2,
				      body.y + body.h / 2 - logical_h / 20, pt,
				      g_ink_body);
		host_menu_text_center(renderer, "数据稍后从游戏内存读取",
				      body.x + body.w / 2,
				      body.y + body.h / 2 + logical_h / 16,
				      pt_small, g_ink_hint);
	} else if (g_layer == HOST_MENU_LAYER_SLOTS) {
		SDL_Rect cell;
		int rows = HOST_MENU_SLOTS / HOST_MENU_SLOT_COLS;
		int cell_w;
		int cell_h;
		int col;
		int row;
		int used;
		char line[32];

		host_menu_text_center(renderer,
				      g_slot_mode ? "选择读档" : "选择存档",
				      body.x + body.w / 2,
				      body.y + logical_h * 3 / 100, pt,
				      g_ink_title);
		cell_w = (body.w - gap * 3) / HOST_MENU_SLOT_COLS;
		cell_h = (body.h - logical_h * 8 / 100 - gap * (rows + 1)) /
			 rows;
		for (i = 0; i < HOST_MENU_SLOTS; i++) {
			col = i % HOST_MENU_SLOT_COLS;
			row = i / HOST_MENU_SLOT_COLS;
			cell.x = body.x + gap + col * (cell_w + gap);
			cell.y = body.y + logical_h * 6 / 100 + gap +
				 row * (cell_h + gap);
			cell.w = cell_w;
			cell.h = cell_h;
			selected = (g_slot_focus == i);
			used = host_menu_slot_used(i);
			if (selected)
				host_menu_fill(renderer, cell, 88, 68, 28,
					       250);
			else
				host_menu_fill(renderer, cell, 36, 32, 24,
					       230);
			host_menu_frame(renderer, cell, selected ? 3 : 1,
					212, 176, 88, selected ? 255 : 140);
			if (selected)
				host_menu_frame(renderer, cell, 3, 255, 220,
						64, 255);
			snprintf(line, sizeof(line), "档位 %d", i + 1);
			host_menu_text_center(renderer, line,
					      cell.x + cell.w / 2,
					      cell.y + cell.h / 3, pt_small,
					      g_ink_body);
			host_menu_text_center(renderer, used ? "已存" : "空",
					      cell.x + cell.w / 2,
					      cell.y + cell.h * 2 / 3,
					      pt_small, g_ink_hint);
		}
	} else if (g_tab == HOST_MENU_TAB_BOOK) {
		int action_w = (body.w - gap * 6) / HOST_MENU_ACTIONS;

		for (i = 0; i < HOST_MENU_ACTIONS; i++) {
			action.x = body.x + gap + i * (action_w + gap);
			action.y = body.y + body.h / 2 - logical_h * 6 / 100;
			action.w = action_w;
			action.h = logical_h * 12 / 100;
			selected = (g_layer == HOST_MENU_LAYER_BOOK &&
				    g_book_focus == i);
			if (selected)
				host_menu_fill(renderer, action, 88, 68, 28,
					       250);
			else
				host_menu_fill(renderer, action, 36, 32, 24,
					       230);
			host_menu_frame(renderer, action, selected ? 3 : 1,
					212, 176, 88, selected ? 255 : 140);
			if (selected)
				host_menu_frame(renderer, action, 3, 255, 220,
						64, 255);
			host_menu_text_center(renderer, g_book_text[i],
					      action.x + action.w / 2,
					      action.y + action.h / 2, pt,
					      g_ink_body);
		}
	} else {
		host_menu_text_center(renderer, g_tab_text[g_tab],
				      body.x + body.w / 2,
				      body.y + body.h / 2 - logical_h / 18,
				      pt_title, g_ink_body);
		host_menu_text_center(renderer, "数据稍后从游戏内存读取",
				      body.x + body.w / 2,
				      body.y + body.h / 2 + logical_h / 18,
				      pt_small, g_ink_hint);
	}

	host_menu_text_center(renderer, "A 确认   B 返回",
			      panel.x + panel.w / 2,
			      panel.y + panel.h - logical_h * 4 / 100,
			      pt_small, g_ink_hint);

	SDL_SetRenderDrawColor(renderer, pr, pg, pb, pa);
	SDL_SetRenderDrawBlendMode(renderer, blend);
}
