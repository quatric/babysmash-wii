#ifndef BS_H
#define BS_H

#include <gccore.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <ogc/lwp_watchdog.h>
#include <grrlib.h>

#define SCR_W 640
#define SCR_H 480
#ifndef RGBA
#define RGBA(r,g,b,a) (((u32)(r)<<24)|((u32)(g)<<16)|((u32)(b)<<8)|(u32)(a))
#endif

/* ---- settings.c (Properties/Settings.settings of the original) ---------- */
enum { SND_MODE_NONE, SND_MODE_LAUGHTER, SND_MODE_SPEECH };
typedef struct {
    bool force_upper;            /* ForceUppercase (true) */
    bool fade_away;              /* FadeAway (true) */
    bool effects;                /* BitmapEffects (false): blur / glow */
    bool mouse_draw;             /* MouseDraw (false): clickless drawing */
    bool faces;                  /* FacesOnShapes (true) */
    int  fade_after;             /* FadeAfter (20 s) */
    int  clear_after;            /* ClearAfter (35 shapes) */
    int  sounds;                 /* Sounds: SND_MODE_* (Speech) */
    int  cursor;                 /* CursorType: 0 = Hand, 1 = Arrow */
} Settings;
extern Settings cfg;
#define MAX_CLEAR_AFTER 99
void settings_defaults(void);
void settings_load(void);
bool settings_save(void);

/* ---- gfx.c ------------------------------------------------------------- */
enum { SH_CIRCLE, SH_OVAL, SH_RECT, SH_HEXAGON, SH_TRAPEZOID, SH_STAR,
       SH_SQUARE, SH_TRIANGLE, SH_HEART, SH_COUNT, SH_LETTER = SH_COUNT };
extern const char *shape_names[SH_COUNT];

typedef struct { u8 r, g, b; const char *name; } BsColor;
#define BS_COLORS 9
extern const BsColor bs_colors[BS_COLORS];

void gfx_init(void);
void gfx_shape(int type, float cx, float cy, float s, float rot, int col,
               float alpha, bool face, float blink);
void gfx_letter(char ch, float cx, float cy, float h, float rot, int col, float alpha);
float gfx_letter_width(float h);
void gfx_disc(float cx, float cy, float r, int col, float alpha);
void gfx_halo(int type, char ch, float cx, float cy, float s, float rot, int col,
              float radius, float alpha);
void gfx_cursor(float x, float y, int kind);
void gfx_text(float cx, float y, float h, const char *s, u32 rgba);
float gfx_text_width(float h, const char *s);
void gfx_panel(float x, float y, float w, float h, u32 fill, u32 border);
void gfx_bar(float x, float y, float w, float h, float frac, u32 rgba);

/* ---- screenshot.c ------------------------------------------------------- */
bool screenshot_save(char *outname, int outlen);

/* ---- words.c ------------------------------------------------------------ */
void words_init(void);
bool word_exists(const char *s, int len);       /* upper-case, not NUL terminated */

/* ---- sound.c ------------------------------------------------------------ */
enum { SND_STARTUP, SND_RISING, SND_FALLING, SND_BEE, SND_LAUGH_FIRST, SND_LAUGH_COUNT = 6 };
void snd_init(void);
void snd_play(int id);
void snd_laugh(void);
void snd_bee(bool on);
void snd_bee_yield(void);                       /* only if not already sounding */
bool snd_speak(const char *a, const char *b, bool priority);   /* queued; false if no clip */
bool snd_speak_word(const char *word);                          /* optional SD clips */
void snd_update(void);
void snd_stop_all(void);

/* ---- input.c ------------------------------------------------------------ */
enum {
    IN_A = 1 << 0, IN_B = 1 << 1, IN_1 = 1 << 2, IN_2 = 1 << 3,
    IN_PLUS = 1 << 4, IN_MINUS = 1 << 5, IN_HOME = 1 << 6,
    IN_UP = 1 << 7, IN_DOWN = 1 << 8, IN_LEFT = 1 << 9, IN_RIGHT = 1 << 10,
    IN_MCLICK = 1 << 11, IN_RCLICK = 1 << 12,
};
enum { NAV_UP = 1, NAV_DOWN = 2, NAV_LEFT = 4, NAV_RIGHT = 8, NAV_OK = 16, NAV_BACK = 32 };
#define MAX_SMASH 24
typedef struct {
    float x, y;                  /* pointer */
    bool cursor_on;              /* show the fun cursor */
    bool moved;                  /* pointer moved this frame */
    bool click_down, click_held, click_up;   /* A / left mouse */
    int  nsmash;
    char smash[MAX_SMASH];       /* typed char, or 0 = random shape (a non-letter key) */
    int  wheel;                  /* +1 up, -1 down pulses */
    bool exit_held;              /* HOME / Start held (2 s to quit) */
    bool exit_now;               /* Alt-F4 */
    bool opt_held;               /* Ctrl-Shift-Alt-O, + and - together, or all mouse buttons */
    u32  nav;                    /* NAV_* pressed this frame (options screen) */
    bool shot_key;               /* F12 / Print Screen pressed */
    bool shot_held;              /* 1 and 2 held together on a controller */
    const char *source;
} Input;
extern Input in;
void input_init(void);
void input_update(void);
void input_shutdown(void);

#endif
