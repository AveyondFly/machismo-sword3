#include "host_font.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOST_FONT_SLOTS 8
#define HOST_FONT_TEST 0x94B1 /* 钱: reject DejaVu and other Latin faces */

static int g_ttf_ready;
static struct {
	int pt;
	TTF_Font *font;
} g_fonts[HOST_FONT_SLOTS];

static int host_font_has_cjk(TTF_Font *font)
{
	return font && TTF_GlyphIsProvided(font, HOST_FONT_TEST);
}

static TTF_Font *host_font_open(const char *path, int pt)
{
	TTF_Font *font;

	if (!path || !path[0])
		return NULL;
	font = TTF_OpenFont(path, pt);
	if (!font)
		return NULL;
	if (!host_font_has_cjk(font)) {
		TTF_CloseFont(font);
		return NULL;
	}
	return font;
}

TTF_Font *host_cjk_font(int pt)
{
	int i;
	int empty;
	int t;
	TTF_Font *font;
	const char *bundle;
	char path[PATH_MAX];
	char path2[PATH_MAX];
	char path3[PATH_MAX];
	char path4[PATH_MAX];
	char path5[PATH_MAX];
	const char *try_path[12];

	if (pt < 11)
		pt = 11;
	if (pt > 72)
		pt = 72;
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
	for (i = 0; i < HOST_FONT_SLOTS; i++) {
		if (g_fonts[i].font && g_fonts[i].pt == pt)
			return g_fonts[i].font;
	}
	empty = -1;
	for (i = 0; i < HOST_FONT_SLOTS; i++) {
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
	memset(try_path, 0, sizeof(try_path));
	try_path[0] = "/storage/roms/ports/sword3/assets/Resource/CS.ttf";
	try_path[1] = "/roms/ports/sword3/assets/Resource/CS.ttf";
	bundle = getenv("SWORD3_BUNDLE_DIR");
	if (bundle && bundle[0] == '/') {
		if (snprintf(path, sizeof(path),
			     "%s/../../../assets/host_menu/cjk.ttf", bundle) <
		    (int)sizeof(path))
			try_path[2] = path;
		if (snprintf(path2, sizeof(path2), "%s/CS.ttf", bundle) <
		    (int)sizeof(path2))
			try_path[3] = path2;
		if (snprintf(path3, sizeof(path3), "%s/Resource/CS.ttf",
			     bundle) < (int)sizeof(path3))
			try_path[4] = path3;
		if (snprintf(path4, sizeof(path4),
			     "%s/../../../../sword3/assets/Resource/CS.ttf",
			     bundle) < (int)sizeof(path4))
			try_path[5] = path4;
		if (snprintf(path5, sizeof(path5), "%s/Resource/CT.ttf",
			     bundle) < (int)sizeof(path5))
			try_path[6] = path5;
	}
	try_path[7] = "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf";
	try_path[8] = "/usr/share/fonts/TTF/DroidSansFallback.ttf";
	try_path[9] = "/usr/share/fonts/truetype/droid/DroidSansFallback.ttf";
	try_path[10] = "/usr/share/fonts/TTF/DejaVuSansMono.ttf";
	font = NULL;
	for (t = 0; t < 11 && !font; t++) {
		if (!try_path[t])
			continue;
		font = host_font_open(try_path[t], pt);
		if (font)
			fprintf(stderr, "sword3-sdl: cjk font %s pt=%d\n",
				try_path[t], pt);
	}
	if (!font)
		return NULL;
	g_fonts[empty].pt = pt;
	g_fonts[empty].font = font;
	return font;
}
