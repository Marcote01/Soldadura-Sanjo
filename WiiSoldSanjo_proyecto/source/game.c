/*
 * SOLDADURA SANJO SIMULATOR - port a Wii (Homebrew)
 * Port del juego HTML. Toda la logica corre a paso fijo de 1/60 s,
 * asi el puntaje no depende de si la tele va a 50 o 60 Hz.
 */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "game.h"
#include "gfx.h"
#include "plat.h"

#define PI_F          3.14159265f
#define STEP          (1.0f / 60.0f)
#define LEVEL_TIME    90.0f
#define MAXPATH       256
#define MAXSPARK      512
#define MAXSMOKE      160
#define MAXHOT        160
#define MAXCOLD       4000
#define BEAD_COOL     1.5f      /* segundos hasta que el cordon queda gris */
#define BEAD_SPACING  4.0f      /* px minimos entre cordones */
#define HEADER_H      56
#define FOOTER_H      24

enum { ST_MENU, ST_PLAY, ST_RESULTS };

typedef struct { float x, y, tol; } GP;
typedef struct { float x, y, vx, vy, life, decay, size; uint32_t col; } Spark;
typedef struct { float x, y, vx, vy, radius, life, decay; } Smoke;
typedef struct { float x, y, r; int born; } Bead;
typedef struct { float x, y, r; } Cold;
typedef struct { float x, y, w, h; } Rect;

static const char *LV_NAME[6] = {
    "1. Media luna", "2. Zigzag curvo", "3. Triángulo",
    "4. Rectángulo", "5. Ocho", "6. Círculos"
};
static const char *LV_DESC[6] = {
    "Arcos cóncavos continuos.", "Ondulaciones horizontales.", "Vértices triangulares agudos.",
    "Ángulos de 90° e hilada cuadrada.", "Lazos cruzados continuos.", "Espirales circulares superpuestas."
};
static const char *LV_UP[6] = {
    "1. MEDIA LUNA", "2. ZIGZAG CURVO", "3. TRIÁNGULO",
    "4. RECTÁNGULO", "5. OCHO", "6. CÍRCULOS"
};

/* ------------------------------------------------------------------ estado */
static int   st = ST_MENU;
static int   level = 0;
static float cur_x = 320, cur_y = 240;
static int   cur_init = 0, cur_valid = 0;

static float fy0, fw, fh;                 /* campo de juego */

static GP    path[MAXPATH];
static int   plen = 0;
static int   progress = 0;

static int   score = 0;
static float psum = 0; static int pcnt = 0;
static float heat = 0;
static int   overheated = 0, need_release = 0, welding = 0;
static float time_left = LEVEL_TIME;
static int   last_sec = 90;
static int   warn_flash = 0, warn_cd = 0;
static int   tick = 0;
static int   bead_count = 0;

static Spark sparks[MAXSPARK]; static int nspark = 0;
static Smoke smokes[MAXSMOKE]; static int nsmoke = 0;
static Bead  hot[MAXHOT];      static int hot_head = 0, hot_n = 0;
static Cold  cold[MAXCOLD];    static int ncold = 0;
static int   have_last = 0; static float last_bx = 0, last_by = 0;

/* resultados */
static char  res_grade = 'D';
static uint32_t res_col = 0;
static float res_prec = 0, res_cov = 0, res_final = 0, res_time = 0;
static int   res_total = 0;
static const char *res_feedback = "";

/* ------------------------------------------------------------------- utils */
static uint32_t rng = 2463534242u;
static float frand(void) {
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return (float)(rng >> 8) / 16777216.0f;
}

static float ctab_c[33][33], ctab_s[33][33];
static char  ctab_ok[33];

static void circle(float cx, float cy, float r, uint32_t c, int segs) {
    Pt p[34]; uint32_t col[34];
    int i;
    if (segs > 32) segs = 32;
    if (!ctab_ok[segs]) {
        for (i = 0; i <= segs; i++) {
            float a = (float)i / (float)segs * 2.0f * PI_F;
            ctab_c[segs][i] = cosf(a); ctab_s[segs][i] = sinf(a);
        }
        ctab_ok[segs] = 1;
    }
    p[0].x = cx; p[0].y = cy; col[0] = c;
    for (i = 0; i <= segs; i++) {
        p[i + 1].x = cx + ctab_c[segs][i] * r;
        p[i + 1].y = cy + ctab_s[segs][i] * r;
        col[i + 1] = c;
    }
    gfx_fan(p, col, segs + 2);
}

static void ring(float cx, float cy, float r, float thick, uint32_t c, int segs) {
    int i;
    float px = cx + r, py = cy;
    for (i = 1; i <= segs; i++) {
        float a = (float)i / (float)segs * 2.0f * PI_F;
        float x = cx + cosf(a) * r, y = cy + sinf(a) * r;
        gfx_line(px, py, x, y, thick, c);
        px = x; py = y;
    }
}

static void panel(float x, float y, float w, float h, uint32_t fill, uint32_t border) {
    gfx_rect(x, y, w, h, fill);
    gfx_rect(x, y, w, 2, border);
    gfx_rect(x, y + h - 2, w, 2, border);
    gfx_rect(x, y, 2, h, border);
    gfx_rect(x + w - 2, y, 2, h, border);
}

