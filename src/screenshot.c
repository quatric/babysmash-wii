/*
 * Screenshots, saved like Kid Pix for Wii does: numbered PNGs (babysmash-001.png, ...) in a
 * saves/ folder next to the app, on the SD card or, failing that, the USB drive.
 * The GX frame buffer is copied into a texture and written out with libpng.
 */
#include <stdio.h>
#include <malloc.h>
#include <sys/stat.h>
#include <png.h>
#include "bs.h"

static const char *roots[] = { "sd:/apps/babysmash", "usb:/apps/babysmash" };

static bool write_png(const char *path, const u32 *px, int w, int h)
{
    png_image pi;
    memset(&pi, 0, sizeof pi);
    pi.version = PNG_IMAGE_VERSION;
    pi.width = w;
    pi.height = h;
    pi.format = PNG_FORMAT_RGBA;
    const int ok = png_image_write_to_file(&pi, path, 0, px, 0, NULL);
    png_image_free(&pi);
    return ok != 0;
}

/* Call after the figures are drawn and before GRRLIB_Render(). Returns the file name in
 * outname, or false when there is no writable SD / USB. */
bool screenshot_save(char *outname, int outlen)
{
    const int w = rmode->fbWidth, h = rmode->efbHeight;
    GRRLIB_texImg *tex = GRRLIB_CreateEmptyTexture(w, h);
    if (!tex) return false;
    GRRLIB_Screen2Texture(0, 0, tex, false);
    GX_DrawDone();                                       /* EFB copy finished ... */
    DCInvalidateRange(tex->data, (u32)w * h * 4);        /* ... and CPU cache refreshed */
    u32 *px = (u32 *)malloc((size_t)w * h * 4);
    if (!px) { GRRLIB_FreeTexture(tex); return false; }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++)
            px[y * w + x] = GRRLIB_GetPixelFromtexImg(x, y, tex) | 0xff;   /* opaque RGBA */
    GRRLIB_FreeTexture(tex);

    bool saved = false;
    for (unsigned r = 0; r < sizeof roots / sizeof roots[0] && !saved; r++) {
        char dir[96], path[128];
        mkdir(roots[r], 0777);
        snprintf(dir, sizeof dir, "%s/saves", roots[r]);
        mkdir(dir, 0777);                                /* ok if it already exists */
        for (int n = 1; n < 10000; n++) {
            snprintf(path, sizeof path, "%s/babysmash-%03d.png", dir, n);
            FILE *f = fopen(path, "rb");
            if (f) { fclose(f); continue; }
            if (write_png(path, px, w, h)) {
                snprintf(outname, outlen, "babysmash-%03d.png", n);
                saved = true;
            }
            break;
        }
    }
    free(px);
    return saved;
}
