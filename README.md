# StarGenPlus

A modernized fork of **StarGen**, Jim Burrows' (Brons / Eldacur
Technologies) classic random planetary-system generator — accretion model
in the Dole/Fogg lineage, C17, building with Visual Studio 2026 and
GCC/Clang, plus a headless JSON mode and a dual-era **MCP stdio server**
for agents.

All generated output is **synthetic, model-based fiction**, not modern
astronomical prediction. The upstream program's CLI behavior is
preserved; see `ReadMe.txt` and `Credits.txt` (untouched upstream docs)
for the original program's story.

## Quick start

```powershell
cmake --preset msvc2026
cmake --build --preset msvc2026-release
ctest --preset msvc2026-test-release
```

Requires Visual Studio 2026 Build Tools + CMake ≥ 3.24 (details, ASan
profile, and GCC/Clang notes: [docs/BUILDING.md](docs/BUILDING.md)).

## Using it

```text
StarGen.exe -t -s42 -m1 -n1 -g        legacy text output (unchanged CLI)
StarGen.exe -x -M -g -s7              Solar System template with moons
StarGen.exe --json --seed 42 --mass-solar 1.0       headless JSON
StarGen.exe --list-stars-json --catalog dole        catalog listing
StarGen.exe --mcp                     MCP stdio server
```

- MCP server (tools, limits, host configuration samples):
  [docs/MCP-SERVER.md](docs/MCP-SERVER.md)
- JSON schema, units, value/status policy:
  [docs/JSON-SCHEMA.md](docs/JSON-SCHEMA.md)
- Corrected CSV layout: [docs/CSV-FORMAT.md](docs/CSV-FORMAT.md)
- What changed vs upstream 1.43 and why: [PLAN.MD](PLAN.MD)

## Repository layout

```text
src/engine/    simulation core (accretion, environment, fixtures)
src/export/    output formats (text/CSV/HTML/SSC/SVG, JSON export)
src/mcp/       MCP stdio server + worker subprocess plumbing
src/app/       CLI entry point + headless one-job driver
tests/         CTest suite + recorded baseline fixtures
docs/          build/CLI/schema/MCP/CSV documentation
third_party/   vendored third-party code (jsmn, MIT)
legacy/        frozen VS6/CodeWarrior-era build artifacts (not built)
ref/, help/, html/   upstream image assets and data dirs (used by HTML output)
```

## Status

Modernization phases A–F of [PLAN.MD](PLAN.MD) are implemented: CMake
build, bug fixes (CSV precision, sentinel leaks, pressure units, the
Callisto fixture typo, exporter contracts), headless JSON, and the MCP
server — verified by a 9-test CTest suite across Release/Debug/ASan and
the official MCP Python SDK client.
