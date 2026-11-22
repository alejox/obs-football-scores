# AC Football Scores

Plugin nativo para OBS Studio que muestra **un partido de fútbol** con su marcador en vivo, los escudos de los equipos y la bandera de la liga.

En las propiedades de la fuente eliges **Liga → Partido** (p. ej. *LaLiga → Real Madrid vs Barcelona*) y la fuente muestra solo ese partido: `[LaLiga 🇪🇸]  Real Madrid [escudo]  2 - 1  [escudo] Barcelona   67'`.

[English](README.en.md)

## Funciones

- **Liga → Partido**: 30 competiciones (LaLiga, Premier, Serie A, Bundesliga, Ligue 1, Champions, Libertadores, Liga BetPlay, Liga MX, Liga Profesional Argentina, Brasileirão, MLS, Mundial, Nations League…).
- **En vivo ahora**: la opción «🔴 En vivo ahora (todas las ligas)» lista solo los partidos que se están jugando en este momento en cualquier competición que cubra ESPN, con el nombre de su liga.
- **Lista limpia**: la lista de partidos solo muestra los que están en vivo y los próximos; los terminados desaparecen.
- **Partido fijo o automático**: elige un partido concreto, o «Automático» para mostrar el partido en vivo de la liga (si no hay, el último resultado durante unas horas y luego el próximo).
- **Marcador en vivo**: minuto, descanso y final; antes del partido, la hora local y el día. Se actualiza cada 15 s durante el partido.
- **Escudos y banderas**: escudo de cada equipo (la bandera en partidos de selecciones), logo de la liga y bandera del país.
- **Panel de fútbol (cancha en vivo)**: fuente aparte que dibuja cada jugada del partido en la cancha (remates, goles, córners, faltas, fueras de juego) con una barra de presión; también alineaciones en la cancha, estadísticas y tabla de posiciones.
- **Sonido de gol**: uno incluido (bocina y público) o el tuyo (WAV, MP3 o FLAC), con volumen; y opcionalmente una fuente multimedia de OBS que se reproduce con cada gol.
- **Animación de gol**: banner con «¡GOOOL!», el goleador y su foto cuando ESPN la tiene (si no, el escudo). Botón y atajo de teclado para probarla.
- **6 diseños**: Clásico (barra delgada), Estadio (escudos que sobresalen de la barra), Choque (colores de los equipos en diagonal), Tarjeta (para una esquina), Flotante (escudos grandes encima del marcador, fondo transparente) y Limpio (solo escudos y marcador, sin fondo). Los diseños con escudos grandes mantienen su proporción: solo eliges el ancho.
- **Se actualiza solo**: al abrir OBS revisa (una vez al día) si hay versión nueva, muestra sus novedades y la instala al reiniciar OBS. También en **Herramientas → AC Football Scores: Buscar actualizaciones**.
- **Estilos** del diseño Clásico: Minimalista, Intenso y Neón; color de acento, color de texto, nombres completos, cortos o abreviados (RMA, FCB), y tamaño libre.

## Instalación

Descarga el archivo de tu sistema desde [Releases](https://github.com/alejox/obs-football-scores/releases) y sigue `INSTALL.txt`:

- **Windows**: cierra OBS y extrae la carpeta `obs-football-scores` en `C:\ProgramData\obs-studio\plugins\`.
- **macOS** (Apple Silicon, macOS 13+): cierra OBS y copia `obs-football-scores.plugin` en `~/Library/Application Support/obs-studio/plugins/`. No está notarizado: si macOS lo bloquea, ejecuta `xattr -dr com.apple.quarantine ~/Library/Application\ Support/obs-studio/plugins/obs-football-scores.plugin`.
- **Linux**: `tar -xzf obs-football-scores-*-linux-x86_64.tar.gz -C ~/.config/obs-studio/plugins`

Desde la 0.2.0 las siguientes versiones se instalan solas desde OBS.

Luego en OBS: **Fuentes → + → Marcador de fútbol**.

## Datos

Marcadores, escudos y banderas vienen de la API pública de ESPN (`site.api.espn.com`, `a.espncdn.com`). No necesita clave ni cuenta. Es una API no oficial: si ESPN la cambia, el plugin puede dejar de leer partidos hasta una actualización.

## Compilar

Igual que los demás plugins de alejox (plantilla obs-plugintemplate):

```bash
cmake --preset linux-x86_64        # o windows-x64 / macos
cmake --build --preset linux-x86_64
```

Tests del núcleo (sin OBS ni Qt):

```bash
cmake -S tests -B build_tests
cmake --build build_tests
ctest --test-dir build_tests --output-on-failure
```

## Estructura

| Carpeta | Qué hay |
| --- | --- |
| `src/core` | Modelo del partido, lector de ESPN, catálogo de ligas, selección automática, detección de goles. Sin OBS ni Qt, con tests. |
| `src/feed` | Hilo que consulta ESPN, listado mensual de la liga y descarga de escudos. |
| `src/render` | Dibujo del marcador con QPainter. |
| `src/obs` | La fuente de OBS y los servicios compartidos. |
| `src/net` | HTTPS con la pila del sistema (WinHTTP, NSURLSession, libcurl). |

## Publicar una versión

1. Agrega la sección `## <versión>` a `CHANGELOG.md` (con `### Español` y `### English`): es lo que el aviso de actualización muestra.
2. Sube la versión en `buildspec.json` y súbelo a `main`. El workflow **Release** compila Windows, macOS y Linux y publica la Release `v<versión>`.

¿Te sirve? [Invítame un café en Ko-fi](https://ko-fi.com/alejoxcode).

El sonido de gol incluido (`data/sounds/goal.wav`) está generado por `tools/make_goal_sound.py`, sin audio de terceros. La lectura de WAV, MP3 y FLAC usa [dr_libs](https://github.com/mackron/dr_libs) (dominio público).

Licencia GPL-2.0, como OBS Studio.