static void text_c(float cx, float y, const char *s, int size, uint32_t c) {
    gfx_text(cx - (float)gfx_text_w(s, size) / 2.0f, y, s, size, c);
}
static void text_r(float rx, float y, const char *s, int size, uint32_t c) {
    gfx_text(rx - (float)gfx_text_w(s, size), y, s, size, c);
}

static int wrap_draw(float x, float y, float maxw, const char *s, int size, uint32_t c, int lineh) {
    char line[512], test[512];
    int lines = 0, len = 0;
    const char *p = s;
    line[0] = 0;
    while (*p) {
        char word[96]; int wl = 0;
        while (*p == ' ') p++;
        while (*p && *p != ' ' && wl < 95) word[wl++] = *p++;
        word[wl] = 0;
        if (!wl) break;
        if (len) snprintf(test, sizeof(test), "%s %s", line, word);
        else     snprintf(test, sizeof(test), "%s", word);
        if (len && gfx_text_w(test, size) > maxw) {
            gfx_text(x, y + lines * lineh, line, size, c);
            lines++;
            snprintf(line, sizeof(line), "%s", word);
            len = (int)strlen(line);
        } else {
            snprintf(line, sizeof(line), "%s", test);
            len = (int)strlen(line);
        }
    }
    if (len) { gfx_text(x, y + lines * lineh, line, size, c); lines++; }
    return lines;
}

static int hit(const Rect *r, float px, float py) {
    return px >= r->x && px <= r->x + r->w && py >= r->y && py <= r->y + r->h;
}

/* ----------------------------------------------------- camino guia (6 niveles) */
static void add_pt(float x, float y, float tol) {
    if (plen < MAXPATH) { path[plen].x = x; path[plen].y = y; path[plen].tol = tol; plen++; }
}

static void gen_path(void) {
    float w = fw, h = fh;
    float margin = w * 0.12f, startX = margin, endX = w - margin;
    float centerY = fy0 + h * 0.5f, amp = h * 0.16f;
    int i, j;
    plen = 0;

    if (level == 0) {                                   /* media luna */
        int repeat = 12; float stepX = (endX - startX) / repeat;
        for (i = 0; i < repeat; i++) {
            float x0 = startX + i * stepX, x1 = startX + (i + 1) * stepX;
            for (j = 0; j <= 12; j++) {
                float t = j / 12.0f;
                add_pt(x0 + (x1 - x0) * t, centerY + sinf(t * PI_F) * amp, 22);
            }
        }
    } else if (level == 1) {                            /* zigzag curvo */
        int steps = 150; float freq = 7;
        for (i = 0; i <= steps; i++) {
            float t = (float)i / steps;
            add_pt(startX + (endX - startX) * t, centerY + sinf(t * PI_F * 2 * freq) * amp, 22);
        }
    } else if (level == 2) {                            /* triangulo */
        int repeat = 9; float stepX = (endX - startX) / repeat;
        for (i = 0; i < repeat; i++) {
            float x0 = startX + i * stepX, xMid = x0 + stepX * 0.5f, x1 = x0 + stepX;
            for (j = 0; j <= 6; j++) { float t = j / 6.0f; add_pt(x0 + (xMid - x0) * t, centerY - amp * t, 20); }
            for (j = 0; j <= 6; j++) { float t = j / 6.0f; add_pt(xMid + (x1 - xMid) * t, (centerY - amp) + amp * t, 20); }
        }
    } else if (level == 3) {                            /* rectangulo */
        int repeat = 7; float stepX = (endX - startX) / repeat;
        for (i = 0; i < repeat; i++) {
            float x0 = startX + i * stepX, x1 = x0 + stepX * 0.5f, x2 = x0 + stepX;
            for (j = 0; j <= 4; j++) add_pt(x0, centerY - (amp * j / 4), 20);
            for (j = 0; j <= 4; j++) add_pt(x0 + (x1 - x0) * (j / 4.0f), centerY - amp, 20);
            for (j = 0; j <= 4; j++) add_pt(x1, (centerY - amp) + (amp * j / 4), 20);
            for (j = 0; j <= 4; j++) add_pt(x1 + (x2 - x1) * (j / 4.0f), centerY, 20);
        }
    } else if (level == 4) {                            /* ocho */
        int steps = 180; float freq = 6;
        for (i = 0; i <= steps; i++) {
            float t = (float)i / steps, ang = t * PI_F * 2 * freq;
            add_pt(startX + (endX - startX) * t, centerY + sinf(ang * 2) * (amp * 0.85f), 22);
        }
    } else {                                            /* circulos */
        int loops = 8; float stepX = (endX - startX) / loops, radius = amp * 0.8f;
        for (i = 0; i < loops; i++) {
            float cx = startX + i * stepX + radius;
            for (j = 0; j <= 16; j++) {
                float ang = (j / 16.0f) * PI_F * 2;
                add_pt(cx + cosf(ang) * radius, centerY + sinf(ang) * radius, 22);
            }
        }
    }
}

static float closest(float px, float py, int *idx) {
    float best = 1e9f; int bi = 0, i;
    for (i = 0; i < plen; i++) {
        float dx = px - path[i].x, dy = py - path[i].y, d = dx * dx + dy * dy;
        if (d < best) { best = d; bi = i; }
    }
    *idx = bi;
    return sqrtf(best);
}

