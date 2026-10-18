/*
 * Unified input: Wiimote (IR pointer, Nunchuk), Classic Controller, GameCube pad,
 * Wii U Pro Controller, Wii U GamePad, DualShock 3, USB mouse and USB keyboard.
 * The devices come from libinput-wii.
 *
 * Everything is reduced to:
 *   - a pointer + click (Wiimote IR / sticks / mouse, A or left button)
 *   - "smash" events: every other button press, mouse button or key press
 *   - exit (HOME / Start held, or Alt-F4) and the options gesture
 */
#include <stdio.h>
#include <ogcsys.h>
#include <wiiuse/wpad.h>
#include <input_wii.h>
#include "bs.h"

Input in;

static u32 prev_held;
static float best_mag;
static float best_x, best_y;
static const char *best_src;
static int idle_frames = 9999;

/* HID usage -> 'A'-'Z' / '0'-'9' (0 if it is some other key) */
static char hid_to_char(u8 c)
{
    if (c >= 0x04 && c <= 0x1d) return 'A' + (c - 0x04);
    if (c >= 0x1e && c <= 0x26) return '1' + (c - 0x1e);
    if (c == 0x27) return '0';
    if (c >= 0x59 && c <= 0x61) return '1' + (c - 0x59);   /* keypad */
    if (c == 0x62) return '0';
    return 0;
}

/* ---- helpers ------------------------------------------------------------ */
static void stick(const char *src, float x, float y)
{
    const float m = sqrtf(x * x + y * y);
    if (m > 1.0f) { x /= m; y /= m; }
    const float mm = m > 1.0f ? 1.0f : m;
    if (mm < 0.18f) return;
    if (mm > best_mag) { best_mag = mm; best_x = x; best_y = y; best_src = src; }
}

static u32 map_buttons(u32 b)
{
    u32 out = 0;
    if (b & WPAD_BUTTON_A) out |= IN_A;
    if (b & WPAD_BUTTON_B) out |= IN_B;
    if (b & WPAD_BUTTON_1) out |= IN_1;
    if (b & WPAD_BUTTON_2) out |= IN_2;
    if (b & WPAD_BUTTON_PLUS) out |= IN_PLUS;
    if (b & WPAD_BUTTON_MINUS) out |= IN_MINUS;
    if (b & WPAD_BUTTON_HOME) out |= IN_HOME;
    if (b & WPAD_BUTTON_UP) out |= IN_UP;
    if (b & WPAD_BUTTON_DOWN) out |= IN_DOWN;
    if (b & WPAD_BUTTON_LEFT) out |= IN_LEFT;
    if (b & WPAD_BUTTON_RIGHT) out |= IN_RIGHT;
    if (b & INPUT_BUTTON_Z) out |= IN_B;
    if (b & INPUT_BUTTON_C) out |= IN_1;
    return out;
}

/* Both sticks of every device; screen y grows downward. */
static void lib_stick(int which)
{
    float x, y;
    const char *src;
    if (INPUT_Stick(which, &x, &y, &src) > 0)
        stick(src, x, -y);
}

static void smash_push(char c)
{
    if (in.nsmash < MAX_SMASH) in.smash[in.nsmash++] = c;
}

/* Controller buttons have no letter, so a mash types a random character: mostly letters,
 * some digits, and now and then 0 = a plain shape (like a non-letter key on the keyboard). */
static char rand_char(void)
{
    const int r = rand() % 20;
    if (r < 13) return 'A' + rand() % 26;
    if (r < 17) return '0' + rand() % 10;
    return 0;
}

static bool mod_ctrl(void)  { return (INPUT_KeyModifiers() & INPUT_MOD_CTRL) != 0; }
static bool mod_shift(void) { return (INPUT_KeyModifiers() & INPUT_MOD_SHIFT) != 0; }
static bool mod_alt(void)   { return (INPUT_KeyModifiers() & INPUT_MOD_ALT) != 0; }

/* ---- public ------------------------------------------------------------- */
void input_init(void)
{
    INPUT_Init();
    INPUT_SetScreenSize(SCR_W, SCR_H);
    /* Every key smashes; none of them stands in for a controller button. */
    INPUT_KeyboardMapClear();

    in.x = SCR_W / 2;
    in.y = SCR_H / 2;
    in.source = "";
}

void input_shutdown(void)
{
    INPUT_Shutdown();
}

