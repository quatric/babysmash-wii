/*
 * Unified input: Wiimote (IR pointer, Nunchuk), Classic Controller, GameCube pad,
 * Wii U Pro Controller (libwupc), Wii U GamePad (libwiidrc), DualShock 3
 * (libsicksaxis), USB mouse and USB keyboard (same set as GlowWii plus mouse).
 *
 * Everything is reduced to:
 *   - a pointer + click (Wiimote IR / sticks / mouse, A or left button)
 *   - "smash" events: every other button press, mouse button or key press
 *   - exit (HOME / Start held, or Alt-F4) and the options gesture
 */
#include <stdio.h>
#include <unistd.h>
#include <ogcsys.h>
#include <wiiuse/wpad.h>
#include <ogc/usb.h>
#include <ogc/usbmouse.h>
#include <wiikeyboard/usbkeyboard.h>
#ifdef HAVE_WIIDRC
#include <wiidrc/wiidrc.h>
#endif
#ifdef HAVE_WUPC
#include <wupc/wupc.h>
#endif
#ifdef HAVE_SICKSAXIS
#include <sicksaxis.h>
#endif
#include "bs.h"

Input in;

static u32 prev_held;
static float best_mag;
static float best_x, best_y;
static const char *best_src;
static int idle_frames = 9999;

#ifdef HAVE_SICKSAXIS
static struct ss_device ds3;
static int ds3_initialized;
#endif

/* ---- keyboard (polled on a thread, like GlowWii) ------------------------ */
static lwp_t kbd_thread_hndl = LWP_THREAD_NULL;
static volatile bool kbd_thread_run;
static volatile u8 key_held[256];
static volatile u8 key_queue[64];
static volatile int kq_head, kq_tail;

static void kq_push(u8 code)
{
    const int n = (kq_head + 1) & 63;
    if (n != kq_tail) { key_queue[kq_head] = code; kq_head = n; }
}

static void kb_event(USBKeyboard_event ev)
{
    if (ev.type == USBKEYBOARD_PRESSED) {
        key_held[ev.keyCode] = 1;
        kq_push(ev.keyCode);
    } else if (ev.type == USBKEYBOARD_RELEASED) {
        key_held[ev.keyCode] = 0;
    }
}

static void *kbd_thread(void *arg)
{
    (void)arg;
    while (kbd_thread_run) {
        if (!USBKeyboard_IsConnected()) USBKeyboard_Open(kb_event);
        USBKeyboard_Scan();
        usleep(2000);
    }
    return NULL;
}

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

static u32 map_classic(u32 b)
{
    u32 out = 0;
    if (b & (WPAD_CLASSIC_BUTTON_ZR | WPAD_CLASSIC_BUTTON_PLUS | WPAD_CLASSIC_BUTTON_FULL_R)) out |= IN_PLUS;
    if (b & (WPAD_CLASSIC_BUTTON_ZL | WPAD_CLASSIC_BUTTON_MINUS | WPAD_CLASSIC_BUTTON_FULL_L)) out |= IN_MINUS;
    if (b & WPAD_CLASSIC_BUTTON_A) out |= IN_A;
    if (b & WPAD_CLASSIC_BUTTON_B) out |= IN_B;
    if (b & WPAD_CLASSIC_BUTTON_X) out |= IN_2;
    if (b & WPAD_CLASSIC_BUTTON_Y) out |= IN_1;
    if (b & WPAD_CLASSIC_BUTTON_HOME) out |= IN_HOME;
    if (b & WPAD_CLASSIC_BUTTON_UP) out |= IN_UP;
    if (b & WPAD_CLASSIC_BUTTON_DOWN) out |= IN_DOWN;
    if (b & WPAD_CLASSIC_BUTTON_LEFT) out |= IN_LEFT;
    if (b & WPAD_CLASSIC_BUTTON_RIGHT) out |= IN_RIGHT;
    return out;
}

static u32 map_wiimote(u32 b)
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
    if (b & WPAD_NUNCHUK_BUTTON_Z) out |= IN_B;
    if (b & WPAD_NUNCHUK_BUTTON_C) out |= IN_1;
    return out;
}

#ifdef HAVE_WIIDRC
static u32 map_drc(u32 b)
{
    u32 out = 0;
    if (b & (WIIDRC_BUTTON_R | WIIDRC_BUTTON_ZR | WIIDRC_BUTTON_PLUS)) out |= IN_PLUS;
    if (b & (WIIDRC_BUTTON_L | WIIDRC_BUTTON_ZL | WIIDRC_BUTTON_MINUS)) out |= IN_MINUS;
    if (b & WIIDRC_BUTTON_A) out |= IN_A;
    if (b & WIIDRC_BUTTON_B) out |= IN_B;
    if (b & WIIDRC_BUTTON_X) out |= IN_2;
    if (b & WIIDRC_BUTTON_Y) out |= IN_1;
    if (b & WIIDRC_BUTTON_HOME) out |= IN_HOME;
    if (b & WIIDRC_BUTTON_UP) out |= IN_UP;
    if (b & WIIDRC_BUTTON_DOWN) out |= IN_DOWN;
    if (b & WIIDRC_BUTTON_LEFT) out |= IN_LEFT;
    if (b & WIIDRC_BUTTON_RIGHT) out |= IN_RIGHT;
    return out;
}
#endif

