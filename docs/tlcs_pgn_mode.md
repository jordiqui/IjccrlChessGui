# TLCS PGN mode (TOURNEYPGN)

TLCS v1.6a lee el PGN en vivo desde `server.ini` usando `TOURNEYPGN=...`.

## Configuración de ejemplo

```
TOURNEYPGN=C:\WinBoard-4.4.0\PSWBTM\pgn\live.pgn
SITE=ijccrl
PORT=16000
ICSMODE=1
SAVEDEBUG=0
```

## Escritura segura (anti-corrupción)

Cada cambio de PGN escribe el fichero completo de forma atómica:

1. Escribir a `live.pgn.tmp` en el mismo directorio.
2. Flush/close del archivo.
3. Reemplazo atómico:
   - Windows: `MoveFileEx(tmp, live.pgn, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)`

El core implementa esta lógica dentro del `TlcsIniAdapter`.
