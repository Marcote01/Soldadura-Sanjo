#include <stdio.h>
#include <string.h>
#include "save.h"

static char save_path[256] = "sd:/apps/WiiWeldSanjo/scores.txt";

void save_init(const char *dir) {
    size_t n;
    if (!dir || !dir[0]) return;
    n = strlen(dir);
    if (n > 200) return;
    memcpy(save_path, dir, n);
    if (save_path[n - 1] != '/') save_path[n++] = '/';
    strcpy(save_path + n, "scores.txt");
}

void save_defaults(SaveData *s) {
    memset(s, 0, sizeof(*s));
    s->haptics = 1;
    s->sound = 1;
}

int save_load(SaveData *s) {
    FILE *f = fopen(save_path, "r");
    char magic[16];
    int i, h, so;
    SaveData t;
    if (!f) return 0;
    save_defaults(&t);
    if (fscanf(f, "%15s %d %d", magic, &h, &so) != 3 || strcmp(magic, "SANJO1") != 0) { fclose(f); return 0; }
    t.haptics = h ? 1 : 0;
    t.sound = so ? 1 : 0;
    for (i = 0; i < 6; i++) {
        int sc; char g[4];
        if (fscanf(f, "%d %3s", &sc, g) != 2) break;
        if (sc > 0 && sc < 100000000 && g[0] >= 'A' && g[0] <= 'D') { t.best[i] = sc; t.grade[i] = g[0]; }
    }
    fclose(f);
    *s = t;
    return 1;
}

int save_store(const SaveData *s) {
    FILE *f = fopen(save_path, "w");
    int i;
    if (!f) return 0;
    fprintf(f, "SANJO1 %d %d\n", s->haptics ? 1 : 0, s->sound ? 1 : 0);
    for (i = 0; i < 6; i++) fprintf(f, "%d %c\n", s->best[i], s->grade[i] ? s->grade[i] : '-');
    fflush(f);
    fclose(f);
    return 1;
}
