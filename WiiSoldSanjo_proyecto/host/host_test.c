/*
 * Banco de pruebas en PC (NO va a la Wii).
 * Compila game.c junto con un rasterizador por software y simula un jugador,
 * para sacar capturas y verificar logica y diseno sin tener la consola.
 *   gcc -O2 -o host_test host/host_test.c -lm   (ver README)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

#include "../source/game.c"      /* incluimos el .c para poder mirar el estado interno */

int GFX_W = 640, GFX_H = 480;
static uint8_t *fb;
static stbtt_fontinfo font;
static unsigned char *fontbuf;

/* ---- plataforma falsa ---- */
static int beeps[8], weld_on = 0, rumble_on = 0;
void plat_beep(int k) { if (k >= 0 && k < 8) beeps[k]++; }
void plat_weld(int on) { weld_on = on; }
void plat_rumble(int on) { rumble_on = on; }

int gfx_init(void) {
    FILE *f = fopen("data/font.ttf", "rb"); long n;
    if (!f) { fprintf(stderr, "falta data/font.ttf\n"); return 0; }
    fseek(f, 0, SEEK_END); n = ftell(f); fseek(f, 0, SEEK_SET);
    fontbuf = malloc(n); if (fread(fontbuf, 1, n, f) != (size_t)n) return 0; fclose(f);
    stbtt_InitFont(&font, fontbuf, 0);
    fb = calloc(GFX_W * GFX_H * 3, 1);
    return 1;
}
void gfx_begin(void) { memset(fb, 0, GFX_W * GFX_H * 3); }
void gfx_end(void) {}

static void blend(int x, int y, float r, float g, float b, float a) {
    uint8_t *p;
    if (x < 0 || y < 0 || x >= GFX_W || y >= GFX_H) return;
    p = fb + (y * GFX_W + x) * 3;
    p[0] = (uint8_t)(p[0] * (1 - a) + r * a);
    p[1] = (uint8_t)(p[1] * (1 - a) + g * a);
    p[2] = (uint8_t)(p[2] * (1 - a) + b * a);
}
#define CR(c) ((c) >> 24 & 255)
#define CG(c) ((c) >> 16 & 255)
#define CB(c) ((c) >> 8 & 255)
#define CA(c) ((c) & 255)

void gfx_rect(float x, float y, float w, float h, uint32_t c) {
    int x0 = (int)floorf(x + 0.5f), y0 = (int)floorf(y + 0.5f), x1 = (int)floorf(x + w + 0.5f), y1 = (int)floorf(y + h + 0.5f), i, j;
    for (j = y0; j < y1; j++) for (i = x0; i < x1; i++) blend(i, j, CR(c), CG(c), CB(c), CA(c) / 255.0f);
}

static void tri(Pt a, Pt b, Pt c, uint32_t ca, uint32_t cb, uint32_t cc) {
    float minx = fminf(a.x, fminf(b.x, c.x)), maxx = fmaxf(a.x, fmaxf(b.x, c.x));
    float miny = fminf(a.y, fminf(b.y, c.y)), maxy = fmaxf(a.y, fmaxf(b.y, c.y));
    float den = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    int x, y;
    if (fabsf(den) < 1e-6f) return;
    for (y = (int)floorf(miny); y <= (int)ceilf(maxy); y++)
        for (x = (int)floorf(minx); x <= (int)ceilf(maxx); x++) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / den;
            float w1 = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / den;
            float w2 = 1 - w0 - w1;
            if (w0 < -0.001f || w1 < -0.001f || w2 < -0.001f) continue;
            blend(x, y,
                  w0 * CR(ca) + w1 * CR(cb) + w2 * CR(cc),
                  w0 * CG(ca) + w1 * CG(cb) + w2 * CG(cc),
                  w0 * CB(ca) + w1 * CB(cb) + w2 * CB(cc),
                  (w0 * CA(ca) + w1 * CA(cb) + w2 * CA(cc)) / 255.0f);
        }
}
void gfx_fan(const Pt *p, const uint32_t *col, int n) {
    int i;
    for (i = 1; i + 1 < n; i++) tri(p[0], p[i], p[i + 1], col[0], col[i], col[i + 1]);
}
void gfx_line(float x1, float y1, float x2, float y2, float t, uint32_t c) {
    float dx = x2 - x1, dy = y2 - y1, l = sqrtf(dx * dx + dy * dy), nx, ny;
    Pt q[4]; uint32_t cc[4] = { c, c, c, c };
    if (l < 0.001f) return;
    nx = -dy / l * t / 2; ny = dx / l * t / 2;
    q[0].x = x1 + nx; q[0].y = y1 + ny; q[1].x = x2 + nx; q[1].y = y2 + ny;
    q[2].x = x2 - nx; q[2].y = y2 - ny; q[3].x = x1 - nx; q[3].y = y1 - ny;
    gfx_fan(q, cc, 4);
}

