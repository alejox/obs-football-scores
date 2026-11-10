# OBS Football Scores

Plugin nativo para OBS Studio que muestra **un partido de fútbol** con su marcador en vivo, los escudos de los equipos y la bandera de la liga.

En las propiedades de la fuente eliges **Liga → Partido** (p. ej. *LaLiga → Real Madrid vs Barcelona*) y la fuente muestra solo ese partido: `[LaLiga 🇪🇸]  Real Madrid [escudo]  2 - 1  [escudo] Barcelona   67'`.

[English](README.en.md)

## Funciones

- **Liga → Partido**: 30 competiciones (LaLiga, Premier, Serie A, Bundesliga, Ligue 1, Champions, Libertadores, Liga BetPlay, Liga MX, Liga Profesional Argentina, Brasileirão, MLS, Mundial, Nations League…).
- **Partido fijo o automático**: elige un partido concreto, o «Automático» para mostrar el partido en vivo de la liga (si no hay, el último resultado durante unas horas y luego el próximo).
- **Marcador en vivo**: minuto, descanso y final; antes del partido, la hora local y el día. Se actualiza cada 15 s durante el partido.
- **Escudos y banderas**: escudo de cada equipo (la bandera en partidos de selecciones), logo de la liga y bandera del país.
- **Animación de gol**: banner con el escudo, «¡GOOOL!» y el goleador. Botón y atajo de teclado para probarla.
- **Estilos** Minimalista, Intenso y Neón; color de acento, color de texto, nombres completos, cortos o abreviados (RMA, FCB), y tamaño libre.

## Instalación

Descarga el archivo de tu sistema desde [Releases](https://github.com/alejox/obs-football-scores/releases) y sigue `INSTALL.txt`:

- **Windows**: cierra OBS y extrae la carpeta `obs-football-scores` en `C:\ProgramData\obs-studio\plugins\`.
- **Linux**: `tar -xzf obs-football-scores-*-linux-x86_64.tar.gz -C ~/.config/obs-studio/plugins`

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

Licencia GPL-2.0, como OBS Studio.
