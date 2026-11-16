# legacy/ — frozen historical build artifacts

These files built StarGen with the toolchains of its era. They are kept
for reference and are **not** part of the modern CMake build; their
internal paths assume the pre-2026 source layout (sources in the
repository root) and they predate the new modules entirely.

| File | Era |
| --- | --- |
| `Makefile` | Unix / Mac OS X gcc make build |
| `StarGen.dsp`, `StarGen.dsw` | Visual C 6 project/workspace (Windows CLI; the `.dsp` omits `main.c` and references an absent `propert.h`) |
| `StarGen.prj`, `StarGen.r` | Metrowerks CodeWarrior (Classic Mac OS X / 68K & PPC) |
| `mac_prefix.h` | Mac prefix header used by the CW build |
| `types.h` | Apple "Types.h" — Macintosh system-header copy shipped for the THINK_C/CW paths (the only reference is `Dumas.c`'s dead `THINK_C` branch) |
| `Stargen.rc`, `resource.h` | WinStarGen GUI resources (the CLI compiles no resources) |

Pristine, pre-reorganization copies of all of these live in git at the
baseline commit (`d7f0db0` — "Baseline: pristine StarGen 1.43 upstream
source drop"); use `git show d7f0db0:Makefile` etc. to view them exactly
as supplied.