/* --------------------------------------------------------------- particulas */
static void add_sparks(float x, float y, int n) {
    int i;
    for (i = 0; i < n && nspark < MAXSPARK; i++) {
        float ang = frand() * PI_F * 2, spd = 2 + frand() * 8;
        Spark *s = &sparks[nspark++];
        s->x = x; s->y = y;
        s->vx = cosf(ang) * spd; s->vy = sinf(ang) * spd - frand() * 2;
        s->life = 1; s->decay = 0.03f + frand() * 0.05f; s->size = 1.5f + frand() * 2.5f;
        s->col = (frand() > 0.3f) ? COL(56, 189, 248, 255) : COL(245, 158, 11, 255);
    }
}
static void add_smoke(float x, float y) {
    Smoke *s;
    if (frand() > 0.4f || nsmoke >= MAXSMOKE) return;
    s = &smokes[nsmoke++];
    s->x = x + (frand() - 0.5f) * 10; s->y = y - 5;
    s->vx = (frand() - 0.5f) * 0.8f; s->vy = -1 - frand() * 1.5f;
    s->radius = 4 + frand() * 6; s->life = 0.8f; s->decay = 0.015f;
}

/* ------------------------------------------------------------------- juego */
static void start_game(void) {
    st = ST_PLAY;
    score = 0; psum = 0; pcnt = 0; heat = 0;
    overheated = 0; need_release = 0; welding = 0;
    time_left = LEVEL_TIME; last_sec = 90;
    nspark = nsmoke = 0; hot_head = hot_n = 0; ncold = 0;
    have_last = 0; bead_count = 0; progress = 0; tick = 0;
    warn_flash = 0; warn_cd = 0;
    gen_path();
    plat_beep(BEEP_START);
}

static void stop_weld(void) {
    if (welding) { welding = 0; plat_weld(0); plat_rumble(0); }
}

static void finish_game(void) {
    float coverage, avg;
    stop_weld();
    st = ST_RESULTS;
    plat_beep(BEEP_FINISH);

    avg = pcnt > 0 ? (psum / pcnt) * 100.0f : 0.0f;
    coverage = (float)bead_count / (float)plen * 120.0f;
    if (coverage > 100) coverage = 100;
    res_final = avg * 0.6f + coverage * 0.4f;
    if (res_final > 100) res_final = 100;
    res_prec = avg; res_cov = coverage;

    if (res_final >= 96.0f) {
        res_grade = 'A'; res_col = COL(52, 211, 153, 255);
        res_feedback = "¡Excelente nivel de maestría técnica! Mantuviste una distancia de arco constante, velocidad uniforme y una penetración homogénea en todo el trazado. Para sostener este nivel en soldadura real, asegurate de mantener el ángulo de trabajo a 45° y controlar adecuadamente la velocidad de enfriamiento del baño de fusión.";
    } else if (res_final >= 86.0f) {
        res_grade = 'B'; res_col = COL(56, 189, 248, 255);
        res_feedback = "Buen desempeño técnico, aunque con pequeños detalles a corregir. Se observaron leves variaciones en el pulso y la alineación del cordón. Recomendación pedagógica: afianzá el apoyo de la mano guía, mantené un ritmo de avance constante para evitar socavaciones o exceso de aporte y estabilizá el puntero en las curvas.";
    } else if (res_final >= 51.0f) {
        res_grade = 'C'; res_col = COL(251, 191, 36, 255);
        res_feedback = "Calificación regular. Existen oscilaciones marcadas fuera del eje guía y discontinuidad en el cordón. Consejo técnico: practicá la postura de trabajo para mejorar el control motriz. Recordá que desviarse de la línea causa falta de fusión y porosidad en las juntas reales. Moderá la velocidad de avance.";
    } else {
        res_grade = 'D'; res_col = COL(239, 68, 68, 255);
        res_feedback = "Calificación insuficiente (50% o menos). El cordón presenta desviaciones severas y falta de continuidad crítica. Diagnóstico técnico: falta de control del arco y movimiento errático. Recomendación: revisá la empuñadura del control, estabilizá los hombros, reducí la aceleración voluntaria y repetí el ejercicio guiando la vista 1 cm por delante del punto de fusión.";
    }
    res_time = LEVEL_TIME - time_left;
    res_total = (int)floorf((float)score + res_final * 50.0f + 0.5f);
}

void game_init(void) {
    fy0 = HEADER_H;
    fw = (float)GFX_W;
    fh = (float)(GFX_H - HEADER_H - FOOTER_H);
    st = ST_MENU; level = 0; cur_init = 0;
}

void game_debug_set(int state, int lvl) {
    level = lvl;
    if (state == ST_PLAY) start_game(); else st = state;
}

