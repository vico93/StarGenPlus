# Phase A baseline fixtures (StarGenPlus fork)

These files record the **first successful modern-toolchain build of the pristine
upstream source (revision 1.43)**, before any substantive change. They are the
reference point for Phase B/C: behavior-changing fixes (e.g. the Callisto
initializer typo, BUG-04) are *expected* to change some of these outputs, and
each such change must be documented when it happens.

Reproducibility contract: these fixtures are valid **for this compiled build
only** (same compiler, same CRT, same source revision). Cross-compiler or
cross-version byte equality is not a project requirement (PLAN.MD §5).

## Build identity

Recorded by `tests/buildinfo.c` (see `baseline-buildinfo-MSVC19.51-x64.txt`):

| Item | Value |
| --- | --- |
| Toolchain | Visual Studio 2026 Build Tools 18.11, MSVC 19.51.36260 (`_MSC_VER` 1951), Windows SDK 10.0.26100 |
| Language | `/std:c17` (`__STDC_VERSION__` 201710), CMake preset `msvc2026`, x64 |
| `long double` | 8 bytes, `LDBL_MANT_DIG` 53, `LDBL_DIG` 15, `DECIMAL_DIG` 17 (**MSVC long double == double**) |
| PRNG | CRT `rand()`, `RAND_MAX` 32767 |
| `sizeof(long)` | 4 (LLP64 — relevant to seed handling on x64) |

`StarGen.exe` (Release) SHA-256 at fixture time:
`07A448800D55AEF52CD4B33F2E5C6286A128DB6148CFF983C46583B06470B67D`

## How the fixtures were produced

Run in a scratch working directory (`tests/tmp/`) with the Release exe, so the
program's file outputs (default `html\` under the CWD) never touch repo data:

```
StarGen.exe -z                         >  baseline-sizecheck.txt
StarGen.exe -t -s42 -m1 -n1 -g        >  baseline-seed42-text.txt        (stderr empty)
StarGen.exe -t -s48 -W5 -n1 -g -v10   >  baseline-seed48-W5-text.txt (+ .stderr)
StarGen.exe -e -s42 -m1 -n1           >  html\StarGen.csv -> baseline-seed42-StarGen.csv
```

All exit codes were 0.

## Observations recorded with this build

1. **BUG-01 reproduced**: seed-42 text shows `1.166 Earth masses` for planet 2,
   while the same body's CSV `mass`/`dust_mass`/`gas_mass` are `0.000`
   (`%5.3Lf` of a ~3e-6 solar-mass value). Also visible for every planet.
2. **BUG-02 reproduced in the MSVC build's dialect**: gas-giant surface fields
   export the literal token `inf` (the legacy 1.34 binary exported `1.#IO`).
   Same sentinel-leak class, different renderer.
3. **`-e ... --` is a silent no-op in source 1.43**: with stdout requested,
   `stargen.c` only wires `sgOut` for `ffCSVdl` (`-C`), and its null-check
   guard deliberately exempts `ffCSV`+stdout, so plain `-e` with `--` writes
   nothing and exits 0. The original 1.34 executable's documented behavior
   ("CSV is written to a file") differs; noted as a source-vs-1.34 discrepancy.
   File-based CSV (`-e` without `--`) works and is what the fixture records.
4. **Silent `exit(0)` on file-open failure confirmed live**: running `-e` in a
   directory without `html\` produced no file, no stderr, exit 0.
5. With no `html/`/`ref/` content in this fork (the `html/` and `help/`
   directories are empty), text/CSV modes still work; HTML mode would want
   those data dirs.

## Files

| File | Producer |
| --- | --- |
| `baseline-buildinfo-MSVC19.51-x64.txt` | `tests/buildinfo.c` output |
| `baseline-sizecheck.txt` | `StarGen.exe -z` |
| `baseline-seed42-text.txt` | `StarGen.exe -t -s42 -m1 -n1 -g` (stdout) |
| `baseline-seed48-W5-text.txt` | `StarGen.exe -t -s48 -W5 -n1 -g -v10` (stdout) |
| `baseline-seed48-W5-text.stderr.txt` | stderr of the above (verbose gas diagnostics) |
| `baseline-seed42-StarGen.csv` | `StarGen.exe -e -s42 -m1 -n1` -> `html\StarGen.csv` |
