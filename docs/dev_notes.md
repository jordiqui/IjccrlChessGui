# Developer Notes

## TLCS feed path normalization checks

Suggested verification cases for `tlcs_feed` path comparisons:

- `C:\X\Y\file.txt` vs `C:/X/Y/file.txt` => equivalent.
- `C:\X\Y\..\Y\file.txt` vs `C:/X/Y/file.txt` => equivalent.
- `PATH="C:\X\Y\file.txt"` parses correctly and matches `C:/X/Y/file.txt`.

Manual validation (Windows):

- With `server.ini` containing `PATH=C:\...` and JSON configured as `C:/...`, the runner should start without aborting.
- The TLCS feed file (`TLCV_File.txt`) and `tournament.pgn` should be created/updated.