static void joy(const char *src, joystick_t *j)
{
    if (j->mag < 0.01f) return;
    const float a = j->ang * 0.017453292f;
    stick(src, sinf(a) * j->mag, -cosf(a) * j->mag);
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

static bool mod_ctrl(void)  { return key_held[0xe0] || key_held[0xe4]; }
static bool mod_shift(void) { return key_held[0xe1] || key_held[0xe5]; }
static bool mod_alt(void)   { return key_held[0xe2] || key_held[0xe6]; }

/* ---- public ------------------------------------------------------------- */
void input_init(void)
{
    PAD_Init();
#ifdef HAVE_WUPC
    WUPC_Init();
#endif
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_ALL, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_ALL, SCR_W, SCR_H);
#ifdef HAVE_WIIDRC
    WiiDRC_Init();
#endif
    USB_Initialize();
#ifdef HAVE_SICKSAXIS
    if (ss_init() >= 0 && ss_initialize(&ds3) >= 0) {
        ds3_initialized = 1;
        if (ss_open(&ds3) > 0) ss_start_reading(&ds3);
    }
#endif
    MOUSE_Init();
    USBKeyboard_Initialize();
    kbd_thread_run = true;
    LWP_CreateThread(&kbd_thread_hndl, kbd_thread, 0, 0, 0x4000, 0x7f);

    in.x = SCR_W / 2;
    in.y = SCR_H / 2;
    in.source = "";
}

void input_shutdown(void)
{
    kbd_thread_run = false;
    usleep(5000);
    USBKeyboard_Close();
    USBKeyboard_Deinitialize();
    if (kbd_thread_hndl != LWP_THREAD_NULL) LWP_JoinThread(kbd_thread_hndl, 0);
    kbd_thread_hndl = LWP_THREAD_NULL;
    MOUSE_Deinit();
#ifdef HAVE_SICKSAXIS
    if (ds3_initialized) {
        if (ss_is_connected(&ds3)) ss_stop_reading(&ds3);
        ss_close(&ds3);
    }
#endif
#ifdef HAVE_WUPC
    WUPC_Shutdown();
#endif
}

