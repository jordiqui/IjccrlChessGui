# Build en Windows

Requisitos:

- CMake >= 3.16
- Compilador C++17 (MSYS2 MinGW64, clang++ o MSVC)

## Comandos estándar

```bash
cmake -S . -B build
cmake --build build --config Release
```

## Salidas esperadas

- `ijccrlcli` (runner consola)
- `ijccrlgui` (stub GUI)
- `ijccrlcore` (librería core)