/* rectangulos de botones */
static float oy(void) { float o = (float)(GFX_H - 480) / 2.0f; return o > 0 ? o : 0; }
static Rect lv_rect(int i) {
    Rect r; r.w = 285; r.h = 56;
    r.x = (GFX_W - 585) / 2.0f + (float)(i % 2) * 300.0f;
    r.y = oy() + 160 + (float)(i / 2) * 64.0f;
    return r;
}
static Rect start_rect(void) { Rect r; r.w = 320; r.h = 48; r.x = (GFX_W - 320) / 2.0f; r.y = oy() + 364; return r; }
static Rect again_rect(void) { Rect r; r.w = 200; r.h = 40; r.x = 100; r.y = oy() + 400; return r; }
static Rect menu_rect(void)  { Rect r; r.w = 200; r.h = 40; r.x = 340; r.y = oy() + 400; return r; }

int game_step(const Input *in) {
    int i;
    if (in->home_press) return 1;

    cur_valid = in->ptr_valid;
    if (in->ptr_valid) {
        if (!cur_init) { cur_x = in->x; cur_y = in->y; cur_init = 1; }
        else { cur_x += (in->x - cur_x) * 0.5f; cur_y += (in->y - cur_y) * 0.5f; }   /* suaviza el temblor del IR */
    }

    if (st == ST_MENU) {
        if (in->a_press && cur_valid) {
            Rect s = start_rect();
            for (i = 0; i < 6; i++) {
                Rect r = lv_rect(i);
                if (hit(&r, cur_x, cur_y)) { level = i; plat_beep(BEEP_UI); }
            }
            if (hit(&s, cur_x, cur_y)) start_game();
        }
        return 0;
    }

    if (st == ST_RESULTS) {
        if (in->a_press && cur_valid) {
            Rect a = again_rect(), m = menu_rect();
            if (hit(&a, cur_x, cur_y)) start_game();
            else if (hit(&m, cur_x, cur_y)) { st = ST_MENU; plat_beep(BEEP_UI); }
        }
        return 0;
    }

    /* ---------------------------------------------------------- ST_PLAY */
    tick++;
    if (warn_cd > 0) warn_cd--;
    warn_flash = 0;

    /* 1. temporizador */
    time_left -= STEP;
    if (time_left < 0) time_left = 0;
    {
        int cs = (int)ceilf(time_left);
        if (time_left <= 10.0f && cs != last_sec) { plat_beep(BEEP_TIMER); last_sec = cs; }
        else if (time_left > 10.0f) last_sec = cs;
    }
    if (time_left <= 0) { finish_game(); return 0; }

    /* entrada de soldadura: hay que soltar B despues de sobrecalentar */
    if (!in->b_held) need_release = 0;
    {
        int want = in->b_held && cur_valid && !overheated && !need_release;
        if (want && !welding) { welding = 1; plat_weld(1); plat_rumble(1); have_last = 0; }
        if (!want && welding) stop_weld();
    }

    /* 2. calor de la antorcha */
    if (welding) {
        heat += 15.0f * STEP;
        if (heat >= 100.0f) {
            heat = 100.0f; overheated = 1; need_release = 1;
            stop_weld();
            plat_beep(BEEP_WARN);
        }
    } else {
        heat -= 35.0f * STEP;
        if (heat <= 0) { heat = 0; overheated = 0; }
    }

    /* 3. soldadura activa */
    if (welding && cur_valid && cur_y >= fy0 && cur_y <= fy0 + fh) {
        int idx, wi, j;
        float dist, tol, dx, dy;

        add_sparks(cur_x, cur_y, 8);
        add_smoke(cur_x, cur_y);
        bead_count++;

        dx = cur_x - last_bx; dy = cur_y - last_by;
        if (!have_last || dx * dx + dy * dy >= BEAD_SPACING * BEAD_SPACING) {
            if (hot_n < MAXHOT) {
                Bead *b = &hot[(hot_head + hot_n) % MAXHOT];
                b->x = cur_x; b->y = cur_y; b->r = 8 + frand() * 3; b->born = tick;
                hot_n++;
                last_bx = cur_x; last_by = cur_y; have_last = 1;
            }
        }

        dist = closest(cur_x, cur_y, &idx);
        tol = path[idx].tol;
        if (dist <= tol) {
            float f = 1.0f - dist / tol; if (f < 0) f = 0;
            psum += f; pcnt++;
            score += (int)floorf(10.0f * f + 0.5f);
        } else {
            psum += 0; pcnt++;
            score -= 5; if (score < 0) score = 0;
            warn_flash = 1;
            if (warn_cd == 0) { plat_beep(BEEP_WARN); warn_cd = 8; }
        }

        /* avance secuencial: solo cuenta si seguis el camino en orden */
        wi = progress;
        {
            float bd = 1e9f; int bj = progress;
            int jmax = progress + 24; if (jmax > plen - 1) jmax = plen - 1;
            for (j = progress; j <= jmax; j++) {
                float ddx = cur_x - path[j].x, ddy = cur_y - path[j].y, d = ddx * ddx + ddy * ddy;
                if (d < bd) { bd = d; bj = j; }
            }
            if (sqrtf(bd) <= path[bj].tol * 1.25f && bj > wi) progress = bj;
        }
        if (progress >= plen - 2) { finish_game(); return 0; }
    }

    /* cordones calientes -> fríos (lista compacta) */
    while (hot_n > 0) {
        Bead *b = &hot[hot_head];
        if ((float)(tick - b->born) * STEP < BEAD_COOL) break;
        if (ncold < MAXCOLD) { cold[ncold].x = b->x; cold[ncold].y = b->y; cold[ncold].r = b->r; ncold++; }
        hot_head = (hot_head + 1) % MAXHOT; hot_n--;
    }

    /* particulas */
    for (i = 0; i < nspark;) {
        Spark *s = &sparks[i];
        s->x += s->vx; s->y += s->vy; s->vy += 0.2f; s->life -= s->decay;
        if (s->life <= 0) sparks[i] = sparks[--nspark]; else i++;
    }
    for (i = 0; i < nsmoke;) {
        Smoke *s = &smokes[i];
        s->x += s->vx; s->y += s->vy; s->radius += 0.2f; s->life -= s->decay;
        if (s->life <= 0) smokes[i] = smokes[--nsmoke]; else i++;
    }
    return 0;
}

