/*
 * BabySmash for Wii - a port of https://github.com/shanselman/babysmash
 *
 * Mash anything and a random coloured shape or letter pops up at a random spot with a sound;
 * letters and digits typed on a USB keyboard become big letters and spell dictionary words.
 * Clicking (A / left mouse) a figure makes it dance; clicking empty space and dragging draws
 * a trail of discs. The options screen opens with Ctrl-Shift-Alt-O held for 3 seconds (or
 * + and - together on a controller, or all three mouse buttons). Alt-F4, or HOME / Start held
 * for two seconds, quits - a baby can't do that by accident.
 */
#include <stdio.h>
#include <fat.h>
#include <ctype.h>
#include <ogcsys.h>
#include <wiiuse/wpad.h>
#include "bs.h"

#define MAX_DISCS   30           /* ellipsesQueue limit in Controller.MouseDraw */
#define EXIT_HOLD   2.0f
#define OPTIONS_HOLD 3.0f        /* OptionsGestureTimer: 3 seconds */
#define PI_F        3.14159265f

/* ---- easing (subset of the Tweener transitions) ------------------------- */
static float ease(int kind, float t)
{
    switch (kind) {
    case 0:                                              /* out bounce */
        if (t < 1 / 2.75f) return 7.5625f * t * t;
        if (t < 2 / 2.75f) { t -= 1.5f / 2.75f; return 7.5625f * t * t + 0.75f; }
        if (t < 2.5f / 2.75f) { t -= 2.25f / 2.75f; return 7.5625f * t * t + 0.9375f; }
        t -= 2.625f / 2.75f; return 7.5625f * t * t + 0.984375f;
    case 1:                                              /* out elastic */
        if (t <= 0) return 0;
        if (t >= 1) return 1;
        return powf(2, -10 * t) * sinf((t - 0.075f) * (2 * PI_F) / 0.3f) + 1;
    case 2: { const float s = 1.70158f; t -= 1; return t * t * ((s + 1) * t + s) + 1; }   /* out back */
    case 3: return t >= 1 ? 1 : 1 - powf(2, -10 * t);   /* out expo */
    case 4: return -t * (t - 2);                        /* out quad */
    case 5: return t < 0.5f ? 4 * t * t * t : 1 - powf(-2 * t + 2, 3) / 2;   /* in-out cubic */
    case 6: t -= 1; return sqrtf(1 - t * t);            /* out circ */
    default: return sinf(t * PI_F / 2);                 /* out sine */
    }
}
#define EASE_KINDS 8

/* ---- figures -------------------------------------------------------------- */
typedef struct {
    int type;                    /* SH_* */
    char ch;                     /* displayed character (letters) */
    int col;
    float x, y;
    float size;                  /* half extent (shapes) or letter height */
    float age;
    int es, er;                  /* spawn easings: scale, rotation */
    float phase;                 /* blink offset */
    bool face;                   /* FacesOnShapes at creation time */
    bool fade;                   /* FadeAway / FadeAfter at creation time */
    float fade_after;
    int eff, eff_col;            /* BitmapEffects: 0 none, 1 blur, 2 glow */
    float eff_r;
    int anim; float anim_t;      /* click animation (-1 = none) */
    float zoom, zoom_to;
    bool moving;                 /* word lining-up */
    float mx0, my0, mx1, my1, ms0, ms1, mt;
} Fig;

static Fig figs[MAX_CLEAR_AFTER + 2];
static int nfig;

typedef struct { float x, y; int col; } Disc;
static Disc discs[MAX_DISCS];
static int ndiscs;

static float now_s;
static bool quit_flag, power_flag, show_fps;

static int rnd(int n) { return rand() % n; }

static void sys_reset_cb(u32 irq, void *ctx) { (void)irq; (void)ctx; quit_flag = true; }
static void sys_power_cb(void) { power_flag = true; }
static void wpad_power_cb(s32 chan) { (void)chan; power_flag = true; }