void input_update(void)
{
    u32 held;
    float px, py;
    int mdx, mdy, mwheel;
    const float old_x = in.x, old_y = in.y;
    bool active = false;
    u32 nav = 0;
    bool opt = false;
    in.shot_key = false;
    best_mag = 0;
    best_src = NULL;
    in.wheel = 0;
    in.nsmash = 0;

    INPUT_Update();

    held = map_buttons(INPUT_ButtonsHeld());
    if (INPUT_Source()[0]) in.source = INPUT_Source();

    if (INPUT_Pointer(&px, &py)) {
        in.x = px;
        in.y = py;
        in.source = "Wiimote";
        active = true;
    }
    lib_stick(INPUT_STICK_LEFT);
    lib_stick(INPUT_STICK_RIGHT);

    /* USB mouse: left = click, right / middle = smash, wheel = zoom sounds */
    INPUT_MouseDelta(&mdx, &mdy, &mwheel);
    if (mwheel > 0) in.wheel = 1;
    if (mwheel < 0) in.wheel = -1;
    {
        const u8 mbtn = INPUT_MouseButtonsHeld();
        if (mbtn & INPUT_MOUSE_LEFT) held |= IN_A;
        if (mbtn & INPUT_MOUSE_RIGHT) held |= IN_RCLICK;
        if (mbtn & INPUT_MOUSE_MIDDLE) held |= IN_MCLICK;
        if ((mbtn & 7) == 7) opt = true;
        if (mdx != 0 || mdy != 0) { in.source = "Mouse"; active = true; }
    }

    /* USB keyboard: letters/digits keep their glyph, every other key is a shape (as in the
     * original, where only letters and digits are drawn as letters). Alt-F4 quits and
     * Ctrl-Shift-Alt-O held opens the options. */
    {
        input_key_event ev;
        while (INPUT_KeyPop(&ev)) {                                     /* modifiers alone are not queued */
            const u8 code = ev.code;
            if (code == 0x52) nav |= NAV_UP;
            else if (code == 0x51) nav |= NAV_DOWN;
            else if (code == 0x50) nav |= NAV_LEFT;
            else if (code == 0x4f) nav |= NAV_RIGHT;
            else if (code == 0x28 || code == 0x58) nav |= NAV_OK;
            else if (code == 0x29) nav |= NAV_BACK;
            if (mod_ctrl() && mod_shift() && mod_alt()) continue;       /* arming the options gesture */
            if (mod_alt() && code == 0x3d) continue;                    /* Alt-F4 */
            if (code == 0x45 || code == 0x46) { in.shot_key = true; continue; }   /* F12 / PrtSc */
            smash_push(hid_to_char(code));
            in.source = "Keyboard";
            active = true;
        }
        in.exit_now = mod_alt() && INPUT_KeyHeld(0x3d);
        bool others = false;
        for (int k = 0; k < 256; k++)
            if (INPUT_KeyHeld(k) && k != 0x12 && !(k >= 0xe0 && k <= 0xe7)) { others = true; break; }
        if (mod_ctrl() && mod_shift() && mod_alt() && INPUT_KeyHeld(0x12) && !others) opt = true;
    }

    /* cursor motion from analog sticks / mouse */
    if (best_mag > 0 && best_src) {
        const float sp = 9.0f * powf(best_mag, 1.6f) + 0.6f;
        in.x += best_x * sp;
        in.y += best_y * sp;
        in.source = best_src;
        active = true;
    }
    if (mdx != 0 || mdy != 0) {
        in.x += mdx * 1.4f;
        in.y += mdy * 1.4f;
    }
    if (in.x < 0) in.x = 0;
    if (in.y < 0) in.y = 0;
    if (in.x > SCR_W - 1) in.x = SCR_W - 1;
    if (in.y > SCR_H - 1) in.y = SCR_H - 1;
    in.moved = in.x != old_x || in.y != old_y;

    /* every non-click button press is a smash */
    {
        const u32 down = held & ~prev_held;
        static const u32 smash_bits[] = { IN_B, IN_1, IN_2, IN_PLUS, IN_MINUS, IN_UP, IN_DOWN,
                                          IN_LEFT, IN_RIGHT };
        for (unsigned i = 0; i < sizeof smash_bits / sizeof smash_bits[0]; i++)
            if (down & smash_bits[i]) smash_push(rand_char());
        if (down & (IN_MCLICK | IN_RCLICK)) smash_push(0);              /* mouse: a shape */
        if (down & IN_UP) nav |= NAV_UP;
        if (down & IN_DOWN) nav |= NAV_DOWN;
        if (down & IN_LEFT) nav |= NAV_LEFT;
        if (down & IN_RIGHT) nav |= NAV_RIGHT;
        if (down & IN_A) nav |= NAV_OK;
        if (down & (IN_B | IN_HOME)) nav |= NAV_BACK;
        if ((held & (IN_PLUS | IN_MINUS)) == (IN_PLUS | IN_MINUS)) opt = true;
        in.nav = nav;
        in.opt_held = opt;
        in.shot_held = (held & (IN_1 | IN_2)) == (IN_1 | IN_2);
        in.click_down = (down & IN_A) != 0;
        in.click_held = (held & IN_A) != 0;
        in.click_up = (~held & prev_held & IN_A) != 0;
        in.exit_held = (held & IN_HOME) != 0;
        if (down & IN_A) active = true;
        prev_held = held;
    }

    if (active) idle_frames = 0; else if (idle_frames < 9999) idle_frames++;
    in.cursor_on = idle_frames < 360;
}