/* ================================================================ DIBUJO */
static void draw_metal_bg(float y0, float h) {
    float x, y;
    gfx_rect(0, y0, (float)GFX_W, h, COL(30, 41, 59, 255));
    for (x = 0; x < (float)GFX_W; x += 40) gfx_rect(x, y0, 1, h, COL(51, 65, 85, 255));
    for (y = 0; y < h; y += 40) gfx_rect(0, y0 + y, (float)GFX_W, 1, COL(51, 65, 85, 255));
}

static void draw_pointer(void) {
    Pt o[3] = { {cur_x - 1.5f, cur_y - 2}, {cur_x - 1.5f, cur_y + 22}, {cur_x + 16, cur_y + 14} };
    Pt f[3] = { {cur_x, cur_y}, {cur_x, cur_y + 18}, {cur_x + 12.5f, cur_y + 12} };
    uint32_t co[3] = { COL(15, 23, 42, 255), COL(15, 23, 42, 255), COL(15, 23, 42, 255) };
    uint32_t cf[3] = { COL(255, 255, 255, 255), COL(255, 255, 255, 255), COL(125, 211, 252, 255) };
    gfx_fan(o, co, 3);
    gfx_fan(f, cf, 3);
}

static void draw_button(const Rect *r, const char *label, int size, int hover, int primary) {
    uint32_t fill = primary ? (hover ? COL(56, 189, 248, 255) : COL(2, 132, 199, 255))
                            : (hover ? COL(51, 65, 85, 255) : COL(30, 41, 59, 245));
    uint32_t border = hover ? COL(186, 230, 253, 255) : COL(56, 189, 248, 140);
    panel(r->x, r->y, r->w, r->h, fill, border);
    text_c(r->x + r->w / 2, r->y + r->h / 2 - (float)size * 0.62f, label, size, COL(255, 255, 255, 255));
}

static void draw_menu(void) {
    float o = oy();
    int i;
    draw_metal_bg(0, (float)GFX_H);
    gfx_rect(0, 0, (float)GFX_W, (float)GFX_H, COL(2, 6, 23, 190));

    text_c(GFX_W / 2.0f, o + 18, "SOLDADURA SANJO", 40, COL(241, 245, 249, 255));
    text_c(GFX_W / 2.0f, o + 58, "SIMULATOR", 40, COL(56, 189, 248, 255));
    text_c(GFX_W / 2.0f, o + 108, "Homebrew Wii Edition • Módulo de Entrenamiento Técnico", 13, COL(148, 163, 184, 255));
    text_c(GFX_W / 2.0f, o + 130, "Elegí un patrón técnico de costura. Tenés 01:30 minutos para el ejercicio.", 14, COL(203, 213, 225, 255));

    for (i = 0; i < 6; i++) {
        Rect r = lv_rect(i);
        int hov = cur_valid && hit(&r, cur_x, cur_y);
        int sel = (i == level);
        uint32_t fill = sel ? COL(12, 74, 110, 250) : (hov ? COL(51, 65, 85, 250) : COL(30, 41, 59, 245));
        uint32_t border = sel ? COL(251, 191, 36, 255) : (hov ? COL(186, 230, 253, 255) : COL(56, 189, 248, 110));
        panel(r.x, r.y, r.w, r.h, fill, border);
        gfx_text(r.x + 14, r.y + 7, LV_NAME[i], 19, sel ? COL(253, 224, 71, 255) : COL(241, 245, 249, 255));
        gfx_text(r.x + 14, r.y + 32, LV_DESC[i], 13, COL(148, 163, 184, 255));
    }
    {
        Rect s = start_rect();
        draw_button(&s, "INICIAR NIVEL (01:30)", 22, cur_valid && hit(&s, cur_x, cur_y), 1);
    }
    text_c(GFX_W / 2.0f, o + 424, "Wiimote P1 • Puntero IR + botón A para elegir • botón B para soldar", 13, COL(148, 163, 184, 255));

    gfx_rect(0, (float)GFX_H - FOOTER_H, (float)GFX_W, FOOTER_H, COL(15, 23, 42, 242));
    gfx_text(10, (float)GFX_H - FOOTER_H + 6, "WII READY | SD CARD: /apps/WiiWeldSanjo/boot.dol", 11, COL(125, 211, 252, 255));
    text_r((float)GFX_W - 10, (float)GFX_H - FOOTER_H + 6, "MODO: ENTRENAMIENTO", 11, COL(148, 163, 184, 255));

    if (cur_valid) draw_pointer();
    else text_c(GFX_W / 2.0f, o + 446, "Apuntá el Wiimote a la pantalla", 15, COL(251, 191, 36, 255));
}