static int utf8_next(const char **s) {
    const unsigned char *p = (const unsigned char *)*s; int c = *p;
    if (c < 0x80) { *s += 1; return c; }
    if ((c & 0xE0) == 0xC0) { *s += 2; return ((c & 0x1F) << 6) | (p[1] & 0x3F); }
    if ((c & 0xF0) == 0xE0) { *s += 3; return ((c & 0x0F) << 12) | ((p[1] & 0x3F) << 6) | (p[2] & 0x3F); }
    *s += 4; return '?';
}
int gfx_text_w(const char *s, int size) {
    float sc = stbtt_ScaleForMappingEmToPixels(&font, (float)size); int w = 0;
    while (*s) { int cp = utf8_next(&s), adv, lsb; stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb); w += (int)floorf(adv * sc + 0.5f); }
    return w;
}
void gfx_text(float x, float y, const char *s, int size, uint32_t c) {
    float sc = stbtt_ScaleForMappingEmToPixels(&font, (float)size); int pen = 0;
    while (*s) {
        int cp = utf8_next(&s), adv, lsb, w, h, xo, yo, i, j;
        unsigned char *bm;
        stbtt_GetCodepointHMetrics(&font, cp, &adv, &lsb);
        bm = stbtt_GetCodepointBitmap(&font, sc, sc, cp, &w, &h, &xo, &yo);
        for (j = 0; j < h; j++) for (i = 0; i < w; i++)
            blend((int)x + pen + xo + i, (int)y + size + yo + j, CR(c), CG(c), CB(c), bm[j * w + i] / 255.0f * CA(c) / 255.0f);
        stbtt_FreeBitmap(bm, NULL);
        pen += (int)floorf(adv * sc + 0.5f);
    }
}

static void shot(const char *name) {
    char path[128]; FILE *f;
    snprintf(path, sizeof(path), "shots/%s.ppm", name);
    f = fopen(path, "wb"); fprintf(f, "P6\n%d %d\n255\n", GFX_W, GFX_H); fwrite(fb, 1, GFX_W * GFX_H * 3, f); fclose(f);
}

/* ---- simulacion de jugador ---- */
static Input in;
static float jitter(float a) { return (frand() - 0.5f) * 2 * a; }

static void frame(void) { game_step(&in); in.a_press = in.b_press = in.home_press = 0; }

/* sigue el camino punto a punto con ruido. noise = px de temblor. devuelve pasos usados */
static int play_path(float noise, float speed, int shot_at, const char *shotname) {
    int steps = 0, k = 0; float px = path[0].x, py = path[0].y;
    int shot_done = 0;
    in.ptr_valid = 1;
    in.x = px; in.y = py;
    while (st == ST_PLAY && steps < 60 * 95 && k < plen) {
        float dx = path[k].x - px, dy = path[k].y - py, d = sqrtf(dx * dx + dy * dy);
        if (d < speed) { k++; continue; }
        /* un jugador real frena mientras la antorcha se enfria */
        if (!overheated) { px += dx / d * speed; py += dy / d * speed; }
        in.x = px + jitter(noise); in.y = py + jitter(noise);
        in.b_held = !overheated;
        frame(); steps++;
        if (!shot_done && shotname && k > plen * shot_at / 100) { game_draw(); shot(shotname); shot_done = 1; }
    }
    return steps;
}