/* ---- spawning -------------------------------------------------------------- */
static Fig *add_figure(char ch)
{
    while (nfig >= cfg.clear_after) {                    /* queue overflow: drop the oldest */
        memmove(&figs[0], &figs[1], sizeof(Fig) * (nfig - 1));
        nfig--;
    }
    Fig *f = &figs[nfig++];
    memset(f, 0, sizeof *f);
    f->ch = ch;
    if (ch && isalpha((unsigned char)ch) && !cfg.force_upper && rnd(2)) f->ch = tolower((unsigned char)ch);
    f->type = ch ? SH_LETTER : rnd(SH_COUNT);
    f->col = rnd(BS_COLORS);
    f->size = ch ? 132.0f : 52.0f + rnd(16);
    const float hw = ch ? gfx_letter_width(f->size) * 0.5f : f->size;
    const float hh = ch ? f->size * 0.5f : f->size;
    f->x = hw + 8 + rnd((int)(SCR_W - 2 * hw - 16));
    f->y = hh + 8 + rnd((int)(SCR_H - 2 * hh - 16));
    f->es = rnd(EASE_KINDS);
    f->er = rnd(EASE_KINDS);
    f->phase = rnd(73) / 10.0f;
    f->face = cfg.faces;
    f->fade = cfg.fade_away;
    f->fade_after = cfg.fade_after;
    f->anim = -1;
    f->zoom = f->zoom_to = 1;
    if (cfg.effects) {                                   /* Animation.GetRandomBitmapEffect */
        const int e = rnd(4);
        if (e == 0) { f->eff = 1; f->eff_col = f->col; f->eff_r = (5 + rnd(16)) * 0.5f; }
        else { f->eff = 2; f->eff_col = rnd(BS_COLORS); f->eff_r = (10 + rnd(41)) * 0.4f; }
    }
    return f;
}

/* WordFinder.LastWord: longest word just finished by the newest letters (case-sensitive,
 * so with "Force letters to UPPERCASE" off a lower-case letter breaks a word, as in the original) */
static int last_word(char *out)
{
    if (nfig < 2) return 0;
    char rev[16];
    int len = 0, best = 0;
    const int lowest = nfig - 1 - 15 < 0 ? 0 : nfig - 1 - 15;
    for (int i = nfig - 1; i >= lowest; i--) {
        if (figs[i].type != SH_LETTER) break;
        if (len >= 15) break;
        rev[len++] = figs[i].ch;
        char s[16];
        for (int k = 0; k < len; k++) s[k] = rev[len - 1 - k];
        if (len >= 2 && word_exists(s, len)) {
            best = len;
            memcpy(out, s, len);
            out[len] = 0;
        }
    }
    return best;
}

/* WordFinder.AnimateLettersIntoWord */
static void line_up_word(int n)
{
    float cx = 0, cy = 0, sumw = 0;
    for (int i = nfig - n; i < nfig; i++) {
        cx += figs[i].x; cy += figs[i].y;
        sumw += gfx_letter_width(figs[i].size);
    }
    cx /= n; cy /= n;
    float fs = (SCR_W - 40.0f) / sumw;                   /* long words shrink to fit the TV */
    if (fs > 1) fs = 1;
    const float total = sumw * fs;
    float left = cx - total / 2;
    if (left < 20) left = 20;
    if (left + total > SCR_W - 20) left = SCR_W - 20 - total;
    const float h = figs[nfig - 1].size * fs;
    if (cy < h / 2 + 10) cy = h / 2 + 10;
    if (cy > SCR_H - h / 2 - 10) cy = SCR_H - h / 2 - 10;
    float off = 0;
    for (int i = nfig - n; i < nfig; i++) {
        Fig *f = &figs[i];
        const float w = gfx_letter_width(f->size) * fs;
        f->mx0 = f->x; f->my0 = f->y; f->ms0 = f->size;
        f->mx1 = left + off + w / 2; f->my1 = cy; f->ms1 = f->size * fs;
        f->mt = 0; f->moving = true;
        off += w;
    }
}

/* Controller.PlaySound */
static void play_figure_sound(const Fig *f)
{
    if (cfg.sounds == SND_MODE_LAUGHTER) {
        snd_laugh();
    } else if (cfg.sounds == SND_MODE_SPEECH) {
        bool ok;
        if (f->type == SH_LETTER) {
            char name[2] = { f->ch, 0 };
            ok = snd_speak(name, NULL, false);
        } else {
            ok = snd_speak(bs_colors[f->col].name, shape_names[f->type], false);
        }
        if (!ok) snd_laugh();                            /* no speech clips available */
    }
}

static void smash(char ch)
{
    Fig *f = add_figure(ch);
    char word[16];
    const int n = ch ? last_word(word) : 0;
    if (n) {
        line_up_word(n);
        if (!snd_speak_word(word)) snd_play(SND_RISING);
    } else {
        play_figure_sound(f);
    }
}

