# Building StarGenPlus

## Requirements

- Visual Studio 2026 Build Tools (Dev 18) with the C++ toolset
  (verified: MSVC 19.51.36260, Windows SDK 10.0.26100) — the presets use
  the `Visual Studio 18 2026` generator, so no developer shell is needed.
- CMake ≥ 3.24 (the Build Tools ship CMake 4.3.1 at
  `...\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe`;
  add it to PATH or call it by full path).
- Optional: Node.js for the MCP Inspector; Python 3.12+ for the SDK
  client test.

## Configure + build

```powershell
cmake --preset msvc2026                 # configure (x64, Debug+Release)
cmake --build --preset msvc2026-release # -> build\msvc2026-x64\Release\StarGen.exe
cmake --build --preset msvc2026-debug   # -> build\msvc2026-x64\Debug\StarGen.exe
```

Alternative toolchains: any CMake ≥ 3.24 with GCC/Clang
(`-std=c17 -Wall -Wextra -Werror` is enforced; link `m` where needed).

## AddressSanitizer profile

```powershell
cmake --preset msvc2026-asan        # own build dir; static-CRT-free: MSVC
cmake --build --preset msvc2026-asan-debug
ctest --preset msvc2026-asan-test   # PATH is set up for the ASan DLLs
```

Notes (why this is configured the way it is):

- MSVC's AddressSanitizer **requires the dynamic CRT** (the compiler
  emits `__imp_*` import thunks for intercepted CRT functions; static
  CRT linking fails with LNK2019).
- The ASan runtime DLL (`clang_rt.asan_dynamic-x86_64.dll`) and the
  debug CRT redist are not on the system PATH; `tests/CMakeLists.txt`
  injects the toolset bin dir and the `debug_nonredist` DebugCRT dir
  into the test processes' PATH.
- LeakSanitizer is not available on MSVC Windows; leak findings come
  from code audit, ASan covers corruption/UAF.

## Tests

```powershell
ctest --preset msvc2026-test-release   # or -debug / -asan-test
```

Suite: build identity, fixture integrity (Solar System template, incl.
the Callisto BUG-04 regression), in-process repeat determinism /
template immutability / dust-ratio restore, numeric formatting + value
sentinels + unit conversions, format escaping, CSV row-width layout
(plain + moons), and headless JSON smoke tests (validity,
reproducibility, strict argument validation).

## Run

```text
StarGen.exe -t -s42 -m1 -n1 -g        legacy text output (unchanged CLI)
StarGen.exe -x -M -g -s7              Solar System template with moons
StarGen.exe --json --seed 42 --mass-solar 1.0       headless JSON
StarGen.exe --list-stars-json --catalog dole        catalog listing
StarGen.exe --mcp                     MCP stdio server (docs/MCP-SERVER.md)
```

Legacy options are unchanged (see `-?`). Headless modes write exactly
one JSON document to stdout, nothing to stdout on failure, create no
files, and exit 0 (success) / 2 (usage) / 3 (failure).

## Warning policy

`/W4 /WX` (MSVC) and `-Wall -Wextra -Werror` (GCC/Clang) are enforced
for all project-owned code. There are no global CRT-suppression defines;
the only vendored third-party code is jsmn (MIT, `third_party/jsmn`).
