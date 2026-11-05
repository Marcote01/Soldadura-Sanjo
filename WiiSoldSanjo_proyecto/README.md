# Soldadura Sanjo Simulator – port para Wii (Homebrew)

Port en C del juego HTML. Usa devkitPPC + GRRLIB + libogc.
Corre desde el Homebrew Channel: `SD:/apps/WiiWeldSanjo/boot.dol`.

## Controles
- Puntero IR: mover la antorcha / elegir en los menús
- A: elegir (nivel, INICIAR, REPETIR, MENÚ)
- B (mantener): soldar. El Wiimote vibra mientras soldás
- HOME: salir al Homebrew Channel

## Cómo obtener el boot.dol (sin instalar nada)
1. Creá una cuenta gratis en github.com y un repositorio nuevo (privado o público).
2. Subí TODO el contenido de esta carpeta (incluida la carpeta oculta `.github`).
3. Pestaña **Actions** → "Compilar boot.dol para Wii" → **Run workflow**.
4. Cuando termine (unos minutos, tilde verde), entrá a esa ejecución y bajá
   el artefacto **WiiWeldSanjo-para-la-SD**. Adentro viene `apps/WiiWeldSanjo/`
   con `boot.dol`, `meta.xml` e `icon.png`.

## Instalar en la Wii
1. SD en FAT32. Copiá la carpeta `apps` a la raíz de la SD (si ya existe, se fusiona).
   Debe quedar: `SD:/apps/WiiWeldSanjo/boot.dol`.
2. Sensor bar conectada y Wiimote con pilas.
3. Menú de la Wii → Homebrew Channel → "Soldadura Sanjo Simulator" → Load.
(El USB externo es para cargar juegos; este programa no lo usa.)

## Probar en PC antes (opcional)
- Dolphin (emulador) abre `boot.dol` directo: Archivo → Abrir.
- `host/host_test.c` compila el juego en PC y genera capturas/estadísticas:
  `gcc -O2 -I host -o host_test host/host_test.c -lm && ./host_test`

## Licencias
Fuente: Chakra Petch (SIL Open Font License 1.1). GRRLIB (MIT). libogc.
