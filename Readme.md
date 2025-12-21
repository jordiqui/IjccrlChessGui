Aquí tienes un **README.md completo (copy/paste)** para que GitHub te deje crear el repo `jordiqui/IjccrlChessGui` y, a la vez, marque desde el minuto 1 el objetivo “ChessGUI-like”: **torneos + retransmisión web en tiempo real** (TLCS/TLCV/node-tlcv), con compilación en **Windows (MinGW64/clang++)** o ejecución híbrida con **VPS Ubuntu**.

> Está basado en lo que describe ChessProgramming sobre ChessGUI (broadcast vía TLCV) y en el README oficial de `jhonnold/node-tlcv` (config, `.env`, puerto 8080, panel `/admin`). ([chessprogramming.org][1])

---

````markdown
# IJCCRL Chess GUI (IjccrlChessGui)

**IjccrlChessGui** is an open, modern **engine-tournament framework + live broadcast pipeline** inspired by the historical “ChessGUI” concept:  
run engine matches locally, and **broadcast them in real time to a website**.

The end-goal is a practical, reproducible, open-source alternative to the closed “ChessGUI-style” broadcast stacks used by major engine leagues, but tailored for the IJCCRL ecosystem (testing, rating lists, and live events).

> ChessGUI is known for combining tournament management with **TLCV-based live broadcasting**. :contentReference[oaicite:1]{index=1}  
> `node-tlcv` is a web server implementation of Tom’s Live Chess Viewer designed for CCRL broadcasts, exposing an admin panel at `/admin` and serving on port 8080 by default. :contentReference[oaicite:2]{index=2}

---

## Project Vision

IjccrlChessGui aims to deliver:

- **Tournament runner** (UCI engines, time controls, openings, adjudication hooks)
- **Broadcast bridge** to Tom’s Live Chess ecosystem:
  - TLCS (Tom’s Live Chess Server) as the live game feed
  - node-tlcv as the web frontend (viewer + admin)
- **Windows-first workflow** (MinGW64 / clang++), with optional Ubuntu VPS hosting for the web viewer
- A clean, open, documented architecture so the community can reproduce and extend it

---

## High-Level Architecture

**Option A (Recommended: Hybrid, already proven in practice):**

Windows (tournament host)
- UCI engines runner (WinBoard / custom runner)
- TLCS connection from Windows (e.g., via WireGuard tunnel)
- Streams live games to the VPS

Ubuntu VPS (broadcast website)
- `node-tlcv` (web server + viewer + admin)
- Public web access: `https://your-domain/` (reverse proxy optional)

**Option B (All-in Ubuntu VPS):**
- Tournament runner on VPS
- TLCS + node-tlcv on VPS
- Suitable when engines and compute are on Linux and you want a single-host stack

---

## Current Status

This repository is being bootstrapped. The first milestone is to produce:

1. A minimal **Windows-native tournament runner** that emits/relays games to TLCS
2. A working **node-tlcv** instance on a VPS that renders those games in real time
3. Clear step-by-step docs and reproducible scripts

---

## Repository Layout (planned)

- `src/`
  - Core tournament framework (C++): engine manager, clocks, pairing, PGN writer
  - TLCS bridge module (socket protocol integration)
- `tools/`
  - Helpers (openings, PGN validation, log parsing, etc.)
- `examples/`
  - Example configs (2 engines, 4 engines, round-robin, gauntlet)
- `docs/`
  - Setup guides (Windows, VPS Ubuntu, WireGuard tunnel, reverse proxy)
- `third_party/`
  - Notes and integration references (without bundling incompatible licenses)

---

## Build Targets

### Windows (MinGW64 / clang++)

Primary target: compile and run the tournament framework **natively on Windows**.

Planned toolchains:
- MSYS2 MinGW64 (GCC/Clang)
- clang++ (Windows) with lld where appropriate

> We will provide “copy-paste ready” commands and scripts once the first buildable milestone lands.

### Ubuntu VPS (node-tlcv)

The web viewer is handled by `node-tlcv` (separate upstream), typically deployed on a VPS.

Upstream reference for running your own node-tlcv: :contentReference[oaicite:3]{index=3}

---

## Quick Start (Viewer on VPS with node-tlcv)

This is the standard `node-tlcv` flow (upstream). It will be mirrored/adapted in `docs/` once our integration scripts are added.

1) Install dependencies
- Node.js (and `pm2` if you want background running)

2) Create `config/config.json`:
```json
{ "connections": ["<host>:<port>"] }
````

3. Create `.env` next to the README:

```bash
TLCV_PASSWORD=your_password_here
```

4. Build and run:

```bash
npm install && npm run build
node build/src/main.js
# or
pm2 start build/src/main.js
```

Admin panel:

* URL: `/admin`
* User: `admin`
* Password: `TLCV_PASSWORD`

(Port defaults to 8080 in upstream docs.) ([GitHub][2])

---

## Roadmap

### Milestone 1 — “Broadcastable Minimal Runner”

* Launch 2 UCI engines
* Run continuous games at fixed TC
* Emit PGN + live updates
* TLCS bridge: push live game state to the viewer stack

### Milestone 2 — “Tournament Modes”

* Round-robin, gauntlet, swiss (as practical)
* Opening suites (PGN/EPD)
* Basic adjudication hooks (TB-assisted where feasible)

### Milestone 3 — “Production Broadcast”

* Robust reconnection
* Crash-proof match supervision
* Clean logs + metrics
* Reverse proxy templates (Nginx)
* “One-command” deployment scripts

### Milestone 4 — “Web Polish”

* Viewer customization
* Branding for IJCCRL
* Embeddable widgets and tournament pages

---

## Why this exists

Some major engine leagues have broadcast tooling that is not shared publicly.
IJCCRL has a strong open-source ethos; this project is intended to be **transparent, reproducible, and extensible** for the wider community.

---

## Credits / References

* ChessProgramming Wiki: ChessGUI overview and its TLCV broadcast concept ([chessprogramming.org][1])
* `jhonnold/node-tlcv`: Tom’s Live Chess Viewer server implementation ([GitHub][2])

---

## Contributing

Contributions are welcome:

* Protocol notes for TLCS/TLCV integration
* Windows build scripts (MSYS2, clang++)
* Tournament features (pairing, adjudication, openings)
* Documentation improvements

Please open an Issue describing:

* your platform (Windows / Ubuntu)
* your toolchain
* a minimal repro / goal

---

## License

TBD (this repository will choose a permissive license unless a strong reason arises).
Upstream components keep their own licenses; this repo will not relicense third-party code.

---

## Maintainer

Jorge Ruiz Centelles (jordiqui)
IJCCRL — International Jorge Chess Computer Rating List

```

---

Si quieres, en el siguiente paso te dejo **la estructura inicial de carpetas + ficheros vacíos** (y un `docs/00-architecture.md`) para que el repo no nazca “solo con README”, sino con el esqueleto exacto del framework IJCCRL (y listo para que Codex empiece a commitear).
::contentReference[oaicite:7]{index=7}
```

[1]: https://www.chessprogramming.org/ChessGUI?utm_source=chatgpt.com "ChessGUI"
[2]: https://github.com/jhonnold/node-tlcv?utm_source=chatgpt.com "jhonnold/node-tlcv: Tom's Live Chess Viewer for CCRL ..."
