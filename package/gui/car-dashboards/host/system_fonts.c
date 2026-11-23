#include "system_fonts.h"
#ifndef HZ_TARGET_UI
#include <fontconfig/fontconfig.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Fontconfig returns face indices as well as paths, including CJK collections. */
struct font_source { char *path; int index; };
static struct font_source sources[3];
static lv_font_t *fonts[97][3];
lv_font_t dashboard_default_font;

static void select_font(unsigned slot, const char *setting, const char *pattern)
{
    const char *requested = getenv(setting);
#ifdef HZ_TARGET_UI
    sources[slot].path = strdup(requested ? requested : pattern);
    if (!sources[slot].path) exit(1);
    sources[slot].index = 0;
#else
    FcPattern *query = FcNameParse((const FcChar8 *)(requested ? requested : pattern));
    if (!query) exit(1);
    FcConfigSubstitute(NULL, query, FcMatchPattern);
    FcDefaultSubstitute(query);
    FcResult result;
    FcPattern *match = FcFontMatch(NULL, query, &result);
    FcPatternDestroy(query);
    FcChar8 *path = NULL;
    if (!match || FcPatternGetString(match, FC_FILE, 0, &path) != FcResultMatch) {
        fprintf(stderr, "Cannot resolve system font %s\n", requested ? requested : pattern);
        exit(1);
    }
    sources[slot].path = strdup((const char *)path);
    if (!sources[slot].path) exit(1);
    FcPatternGetInteger(match, FC_INDEX, 0, &sources[slot].index);
    FcPatternDestroy(match);
#endif
    fprintf(stderr, "System font: %s (face %d)\n", sources[slot].path, sources[slot].index);
}

const lv_font_t *dashboard_font(unsigned size)
{
    if (size < 8 || size > 96) {
        fprintf(stderr, "Unsupported dashboard font size: %u\n", size);
        exit(1);
    }
    if (!fonts[size][0]) {
        for (unsigned i = 0; i < 3; i++) {
#ifdef HZ_TARGET_UI
            if(i==1)continue;
#endif
            lv_ft_info_t info = {
                .name = sources[i].path,
                .weight = size,
                .face_index = sources[i].index,
                .style = FT_FONT_STYLE_NORMAL,
            };
            if (!info.name || !lv_ft_font_init(&info)) {
                fprintf(stderr, "Cannot load system font at %u px\n", size);
                exit(1);
            }
            fonts[size][i] = info.font;
        }
#ifdef HZ_TARGET_UI
        /* The image has no CJK font (19 MB); such characters render as boxes. */
        fonts[size][0]->fallback=fonts[size][2];
#else
        fonts[size][0]->fallback = fonts[size][1];fonts[size][1]->fallback = fonts[size][2];
#endif
    }
    return fonts[size][0];
}

void dashboard_fonts_init(void)
{
#ifndef HZ_TARGET_UI
    if (!FcInit()) {
        fprintf(stderr, "Cannot initialize Fontconfig\n");
        exit(1);
    }
    select_font(0, "DASHBOARD_FONT", "sans");
    select_font(1, "DASHBOARD_CJK_FONT", "sans:lang=zh-cn");
    select_font(2, "DASHBOARD_ICON_FONT", "FontAwesome");
#else
    select_font(0, "DASHBOARD_FONT", "/usr/share/fonts/hz-ui/NotoSans-Regular.ttf");
    select_font(2, "DASHBOARD_ICON_FONT", "/usr/share/fonts/hz-ui/fontawesome-webfont.ttf");
#endif
    dashboard_default_font = *dashboard_font(14);
    const uint32_t required[] = {'A', 0xf04b
#ifndef HZ_TARGET_UI
        ,0x4e2d
#endif
    };
    for (unsigned i = 0; i < sizeof(required) / sizeof(required[0]); i++) {
        lv_font_glyph_dsc_t glyph;
        if (!lv_font_get_glyph_dsc(&dashboard_default_font, &glyph, required[i], 0)) {
            fprintf(stderr, "System font missing U+%04X; install Latin, CJK and Font Awesome fonts\n", required[i]);
            exit(1);
        }
    }
#ifdef HZ_TARGET_UI
    fprintf(stderr, "System fonts ready: Latin and icons\n");
#else
    fprintf(stderr, "System fonts ready: Latin, CJK and icons\n");
#endif
}