/* ---- click handling ---------------------------------------------------------- */
static float fig_scale(const Fig *f)
{
    const float t = f->age < 1 ? f->age : 1;
    float sc = ease(f->es, t);
    if (sc < 0) sc = 0;
    return sc * f->zoom;
}

static float fig_alpha(const Fig *f)
{
    if (!f->fade) return 1.0f;
    const float a = 1.0f - f->age / f->fade_after;
    return a < 0 ? 0 : a;
}

static Fig *fig_at(float x, float y)
{
    for (int i = nfig - 1; i >= 0; i--) {
        Fig *f = &figs[i];
        if (fig_alpha(f) <= 0.1f) continue;              /* can it be seen? */
        const float sc = fig_scale(f);
        const float dx = fabsf(x - f->x), dy = fabsf(y - f->y);
        if (f->type == SH_LETTER) {
            if (dx <= gfx_letter_width(f->size) * 0.5f * sc && dy <= f->size * 0.5f * sc) return f;
        } else if (dx <= f->size * sc && dy <= f->size * sc) {
            return f;
        }
    }
    return NULL;
}

static void add_disc(float x, float y)
{
    if (ndiscs >= MAX_DISCS) {
        memmove(&discs[0], &discs[1], sizeof(Disc) * (MAX_DISCS - 1));
        ndiscs--;
    }
    discs[ndiscs].x = x; discs[ndiscs].y = y; discs[ndiscs].col = rnd(BS_COLORS);
    ndiscs++;
}

/* ---- options screen (Options.xaml) ------------------------------------------------ */
enum { R_SOUNDS, R_CURSOR, R_UPPER, R_FACES, R_EFFECTS, R_MDRAW, R_FADE, R_FADE_AFTER,
       R_CLEAR, R_SHOT, R_OK, R_CANCEL, R_COUNT };
#define MENU_X 70
#define MENU_Y 40
#define MENU_W 500
#define MENU_ROW 26
#define MENU_ROWS_Y (MENU_Y + 64)

static bool menu_open;
static bool shot_req;                                    /* capture on the next draw */
static char toast[96];
static float toast_t;
static int menu_sel;
static Settings cfg_backup;
static bool opt_block;                                   /* gesture must be released first */
static float opt_t;

static void row_text(int r, char *buf, int n)
{
    static const char *snd_names[] = { "None", "Laughter", "Speech" };
    static const char *cur_names[] = { "Hand", "Arrow" };
    static const char *onoff[] = { "OFF", "ON" };
    switch (r) {
    case R_SOUNDS: snprintf(buf, n, "Sounds: < %s >", snd_names[cfg.sounds]); break;
    case R_CURSOR: snprintf(buf, n, "Cursor: < %s >", cur_names[cfg.cursor]); break;
    case R_UPPER: snprintf(buf, n, "Force letters to UPPERCASE: %s", onoff[cfg.force_upper]); break;
    case R_FACES: snprintf(buf, n, "Faces on shapes: %s", onoff[cfg.faces]); break;
    case R_EFFECTS: snprintf(buf, n, "Visual effects (blur, glow): %s", onoff[cfg.effects]); break;
    case R_MDRAW: snprintf(buf, n, "Clickless mouse drawing: %s", onoff[cfg.mouse_draw]); break;
    case R_FADE: snprintf(buf, n, "Fade shapes away: %s", onoff[cfg.fade_away]); break;
    case R_FADE_AFTER: snprintf(buf, n, "Fade shapes away in: < %d secs >", cfg.fade_after); break;
    case R_CLEAR: snprintf(buf, n, "Start removing after: < %d shapes >", cfg.clear_after); break;
    case R_SHOT: snprintf(buf, n, "Save screenshot"); break;
    case R_OK: snprintf(buf, n, "OK (save)"); break;
    default: snprintf(buf, n, "Cancel"); break;
    }
}

static void menu_close(bool save)
{
    if (save) settings_save();
    else cfg = cfg_backup;
    menu_open = false;
    opt_block = true;                                    /* require releasing the gesture */
    opt_t = 0;
}

