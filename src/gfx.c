/*
 * Vector rendering through GX triangles: the nine BabySmash shapes (radial gradient,
 * black outline, blinking face), stroke-font letters/digits, trail discs and the cursor.
 * Everything is submitted as triangle lists through GRRLIB_GXEngine in 640x480 screen space.
 */
#include <stdio.h>
#include <ctype.h>
#include "bs.h"

const char *shape_names[SH_COUNT] = {
    "Circle", "Oval", "Rectangle", "Hexagon", "Trapezoid", "Star", "Square", "Triangle", "Heart"
};

/* System.Windows.Media.Colors values used by the original */
const BsColor bs_colors[BS_COLORS] = {
    { 255, 0, 0, "Red" }, { 0, 0, 255, "Blue" }, { 255, 255, 0, "Yellow" },
    { 0, 128, 0, "Green" }, { 128, 0, 128, "Purple" }, { 255, 192, 203, "Pink" },
    { 255, 165, 0, "Orange" }, { 210, 180, 140, "Tan" }, { 128, 128, 128, "Gray" },
};

/* ---- triangle batch ------------------------------------------------------ */
#define MAXV 3072
static guVector V[MAXV];
static u32 C[MAXV];
static int nv;

static void b_flush(void)
{
    if (nv) GRRLIB_GXEngine(V, C, nv, GX_TRIANGLES);
    nv = 0;
}

static void b_vert(float x, float y, u32 c)
{
    V[nv].x = x; V[nv].y = y; V[nv].z = 0;
    C[nv++] = c;
}

static void b_tri(float x0, float y0, u32 c0, float x1, float y1, u32 c1,
                  float x2, float y2, u32 c2)
{
    if (nv + 3 > MAXV) b_flush();
    b_vert(x0, y0, c0); b_vert(x1, y1, c1); b_vert(x2, y2, c2);
}

static void b_seg(float x0, float y0, float x1, float y1, float w, u32 c)
{
    const float dx = x1 - x0, dy = y1 - y0;
    const float l = sqrtf(dx * dx + dy * dy);
    if (l < 0.001f) return;
    const float nx = -dy / l * w * 0.5f, ny = dx / l * w * 0.5f;
    b_tri(x0 + nx, y0 + ny, c, x0 - nx, y0 - ny, c, x1 + nx, y1 + ny, c);
    b_tri(x1 + nx, y1 + ny, c, x0 - nx, y0 - ny, c, x1 - nx, y1 - ny, c);
}

static void b_disc(float x, float y, float r, u32 c)
{
    float px = x + r, py = y;
    for (int i = 1; i <= 8; i++) {
        const float a = i * (6.2831853f / 8);
        const float nx = x + cosf(a) * r, ny = y + sinf(a) * r;
        b_tri(x, y, c, px, py, c, nx, ny, c);
        px = nx; py = ny;
    }
}

