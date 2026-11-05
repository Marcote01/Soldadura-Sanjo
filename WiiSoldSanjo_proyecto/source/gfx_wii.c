/* Capa grafica para Wii: GRRLIB + GX. Resolucion logica = la del modo de video (640x480). */
#include <grrlib.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
#include "gfx.h"
#include "font_ttf.h"          /* generado desde data/font.ttf */

int GFX_W = 640, GFX_H = 480;
static GRRLIB_ttfFont *font = NULL;

int gfx_init(void) {
    int r = GRRLIB_Init();
    if (r == -1 || r == -3) return 0;      /* -2 = no pudo montar la SD: no es grave */
    GFX_W = rmode->fbWidth;
    GFX_H = rmode->efbHeight;
    GRRLIB_SetBackgroundColour(0x0F, 0x17, 0x2A, 0xFF);
    GRRLIB_SetBlend(GRRLIB_BLEND_ALPHA);
    font = GRRLIB_LoadTTF(font_ttf, font_ttf_size);
    return font != NULL;
}

void gfx_begin(void) { }
void gfx_end(void)   { GRRLIB_Render(); }     /* espera el vsync */

void gfx_rect(float x, float y, float w, float h, uint32_t c) {
    GRRLIB_Rectangle(x, y, w, h, c, true);
}

void gfx_fan(const Pt *p, const uint32_t *col, int n) {
    guVector v[64];
    int i;
    if (n > 64) n = 64;
    for (i = 0; i < n; i++) { v[i].x = p[i].x; v[i].y = p[i].y; v[i].z = 0.0f; }
    GRRLIB_NGoneFilled(v, col, (u16)n);
}

void gfx_line(float x1, float y1, float x2, float y2, float t, uint32_t c) {
    float dx = x2 - x1, dy = y2 - y1, l2 = dx * dx + dy * dy, l, nx, ny;
    Pt q[4]; uint32_t cc[4];
    if (l2 < 0.0001f) return;
    l = sqrtf(l2);
    nx = -dy / l * t * 0.5f; ny = dx / l * t * 0.5f;
    q[0].x = x1 + nx; q[0].y = y1 + ny; q[1].x = x2 + nx; q[1].y = y2 + ny;
    q[2].x = x2 - nx; q[2].y = y2 - ny; q[3].x = x1 - nx; q[3].y = y1 - ny;
    cc[0] = cc[1] = cc[2] = cc[3] = c;
    gfx_fan(q, cc, 4);
}

/* UTF-8 -> wchar_t a mano (no dependemos del locale de newlib) */
static int to_wide(const char *s, wchar_t *out, int max) {
    const unsigned char *p = (const unsigned char *)s;
    int n = 0;
    while (*p && n < max - 1) {
        unsigned c = *p;
        if (c < 0x80)               { out[n++] = c; p += 1; }
        else if ((c & 0xE0) == 0xC0 && p[1]) { out[n++] = ((c & 0x1F) << 6) | (p[1] & 0x3F); p += 2; }
        else if ((c & 0xF0) == 0xE0 && p[1] && p[2]) { out[n++] = ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); p += 3; }
        else                        { out[n++] = '?'; p += 1; }
    }
    out[n] = 0;
    return n;
}

void gfx_text(float x, float y, const char *s, int size, uint32_t c) {
    wchar_t w[512];
    to_wide(s, w, 512);
    GRRLIB_PrintfTTFW((int)x, (int)y, font, w, (unsigned)size, c);
}

int gfx_text_w(const char *s, int size) {
    wchar_t w[512];
    to_wide(s, w, 512);
    return (int)GRRLIB_WidthTTFW(font, w, (unsigned)size);
}
