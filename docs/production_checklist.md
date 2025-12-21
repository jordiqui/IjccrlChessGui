# Production readiness checklist

## Manual checks

1. Arrancar un torneo con salida en `out/`.
2. Matar un motor desde Task Manager → debe reiniciar y seguir.
3. Cerrar la GUI en medio del torneo → reabrir y usar “Resume Tournament” → debe continuar.
4. Cortar TLCS (cerrar `tlc_server`) → el torneo sigue; al reabrir TLCS vuelve a mostrar `live.pgn`.

## Mini auto checks

- Checkpoint save/load roundtrip (serialización consistente).
