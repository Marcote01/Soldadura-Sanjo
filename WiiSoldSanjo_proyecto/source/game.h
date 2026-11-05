#ifndef SANJO_GAME_H
#define SANJO_GAME_H
#include "plat.h"

void game_init(void);
int  game_step(const Input *in);   /* un paso fijo de 1/60 s; devuelve 1 si hay que salir */
void game_draw(void);

/* solo para pruebas en PC */
void game_debug_set(int state, int level);

#endif