static void row_adjust(int r, int d)                     /* d = +1 / -1; toggles ignore the sign */
{
    switch (r) {
    case R_SOUNDS: cfg.sounds = (cfg.sounds + d + 3) % 3; break;
    case R_CURSOR: cfg.cursor ^= 1; break;
    case R_UPPER: cfg.force_upper = !cfg.force_upper; break;
    case R_FACES: cfg.faces = !cfg.faces; break;
    case R_EFFECTS: cfg.effects = !cfg.effects; break;
    case R_MDRAW: cfg.mouse_draw = !cfg.mouse_draw; break;
    case R_FADE: cfg.fade_away = !cfg.fade_away; break;
    case R_FADE_AFTER:
        cfg.fade_after += d;
        if (cfg.fade_after < 1) cfg.fade_after = 1;
        if (cfg.fade_after > 120) cfg.fade_after = 120;
        break;
    case R_CLEAR:
        cfg.clear_after += d;
        if (cfg.clear_after < 1) cfg.clear_after = 1;
        if (cfg.clear_after > MAX_CLEAR_AFTER) cfg.clear_after = MAX_CLEAR_AFTER;
        break;
    case R_SHOT: shot_req = true; break;
    case R_OK: menu_close(true); break;
    case R_CANCEL: menu_close(false); break;
    }
}

static void menu_update(void)
{
    static float px, py;
    if (in.nav & NAV_UP) menu_sel = (menu_sel + R_COUNT - 1) % R_COUNT;
    if (in.nav & NAV_DOWN) menu_sel = (menu_sel + 1) % R_COUNT;
    if (in.nav & NAV_LEFT) row_adjust(menu_sel, -1);
    if (in.nav & NAV_RIGHT) row_adjust(menu_sel, 1);
    if (in.nav & NAV_BACK) { menu_close(false); return; }
    if (in.x != px || in.y != py) {
        for (int i = 0; i < R_COUNT; i++) {
            const float y = MENU_ROWS_Y + i * MENU_ROW;
            if (in.x >= MENU_X && in.x < MENU_X + MENU_W && in.y >= y && in.y < y + MENU_ROW) menu_sel = i;
        }
    }
    px = in.x; py = in.y;
    if (in.nav & NAV_OK) row_adjust(menu_sel, 1);
}

static void menu_draw(void)
{
    gfx_panel(MENU_X, MENU_Y, MENU_W, 410, RGBA(48, 48, 48, 255), RGBA(0, 0, 0, 255));
    gfx_text(MENU_X + MENU_W / 2, MENU_Y + 12, 22, "BabySmash! Options", RGBA(255, 255, 255, 255));
    for (int i = 0; i < R_COUNT; i++) {
        const float y = MENU_ROWS_Y + i * MENU_ROW;
        if (i == menu_sel) GRRLIB_Rectangle(MENU_X + 8, y, MENU_W - 16, MENU_ROW - 2, RGBA(40, 90, 200, 255), true);
        char buf[64];
        row_text(i, buf, sizeof buf);
        const float h = 16;
        gfx_text(MENU_X + 20 + gfx_text_width(h, buf) / 2, y + 5, h, buf,
                 i == menu_sel ? RGBA(255, 255, 255, 255) : RGBA(209, 209, 209, 255));
    }
    gfx_text(MENU_X + MENU_W / 2, MENU_Y + 380, 12, "D-pad/arrows change, A/Enter select, B/Esc cancel",
             RGBA(180, 180, 180, 255));
}