void input_update(void)
{
    static int frame;
    static u8 mbtn;
    u32 held = 0;
    float mdx = 0, mdy = 0;
    const float old_x = in.x, old_y = in.y;
    bool active = false;
    u32 nav = 0;
    bool opt = false;
    in.shot_key = false;
    best_mag = 0;
    best_src = NULL;
    in.wheel = 0;
    in.nsmash = 0;
    frame++;

    /* Wii U Pro Controller (emulated classic) */
#ifdef HAVE_WUPC
    WUPC_UpdateButtonStats();
    for (int ch = 0; ch < 4; ch++) {
        const u32 b = WUPC_ButtonsHeld(ch);
        if (b) { held |= map_classic(b); in.source = "Wii U Pro"; }
        stick("Wii U Pro", WUPC_lStickX(ch) / 900.0f, -WUPC_lStickY(ch) / 900.0f);
        stick("Wii U Pro", WUPC_rStickX(ch) / 900.0f, -WUPC_rStickY(ch) / 900.0f);
    }
#endif

    /* Wiimotes (+ Nunchuk / Classic) */
    WPAD_ScanPads();
    for (int ch = 0; ch < 4; ch++) {
        u32 type;
        if (WPAD_Probe(ch, &type) != WPAD_ERR_NONE) continue;
        const u32 b = WPAD_ButtonsHeld(ch);
        WPADData *d = WPAD_Data(ch);
        held |= map_wiimote(b) | map_classic(b);
        if (b) in.source = d->exp.type == WPAD_EXP_CLASSIC ? "Classic" : "Wiimote";
        if (d->ir.valid) {
            in.x = d->ir.x;
            in.y = d->ir.y;
            in.source = "Wiimote";
            active = true;
        }
        if (d->exp.type == WPAD_EXP_NUNCHUK) joy("Nunchuk", &d->exp.nunchuk.js);
        else if (d->exp.type == WPAD_EXP_CLASSIC) {
            joy("Classic", &d->exp.classic.ljs);
            joy("Classic", &d->exp.classic.rjs);
        }
    }

    /* Wii U GamePad */
#ifdef HAVE_WIIDRC
    if (WiiDRC_Inited() && WiiDRC_Connected() && WiiDRC_ScanPads()) {
        const u32 b = WiiDRC_ButtonsHeld();
        if (b) { held |= map_drc(b); in.source = "GamePad"; }
        stick("GamePad", WiiDRC_lStickX() / 900.0f, -WiiDRC_lStickY() / 900.0f);
        stick("GamePad", WiiDRC_rStickX() / 900.0f, -WiiDRC_rStickY() / 900.0f);
    }
#endif

    /* DualShock 3 */
#ifdef HAVE_SICKSAXIS
    if (ds3_initialized) {
        if (!ss_is_connected(&ds3)) {
            if ((frame % 120) == 0 && ss_open(&ds3) > 0) ss_start_reading(&ds3);
        } else {
            const struct SS_BUTTONS *b = &ds3.pad.buttons;
            u32 h = 0;
            if (b->start || b->R1 || b->R2) h |= IN_PLUS;
            if (b->select || b->L1 || b->L2) h |= IN_MINUS;
            if (b->cross) h |= IN_A;
            if (b->circle) h |= IN_B;
            if (b->triangle) h |= IN_2;
            if (b->square) h |= IN_1;
            if (b->PS) h |= IN_HOME;
            if (b->up) h |= IN_UP;
            if (b->down) h |= IN_DOWN;
            if (b->left) h |= IN_LEFT;
            if (b->right) h |= IN_RIGHT;
            if (h) { held |= h; in.source = "DualShock 3"; }
            stick("DualShock 3", (ds3.pad.left_analog.x - 128) / 110.0f, (ds3.pad.left_analog.y - 128) / 110.0f);
            stick("DualShock 3", (ds3.pad.right_analog.x - 128) / 110.0f, (ds3.pad.right_analog.y - 128) / 110.0f);
        }
    }
#endif

    /* GameCube pads */
    PAD_ScanPads();
    for (int ch = 0; ch < 4; ch++) {
        const u16 b = PAD_ButtonsHeld(ch);
        u32 h = 0;
        if (b & (PAD_TRIGGER_R | PAD_TRIGGER_Z)) h |= IN_PLUS;
        if (b & PAD_TRIGGER_L) h |= IN_MINUS;
        if (b & PAD_BUTTON_A) h |= IN_A;
        if (b & PAD_BUTTON_B) h |= IN_B;
        if (b & PAD_BUTTON_X) h |= IN_2;
        if (b & PAD_BUTTON_Y) h |= IN_1;
        if (b & PAD_BUTTON_START) h |= IN_HOME;
        if (b & PAD_BUTTON_UP) h |= IN_UP;
        if (b & PAD_BUTTON_DOWN) h |= IN_DOWN;
        if (b & PAD_BUTTON_LEFT) h |= IN_LEFT;
        if (b & PAD_BUTTON_RIGHT) h |= IN_RIGHT;
        if (h) { held |= h; in.source = "GameCube"; }
        stick("GameCube", PAD_StickX(ch) / 70.0f, -PAD_StickY(ch) / 70.0f);
        stick("GameCube", PAD_SubStickX(ch) / 60.0f, -PAD_SubStickY(ch) / 60.0f);
    }

    /* USB mouse: left = click, right / middle = smash, wheel = zoom sounds */
    if (!MOUSE_IsConnected() && (frame % 180) == 0) MOUSE_Init();
    if (MOUSE_IsConnected()) {
        mouse_event me;
        int guard = 0;
        while (guard++ < 64 && MOUSE_GetEvent(&me) > 0) {
            mdx += me.rx;
            mdy += me.ry;
            if (me.rz > 0) in.wheel++;
            if (me.rz < 0) in.wheel--;
            mbtn = me.button;
        }
        if (mbtn & 1) held |= IN_A;
        if (mbtn & 2) held |= IN_RCLICK;
        if (mbtn & 4) held |= IN_MCLICK;
        if ((mbtn & 7) == 7) opt = true;
        if (mdx != 0 || mdy != 0) { in.source = "Mouse"; active = true; }
    } else {
        mbtn = 0;
    }

    /* USB keyboard: letters/digits keep their glyph, every other key is a shape (as in the
     * original, where only letters and digits are drawn as letters). Alt-F4 quits and
     * Ctrl-Shift-Alt-O held opens the options. */
    {
        while (kq_tail != kq_head) {
            const u8 code = key_queue[kq_tail];
            kq_tail = (kq_tail + 1) & 63;
            if (code >= 0xe0 && code <= 0xe7) continue;                 /* modifiers alone */
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
        in.exit_now = mod_alt() && key_held[0x3d];
        bool others = false;
        for (int k = 0; k < 256; k++)
            if (key_held[k] && k != 0x12 && !(k >= 0xe0 && k <= 0xe7)) { others = true; break; }
        if (mod_ctrl() && mod_shift() && mod_alt() && key_held[0x12] && !others) opt = true;
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
