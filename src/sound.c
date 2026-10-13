/*
 * Sounds from the original BabySmash (embedded, 16-bit mono 22.05 kHz) played through ASND.
 * Spoken letters/digits/colours/shapes come from a pack linked into the DOL (data/speech.bin,
 * built by scripts/gen_speech.sh) or loose <app dir>/speech/<name>.bin files; spoken words
 * come from <app dir>/speech/words/<word>.bin. Clips are 16-bit big-endian mono 22.05 kHz.
 * Speech is queued (at most 5 pending, oldest dropped) like the original's SpeechQueue.
 */
#include <stdio.h>
#include <malloc.h>
#include <ctype.h>
#include <asndlib.h>
#include "bs.h"
#include "EditedJackPlaysBabySmash_bin.h"
#include "babygigl2_bin.h"
#include "babylaugh_bin.h"
#include "ccgiggle_bin.h"
#include "falling_bin.h"
#include "giggle_bin.h"
#include "laughingmice_bin.h"
#include "rising_bin.h"
#include "scooby2_bin.h"
#include "smallbumblebee_bin.h"
#include "speech_bin.h"

#define RATE 22050
#define V_BEE 0
#define V_SPEECH 1
#define V_FX_FIRST 2
#define V_FX_COUNT 12

typedef struct { const u8 *src; u32 size; s16 *buf; u32 bytes; } Snd;

static Snd snds[] = {
    { EditedJackPlaysBabySmash_bin, 0 }, { rising_bin, 0 }, { falling_bin, 0 }, { smallbumblebee_bin, 0 },
    { giggle_bin, 0 }, { babylaugh_bin, 0 }, { babygigl2_bin, 0 },
    { ccgiggle_bin, 0 }, { laughingmice_bin, 0 }, { scooby2_bin, 0 },
};
#define NSND ((int)(sizeof snds / sizeof snds[0]))

static bool ready;
static int qn;                                     /* pending speech (see below) */
static int next_voice;

static void load(Snd *s)
{
    if (s->buf) return;
    s->bytes = (s->size + 31) & ~31u;
    s->buf = (s16 *)memalign(32, s->bytes);
    if (!s->buf) return;
    memset(s->buf, 0, s->bytes);
    for (u32 i = 0; i + 1 < s->size; i += 2)
        s->buf[i / 2] = (s16)((s->src[i] << 8) | s->src[i + 1]);
    DCFlushRange(s->buf, s->bytes);
}

void snd_init(void)
{
    snds[0].size = EditedJackPlaysBabySmash_bin_size;
    snds[1].size = rising_bin_size;
    snds[2].size = falling_bin_size;
    snds[3].size = smallbumblebee_bin_size;
    snds[4].size = giggle_bin_size;
    snds[5].size = babylaugh_bin_size;
    snds[6].size = babygigl2_bin_size;
    snds[7].size = ccgiggle_bin_size;
    snds[8].size = laughingmice_bin_size;
    snds[9].size = scooby2_bin_size;
    ASND_Init();
    ASND_Pause(0);
    ready = true;
}

static void play_on(int voice, Snd *s, bool loop)
{
    load(s);
    if (!s->buf) return;
    ASND_StopVoice(voice);
    ASND_SetVoice(voice, VOICE_MONO_16BIT, RATE, 0, s->buf, s->bytes, 255, 255, NULL);
    (void)loop;
}

void snd_play(int id)
{
    if (!ready || id < 0 || id >= NSND) return;
    const int v = V_FX_FIRST + (next_voice++ % V_FX_COUNT);
    play_on(v, &snds[id], false);
}

void snd_laugh(void)
{
    snd_play(SND_LAUGH_FIRST + rand() % SND_LAUGH_COUNT);
}

void snd_bee(bool on)
{
    if (!ready) return;
    if (on) play_on(V_BEE, &snds[SND_BEE], false);
    else ASND_StopVoice(V_BEE);
}

void snd_bee_yield(void)
{
    if (!ready || ASND_StatusVoice(V_BEE) == SND_WORKING) return;
    play_on(V_BEE, &snds[SND_BEE], false);
}

void snd_stop_all(void)
{
    if (!ready) return;
    qn = 0;
    for (int i = 0; i < 16; i++) ASND_StopVoice(i);
}

/* ---- spoken clips ----------------------------------------------------------------- */
#define MAX_CLIPS 80
#define WORD_SLOT MAX_CLIPS
#define QUEUE_CAP 5
static struct { char name[24]; s16 *buf; u32 bytes; u32 len; } clips[MAX_CLIPS + 1];
static int nclips;
static const char *speech_dirs[] = { "sd:/apps/babysmash/speech", "usb:/apps/babysmash/speech" };

static u32 rd32be(const u8 *p) { return ((u32)p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3]; }

