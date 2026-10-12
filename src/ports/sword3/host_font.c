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

static int host_font_has_ascii(TTF_Font *font)
{
	return font && TTF_GlyphIsProvided(font, '0') &&
	       TTF_GlyphIsProvided(font, 'A');
}

static TTF_Font *host_font_open(const char *path, int pt, int need_ascii)
{
	TTF_Font *font;

	if (!path || !path[0])
		return NULL;
	font = TTF_OpenFont(path, pt);
	if (!font)
		return NULL;
	/* A CJK-only font passes the 钱 test but has no Latin range at all, which is
	 * how the trainer drew Chinese correctly and every digit as a box. */
	if (!TTF_GlyphIsProvided(font, HOST_FONT_TEST) ||
	    (need_ascii && !host_font_has_ascii(font))) {
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
	char bundled_cs[PATH_MAX];
	int empty = -1;
	int i;
	int t;
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
		/* The trainer strings are simplified Chinese and the test glyph
		 * below is 钱: the bundled traditional-only CT.ttf fails that
		 * check, so try the simplified CS.ttf first. */
		if (snprintf(bundled_cs, sizeof(bundled_cs), "%s/Resource/CS.ttf",
			     bundle) < (int)sizeof(bundled_cs))
			paths[2] = bundled_cs;
		if (snprintf(bundled, sizeof(bundled), "%s/Resource/CT.ttf",
			     bundle) < (int)sizeof(bundled))
			paths[3] = bundled;
	}
	paths[0] = "/usr/share/fonts/TTF/DejaVuSansMono.ttf";
	paths[4] = "/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf";
	paths[5] = "/usr/share/fonts/TTF/DroidSansFallback.ttf";
	paths[6] = "/usr/share/fonts/truetype/droid/DroidSansFallback.ttf";
	paths[7] = "/usr/share/fonts/source-han-sans-cn/SourceHanSansCN-Regular.otf";
	/* Two passes: Latin coverage is mandatory while any such font exists; a
	 * CJK-only face is only accepted as a last resort so the overlay still shows
	 * Chinese, and then it is named in the log as the cause of box glyphs. */
	for (t = 0; t < 2 && !font; t++) {
		int need_ascii = !t;

		for (i = 0; i < 8 && !font; i++) {
			font = host_font_open(paths[i], pt, need_ascii);
			if (font)
				fprintf(stderr,
					"sword3-sdl: trainer font %s pt=%d%s\n",
					paths[i], pt,
					need_ascii ? ""
						   : " (no ASCII: digits and letters will be boxes)");
		}
	}
	if (!font) {
		fprintf(stderr, "sword3-sdl: no CJK font available for trainer\n");
		return NULL;
	}
	g_fonts[empty].pt = pt;
	g_fonts[empty].font = font;
	return font;
}
