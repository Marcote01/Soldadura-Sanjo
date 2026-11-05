/* Plataforma Wii: video (via gfx_wii.c), Wiimote, audio y bucle principal. */
#include <grrlib.h>
#include <asndlib.h>
#include <wiiuse/wpad.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "gfx.h"
#include "game.h"
#include "plat.h"

#define SR 32000                       /* frecuencia de los sonidos generados */
#define STEP_US 16667.0                /* 1/60 s */

static s16 *weld_buf = NULL;  static u32 weld_bytes = 0;
static s16 *beep_buf[5];      static u32 beep_bytes[5];
static int  beep_rr = 0;
static volatile int want_exit = 0;

static void on_reset(u32 irq, void *ctx) { (void)irq; (void)ctx; want_exit = 1; }
static void on_power(void) { want_exit = 1; }

/* ------------------------------------------------------------ audio */
static u32 xr = 12345;
static float nrand(void) { xr ^= xr << 13; xr ^= xr >> 17; xr ^= xr << 5; return ((float)(xr >> 8) / 8388608.0f) - 1.0f; }

/* wave: 0 seno, 1 triangular, 2 cuadrada. decay: 1 = cae exponencial, 0 = plano */
static s16 *make_beep(float freq, float dur, int wave, float amp, int decay, u32 *bytes) {
    u32 n = ((u32)(dur * SR) / 16 + 1) * 16;          /* multiplo de 16 muestras = 32 bytes */
    s16 *b = (s16 *)memalign(32, n * 2);
    u32 i;
    if (!b) return NULL;
    for (i = 0; i < n; i++) {
        float t = (float)i / SR, ph = fmodf(t * freq, 1.0f), v;
        float env = decay ? expf(-4.6f * (float)i / (float)n) : (i > n - 64 ? (float)(n - i) / 64.0f : 1.0f);
        if (wave == 0) v = sinf(ph * 6.2831853f);
        else if (wave == 1) v = ph < 0.5f ? 4 * ph - 1 : 3 - 4 * ph;
        else v = ph < 0.5f ? 1.0f : -1.0f;
        b[i] = (s16)(v * env * amp * 32000.0f);
    }
    *bytes = n * 2;
    return b;
}

static void audio_init(void) {
    u32 i, n = 8192, lp1 = 0; float a = 0, c = 0;
    ASND_Init();
    ASND_Pause(0);
    /* zumbido de soldadura: diente de sierra 125 Hz + ruido filtrado (loop exacto de 8192 muestras) */
    weld_buf = (s16 *)memalign(32, n * 2);
    if (weld_buf) {
        for (i = 0; i < n; i++) {
            float saw = ((float)(i % 256) / 256.0f) * 2.0f - 1.0f, nz = nrand();
            a += (nz - a) * 0.5f; c += (nz - c) * 0.05f;           /* pasa-banda barato */
            weld_buf[i] = (s16)((0.35f * saw + 0.9f * (a - c)) * 14000.0f);
        }
        weld_bytes = n * 2;
    }
    (void)lp1;
    beep_buf[BEEP_UI]     = make_beep(800.0f, 0.08f, 0, 0.35f, 1, &beep_bytes[BEEP_UI]);
    beep_buf[BEEP_START]  = make_beep(900.0f, 0.08f, 0, 0.35f, 1, &beep_bytes[BEEP_START]);
    beep_buf[BEEP_FINISH] = make_beep(1000.0f, 0.20f, 0, 0.35f, 1, &beep_bytes[BEEP_FINISH]);
    beep_buf[BEEP_TIMER]  = make_beep(950.0f, 0.15f, 1, 0.45f, 1, &beep_bytes[BEEP_TIMER]);
    beep_buf[BEEP_WARN]   = make_beep(880.0f, 0.10f, 2, 0.20f, 0, &beep_bytes[BEEP_WARN]);
}

void plat_beep(int k) {
    int v;
    if (k < 0 || k > 4 || !beep_buf[k]) return;
    v = 1 + (beep_rr++ % 3);
    ASND_SetVoice(v, VOICE_MONO_16BIT, SR, 0, beep_buf[k], beep_bytes[k], 255, 255, NULL);
}
void plat_weld(int on) {
    if (on && weld_buf) ASND_SetInfiniteVoice(0, VOICE_MONO_16BIT, SR, 0, weld_buf, weld_bytes, 150, 150);
    else ASND_StopVoice(0);
}
void plat_rumble(int on) { WPAD_Rumble(0, on ? 1 : 0); }

/* -------------------------------------------------------------- main */
int main(int argc, char **argv) {
    Input in;
    int pa = 0, pb = 0, ph = 0;
    u64 prev;
    double acc = 0.0;
    (void)argc; (void)argv;

    if (!gfx_init()) return 1;
    WPAD_Init();
    WPAD_SetDataFormat(WPAD_CHAN_0, WPAD_FMT_BTNS_ACC_IR);
    WPAD_SetVRes(WPAD_CHAN_0, (u32)GFX_W, (u32)GFX_H);
    audio_init();
    SYS_SetResetCallback(on_reset);
    SYS_SetPowerCallback(on_power);

    game_init();
    memset(&in, 0, sizeof(in));
    prev = gettime();

    while (!want_exit) {
        u64 now = gettime();
        u32 down, held;
        ir_t ir;

        acc += (double)ticks_to_microsecs(now - prev);
        prev = now;
        if (acc > 250000.0) acc = 250000.0;      /* evita "espiral de la muerte" si se traba */

        WPAD_ScanPads();
        down = WPAD_ButtonsDown(0);
        held = WPAD_ButtonsHeld(0);
        WPAD_IR(0, &ir);
        in.ptr_valid = ir.valid ? 1 : 0;
        in.x = ir.x; in.y = ir.y;
        in.b_held = (held & WPAD_BUTTON_B) ? 1 : 0;
        if (down & WPAD_BUTTON_A)    pa = 1;
        if (down & WPAD_BUTTON_B)    pb = 1;
        if (down & WPAD_BUTTON_HOME) ph = 1;

        while (acc >= STEP_US) {
            in.a_press = pa; in.b_press = pb; in.home_press = ph;
            pa = pb = ph = 0;
            if (game_step(&in)) want_exit = 1;
            acc -= STEP_US;
        }
        game_draw();                             /* GRRLIB_Render espera el vsync */
    }

    plat_weld(0);
    WPAD_Rumble(0, 0);
    ASND_End();
    GRRLIB_Exit();
    exit(0);                                     /* vuelve al Homebrew Channel */
    return 0;
}
