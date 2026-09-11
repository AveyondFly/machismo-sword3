#include "host_cheat.h"
#include "host_font.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <SDL2/SDL_ttf.h>

#define CHEAT_GAME_W 640
#define CHEAT_GAME_H 480
#define CHEAT_PANEL_W 344
#define CHEAT_PANEL_H 272
#define CHEAT_ROW_H 28
#define CHEAT_TEXT_CACHE 32
#define CHEAT_MONEY_MAX 99999999

/* Paladin 2's SaveData bridge and its four persistent player records. */
#define PAL2_LUA_DATA 0x100490e18ull
#define PAL2_LUA_SET_I32 0x1001eb054ull
#define PAL2_STR_PLAYER_DATA 0x1002285fbull
#define PAL2_STR_GOLDS 0x100228606ull
#define PAL2_PLAYER_DATA 0x10046ee30ull
#define PAL2_PLAYER_COUNT 4
#define PAL2_PLAYER_STRIDE 0x28cu
#define PAL2_PLAYER_FIRST 4u
#define PAL2_PLAYER_ID 0u
#define PAL2_PLAYER_HP 0x118u
#define PAL2_PLAYER_HP_MAX 0x11cu
#define PAL2_PLAYER_MP 0x124u
#define PAL2_PLAYER_MP_MAX 0x128u
#define PAL2_PLAYER_GOLD 0xb08u

enum cheat_item {
	CHEAT_MONEY = 0,
	CHEAT_HP,
	CHEAT_NO_ENCOUNTER,
	CHEAT_LEVEL_UP,
	CHEAT_ONE_HIT,
	CHEAT_CLOSE,
	CHEAT_ITEM_COUNT
};

static int g_open;
static int g_selected;
static int g_no_encounter;
static int g_level_up;
static int g_one_hit;
static char g_status[64];
static Uint32 g_status_until;
static struct {
	SDL_Texture *texture;
	SDL_Renderer *renderer;
	char text[80];
	int pt;
	Uint32 rgba;
	int w;
	int h;
} g_text[CHEAT_TEXT_CACHE];
static int g_text_clock;
static SDL_Renderer *g_text_renderer;

static void cheat_set_status(const char *status);

static int cheat_sane_stat(int value)
{
	return value > 0 && value <= 9999999;
}

static void cheat_apply_money(void)
{
	volatile int32_t *gold = (volatile int32_t *)(uintptr_t)(
		PAL2_PLAYER_DATA + PAL2_PLAYER_GOLD);

	*gold = CHEAT_MONEY_MAX;
	((void (*)(void *, const void *, const void *, int))
		(uintptr_t)PAL2_LUA_SET_I32)(
		(void *)(uintptr_t)PAL2_LUA_DATA,
		(const void *)(uintptr_t)PAL2_STR_PLAYER_DATA,
		(const void *)(uintptr_t)PAL2_STR_GOLDS,
		CHEAT_MONEY_MAX);
	cheat_set_status("金钱已拉满");
	fprintf(stderr, "sword3-sdl: Pal2 trainer money=%d\n",
		CHEAT_MONEY_MAX);
}

static void cheat_apply_hp(void)
{
	volatile int32_t *manager =
		(volatile int32_t *)(uintptr_t)PAL2_PLAYER_DATA;
	int count = manager[0];
	int filled = 0;
	int i;

	if (count < 0 || count > PAL2_PLAYER_COUNT)
		count = 0;
	for (i = 0; i < count; i++) {
		volatile uint8_t *record = (volatile uint8_t *)manager +
			PAL2_PLAYER_FIRST + (uintptr_t)i * PAL2_PLAYER_STRIDE;
		volatile int32_t *id =
			(volatile int32_t *)(record + PAL2_PLAYER_ID);
		volatile int32_t *hp =
			(volatile int32_t *)(record + PAL2_PLAYER_HP);
		volatile int32_t *hp_max =
			(volatile int32_t *)(record + PAL2_PLAYER_HP_MAX);
		volatile int32_t *mp =
			(volatile int32_t *)(record + PAL2_PLAYER_MP);
		volatile int32_t *mp_max =
			(volatile int32_t *)(record + PAL2_PLAYER_MP_MAX);

		if (*id <= 0 || !cheat_sane_stat(*hp_max))
			continue;
		*hp = *hp_max;
		if (cheat_sane_stat(*mp_max))
			*mp = *mp_max;
		filled++;
	}
	if (filled) {
		cheat_set_status("全员已满血");
		fprintf(stderr,
			"sword3-sdl: Pal2 trainer filled %d player records\n",
			filled);
	} else {
		cheat_set_status("找不到队伍数据");
	}
}

static int cheat_sx(int value, int logical_w)
{
	return value * logical_w / CHEAT_GAME_W;
}

static int cheat_sy(int value, int logical_h)
{
	return value * logical_h / CHEAT_GAME_H;
}