int main(void) {
    int lv, i;
    if (!gfx_init()) return 1;
    game_init();
    system("mkdir -p shots");

    /* menu con puntero sobre el nivel 3 */
    { Rect r = lv_rect(2); in.ptr_valid = 1; in.x = r.x + 100; in.y = r.y + 20; for (i = 0; i < 30; i++) frame(); }
    game_draw(); shot("01_menu");

    printf("%-3s %7s %8s %8s %8s %6s %5s\n", "lv", "pasos", "precis", "cobert", "final", "nota", "cold");
    for (lv = 0; lv < 6; lv++) {
        int steps;
        char nm[32];
        game_debug_set(ST_PLAY, lv);
        snprintf(nm, sizeof(nm), "02_play_lv%d", lv + 1);
        steps = play_path(5.0f, 4.0f, 55, nm);
        printf("%-3d %7d %7.1f%% %7.1f%% %7.1f%% %6c %5d (estado=%d, plen=%d)\n", lv + 1, steps, res_prec, res_cov, res_final, res_grade, ncold, st, plen);
        if (st == ST_RESULTS && lv == 3) { in.x = 400; in.y = 440; game_draw(); shot("03_results_lv4"); }
        if (st == ST_RESULTS && lv == 0) { in.x = 200; in.y = 445; frame(); game_draw(); shot("03_results_lv1"); }
    }

    /* prueba del exploit: saltar al final y mantener B NO debe terminar el nivel */
    game_debug_set(ST_PLAY, 0);
    in.ptr_valid = 1; in.x = path[plen - 1].x; in.y = path[plen - 1].y; in.b_held = 1;
    for (i = 0; i < 300 && st == ST_PLAY; i++) { frame(); if (overheated) in.b_held = 0; }
    printf("exploit al final (300 pasos): estado=%s (esperado: sigue jugando)\n", st == ST_PLAY ? "JUGANDO" : "RESULTADOS");

    /* prueba: no hacer nada 90 s -> termina por tiempo con nota D */
    game_debug_set(ST_PLAY, 1);
    in.ptr_valid = 1; in.x = 300; in.y = 300; in.b_held = 0;
    for (i = 0; i < 60 * 95 && st == ST_PLAY; i++) frame();
    printf("sin jugar 90 s: estado=%s nota=%c tiempo=%.1fs total=%d\n", st == ST_RESULTS ? "RESULTADOS" : "JUGANDO", res_grade, res_time, res_total);

    /* prueba de sobrecalentamiento: B mantenido siempre */
    game_debug_set(ST_PLAY, 1);
    in.ptr_valid = 1; in.x = path[10].x; in.y = path[10].y; in.b_held = 1;
    { int first_over = -1, welds = 0;
      for (i = 0; i < 60 * 20; i++) { frame(); if (overheated && first_over < 0) first_over = i; if (welding) welds++; }
      printf("B mantenido 20 s: sobrecalienta en el paso %d (~%.1fs), pasos soldando=%d (de 1200)\n", first_over, first_over / 60.0f, welds); }

    /* captura de sobrecalentamiento + aviso ultimos segundos */
    game_debug_set(ST_PLAY, 4);
    in.ptr_valid = 1; in.b_held = 0;
    { int s2 = play_path(5.0f, 3.0f, 30, NULL); (void)s2; }
    game_debug_set(ST_PLAY, 4);
    in.x = path[40].x; in.y = path[40].y + 60; in.b_held = 1; time_left = 9.5f; heat = 70;
    for (i = 0; i < 20; i++) { in.x += 2; frame(); }
    game_draw(); shot("04_desvio_ultimos10s");
    return 0;
}
