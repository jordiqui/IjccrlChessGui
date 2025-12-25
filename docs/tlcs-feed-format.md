# TLCS feed format (TLCV_File.txt)

This document describes the exact TLCS feed file format that `node-tlcv` parses and that
IjccrlChessGui emits when `broadcast.adapter = "tlcs_feed"`.

## TLCS compatibility hard rules

When emitting the `tlcv` feed, the writer obeys the following TLCS-specific requirements:

1. Every update performs a **full snapshot rewrite** of the feed file (truncate + write all
   lines) using ASCII text, `\r\n` line endings, and a trailing `\r\n` after the last line.
2. The feed file is rewritten **in place**. Atomic renames or temp-file swaps are avoided so
   TLCS always observes updates on the same path.
3. After each write, the file handle is flushed and closed; no handles are kept open between
   updates.

## Parser reference (node-tlcv)

The `node-tlcv` server listens for TLCS/TLCV UDP packets and parses each line with:

- `src/server/handler.ts` (command definitions and field parsing)
- `src/server/util/string.ts` (`splitOnCommand`, splits at the first space or `:`)

Key takeaways from the parser:

- Commands are the token before the first space or `:` (e.g., `FEN`, `WMOVE`, `result`).
- `WMOVE`/`BMOVE` accept permissive move strings (SAN or coordinate notation like `e2e4`).
- `FEN` lines expect the first **four** FEN fields (piece placement, side-to-move, castling, en-passant).
- `FMR` is the halfmove clock, combined with the FEN and move number to validate a position.

## TLCS feed commands used by IjccrlChessGui

Each line is a single command. Lines are terminated with `\r\n` when written.

### `SITE`
```
SITE <string>
```

Sets the site/event name used by the viewer.

### `WPLAYER` / `BPLAYER`
```
WPLAYER <white_name>
BPLAYER <black_name>
```

Updates the player names.

### `FEN`
```
FEN <placement> <stm> <castling> <ep>
```

Only the first four fields of the FEN are supplied. The viewer later reconstructs a full FEN
using the latest `FMR` and the current move number.

### `FMR`
```
FMR <halfmove_clock>
```

Sets the halfmove clock used by the viewer to validate the next `FEN`.

### `WMOVE` / `BMOVE`
```
WMOVE <move_number>. <move>
BMOVE <move_number>... <move>
```

`<move>` can be SAN **or** coordinate notation (e.g., `e2e4`, `e7e8q`). The parser uses
chess.js’ permissive move parser (`strict: false`).

### `result`
```
result <score>
```

Sets the game result. Valid scores include `1-0`, `0-1`, `1/2-1/2`, or `*`.

## Emission order used by IjccrlChessGui

At a minimum, the writer emits:

1. `SITE` (if available)
2. `WPLAYER`, `BPLAYER`
3. `FMR`, `FEN` for the initial position
4. For each move:
   - `WMOVE` or `BMOVE`
   - `FMR`, `FEN` backup
5. `result` at game end (after a final `FMR`/`FEN` backup)

This mirrors the expectations of `node-tlcv`’s UDP parser and enables the TLCV viewer
to replay the game in real time.