static int cheat_pt(int logical_h)
{
	int pt = 18 * logical_h / CHEAT_GAME_H;

	if (pt < 12)
		pt = 12;
	if (pt > 28)
		pt = 28;
	return pt;
}

static void cheat_set_status(const char *status)
{
	snprintf(g_status, sizeof(g_status), "%s", status);
	g_status_until = SDL_GetTicks() + 1800;
}

static void cheat_open(void)
{
	g_open = 1;
	g_selected = 0;
	g_status[0] = 0;
	fprintf(stderr, "sword3-sdl: Pal2 trainer open\n");
}

static void cheat_close(void)
{
	if (!g_open)
		return;
	g_open = 0;
	g_status[0] = 0;
	fprintf(stderr, "sword3-sdl: Pal2 trainer close\n");
}

static void cheat_apply(void)
{
	switch (g_selected) {
	case CHEAT_MONEY:
		cheat_apply_money();
		break;
	case CHEAT_HP:
		cheat_apply_hp();
		break;
	case CHEAT_NO_ENCOUNTER:
		g_no_encounter = !g_no_encounter;
		cheat_set_status(g_no_encounter ? "不遇敌已开" : "不遇敌已关");
		break;
	case CHEAT_LEVEL_UP:
		g_level_up = !g_level_up;
		cheat_set_status(g_level_up ? "战后升级已开" : "战后升级已关");
		break;
	case CHEAT_ONE_HIT:
		g_one_hit = !g_one_hit;
		cheat_set_status(g_one_hit ? "一击必杀已开" : "一击必杀已关");
		break;
	case CHEAT_CLOSE:
		cheat_close();
		break;
	default:
		break;
	}
}

int host_cheat_active(void)
{
	return g_open;
}

int host_cheat_key(SDL_Scancode scancode, int down, int repeat)
{
	if (scancode == SDL_SCANCODE_TAB) {
		if (down && !repeat) {
			if (g_open)
				cheat_close();
			else
				cheat_open();
		}
		return 1;
	}
	if (!g_open)
		return 0;
	if (!down || repeat)
		return 1;
	switch (scancode) {
	case SDL_SCANCODE_UP:
		g_selected = (g_selected + CHEAT_ITEM_COUNT - 1) %
			CHEAT_ITEM_COUNT;
		break;
	case SDL_SCANCODE_DOWN:
		g_selected = (g_selected + 1) % CHEAT_ITEM_COUNT;
		break;
	case SDL_SCANCODE_SPACE:
		cheat_apply();
		break;
	case SDL_SCANCODE_ESCAPE:
		cheat_close();
		break;
	default:
		break;
	}
	return 1;
}

static Uint32 cheat_rgba(SDL_Color color)
{
	return ((Uint32)color.r << 24) | ((Uint32)color.g << 16) |
	       ((Uint32)color.b << 8) | color.a;
}

static void cheat_text_flush(SDL_Renderer *renderer)
{
	int i;

	for (i = 0; i < CHEAT_TEXT_CACHE; i++) {
		if (!g_text[i].texture)
			continue;
		if (renderer && g_text[i].renderer != renderer)
			continue;
		SDL_DestroyTexture(g_text[i].texture);
		memset(&g_text[i], 0, sizeof(g_text[i]));
	}
}

static SDL_Texture *cheat_text_texture(SDL_Renderer *renderer,
				       const char *text, int pt,
				       SDL_Color color, int *w, int *h)
{
	TTF_Font *font;
	SDL_Surface *surface;
	SDL_Texture *texture;
	Uint32 rgba = cheat_rgba(color);
	int i;
	int slot;

	for (i = 0; i < CHEAT_TEXT_CACHE; i++) {
		if (g_text[i].texture && g_text[i].renderer == renderer &&
		    g_text[i].pt == pt && g_text[i].rgba == rgba &&
		    strcmp(g_text[i].text, text) == 0) {
			*w = g_text[i].w;
			*h = g_text[i].h;
			return g_text[i].texture;
		}
	}
	font = host_cjk_font(pt);
	if (!font)
		return NULL;
	surface = TTF_RenderUTF8_Blended(font, text, color);
	if (!surface)
		return NULL;
	texture = SDL_CreateTextureFromSurface(renderer, surface);
	if (texture)
		SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
	*w = surface->w;
	*h = surface->h;
	slot = g_text_clock++ % CHEAT_TEXT_CACHE;
	if (g_text[slot].texture)
		SDL_DestroyTexture(g_text[slot].texture);
	snprintf(g_text[slot].text, sizeof(g_text[slot].text), "%s", text);
	g_text[slot].texture = texture;
	g_text[slot].renderer = renderer;
	g_text[slot].pt = pt;
	g_text[slot].rgba = rgba;
	g_text[slot].w = *w;
	g_text[slot].h = *h;
	SDL_FreeSurface(surface);
	return texture;
}

