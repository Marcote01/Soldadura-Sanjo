#ifndef SANJO_SAVE_H
#define SANJO_SAVE_H

/* Ajustes y mejores puntajes (se guardan en un archivo de texto en la SD) */
typedef struct {
    int  haptics;       /* vibracion del Wiimote */
    int  sound;         /* sonido */
    int  best[6];       /* mejor puntaje por nivel (0 = sin record) */
    char grade[6];      /* nota del mejor puntaje ('A'..'D', 0 = sin record) */
} SaveData;

void save_init(const char *dir);        /* carpeta donde vive scores.txt, ej. "sd:/apps/WiiWeldSanjo/" */
void save_defaults(SaveData *s);
int  save_load(SaveData *s);            /* 1 si leyo el archivo */
int  save_store(const SaveData *s);     /* 1 si pudo escribir */

#endif