static void draw_torch(float tx, float ty, int arc) {
    const float cs = 0.8660254f, sn = 0.5f;
#define TX(lx, ly) (tx + (lx) * cs - (ly) * sn)
#define TY(lx, ly) (ty + (lx) * sn + (ly) * cs)
    uint32_t ink = COL(71, 85, 105, 255);
    /* mira de precision */
    ring(tx, ty, 4, 1.5f, arc ? COL(251, 191, 36, 255) : COL(56, 189, 248, 255), 12);
    /* cuello metalico */
    gfx_line(TX(15, 13), TY(15, 13), TX(97, 75), TY(97, 75), 12, ink);
    gfx_line(TX(15, 13), TY(15, 13), TX(97, 75), TY(97, 75), 9, COL(148, 163, 184, 255));
    /* boquilla ceramica */
    {
        Pt n[4] = { {TX(0, 0), TY(0, 0)}, {TX(16, 6), TY(16, 6)}, {TX(24, 14), TY(24, 14)}, {TX(6, 18), TY(6, 18)} };
        uint32_t c[4] = { COL(248, 250, 252, 255), COL(248, 250, 252, 255), COL(226, 232, 240, 255), COL(226, 232, 240, 255) };
        gfx_fan(n, c, 4);
        gfx_line(n[0].x, n[0].y, n[1].x, n[1].y, 1.5f, ink);
        gfx_line(n[1].x, n[1].y, n[2].x, n[2].y, 1.5f, ink);
        gfx_line(n[2].x, n[2].y, n[3].x, n[3].y, 1.5f, ink);
        gfx_line(n[3].x, n[3].y, n[0].x, n[0].y, 1.5f, ink);
    }
    /* mango naranja y agarre de goma */
    gfx_line(TX(82, 79), TY(82, 79), TX(176, 165), TY(176, 165), 27, ink);
    gfx_line(TX(82, 79), TY(82, 79), TX(176, 165), TY(176, 165), 24, COL(234, 88, 12, 255));
    gfx_line(TX(112, 86), TY(112, 86), TX(142, 106), TY(142, 106), 12, COL(15, 23, 42, 255));
    /* guante */
    {
        Pt g[4] = { {TX(135, 135), TY(135, 135)}, {TX(240, 220), TY(240, 220)}, {TX(290, 170), TY(290, 170)}, {TX(170, 105), TY(170, 105)} };
        uint32_t c[4] = { COL(51, 65, 85, 255), COL(51, 65, 85, 255), COL(51, 65, 85, 255), COL(51, 65, 85, 255) };
        gfx_fan(g, c, 4);
        gfx_line(g[0].x, g[0].y, g[1].x, g[1].y, 2, COL(30, 41, 59, 255));
        gfx_line(g[1].x, g[1].y, g[2].x, g[2].y, 2, COL(30, 41, 59, 255));
        gfx_line(g[2].x, g[2].y, g[3].x, g[3].y, 2, COL(30, 41, 59, 255));
        gfx_line(g[3].x, g[3].y, g[0].x, g[0].y, 2, COL(30, 41, 59, 255));
    }
#undef TX
#undef TY
}

static void draw_arc_glow(float x, float y) {
    /* gradiente radial 0..45 px: blanco -> celeste -> transparente */
    uint32_t col[4];
    int k, s, segs = 16;
    col[0] = COL(255, 255, 255, 242);
    col[1] = COL(56, 189, 248, 204);
    col[2] = COL(14, 165, 233, 51);
    col[3] = COL(14, 165, 233, 0);
    {
        Pt p[18]; uint32_t c[18];
        p[0].x = x; p[0].y = y; c[0] = col[0];
        for (s = 0; s <= segs; s++) {
            float a = (float)s / segs * 2 * PI_F;
            p[s + 1].x = x + cosf(a) * 9; p[s + 1].y = y + sinf(a) * 9; c[s + 1] = col[1];
        }
        gfx_fan(p, c, segs + 2);
    }
    {
        static const float rr[4] = { 9, 27, 45, 0 };
        for (k = 0; k < 2; k++) {
            for (s = 0; s < segs; s++) {
                float a0 = (float)s / segs * 2 * PI_F, a1 = (float)(s + 1) / segs * 2 * PI_F;
                Pt q[4];
                uint32_t qc[4];
                q[0].x = x + cosf(a0) * rr[k];     q[0].y = y + sinf(a0) * rr[k];     qc[0] = col[k + 1];
                q[1].x = x + cosf(a1) * rr[k];     q[1].y = y + sinf(a1) * rr[k];     qc[1] = col[k + 1];
                q[2].x = x + cosf(a1) * rr[k + 1]; q[2].y = y + sinf(a1) * rr[k + 1]; qc[2] = col[k + 2];
                q[3].x = x + cosf(a0) * rr[k + 1]; q[3].y = y + sinf(a0) * rr[k + 1]; qc[3] = col[k + 2];
                gfx_fan(q, qc, 4);
            }
        }
    }
}

