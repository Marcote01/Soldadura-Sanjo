#ifndef SANJO_GFX_H
#define SANJO_GFX_H
#include <stdint.h>

#define COL(r,g,b,a) ((uint32_t)((((uint32_t)(r))<<24)|(((uint32_t)(g))<<16)|(((uint32_t)(b))<<8)|((uint32_t)(a))))

typedef struct { float x, y; } Pt;

extern int GFX_W, GFX_H;                 /* resolucion logica (640x480 en la Wii) */

int  gfx_init(void);
void gfx_begin(void);                    /* empieza el cuadro */
void gfx_end(void);                      /* muestra el cuadro (espera vsync en la Wii) */
void gfx_rect(float x, float y, float w, float h, uint32_t c);
void gfx_line(float x1, float y1, float x2, float y2, float thick, uint32_t c);
void gfx_fan(const Pt *p, const uint32_t *col, int n);   /* abanico de triangulos, color por vertice */
void gfx_text(float x, float y, const char *utf8, int size, uint32_t c);   /* (x,y) = esquina sup. izq. */
int  gfx_text_w(const char *utf8, int size);

#endif
