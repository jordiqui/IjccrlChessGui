# MVP Test Checklist (Bloque 2)

1. Arrancar TLCS con `server.ini` en modo ICS y puerto 16000.
2. Verificar que `TOURNEYPGN` está definido en el `server.ini` y apunta a `live.pgn`.
3. Ejecutar `ijccrlcli.exe config\example.runner.json`.
4. Confirmar que `live.pgn` cambia tras cada jugada (se actualiza de forma atómica).
5. Confirmar que TLCS detecta el fichero y que `node-tlcv` muestra la partida en vivo.