static void draw_play(void) {
    int i;
    draw_metal_bg(fy0, fh);

    /* guia: banda de 22 px (color ya mezclado con el fondo) + linea punteada */
    for (i = 0; i + 1 < plen; i++)
        gfx_line(path[i].x, path[i].y, path[i + 1].x, path[i + 1].y, 22, COL(39, 93, 125, 255));
    for (i = 0; i < plen; i++) circle(path[i].x, path[i].y, 11, COL(39, 93, 125, 255), 12);
    {
        float carry = 0; int on = 1;
        for (i = 0; i + 1 < plen; i++) {
            float x0 = path[i].x, y0 = path[i].y, x1 = path[i + 1].x, y1 = path[i + 1].y;
            float dx = x1 - x0, dy = y1 - y0, len = sqrtf(dx * dx + dy * dy), pos = 0;
            if (len < 0.01f) continue;
            while (pos < len) {
                float seg = 6.0f - carry, step = (len - pos < seg) ? (len - pos) : seg;
                if (on) gfx_line(x0 + dx * pos / len, y0 + dy * pos / len,
                                 x0 + dx * (pos + step) / len, y0 + dy * (pos + step) / len, 2, COL(2, 132, 199, 255));
                pos += step; carry += step;
                if (carry >= 5.99f) { carry = 0; on = !on; }
            }
        }
    }
    /* inicio / fin */
    circle(path[0].x, path[0].y, 9, COL(34, 197, 94, 255), 12);
    circle(path[0].x, path[0].y, 5, COL(20, 83, 45, 255), 10);
    circle(path[plen - 1].x, path[plen - 1].y, 9, COL(239, 68, 68, 255), 12);
    circle(path[plen - 1].x, path[plen - 1].y, 5, COL(127, 29, 29, 255), 10);

    /* cordones frios (poligonos livianos) */
    for (i = 0; i < ncold; i++) {
        circle(cold[i].x, cold[i].y, cold[i].r, COL(100, 116, 139, 255), 10);
        circle(cold[i].x, cold[i].y, cold[i].r - 1.6f, COL(148, 163, 184, 255), 10);
    }
    /* cordones calientes */
    for (i = 0; i < hot_n; i++) {
        Bead *b = &hot[(hot_head + i) % MAXHOT];
        float age = (float)(tick - b->born) * STEP;
        if (age < 0.4f) {
            circle(b->x, b->y, b->r + 6, COL(249, 115, 22, 90), 10);
            circle(b->x, b->y, b->r, COL(255, 255, 255, 255), 10);
        } else {
            circle(b->x, b->y, b->r + 3, COL(239, 68, 68, 70), 10);
            circle(b->x, b->y, b->r, COL(249, 115, 22, 255), 10);
        }
    }
    /* humo y chispas */
    for (i = 0; i < nsmoke; i++)
        circle(smokes[i].x, smokes[i].y, smokes[i].radius, COL(148, 163, 184, (int)(smokes[i].life * 0.25f * 255.0f)), 10);
    for (i = 0; i < nspark; i++) {
        Spark *s = &sparks[i];
        circle(s->x, s->y, s->size + 3, (s->col & 0xFFFFFF00u) | 70u, 6);
        circle(s->x, s->y, s->size, s->col, 6);
    }
    if (welding && cur_valid) draw_arc_glow(cur_x, cur_y);

    if (warn_flash) gfx_rect(0, fy0, (float)GFX_W, fh, COL(220, 38, 38, 40));

    if (cur_valid) draw_torch(cur_x, cur_y, welding);
    else text_c(GFX_W / 2.0f, fy0 + fh / 2 - 10, "Apuntá el Wiimote a la pantalla", 20, COL(251, 191, 36, 255));

    /* aviso de ultimos segundos */
    if (time_left <= 10.0f) {
        Rect r; r.w = 300; r.h = 34; r.x = (GFX_W - 300) / 2.0f; r.y = fy0 + 8;
        panel(r.x, r.y, r.w, r.h, COL(220, 38, 38, 245), COL(253, 224, 71, 255));
        text_c(GFX_W / 2.0f, r.y + 6, "¡ÚLTIMOS 10 SEGUNDOS!", 19, COL(255, 255, 255, 255));
    }

    /* ---- HUD superior ---- */
    {
        char buf[64];
        int cs = (int)ceilf(time_left);
        float avg = pcnt > 0 ? psum / pcnt * 100.0f : 100.0f;
        uint32_t tcol = (time_left <= 10.0f && ((tick / 15) & 1)) ? COL(248, 113, 113, 255)
                      : (time_left <= 10.0f ? COL(239, 68, 68, 255) : COL(125, 211, 252, 255));
        gfx_rect(0, 0, (float)GFX_W, HEADER_H, COL(15, 23, 42, 245));
        gfx_rect(0, HEADER_H - 2, (float)GFX_W, 2, COL(56, 189, 248, 110));

        gfx_text(16, 6, "TIEMPO", 11, COL(148, 163, 184, 255));
        snprintf(buf, sizeof(buf), "%02d:%02d", cs / 60, cs % 60);
        gfx_text(16, 20, buf, 28, tcol);

        gfx_text(130, 6, "PRECISIÓN", 11, COL(148, 163, 184, 255));
        snprintf(buf, sizeof(buf), "%.1f%%", avg);
        gfx_text(130, 20, buf, 28, COL(52, 211, 153, 255));

        gfx_text(282, 6, "PUNTAJE", 11, COL(148, 163, 184, 255));
        snprintf(buf, sizeof(buf), "%05d", score);
        gfx_text(282, 20, buf, 28, COL(251, 191, 36, 255));

        gfx_text(442, 6, "TEMP ANTORCHA", 11, COL(148, 163, 184, 255));
        gfx_rect(442, 22, 182, 16, COL(2, 6, 23, 255));
        {
            uint32_t hc = heat < 50 ? COL(34, 197, 94, 255) : (heat < 80 ? COL(245, 158, 11, 255) : COL(239, 68, 68, 255));
            gfx_rect(443, 23, 180.0f * heat / 100.0f, 14, hc);
        }
        if (overheated) gfx_text(442, 39, "SOBRECALENTADO • ENFRIANDO", 11, COL(248, 113, 113, 255));
    }
    /* ---- pie ---- */
    gfx_rect(0, (float)GFX_H - FOOTER_H, (float)GFX_W, FOOTER_H, COL(15, 23, 42, 245));
    gfx_text(10, (float)GFX_H - FOOTER_H + 6, "WII READY | SD CARD: /apps/WiiWeldSanjo/boot.dol", 11, COL(125, 211, 252, 255));
    {
        char buf[96];
        snprintf(buf, sizeof(buf), "MODO: %s • PRESIONAR 'HOME' PARA SALIR", LV_UP[level]);
        text_r((float)GFX_W - 10, (float)GFX_H - FOOTER_H + 6, buf, 11, COL(148, 163, 184, 255));
    }
}