/* ---- frame ---------------------------------------------------------------------- */
static void update(float dt)
{
    static bool drawing;
    static float last_dx, last_dy;

    /* options gesture: held 3 seconds, then must be released before it can re-arm */
    if (!in.opt_held) { opt_block = false; opt_t = 0; }
    else if (!opt_block && !menu_open) {
        opt_t += dt;
        if (opt_t >= OPTIONS_HOLD) {
            cfg_backup = cfg;
            menu_open = true;
            menu_sel = 0;
            opt_block = true;
            drawing = false;
            snd_bee(false);
        }
    }

    /* screenshot: F12 / Print Screen, or hold 1 + 2 together for 2 seconds */
    static float shot_t;
    static bool shot_block;
    if (in.shot_key) shot_req = true;
    if (!in.shot_held) { shot_t = 0; shot_block = false; }
    else if (!shot_block && (shot_t += dt) >= EXIT_HOLD) { shot_req = true; shot_block = true; }
    if (toast_t > 0) toast_t -= dt;

    for (int i = 0; i < nfig; i++) {
        Fig *f = &figs[i];
        f->age += dt;
        f->zoom += (f->zoom_to - f->zoom) * (dt * 7 > 1 ? 1 : dt * 7);
        if (f->anim >= 0) {
            f->anim_t += dt;
            if (f->anim_t >= 1.0f) f->anim = -1;
        }
        if (f->moving) {
            f->mt += dt / 1.2f;
            const float t = f->mt >= 1 ? 1 : f->mt;
            const float e = ease(5, t);
            f->x = f->mx0 + (f->mx1 - f->mx0) * e;
            f->y = f->my0 + (f->my1 - f->my0) * e;
            f->size = f->ms0 + (f->ms1 - f->ms0) * e;
            if (t >= 1) f->moving = false;
        }
    }

    if (menu_open) { menu_update(); return; }

    for (int i = 0; i < in.nsmash; i++) smash(in.smash[i]);

    if (in.wheel) {                                      /* Controller.MouseWheel + zoom */
        snd_play(in.wheel > 0 ? SND_RISING : SND_FALLING);
        Fig *f = fig_at(in.x, in.y);
        if (f) f->zoom_to = in.wheel > 0 ? 0.5f : 2.5f;
    }

    if (in.click_down) {                                 /* HandleMouseLeftButtonDown + MouseDown */
        Fig *f = fig_at(in.x, in.y);
        if (f) {
            f->anim = rnd(4);                            /* jiggle / snap / throb / rotate */
            f->anim_t = 0;
            snd_laugh();
        } else if (!cfg.mouse_draw) {
            drawing = true;
            add_disc(in.x, in.y);
            last_dx = in.x; last_dy = in.y;
            snd_bee(true);
        }
    }
    if (drawing && in.click_held) {                      /* MouseMove while drawing */
        const float dx = in.x - last_dx, dy = in.y - last_dy;
        if (dx * dx + dy * dy > 100.0f) {
            add_disc(in.x, in.y);
            last_dx = in.x; last_dy = in.y;
        }
    }
    if (drawing && !in.click_held) {                     /* MouseUp */
        drawing = false;
        snd_bee(false);
    }
    if (cfg.mouse_draw && in.cursor_on) {                /* "Clickless mouse drawing" */
        const float dx = in.x - last_dx, dy = in.y - last_dy;
        if (dx * dx + dy * dy > 64.0f) {
            add_disc(in.x, in.y);
            snd_bee_yield();
            last_dx = in.x; last_dy = in.y;
        }
    }
}

static float exit_progress;

static void draw_figure(const Fig *f)
{
    const float alpha = fig_alpha(f);
    if (alpha <= 0) return;
    const float t = f->age < 1 ? f->age : 1;
    float sc = fig_scale(f);
    float rot = 2 * PI_F * (1 - ease(f->er, t));
    if (f->anim >= 0) {
        const float u = f->anim_t;
        switch (f->anim) {
        case 0: rot += 0.26f * sinf(u * 12 * PI_F) * (1 - u); break;            /* jiggle */
        case 1: sc *= fabsf(cosf(u * PI_F)); break;                             /* snap */
        case 2: sc *= 1 + 0.5f * sinf(u * PI_F); break;                         /* throb */
        default: rot += 2 * PI_F * ease(5, u); break;                           /* rotate */
        }
    }
    if (f->eff)
        gfx_halo(f->type, f->ch, f->x, f->y, f->size * sc, rot, f->eff_col, f->eff_r * sc, alpha);
    if (f->type == SH_LETTER) {
        gfx_letter(f->ch, f->x, f->y, f->size * sc, rot, f->col, alpha);
    } else {
        const float blink = fmodf(now_s + f->phase, 7.3f) > 7.1f ? 1.0f : 0.0f;
        gfx_shape(f->type, f->x, f->y, f->size * sc, rot, f->col, alpha, f->face, blink);
    }
}

static void draw_info_label(void)
{
    static const char *lines[] = {
        "BabySmash! by Scott Hanselman",
        "with community contributions! - babysmash.com",
        "Hold Ctrl-Shift-Alt-O (or + and -) 3 secs for options",
        "Alt-F4 (or hold HOME 2 secs) to exit",
    };
    const float h = 14, step = 22;
    gfx_panel(14, 12, 612, 4 * step + 6, RGBA(24, 24, 24, 255), RGBA(255, 230, 0, 255));
    for (int i = 0; i < 4; i++)
        gfx_text(SCR_W / 2, 16 + i * step, h, lines[i], RGBA(255, 230, 0, 255));
}

