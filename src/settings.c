/* User settings, defaults from the original Properties/Settings.settings. Stored as key=value. */
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "bs.h"

Settings cfg;

static const char *dirs[] = { "sd:/apps/babysmash", "usb:/apps/babysmash" };

void settings_defaults(void)
{
    cfg.force_upper = true;
    cfg.fade_away = true;
    cfg.effects = false;
    cfg.mouse_draw = false;
    cfg.faces = true;
    cfg.fade_after = 20;
    cfg.clear_after = 35;
    cfg.sounds = SND_MODE_SPEECH;
    cfg.cursor = 0;
}

static void clamp_cfg(void)
{
    if (cfg.fade_after < 1) cfg.fade_after = 1;
    if (cfg.fade_after > 120) cfg.fade_after = 120;
    if (cfg.clear_after < 1) cfg.clear_after = 1;
    if (cfg.clear_after > MAX_CLEAR_AFTER) cfg.clear_after = MAX_CLEAR_AFTER;
    if (cfg.sounds < 0 || cfg.sounds > SND_MODE_SPEECH) cfg.sounds = SND_MODE_SPEECH;
    if (cfg.cursor < 0 || cfg.cursor > 1) cfg.cursor = 0;
}

void settings_load(void)
{
    settings_defaults();
    for (unsigned d = 0; d < sizeof dirs / sizeof dirs[0]; d++) {
        char path[96];
        snprintf(path, sizeof path, "%s/settings.cfg", dirs[d]);
        FILE *f = fopen(path, "r");
        if (!f) continue;
        char line[64];
        while (fgets(line, sizeof line, f)) {
            char k[32];
            int v;
            if (sscanf(line, "%31[^=]=%d", k, &v) != 2) continue;
            if (!strcmp(k, "force_upper")) cfg.force_upper = v;
            else if (!strcmp(k, "fade_away")) cfg.fade_away = v;
            else if (!strcmp(k, "effects")) cfg.effects = v;
            else if (!strcmp(k, "mouse_draw")) cfg.mouse_draw = v;
            else if (!strcmp(k, "faces")) cfg.faces = v;
            else if (!strcmp(k, "fade_after")) cfg.fade_after = v;
            else if (!strcmp(k, "clear_after")) cfg.clear_after = v;
            else if (!strcmp(k, "sounds")) cfg.sounds = v;
            else if (!strcmp(k, "cursor")) cfg.cursor = v;
        }
        fclose(f);
        break;
    }
    clamp_cfg();
}

bool settings_save(void)
{
    clamp_cfg();
    for (unsigned d = 0; d < sizeof dirs / sizeof dirs[0]; d++) {
        char path[96];
        mkdir(dirs[d], 0777);
        snprintf(path, sizeof path, "%s/settings.cfg", dirs[d]);
        FILE *f = fopen(path, "w");
        if (!f) continue;
        fprintf(f, "force_upper=%d\nfade_away=%d\neffects=%d\nmouse_draw=%d\nfaces=%d\n"
                   "fade_after=%d\nclear_after=%d\nsounds=%d\ncursor=%d\n",
                cfg.force_upper, cfg.fade_away, cfg.effects, cfg.mouse_draw, cfg.faces,
                cfg.fade_after, cfg.clear_after, cfg.sounds, cfg.cursor);
        fclose(f);
        return true;
    }
    return false;
}
