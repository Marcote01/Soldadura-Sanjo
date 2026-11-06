#ifndef SANJO_PLAT_H
#define SANJO_PLAT_H

typedef struct {
    float x, y;        /* puntero en coordenadas de pantalla */
    int   ptr_valid;   /* 1 si el Wiimote apunta a la pantalla */
    int   a_press;     /* A recien apretado */
    int   b_press;     /* B recien apretado */
    int   b_held;      /* B mantenido */
    int   plus_press;  /* + recien apretado (pausa) */
    int   home_press;  /* HOME recien apretado */
} Input;

enum { BEEP_UI = 0, BEEP_START, BEEP_FINISH, BEEP_TIMER, BEEP_WARN };

void plat_beep(int kind);
void plat_weld(int on);      /* zumbido de soldadura (loop) */
void plat_rumble(int on);    /* vibracion del Wiimote */

#endif
