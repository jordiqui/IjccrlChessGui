# Arquitectura

Objetivo: un framework tipo ChessGUI con núcleo headless reproducible y capas separadas.

## Capas obligatorias

- `core/` (`ijccrlcore`): lógica de UCI, torneos, openings, clocks, PGN, standings y broadcast.
- `cli/` (`ijccrlcli`): runner de consola para reproducir y depurar torneos.
- `gui/` (`ijccrlgui`): interfaz gráfica que consume el core (sin lógica duplicada).

Regla: la GUI llama al core, nunca al revés.

## Broadcast TLCS (modo fichero)

El core implementa `IBroadcastAdapter` y un adapter concreto `TlcsIniAdapter` que lee `server.ini`
para ubicar `TOURNEYPGN`. El flujo escribe PGN en `live.pgn` con reemplazo atómico.

## Salidas

- `out/tournament.pgn` (todas las partidas)
- `out/live.pgn` (partida actual)
- `out/results.json` (standings)