static void fmt_thousands(char *out, size_t n, int v) {
    char tmp[24]; int l, i, k = 0;
    snprintf(tmp, sizeof(tmp), "%d", v);
    l = (int)strlen(tmp);
    for (i = 0; i < l && (size_t)k < n - 2; i++) {
        if (i > 0 && (l - i) % 3 == 0) out[k++] = ',';
        out[k++] = tmp[i];
    }
    out[k] = 0;
}

static void draw_results(void) {
    float o = oy();
    char buf[64], num[32], g[2];
    Rect a = again_rect(), m = menu_rect();

    draw_play();                                       /* el cordon queda de fondo */
    gfx_rect(0, 0, (float)GFX_W, (float)GFX_H, COL(2, 6, 23, 238));
    panel(50, o + 20, (float)GFX_W - 100, 440, COL(15, 23, 42, 250), COL(56, 189, 248, 150));

    text_c(GFX_W / 2.0f, o + 32, "REPORTE TÉCNICO DE SOLDADURA", 24, COL(125, 211, 252, 255));
    text_c(GFX_W / 2.0f, o + 64, "EVALUACIÓN DE PIEZA", 12, COL(148, 163, 184, 255));

    gfx_text(74, o + 98, "CALIFICACIÓN", 13, COL(148, 163, 184, 255));
    g[0] = res_grade; g[1] = 0;
    gfx_text(98, o + 112, g, 84, res_col);

    gfx_text(206, o + 100, "Precisión de Guía:", 16, COL(203, 213, 225, 255));
    snprintf(buf, sizeof(buf), "%.1f%%", res_prec);
    text_r(556, o + 100, buf, 16, COL(255, 255, 255, 255));

    gfx_text(206, o + 130, "Cobertura del Cordón:", 16, COL(203, 213, 225, 255));
    snprintf(buf, sizeof(buf), "%.1f%% (%.1f%% final)", res_cov, res_final);
    text_r(556, o + 130, buf, 16, COL(255, 255, 255, 255));

    gfx_text(206, o + 160, "Tiempo Empleado:", 16, COL(203, 213, 225, 255));
    snprintf(buf, sizeof(buf), "%.1fs / 90s", res_time);
    text_r(556, o + 160, buf, 16, COL(255, 255, 255, 255));

    gfx_rect(206, o + 188, 346, 1, COL(56, 189, 248, 90));
    gfx_text(206, o + 198, "PUNTAJE TOTAL:", 17, COL(203, 213, 225, 255));
    fmt_thousands(num, sizeof(num), res_total);
    snprintf(buf, sizeof(buf), "%s PTS", num);
    text_r(556, o + 196, buf, 22, COL(251, 191, 36, 255));

    gfx_rect(82, o + 238, (float)GFX_W - 164, 1, COL(56, 189, 248, 90));
    wrap_draw(88, o + 250, (float)GFX_W - 176, res_feedback, 14, COL(203, 213, 225, 255), 19);

    draw_button(&a, "REPETIR", 20, cur_valid && hit(&a, cur_x, cur_y), 1);
    draw_button(&m, "MENÚ", 20, cur_valid && hit(&m, cur_x, cur_y), 0);
    if (cur_valid) draw_pointer();
}

void game_draw(void) {
    gfx_begin();
    if (st == ST_MENU) draw_menu();
    else if (st == ST_PLAY) draw_play();
    else draw_results();
    gfx_end();
}