static bool set_clip(int i, const u8 *data, u32 len)
{
    len &= ~1u;
    if (!len) return false;
    free(clips[i].buf);
    clips[i].bytes = (len + 31) & ~31u;
    clips[i].buf = (s16 *)memalign(32, clips[i].bytes);
    if (!clips[i].buf) return false;
    memset(clips[i].buf, 0, clips[i].bytes);
    memcpy(clips[i].buf, data, len);                    /* already big-endian = native */
    DCFlushRange(clips[i].buf, clips[i].bytes);
    clips[i].len = len;
    return true;
}

static bool pack_find(const char *key, const u8 **data, u32 *len)
{
    const u8 *p = speech_bin;
    if (speech_bin_size < 4) return false;
    const u32 n = rd32be(p);
    for (u32 i = 0; i < n && 4 + (i + 1) * 24 <= speech_bin_size; i++) {
        const u8 *e = p + 4 + i * 24;
        if (strncmp((const char *)e, key, 16)) continue;
        const u32 off = rd32be(e + 16), l = rd32be(e + 20);
        if (off + l > speech_bin_size) return false;
        *data = p + off;
        *len = l;
        return true;
    }
    return false;
}

static bool file_clip(int slot, const char *dirsuffix, const char *key)
{
    for (unsigned d = 0; d < sizeof speech_dirs / sizeof speech_dirs[0]; d++) {
        char path[160];
        snprintf(path, sizeof path, "%s%s/%s.bin", speech_dirs[d], dirsuffix, key);
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        fseek(f, 0, SEEK_END);
        const long sz = ftell(f);
        fseek(f, 0, SEEK_SET);
        u8 *raw = sz > 0 ? (u8 *)malloc(sz) : NULL;
        bool ok = raw && fread(raw, 1, sz, f) == (size_t)sz && set_clip(slot, raw, sz);
        free(raw);
        fclose(f);
        if (ok) return true;
    }
    return false;
}

static void lower_key(char *key, const char *name)
{
    int i;
    for (i = 0; name[i] && i < 23; i++) key[i] = tolower((unsigned char)name[i]);
    key[i] = 0;
}

static int clip_get(const char *name)
{
    char key[24];
    lower_key(key, name);
    for (int i = 0; i < nclips; i++)
        if (!strcmp(clips[i].name, key)) return clips[i].buf ? i : -1;
    if (nclips >= MAX_CLIPS) return -1;
    const int i = nclips++;
    strcpy(clips[i].name, key);
    clips[i].buf = NULL;
    const u8 *data;
    u32 len;
    if (pack_find(key, &data, &len)) set_clip(i, data, len);
    else file_clip(i, "", key);
    return clips[i].buf ? i : -1;
}

typedef struct { int a, b; } Utter;
static Utter queue[QUEUE_CAP];

static void queue_push(int a, int b, bool priority)
{
    if (priority) { qn = 0; ASND_StopVoice(V_SPEECH); }
    if (qn >= QUEUE_CAP) {                              /* drop the oldest */
        memmove(&queue[0], &queue[1], sizeof(Utter) * (QUEUE_CAP - 1));
        qn--;
    }
    queue[qn].a = a;
    queue[qn].b = b;
    qn++;
}

bool snd_speak(const char *a, const char *b, bool priority)
{
    if (!ready) return false;
    const int ia = clip_get(a);
    const int ib = b ? clip_get(b) : -1;
    if (ia < 0 || (b && ib < 0)) return false;
    queue_push(ia, ib, priority);
    return true;
}

bool snd_speak_word(const char *word)
{
    if (!ready) return false;
    char key[24];
    lower_key(key, word);
    if (!file_clip(WORD_SLOT, "/words", key)) return false;
    queue_push(WORD_SLOT, -1, true);
    return true;
}

static void play_utterance(const Utter *u)
{
    static s16 *mix;
    static u32 mix_cap;
    const u32 la = clips[u->a].len, lb = u->b >= 0 ? clips[u->b].len : 0;
    const u32 bytes = (la + lb + 31) & ~31u;
    if (bytes > mix_cap) {
        free(mix);
        mix = (s16 *)memalign(32, bytes);
        mix_cap = mix ? bytes : 0;
    }
    if (!mix) return;
    memset(mix, 0, bytes);
    memcpy(mix, clips[u->a].buf, la);
    if (u->b >= 0) memcpy((u8 *)mix + la, clips[u->b].buf, lb);
    DCFlushRange(mix, bytes);
    ASND_StopVoice(V_SPEECH);
    ASND_SetVoice(V_SPEECH, VOICE_MONO_16BIT, RATE, 0, mix, bytes, 255, 255, NULL);
}

void snd_update(void)
{
    if (!ready || !qn || ASND_StatusVoice(V_SPEECH) == SND_WORKING) return;
    const Utter u = queue[0];
    memmove(&queue[0], &queue[1], sizeof(Utter) * (QUEUE_CAP - 1));
    qn--;
    play_utterance(&u);
}
