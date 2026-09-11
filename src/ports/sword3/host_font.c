#include "host_font.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HOST_FONT_SLOTS 8
#define HOST_FONT_TEST 0x94b1 /* 钱 */

static int g_ttf_ready;
static struct {
	int pt;
	TTF_Font *font;
} g_fonts[HOST_FONT_SLOTS];

static TTF_Font *host_font_open(const char *path, int pt)
{
	TTF_Font *font;

	if (!path || !path[0])
		return NULL;
	font = TTF_OpenFont(path, pt);
	if (!font)
		return NULL;
	if (!TTF_GlyphIsProvided(font, HOST_FONT_TEST)) {
		TTF_CloseFont(font);
		return NULL;
	}
	return font;
}

TTF_Font *host_cjk_font(int pt)
{
	const char *bundle;
	const char *paths[10] = {0};
	char packaged[PATH_MAX];
	char bundled[PATH_MAX];
	int empty = -1;
	int i;
	TTF_Font *font = NULL;

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
		if (!g_fonts[i].font && empty < 0)
			empty = i;
	}
	if (empty < 0) {
		empty = 0;
		TTF_CloseFont(g_fonts[0].font);
		g_fonts[0].font = NULL;
	}

	bundle = getenv("SWORD3_BUNDLE_DIR");
	if (bundle && bundle[0] == '/') {
		if (snprintf(packaged, sizeof(packaged),
			     "%s/../../../assets/host_menu/cjk.ttf", bundle) <
		    (int)sizeof(packaged))
			paths[1] = packaged;
		if (snprintf(bundled, sizeof(bundled), "%s/Resource/CT.ttf",
			     bundle) < (int)sizeof(bundled))
			paths[2] = bundled;
	}
	paths[0] = "/usr/share/fonts/TTF/DejaVuSansMono.ttf";
	paths[3] = "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf";
	paths[4] = "/usr/share/fonts/TTF/DroidSansFallback.ttf";
	paths[5] = "/usr/share/fonts/truetype/droid/DroidSansFallback.ttf";
	for (i = 0; i < 6 && !font; i++)
		font = host_font_open(paths[i], pt);
	if (!font) {
		fprintf(stderr, "sword3-sdl: no CJK font available for trainer\n");
		return NULL;
	}
	fprintf(stderr, "sword3-sdl: trainer font %s pt=%d\n", paths[i - 1],
		pt);
	g_fonts[empty].pt = pt;
	g_fonts[empty].font = font;
	return font;
}
