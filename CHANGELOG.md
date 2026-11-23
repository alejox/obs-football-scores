# Changelog

Each `## <version>` section becomes the description of that release, and the plugin's updater shows
its items before installing (the Spanish ones when OBS is in Spanish, the English ones otherwise).

## 0.3.2

### Español

- **Nuevo:** fuente «Fútbol · Goles del partido»: los goleadores de cada equipo con su minuto, el local a la izquierda y el visitante a la derecha, con escudo y nombre. Marca los penales y los goles en propia puerta. Pensada para ponerla debajo del marcador.

### English

- **New:** "Football · Match goals" source: each team's scorers with the minute, home on the left and away on the right, with crest and name. Penalties and own goals are marked. Made to sit under the scoreboard.

## 0.3.1

### Español

- **Nuevo:** diez fuentes nuevas, cada una por separado en «Fuentes → +»: Cancha en vivo, Alineaciones en la cancha, Estadísticas del partido, Tabla de posiciones, Relato en vivo, Momentum, Línea de tiempo, Previa del partido, Goleadores de la liga y Resultados de la jornada.
- **Nuevo:** casilla «Conectar con las demás fuentes de fútbol»: con ella activada, al cambiar la liga o el partido en una fuente cambian todas las conectadas; desactívala en la fuente que deba seguir otro partido. El marcador ahora se llama «Fútbol · Marcador».
- **Nuevo en la cancha:** recorrido del balón entre jugadas: una línea une las últimas jugadas y el balón viaja de una a la siguiente cuando llega una nueva. Es una aproximación entre jugadas registradas, no el recorrido real; se puede apagar en las propiedades, y el botón «Probar el movimiento del balón» lo muestra sin esperar a un partido en vivo.
- **Cancha en vivo:** cada remate, gol, córner, falta y fuera de juego dibujado donde ocurrió, la última jugada con su etiqueta y un cono hacia el arco, y las estadísticas clave debajo. Vista en planta o en perspectiva de estadio. El resultado va con la fuente «Marcador» y sus diseños de siempre, y el momentum es una fuente aparte.
- **Relato en vivo** en español: «¡Atajada! Remate de… atajado abajo a la derecha por…».
- Los partidos en descanso, detenidos o en prórroga cuentan como en vivo en todas las listas.

### English

- **New:** ten new sources, each one separate under "Sources → +": Live pitch, Lineups on the pitch, Match stats, League table, Live commentary, Momentum, Match timeline, Match preview, League top scorers and Today's results.
- **New:** "Link with the other football sources" checkbox: when on, changing the league or the match in one source changes every linked one; turn it off on a source that should follow another match. The scoreboard is now called "Football · Scoreboard".
- **New on the pitch:** the ball's route between plays: a line joins the latest plays and the ball travels from one to the next when a new one arrives. It is an approximation between recorded plays, not the real path; it can be turned off in the properties, and the "Test the ball movement" button shows it without waiting for a live match.
- **Live pitch:** every shot, goal, corner, foul and offside drawn where it happened, the latest play tagged with a wedge towards the goal, and the key stats below it. Top-down or stadium perspective view. The score comes from the "Scoreboard" source with its usual designs, and the momentum is a source of its own.
- **Live commentary** in Spanish when OBS is in Spanish.
- Matches at half time, stopped or in extra time count as live in every list.

## 0.2.2

### Español

- **Nuevo:** sonido de gol. Viene uno incluido (bocina de estadio y público) y puedes poner el tuyo (WAV, MP3 o FLAC), con control de volumen.
- **Nuevo:** «Además, reproducir esta fuente al marcar gol»: elige cualquier fuente multimedia (un clip o un sonido en otro formato) y se reproduce con cada gol.
- El sonido sale por la propia fuente: aparece en el mezclador de OBS y se escucha en el directo y en tus auriculares.
- «Probar animación de gol» también prueba el sonido.

### English

- **New:** goal sound. A built-in one (stadium horn and crowd), or your own (WAV, MP3 or FLAC), with a volume control.
- **New:** "Also play this source on a goal": pick any media source (a clip, or a sound in another format) and it plays on every goal.
- The sound comes out of the source itself: it shows in the OBS mixer and is heard on stream and in your headphones.
- "Test goal animation" tests the sound too.

## 0.2.1

### Español

- Las propiedades ya no muestran el aviso sobre el origen de los datos.

### English

- The properties no longer show the notice about where the data comes from.

## 0.2.0

### Español

- **Nuevo:** 6 diseños para elegir en la fuente: Clásico, Estadio, Choque, Tarjeta, Flotante (escudos encima del marcador, fondo transparente) y Limpio (sin fondo).
- **Nuevo:** «🔴 En vivo ahora (todas las ligas)» lista solo los partidos que se están jugando en este momento, en cualquier liga.
- La lista de partidos solo muestra los en vivo y los próximos; los terminados desaparecen.
- La animación de gol muestra la foto del goleador cuando ESPN la tiene.
- **Nuevo:** el plugin se actualiza solo y este aviso muestra qué trae cada versión.
- Botón «¿Te sirve? Invítame un café» al final de las propiedades.

### English

- **New:** 6 designs to choose from in the source: Classic, Stadium, Clash, Card, Floating (crests above the score, transparent background) and Clean (no background).
- **New:** "🔴 Live now (all leagues)" lists only the matches being played right now, in any league.
- The match list only shows live and upcoming matches; finished ones drop out.
- The goal animation shows the scorer's photo when ESPN has one.
- **New:** the plugin updates itself and this notice shows what each version brings.
- "Is it useful? Buy me a coffee" button at the end of the properties.

## 0.1.0

### Español

- Primera versión: marcador de fútbol en vivo con escudos y banderas, Liga → Partido, animación de gol y estilos Minimalista, Intenso y Neón.

### English

- First version: live football scoreboard with crests and flags, League → Match, goal animation and Minimal, Bold and Neon styles.