static void draw(float fps)
{
    GRRLIB_FillScreen(0xf5f5f5ff);                       /* WhiteSmoke */
    draw_info_label();
    for (int i = 0; i < ndiscs; i++) gfx_disc(discs[i].x, discs[i].y, 17, discs[i].col, 1.0f);
    for (int i = 0; i < nfig; i++) draw_figure(&figs[i]);

    if (show_fps) {
        char buf[48];
        snprintf(buf, sizeof buf, "FPS: %d | Shapes: %d", (int)(fps + 0.5f), nfig + ndiscs);
        gfx_text(12 + gfx_text_width(15, buf) / 2, SCR_H - 26, 15, buf, RGBA(40, 40, 40, 255));
    }
    if (shot_req) {                                      /* figures only: no menu, cursor or toast */
        char name[64];
        shot_req = false;
        if (screenshot_save(name, sizeof name)) {
            snprintf(toast, sizeof toast, "Saved %s", name);
            snd_play(SND_RISING);
        } else {
            snprintf(toast, sizeof toast, "Could not save - no writable SD/USB");
            snd_play(SND_FALLING);
        }
        toast_t = 3.0f;
    }
    if (menu_open) menu_draw();
    if (toast_t > 0) {
        const float w = gfx_text_width(14, toast) + 24;
        gfx_panel((SCR_W - w) / 2, 6, w, 26, RGBA(0, 0, 0, 255), RGBA(255, 255, 255, 255));
        gfx_text(SCR_W / 2, 11, 14, toast, RGBA(255, 255, 255, 255));
    }
    if (exit_progress > 0)
        gfx_bar(SCR_W / 2 - 120, SCR_H - 40, 240, 14, exit_progress, RGBA(220, 40, 40, 255));
    if (in.cursor_on) gfx_cursor(in.x, in.y, cfg.cursor);
    GRRLIB_Render();
}

int main(int argc, char **argv)
{
    for (int i = 1; i < argc; i++)
        if (!strcmp(argv[i], "--fps") || !strcmp(argv[i], "-fps")) show_fps = true;
    gfx_init();
    fatInitDefault();
    settings_load();
    SYS_SetResetCallback(sys_reset_cb);
    SYS_SetPowerCallback(sys_power_cb);
    srand(gettime() & 0xffffffff);
    snd_init();
    words_init();
    input_init();
    WPAD_SetPowerButtonCallback(wpad_power_cb);
    atexit(input_shutdown);
    snd_play(SND_STARTUP);

    u64 prev = gettime();
    float hold = 0, fps = 60, fps_acc = 0;
    int fps_frames = 0;
    for (;;) {
        const u64 t = gettime();
        float dt = (float)ticks_to_microsecs(diff_ticks(prev, t)) / 1e6f;
        prev = t;
        if (dt > 0.1f) dt = 0.1f;
        now_s += dt;
        fps_acc += dt; fps_frames++;
        if (fps_acc >= 1.0f) { fps = fps_frames / fps_acc; fps_acc = 0; fps_frames = 0; }

        input_update();
#ifdef AUTOTEST   /* make EXTRA_CFLAGS=-DAUTOTEST: fake input for emulator smoke tests */
        {
            static float acc;
            static int step;
            acc += dt;
            if (acc > 0.5f) {
                acc = 0;
                static const char txt[] = { 0, 'A', 0, 'B', 0, 0, 'H', 'E', 'L', 'L', 'O', 0, 'C', 'A', 'T', '7', 0, 'q', 0 };
                in.smash[in.nsmash++] = txt[step++ % sizeof txt];
                if (step == 12) { in.x = figs[nfig - 1].x; in.y = figs[nfig - 1].y; in.click_down = true; }
                if (step == 1) { in.opt_held = true; opt_t = OPTIONS_HOLD; }
                if (step == 2) in.shot_key = true;
                if (step == 3) in.nav |= NAV_DOWN;
                if (step == 4) in.nav |= NAV_RIGHT;
                if (step == 40) quit_flag = true;
            }
        }
#endif
        snd_update();
        update(dt);
        hold = in.exit_held ? hold + dt : 0;
        exit_progress = hold / EXIT_HOLD;
        draw(fps);

        if (hold >= EXIT_HOLD || in.exit_now) quit_flag = true;
        if (power_flag) {
            snd_stop_all();
            SYS_ResetSystem(SYS_POWEROFF, 0, 0);
        }
        if (quit_flag) break;
    }
    snd_stop_all();
    GRRLIB_Exit();
    return 0;
}