static void cheat_text(SDL_Renderer *renderer, int x, int y, int pt,
		       SDL_Color color, const char *text)
{
	SDL_Texture *texture;
	SDL_Rect dst;
	int w;
	int h;

	texture = cheat_text_texture(renderer, text, pt, color, &w, &h);
	if (!texture)
		return;
	dst.x = x;
	dst.y = y;
	dst.w = w;
	dst.h = h;
	SDL_RenderCopy(renderer, texture, NULL, &dst);
}

static const char *cheat_label(int item, char *buffer, size_t size)
{
	switch (item) {
	case CHEAT_MONEY:
		return "金钱最大";
	case CHEAT_HP:
		return "人物满血";
	case CHEAT_NO_ENCOUNTER:
		snprintf(buffer, size, "不遇敌    %s",
			 g_no_encounter ? "开" : "关");
		return buffer;
	case CHEAT_LEVEL_UP:
		snprintf(buffer, size, "战后升级  %s",
			 g_level_up ? "开" : "关");
		return buffer;
	case CHEAT_ONE_HIT:
		snprintf(buffer, size, "一击必杀  %s",
			 g_one_hit ? "开" : "关");
		return buffer;
	case CHEAT_CLOSE:
		return "关闭";
	default:
		return "";
	}
}

void host_cheat_draw(SDL_Renderer *renderer, int logical_w, int logical_h)
{
	SDL_BlendMode old_blend;
	SDL_Color old_color;
	SDL_Rect rect;
	SDL_Rect highlight;
	SDL_Color title = {255, 220, 120, 255};
	SDL_Color normal = {220, 210, 190, 255};
	SDL_Color selected = {255, 255, 220, 255};
	SDL_Color hint = {170, 160, 140, 255};
	SDL_Color status = {140, 230, 150, 255};
	char buffer[40];
	const char *label;
	int panel_w;
	int panel_h;
	int panel_x;
	int panel_y;
	int row_h;
	int pt;
	int i;
	int y;

	if (!g_open || !renderer || logical_w <= 0 || logical_h <= 0)
		return;
	if (g_text_renderer != renderer) {
		cheat_text_flush(g_text_renderer);
		g_text_renderer = renderer;
	}
	panel_w = cheat_sx(CHEAT_PANEL_W, logical_w);
	panel_h = cheat_sy(CHEAT_PANEL_H, logical_h);
	panel_x = (logical_w - panel_w) / 2;
	panel_y = (logical_h - panel_h) / 2;
	row_h = cheat_sy(CHEAT_ROW_H, logical_h);
	pt = cheat_pt(logical_h);

	SDL_GetRenderDrawBlendMode(renderer, &old_blend);
	SDL_GetRenderDrawColor(renderer, &old_color.r, &old_color.g,
			       &old_color.b, &old_color.a);
	SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
	rect.x = 0;
	rect.y = 0;
	rect.w = logical_w;
	rect.h = logical_h;
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 140);
	SDL_RenderFillRect(renderer, &rect);
	rect.x = panel_x;
	rect.y = panel_y;
	rect.w = panel_w;
	rect.h = panel_h;
	SDL_SetRenderDrawColor(renderer, 28, 22, 16, 235);
	SDL_RenderFillRect(renderer, &rect);
	SDL_SetRenderDrawColor(renderer, 210, 170, 70, 255);
	SDL_RenderDrawRect(renderer, &rect);
	rect.x += 2;
	rect.y += 2;
	rect.w -= 4;
	rect.h -= 4;
	SDL_RenderDrawRect(renderer, &rect);

	cheat_text(renderer, panel_x + cheat_sx(24, logical_w),
		   panel_y + cheat_sy(16, logical_h), pt, title, "修改器");
	for (i = 0; i < CHEAT_ITEM_COUNT; i++) {
		y = panel_y + cheat_sy(54, logical_h) + i * row_h;
		label = cheat_label(i, buffer, sizeof(buffer));
		if (i == g_selected) {
			highlight.x = panel_x + cheat_sx(16, logical_w);
			highlight.y = y - cheat_sy(4, logical_h);
			highlight.w = panel_w - cheat_sx(32, logical_w);
			highlight.h = row_h;
			SDL_SetRenderDrawColor(renderer, 90, 70, 28, 220);
			SDL_RenderFillRect(renderer, &highlight);
		}
		cheat_text(renderer, panel_x + cheat_sx(28, logical_w), y,
			   pt, i == g_selected ? selected : normal, label);
	}
	cheat_text(renderer, panel_x + cheat_sx(24, logical_w),
		   panel_y + panel_h - cheat_sy(48, logical_h), pt, hint,
		   "A 确定   B/SELECT 关闭   上下选择");
	if (g_status[0] && SDL_GetTicks() < g_status_until)
		cheat_text(renderer, panel_x + cheat_sx(24, logical_w),
			   panel_y + panel_h - cheat_sy(24, logical_h), pt,
			   status, g_status);
	SDL_SetRenderDrawBlendMode(renderer, old_blend);
	SDL_SetRenderDrawColor(renderer, old_color.r, old_color.g,
			       old_color.b, old_color.a);
}