static int clamp255(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* Fading blends towards the WhiteSmoke background (opaque) instead of using alpha, so the
 * overlapping layers of one figure never show through each other. */
#define BG 245
static u32 faded(int r, int g, int b, float alpha)
{
    if (alpha > 1) alpha = 1;
    if (alpha < 0) alpha = 0;
    return RGBA(clamp255((int)(BG + (r - BG) * alpha)), clamp255((int)(BG + (g - BG) * alpha)),
                clamp255((int)(BG + (b - BG) * alpha)), 255);
}

static u32 shade(int col, int delta, float alpha)
{
    return faded(clamp255(bs_colors[col].r + delta), clamp255(bs_colors[col].g + delta),
                 clamp255(bs_colors[col].b + delta), alpha);
}

static u32 black(float alpha)
{
    return faded(0, 0, 0, alpha);
}

/* ---- shape outlines in a -1..1 box ---------------------------------------- */
#define MAXP 64
static float shp[SH_COUNT][MAXP][2];
static int shn[SH_COUNT];

static void shp_add(int t, float x, float y)
{
    shp[t][shn[t]][0] = x;
    shp[t][shn[t]][1] = y;
    shn[t]++;
}

static void build_shapes(void)
{
    for (int i = 0; i < 36; i++) {
        const float a = i * (6.2831853f / 36);
        shp_add(SH_CIRCLE, cosf(a), sinf(a));
        shp_add(SH_OVAL, cosf(a), sinf(a) * 0.66f);
    }
    shp_add(SH_RECT, -1, -0.62f); shp_add(SH_RECT, 1, -0.62f);
    shp_add(SH_RECT, 1, 0.62f);   shp_add(SH_RECT, -1, 0.62f);
    shp_add(SH_SQUARE, -0.8f, -0.8f); shp_add(SH_SQUARE, 0.8f, -0.8f);
    shp_add(SH_SQUARE, 0.8f, 0.8f);   shp_add(SH_SQUARE, -0.8f, 0.8f);
    for (int i = 0; i < 6; i++) {
        const float a = i * (6.2831853f / 6);
        shp_add(SH_HEXAGON, cosf(a), sinf(a));
    }
    shp_add(SH_TRAPEZOID, -0.6f, -0.7f); shp_add(SH_TRAPEZOID, 0.6f, -0.7f);
    shp_add(SH_TRAPEZOID, 1, 0.7f);      shp_add(SH_TRAPEZOID, -1, 0.7f);
    for (int i = 0; i < 10; i++) {
        const float a = -1.5707963f + i * (6.2831853f / 10);
        const float r = (i & 1) ? 0.42f : 1.0f;
        shp_add(SH_STAR, cosf(a) * r, sinf(a) * r);
    }
    shp_add(SH_TRIANGLE, 0, -1); shp_add(SH_TRIANGLE, 1, 0.75f); shp_add(SH_TRIANGLE, -1, 0.75f);

    /* heart: classic parametric curve, recentred on its bounding box */
    float hx[48], hy[48], minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
    for (int i = 0; i < 48; i++) {
        const float t = i * (6.2831853f / 48);
        const float s = sinf(t);
        hx[i] = 16 * s * s * s;
        hy[i] = -(13 * cosf(t) - 5 * cosf(2 * t) - 2 * cosf(3 * t) - cosf(4 * t));
        if (hx[i] < minx) minx = hx[i];
        if (hx[i] > maxx) maxx = hx[i];
        if (hy[i] < miny) miny = hy[i];
        if (hy[i] > maxy) maxy = hy[i];
    }
    const float cx = (minx + maxx) / 2, cy = (miny + maxy) / 2;
    const float sc = 1.0f / fmaxf(maxx - minx, maxy - miny) * 2.0f;
    for (int i = 0; i < 48; i++) shp_add(SH_HEART, (hx[i] - cx) * sc, (hy[i] - cy) * sc);
}

/* ---- stroke font -------------------------------------------------------------- */
/* 4 x 6 grid, y down; strokes separated by ';' */
static const char *glyph_src[36] = {
    /* A */ "0,6 2,0 4,6;0.8,4 3.2,4",
    /* B */ "0,0 0,6;0,0 2.6,0 3.6,0.8 3.6,2.2 2.6,3 0,3;2.6,3 3.8,3.8 3.8,5.2 2.8,6 0,6",
    /* C */ "3.8,1 3,0 1,0 0,1 0,5 1,6 3,6 3.8,5",
    /* D */ "0,0 0,6;0,0 2.4,0 3.8,1.2 3.8,4.8 2.4,6 0,6",
    /* E */ "3.8,0 0,0 0,6 3.8,6;0,3 3,3",
    /* F */ "3.8,0 0,0 0,6;0,3 3,3",
    /* G */ "3.8,1 3,0 1,0 0,1 0,5 1,6 3,6 3.8,5 3.8,3.2 2.2,3.2",
    /* H */ "0,0 0,6;4,0 4,6;0,3 4,3",
    /* I */ "0.5,0 3.5,0;2,0 2,6;0.5,6 3.5,6",
    /* J */ "3.6,0 3.6,4.8 2.8,6 1,6 0,5",
    /* K */ "0,0 0,6;3.8,0 0,3.6;1.2,2.5 4,6",
    /* L */ "0,0 0,6 3.8,6",
    /* M */ "0,6 0,0 2,3.4 4,0 4,6",
    /* N */ "0,6 0,0 4,6 4,0",
    /* O */ "1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,1 1,0",
    /* P */ "0,6 0,0 2.6,0 3.8,0.8 3.8,2.4 2.6,3.2 0,3.2",
    /* Q */ "1,0 3,0 4,1 4,5 3,6 1,6 0,5 0,1 1,0;2.4,4.2 4,6.4",
    /* R */ "0,6 0,0 2.6,0 3.8,0.8 3.8,2.4 2.6,3.2 0,3.2;1.8,3.2 4,6",
    /* S */ "3.8,1 3,0 1,0 0,1 0,2.2 1,3 3,3 4,3.8 4,5 3,6 1,6 0,5",
    /* T */ "0,0 4,0;2,0 2,6",
    /* U */ "0,0 0,5 1,6 3,6 4,5 4,0",
    /* V */ "0,0 2,6 4,0",
    /* W */ "0,0 1,6 2,2.6 3,6 4,0",
    /* X */ "0,0 4,6;4,0 0,6",
    /* Y */ "0,0 2,3 4,0;2,3 2,6",
    /* Z */ "0,0 4,0 0,6 4,6",
    /* 0 */ "1.2,0 2.8,0 3.8,1 3.8,5 2.8,6 1.2,6 0.2,5 0.2,1 1.2,0",
    /* 1 */ "0.8,1.2 2.2,0 2.2,6;0.8,6 3.6,6",
    /* 2 */ "0.2,1.2 1,0 3,0 3.8,1 3.8,2.2 0,6 4,6",
    /* 3 */ "0.2,0.8 1,0 3,0 3.8,1 3.8,2.2 3,3 1.6,3;3,3 3.8,3.8 3.8,5 3,6 1,6 0.2,5.2",
    /* 4 */ "3,6 3,0 0,4.2 4,4.2",
    /* 5 */ "3.8,0 0.6,0 0.4,2.8 2.6,2.6 3.8,3.4 3.8,5 3,6 1,6 0.2,5.2",
    /* 6 */ "3.4,0.4 2.4,0 1,0 0,1.2 0,5 1,6 3,6 4,5 4,3.6 3,2.8 1,2.8 0,3.8",
    /* 7 */ "0,0 4,0 1.6,6",
    /* 8 */ "1,3 0.2,2.2 0.2,1 1,0 3,0 3.8,1 3.8,2.2 3,3 1,3 0,3.8 0,5 1,6 3,6 4,5 4,3.8 3,3",
    /* 9 */ "0.6,5.6 1.6,6 3,6 4,4.8 4,1 3,0 1,0 0,1 0,2.4 1,3.2 3,3.2 4,2.2",
};

static const char *lower_src[26] = {
    /* a */ "3.8,3.2 3,2.4 1,2.4 0.2,3.4 0.2,4.2 1,5 3,5 3.8,4.2;3.8,2.4 3.8,5",
    /* b */ "0.4,0 0.4,5;0.4,3.4 1,2.4 3,2.4 3.8,3.4 3.8,4.2 3,5 1,5 0.4,4",
    /* c */ "3.6,3 3,2.4 1,2.4 0.2,3.2 0.2,4.2 1,5 3,5 3.6,4.4",
    /* d */ "3.8,0 3.8,5;3.8,3.4 3,2.4 1,2.4 0.2,3.4 0.2,4.2 1,5 3,5 3.8,4",
    /* e */ "0.2,3.7 3.8,3.7 3.6,2.9 3,2.4 1,2.4 0.2,3.2 0.2,4.2 1,5 3,5 3.6,4.5",
    /* f */ "3.4,0.6 2.8,0 2,0 1.4,0.6 1.4,5;0.2,2.4 3.2,2.4",
    /* g */ "3.8,2.4 3.8,5.6 3.2,6.4 1,6.4 0.4,5.8;3.8,3.4 3,2.4 1,2.4 0.2,3.4 0.2,4.2 1,5 3,5 3.8,4",
    /* h */ "0.4,0 0.4,5;0.4,3.6 1.2,2.4 2.8,2.4 3.6,3.2 3.6,5",
    /* i */ "2,2.4 2,5;2,0.8 2,1",
    /* j */ "2.6,2.4 2.6,5.6 2,6.4 1,6.4 0.4,5.8;2.6,0.8 2.6,1",
    /* k */ "0.4,0 0.4,5;3.4,2.4 0.4,4;1.4,3.5 3.6,5",
    /* l */ "2,0 2,5",
    /* m */ "0.2,5 0.2,2.4;0.2,3.4 0.8,2.4 1.6,2.4 2,3.2 2,5;2,3.2 2.6,2.4 3.4,2.4 3.9,3.2 3.9,5",
    /* n */ "0.4,5 0.4,2.4;0.4,3.4 1.2,2.4 2.8,2.4 3.6,3.2 3.6,5",
    /* o */ "1,2.4 3,2.4 3.8,3.2 3.8,4.2 3,5 1,5 0.2,4.2 0.2,3.2 1,2.4",
    /* p */ "0.4,2.4 0.4,6.4;0.4,3.4 1,2.4 3,2.4 3.8,3.4 3.8,4.2 3,5 1,5 0.4,4",
    /* q */ "3.8,2.4 3.8,6.4;3.8,3.4 3,2.4 1,2.4 0.2,3.4 0.2,4.2 1,5 3,5 3.8,4",
    /* r */ "0.6,5 0.6,2.4;0.6,3.6 1.4,2.6 2.4,2.4 3.4,2.6",
    /* s */ "3.6,3 3,2.4 1,2.4 0.4,3 0.4,3.6 1,3.8 3,3.8 3.6,4.4 3.6,4.6 3,5 1,5 0.4,4.4",
    /* t */ "1.6,0.6 1.6,4.4 2.2,5 3,5;0.4,2.4 3.2,2.4",
    /* u */ "0.4,2.4 0.4,4.2 1.2,5 2.8,5 3.6,4.2;3.6,2.4 3.6,5",
    /* v */ "0.2,2.4 2,5 3.8,2.4",
    /* w */ "0,2.4 1,5 2,3 3,5 4,2.4",
    /* x */ "0.2,2.4 3.8,5;3.8,2.4 0.2,5",
    /* y */ "0.2,2.4 2,5 3.8,2.4;2,5 1.4,6.2 0.8,6.4",
    /* z */ "0.4,2.4 3.6,2.4 0.4,5 3.6,5",
};

static const char punct_chars[] = "!-.:(),/+'?<>";
static const char *punct_src[13] = {
    "2,0 2,3.8;2,5 2,5.2",
    "0.8,3 3.2,3",
    "2,5.4 2,5.6",
    "2,2 2,2.2;2,5 2,5.2",
    "3,0 1.2,1.5 0.8,4.5 3,6",
    "1,0 2.8,1.5 3.2,4.5 1,6",
    "2.2,5 1.6,6.2",
    "3.4,0 0.6,6",
    "0.6,3 3.4,3;2,1.6 2,4.4",
    "2,0 2,1.4",
    "0.4,1 1,0 3,0 3.6,1 3.6,2 2,3.2 2,4;2,5.2 2,5.4",
    "3.4,0.8 0.8,3 3.4,5.2",
    "0.6,0.8 3.2,3 0.6,5.2",
};

typedef struct { int ns; short first[4]; short count[4]; float p[28][2]; } Glyph;
#define NGLYPH (36 + 26 + 13)
static Glyph glyphs[NGLYPH];

static void parse_glyph(Glyph *g, const char *s)
{
    int np = 0;
    g->ns = 0;
    while (*s) {
        g->first[g->ns] = np;
        while (*s && *s != ';') {
            char *e, *e2;
            const float x = strtof(s, &e);
            if (*e != ',') return;
            const float y = strtof(e + 1, &e2);
            s = e2;
            g->p[np][0] = x; g->p[np][1] = y; np++;
            while (*s == ' ') s++;
        }
        g->count[g->ns] = np - g->first[g->ns];
        g->ns++;
        if (*s == ';') s++;
    }
}

static int glyph_index(char ch)
{
    if (ch >= 'A' && ch <= 'Z') return ch - 'A';
    if (ch >= '0' && ch <= '9') return 26 + (ch - '0');
    if (ch >= 'a' && ch <= 'z') return 36 + (ch - 'a');
    const char *p = ch ? strchr(punct_chars, ch) : NULL;
    return p ? 62 + (int)(p - punct_chars) : -1;
}

/* ear clipping for the hand cursor outline */
static int hand_tri[40][3];
static int hand_ntri;
static const float HAND[][2] = {
    {6,1},{7,0},{9,0},{10,1},{10,8},{11,7},{13,7},{14,8},{14,9},{15,8},{17,8},{18,9},{18,10},
    {19,10},{21,10},{22,11},{22,18},{20,22},{11,22},{8,18},{4,13},{3,11},{4,10},{6,12},
};
#define HAND_N ((int)(sizeof HAND / sizeof HAND[0]))

static float cross3(const float *a, const float *b, const float *c)
{
    return (b[0] - a[0]) * (c[1] - b[1]) - (b[1] - a[1]) * (c[0] - b[0]);
}

static void ear_clip(void)
{
    int idx[HAND_N], m = HAND_N;
    float area = 0;
    for (int i = 0; i < HAND_N; i++) {
        const float *a = HAND[i], *b = HAND[(i + 1) % HAND_N];
        area += a[0] * b[1] - b[0] * a[1];
        idx[i] = i;
    }
    if (area < 0) for (int i = 0; i < m / 2; i++) { const int t = idx[i]; idx[i] = idx[m - 1 - i]; idx[m - 1 - i] = t; }
    int guard = 0;
    while (m > 3 && guard++ < 200) {
        bool clipped = false;
        for (int i = 0; i < m && !clipped; i++) {
            const int a = idx[(i + m - 1) % m], b = idx[i], c = idx[(i + 1) % m];
            if (cross3(HAND[a], HAND[b], HAND[c]) <= 0) continue;
            bool ok = true;
            for (int j = 0; j < m && ok; j++) {
                const int q = idx[j];
                if (q == a || q == b || q == c) continue;
                if (cross3(HAND[a], HAND[b], HAND[q]) >= 0 && cross3(HAND[b], HAND[c], HAND[q]) >= 0 &&
                    cross3(HAND[c], HAND[a], HAND[q]) >= 0) ok = false;
            }
            if (!ok) continue;
            hand_tri[hand_ntri][0] = a; hand_tri[hand_ntri][1] = b; hand_tri[hand_ntri][2] = c;
            hand_ntri++;
            for (int k = i; k < m - 1; k++) idx[k] = idx[k + 1];
            m--;
            clipped = true;
        }
        if (!clipped) break;
    }
    if (m == 3) {
        hand_tri[hand_ntri][0] = idx[0]; hand_tri[hand_ntri][1] = idx[1]; hand_tri[hand_ntri][2] = idx[2];
        hand_ntri++;
    }
}

void gfx_init(void)
{
    build_shapes();
    for (int i = 0; i < 36; i++) parse_glyph(&glyphs[i], glyph_src[i]);
    for (int i = 0; i < 26; i++) parse_glyph(&glyphs[36 + i], lower_src[i]);
    for (int i = 0; i < 13; i++) parse_glyph(&glyphs[62 + i], punct_src[i]);
    ear_clip();
    GRRLIB_Init();
    GRRLIB_SetAntiAliasing(true);
    GRRLIB_SetBlend(GRRLIB_BLEND_ALPHA);
}

/* strokes of one glyph, appended to the batch. (ox,oy) = glyph centre, k = pixels per grid unit */
static void stroke_glyph(const Glyph *g, float ox, float oy, float k, float rot,
                         float width, float jx, float jy, u32 c)
{
    const float cr = cosf(rot), sr = sinf(rot);
    for (int s = 0; s < g->ns; s++) {
        float px = 0, py = 0;
        for (int i = 0; i < g->count[s]; i++) {
            const float gx = (g->p[g->first[s] + i][0] - 2.0f + jx) * k;
            const float gy = (g->p[g->first[s] + i][1] - 3.0f + jy) * k;
            const float x = ox + gx * cr - gy * sr, y = oy + gx * sr + gy * cr;
            if (i > 0) b_seg(px, py, x, y, width * k, c);
            b_disc(x, y, width * k * 0.5f, c);
            px = x; py = y;
        }
    }
}

float gfx_letter_width(float h) { return h * 0.85f; }

void gfx_letter(char ch, float cx, float cy, float h, float rot, int col, float alpha)
{
    const int gi = glyph_index(ch);
    if (gi < 0) return;
    const float k = h / 7.2f;
    stroke_glyph(&glyphs[gi], cx, cy, k, rot, 1.9f, 0, 0, black(alpha));
    b_flush();
    stroke_glyph(&glyphs[gi], cx, cy, k, rot, 1.15f, 0, 0, shade(col, -40, alpha));
    b_flush();
    stroke_glyph(&glyphs[gi], cx, cy, k, rot, 0.75f, -0.05f, -0.07f, shade(col, 0, alpha));
    b_flush();
    stroke_glyph(&glyphs[gi], cx, cy, k, rot, 0.3f, -0.12f, -0.16f, shade(col, 70, alpha));
    b_flush();
}

float gfx_text_width(float h, const char *s) { return strlen(s) * h * 0.78f; }

void gfx_text(float cx, float y, float h, const char *s, u32 rgba)
{
    const float adv = h * 0.78f;
    const int n = strlen(s);
    const float x = cx - n * adv * 0.5f + adv * 0.5f;
    const float k = h / 7.0f;
    for (int pass = 0; pass < 2; pass++) {
        const u32 c = pass == 0 ? RGBA(0, 0, 0, rgba & 255) : rgba;
        for (int i = 0; i < n; i++) {
            const int gi = glyph_index(s[i]);
            if (gi >= 0)
                stroke_glyph(&glyphs[gi], x + i * adv, y + h * 0.5f, k, 0,
                             pass == 0 ? 1.1f : 0.55f, 0, 0, c);
        }
        b_flush();
    }
}

void gfx_panel(float x, float y, float w, float h, u32 fill, u32 border)
{
    GRRLIB_Rectangle(x - 3, y - 3, w + 6, h + 6, border, true);
    GRRLIB_Rectangle(x, y, w, h, fill, true);
}

void gfx_bar(float x, float y, float w, float h, float frac, u32 rgba)
{
    GRRLIB_Rectangle(x - 2, y - 2, w + 4, h + 4, RGBA(0, 0, 0, rgba & 255), true);
    GRRLIB_Rectangle(x, y, w * frac, h, rgba, true);
}

/* ---- shapes ------------------------------------------------------------------- */
typedef struct { float cx, cy, s, cr, sr; } Tf;

static void tf_pt(const Tf *t, float px, float py, float *ox, float *oy)
{
    *ox = t->cx + t->s * (px * t->cr - py * t->sr);
    *oy = t->cy + t->s * (px * t->sr + py * t->cr);
}

static void ellipse_tf(const Tf *t, float x, float y, float rx, float ry, u32 c)
{
    float px, py;
    tf_pt(t, x + rx, y, &px, &py);
    float cx, cy;
    tf_pt(t, x, y, &cx, &cy);
    for (int i = 1; i <= 10; i++) {
        const float a = i * (6.2831853f / 10);
        float nx, ny;
        tf_pt(t, x + cosf(a) * rx, y + sinf(a) * ry, &nx, &ny);
        b_tri(cx, cy, c, px, py, c, nx, ny, c);
        px = nx; py = ny;
    }
}

void gfx_shape(int type, float cx, float cy, float s, float rot, int col,
               float alpha, bool face, float blink)
{
    if (type < 0 || type >= SH_COUNT || alpha <= 0 || s < 0.5f) return;
    const int n = shn[type];
    const Tf t = { cx, cy, s, cosf(rot), sinf(rot) };
    const u32 cdark = shade(col, -50, alpha), cmid = shade(col, 0, alpha), clite = shade(col, 50, alpha);
    const u32 cblk = black(alpha);
    float ox[MAXP], oy[MAXP], mx[MAXP], my[MAXP];

    /* black outline: fan of the outline pushed out radially */
    float hx, hy;
    tf_pt(&t, 0, 0, &hx, &hy);
    for (int i = 0; i < n; i++) {
        const float px = shp[type][i][0], py = shp[type][i][1];
        const float l = sqrtf(px * px + py * py);
        const float e = 1.0f + 0.07f / (l > 0.1f ? l : 0.1f);
        tf_pt(&t, px * e, py * e, &ox[i], &oy[i]);
    }
    for (int i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        b_tri(hx, hy, cblk, ox[i], oy[i], cblk, ox[j], oy[j], cblk);
    }

    /* body: dark rim -> mid ring -> lit centre */
    for (int i = 0; i < n; i++) {
        tf_pt(&t, shp[type][i][0], shp[type][i][1], &ox[i], &oy[i]);
        tf_pt(&t, shp[type][i][0] * 0.55f, shp[type][i][1] * 0.55f, &mx[i], &my[i]);
    }
    for (int i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        b_tri(ox[i], oy[i], cdark, ox[j], oy[j], cdark, mx[i], my[i], cmid);
        b_tri(ox[j], oy[j], cdark, mx[j], my[j], cmid, mx[i], my[i], cmid);
    }
    float lx, ly;
    tf_pt(&t, 0.14f, -0.14f, &lx, &ly);
    for (int i = 0; i < n; i++) {
        const int j = (i + 1) % n;
        b_tri(lx, ly, clite, mx[i], my[i], cmid, mx[j], my[j], cmid);
    }

    if (face) {
        const float eh = blink > 0.5f ? 0.02f : 0.12f;
        ellipse_tf(&t, -0.26f, -0.18f, 0.075f, eh, cblk);
        ellipse_tf(&t, 0.26f, -0.18f, 0.075f, eh, cblk);
        float px = 0, py = 0;
        for (int i = 0; i <= 8; i++) {
            const float a = 0.35f + i * ((3.14159f - 0.7f) / 8);
            float x, y;
            tf_pt(&t, cosf(a) * 0.3f, 0.02f + sinf(a) * 0.27f, &x, &y);
            if (i > 0) b_seg(px, py, x, y, fmaxf(2.0f, s * 0.07f), cblk);
            px = x; py = y;
        }
    }
    b_flush();
}

void gfx_disc(float cx, float cy, float r, int col, float alpha)
{
    const u32 cd = shade(col, -50, alpha), cm = shade(col, 0, alpha), cl = shade(col, 50, alpha);
    const int N = 16;
    float px = cx + r, py = cy, qx = cx + r * 0.5f, qy = cy;
    for (int i = 1; i <= N; i++) {
        const float a = i * (6.2831853f / N);
        const float nx = cx + cosf(a) * r, ny = cy + sinf(a) * r;
        const float mx = cx + cosf(a) * r * 0.5f, my = cy + sinf(a) * r * 0.5f;
        b_tri(px, py, cd, nx, ny, cd, qx, qy, cm);
        b_tri(nx, ny, cd, mx, my, cm, qx, qy, cm);
        b_tri(cx + r * 0.12f, cy - r * 0.12f, cl, qx, qy, cm, mx, my, cm);
        px = nx; py = ny; qx = mx; qy = my;
    }
    b_flush();
}

/* ---- visual effects (BitmapEffects): a soft halo of growing silhouettes ---------- */
void gfx_halo(int type, char ch, float cx, float cy, float s, float rot, int col,
              float radius, float alpha)
{
    if (alpha <= 0) return;
    static const float layer_alpha[3] = { 0.16f, 0.3f, 0.5f };
    for (int k = 0; k < 3; k++) {
        const float grow = radius * (3 - k) / 3.0f;
        const u32 c = faded(bs_colors[col].r, bs_colors[col].g, bs_colors[col].b, alpha * layer_alpha[k]);
        if (type == SH_LETTER) {
            const int gi = glyph_index(ch);
            if (gi < 0) continue;
            const float kk = s / 7.2f;
            stroke_glyph(&glyphs[gi], cx, cy, kk, rot, 1.9f + 2.0f * grow / kk, 0, 0, c);
            b_flush();
        } else if (type >= 0 && type < SH_COUNT) {
            const Tf t = { cx, cy, s, cosf(rot), sinf(rot) };
            const int n = shn[type];
            float ox[MAXP], oy[MAXP], hx, hy;
            tf_pt(&t, 0, 0, &hx, &hy);
            for (int i = 0; i < n; i++) {
                const float px = shp[type][i][0], py = shp[type][i][1];
                const float l = sqrtf(px * px + py * py);
                const float e = 1.0f + (0.07f + grow / s) / (l > 0.1f ? l : 0.1f);
                tf_pt(&t, px * e, py * e, &ox[i], &oy[i]);
            }
            for (int i = 0; i < n; i++) {
                const int j = (i + 1) % n;
                b_tri(hx, hy, c, ox[i], oy[i], c, ox[j], oy[j], c);
            }
            b_flush();
        }
    }
}

/* ---- cursors: FunCursor1 (arrow) and FunCursor2 (hand), cyan -> blue gradient ---------- */
void gfx_cursor(float x, float y, int kind)
{
    static const float P[7][2] = { {0, 0}, {0, 17}, {4.5f, 13}, {7.5f, 20}, {10, 19}, {7, 12}, {13, 12} };
    static const int T[5][3] = { {0, 1, 2}, {2, 3, 4}, {2, 4, 5}, {0, 2, 5}, {0, 5, 6} };
    static const float off[8][2] = { {-2,0},{2,0},{0,-2},{0,2},{-1.5f,-1.5f},{1.5f,-1.5f},{-1.5f,1.5f},{1.5f,1.5f} };
    const u32 cb = RGBA(0, 0, 0, 255), cc = RGBA(0, 255, 255, 255), cd = RGBA(0, 0, 255, 255);
    if (kind == 1) {                                           /* arrow */
        const float k = 1.9f;
        for (int o = 0; o < 8; o++)
            for (int i = 0; i < 5; i++)
                b_tri(x + off[o][0] + P[T[i][0]][0] * k, y + off[o][1] + P[T[i][0]][1] * k, cb,
                      x + off[o][0] + P[T[i][1]][0] * k, y + off[o][1] + P[T[i][1]][1] * k, cb,
                      x + off[o][0] + P[T[i][2]][0] * k, y + off[o][1] + P[T[i][2]][1] * k, cb);
        for (int i = 0; i < 5; i++)
            b_tri(x + P[T[i][0]][0] * k, y + P[T[i][0]][1] * k, T[i][0] == 0 ? cc : cd,
                  x + P[T[i][1]][0] * k, y + P[T[i][1]][1] * k, T[i][1] == 0 ? cc : cd,
                  x + P[T[i][2]][0] * k, y + P[T[i][2]][1] * k, T[i][2] == 0 ? cc : cd);
    } else {                                                   /* hand, fingertip at the pointer */
        const float k = 1.45f, hx = x - 8 * k, hy = y;
        for (int o = 0; o < 8; o++)
            for (int i = 0; i < hand_ntri; i++)
                b_tri(hx + off[o][0] + HAND[hand_tri[i][0]][0] * k, hy + off[o][1] + HAND[hand_tri[i][0]][1] * k, cb,
                      hx + off[o][0] + HAND[hand_tri[i][1]][0] * k, hy + off[o][1] + HAND[hand_tri[i][1]][1] * k, cb,
                      hx + off[o][0] + HAND[hand_tri[i][2]][0] * k, hy + off[o][1] + HAND[hand_tri[i][2]][1] * k, cb);
        for (int i = 0; i < hand_ntri; i++) {
            float px[3], py[3];
            u32 c[3];
            for (int v = 0; v < 3; v++) {
                const float *q = HAND[hand_tri[i][v]];
                px[v] = hx + q[0] * k; py[v] = hy + q[1] * k;
                c[v] = q[1] < 6 ? cc : cd;
            }
            b_tri(px[0], py[0], c[0], px[1], py[1], c[1], px[2], py[2], c[2]);
        }
    }
    b_flush();
}
