# StarGen modernization and MCP integration handoff

Date: 2026-11-15

> **ADDENDUM (2026-11-16, implementation delivered).** The plan was
> executed through Phase E with one agreed deviation and the following
> deltas:
>
> 1. **MCP architecture deviation (owner-approved):** §1/§7 recommended a
>    Python SDK adapter; the owner explicitly requested a `--mcp` flag on
>    the program itself. The delivered system is a **C-native dual-era
>    MCP stdio server inside the same executable**, which spawns fresh
>    `--json` worker processes per generation — preserving this plan's
>    safety rules (fresh process per system, worker timeouts, crash
>    isolation, bounded output). PLAN §7's limits (10 s worker / 30 s
>    search / 1000 attempts / 10 results) are implemented with
>    environment overrides. MCP revision support was updated to the
>    current **2026-07-28** spec (per-request `_meta` versioning,
>    mandatory `server/discover`, `-32022`) plus the legacy
>    initialize-handshake era (≤ 2025-11-25), and was verified with the
>    official Python SDK client (`tests/test_mcp_client.py`).
> 2. **Phase completion:** A (CMake/MSVC 2026 x64 baseline + fixtures),
>    B (B1 mechanical modernization warning-clean at /W4 /WX; B2 BUG-04
>    Callisto fix + audit; B3 ownership/template/state; B4 exit codes +
>    stream ownership; B5 /WX + ASan profile + repeat-call tests),
>    C (C1 shared helpers; C2 CSV; C3 text; C4 HTML/SSC/SVG), D (headless
>    `--json` + `--list-stars-json`, schema_version 1), E (MCP server,
>    dual-era, three tools, worker manager, cancellation), F (docs:
>    docs/BUILDING.md, docs/JSON-SCHEMA.md, docs/MCP-SERVER.md,
>    docs/CSV-FORMAT.md) are all implemented and committed; see git log
>    for the per-phase commits and evidence.
> 3. **Documented legacy quirks preserved:** `-e ... --` remains a
>    silent no-op (source-1.43 behavior, documented in
>    docs/CSV-FORMAT.md); exit codes for success paths unchanged.
> 4. **Not done (by scope agreement):** licensing investigation; no
>    publishing/installing; no changes to the original
>    `D:\Apps\WinStarGen` installation.
> 5. **Repository reorganization (2026-11-16):** sources moved to a
>    grouped `src/{engine,export,mcp,app}` layout and the frozen
>    VS6/CW-era build files to `legacy/`; source-file references in the
>    body below (e.g. `stargen.c:113`) describe the pre-reorganization
>    root layout and remain valid via `git log --follow`.

## 1. Owner's request and chosen approach

Implement this work in the owner's NEW FORK DIRECTORY, not in the original
`D:\Apps\WinStarGen` installation. This document is a handoff specification;
no implementation, bug fixes, or modern compiler build have been performed yet.

The owner wants:

1. An agent-friendly MCP interface to StarGen's generated systems.
2. The output bugs described below fixed in the other supported output formats,
   not just worked around in the new JSON format.
3. Source modernization so the program builds with a modern Microsoft Visual
   C/C++ toolchain, as well as retaining a portable C build where practical.

**Chosen architecture: a C17 StarGen executable with a headless JSON mode, plus a
small SDK-based MCP adapter.** Keep the simulation in C. A Microsoft Visual C/C++
toolchain can compile C using `/TC` and `/std:c17`; a wholesale C++ rewrite is not
required or recommended.

```text
MCP host / agent
      |
      | MCP over stdio
      v
Small Python MCP server using the official SDK
      |
      | validated arguments; a fresh child process
      v
Forked StarGen.exe --json
      |
      | one complete JSON result on stdout; diagnostics on stderr
      v
Schema validation, compact summaries, and bounded searches
```

The adapter is a wrapper around the ENHANCED fork, not a parser for the old
executable's HTML/CSV/text. A `--mcp` convenience launcher may be added later, but
implementing the MCP wire protocol inside the legacy C engine is not a first-stage
requirement.

Recommended adapter language: Python, with the official MCP SDK and pinned
dependencies. Follow the current SDK documentation and verify compatibility with
the target host. Do not copy obsolete SDK APIs or hand-code a fixed protocol
handshake. TypeScript with the official SDK is an acceptable owner-approved
substitution if the fork already standardizes on Node.

Licensing/legal investigation is explicitly outside the requested work. Preserve
existing notices and credits; do not silently replace them with a different
license. Do not publish, push, deploy, or install global tooling without the
owner's authorization.

## 2. Evidence, baseline, and version boundary

### Two different versions were supplied

- Original CLI: `D:\Apps\WinStarGen\StarGen.exe`, upstream revision **1.34**.
- Supplied source: `D:\Apps\WinStarGen\src\`, `stargen.c` revision **1.43**.
- The original executable is native PE32/x86, 208,896 bytes. Its `-z` diagnostic
  reports an 8-byte `long double`.
- The new fork is a newly compiled engine. Do NOT claim numerical equivalence to
  executable 1.34 or use its random outputs as unconditional 1.43 regression
  expectations.

SHA-256 values recorded before implementation:

| Artifact | SHA-256 |
| --- | --- |
| `StarGen.exe` | `d476523ee3dea7f206435097cc12bc0db7bc8fc288e9e89b6ed92c427c6ef17c` |
| `src/stargen.c` | `e89be9db5c2d0005e7c079cdedb506be9f5121c760bfef8c6a6ffbe417e0260f` |
| `src/display.c` | `3ac49342c1525dc5e0159b56d4e613f890de0580b43951b4212518bd35f96708` |

Source references below use the supplied `src/` layout and original line numbers.
Adjust paths if the new fork has a different layout; use function names as the
durable references after edits.

### Observed executable-1.34 behavior

| Invocation / mode | Observation |
| --- | --- |
| No arguments or `-?` | Usage on stderr, exit code 1. |
| `-w -l` | Catalog on stdout: Sol at index 0, followed by 51 nearby stars. |
| `-t -s42 -m1 -n1 -g` | 11 planets on stdout; repeated tested runs were identical. |
| `-t -s48 -W5 -n1 -g -v10` | Text on stdout; detailed gas IDs, partial pressures, and fractions on stderr. |
| `-t ... -M` | Moon generation changes some generated planet properties; it is not only a display switch. |
| `-e ... --` | CSV is written to a file, not stdout. |
| Default HTML with `--` | HTML is written to a file, not stdout. |
| `-C` | Rejected as an unknown option. Source 1.43 contains this option. |
| `-c -s42 -m1 -n1` | No output for an arbitrary generated star; eligible catalog stars can produce SSC. |
| `-t -s42 -m1 -n1 -H` | Empty output with exit code 0 when the system does not match. |
| `-t ... -n0` | Empty output with exit code 0. |
| `-t -s0 -m1 -n1` | Automatic seed selection; report contains the resolved, nonzero seed. |

The original binary was tested, but source 1.43 has NOT yet been compiled or run.
All claims about its internal control flow, ownership, and defects are static
source findings, not confirmation of executable-1.34 internals.

The investigation generated three files in the original installation:
`html/StarGen.csv`, `html/StarGen.exe-42-1.html`, and `html/TAU-Cet-48.html`.
They are reference artifacts, not implementation files. Do not overwrite or
delete anything in the original installation as part of the fork's test runs.
Run legacy-binary comparisons only in explicitly isolated test directories.

## 3. Required bug fixes and output correctness

### BUG-01: CSV destroys planet, dust, and gas mass precision

**Confirmed in executable 1.34 and present in source 1.43.**

`planets.mass`, `dust_mass`, and `gas_mass` are stored in SOLAR MASSES, but the CSV
emitter prints them using `%5.3Lf`. An Earth mass is approximately 3e-6 solar
masses, so an ordinary terrestrial planet becomes `0.000`. Even many gas planets
become zero; near this scale a three-decimal solar-mass display quantizes values
in steps of approximately 333 Earth masses.

Observed example: the seed-42/mass-1 legacy system has a **1.166 Earth-mass**
planet in text, but that body's CSV `mass` field is **`0.000`**. Its physical mass
was not zero; the exporter destroyed the value. Dust/gas components suffer the
same defect.

Relevant source: `structs.h:36-44`; `display.c:188-196,297-347,396-445`.

Required correction:

- Keep internal units unchanged during the initial modernization.
- Machine-readable numeric fields must use locale-neutral, round-trip significant
  digits, allowing scientific notation. Account for the actual `long double`
  precision using `<float.h>`; do not silently narrow to `double` in a JSON library.
- If existing CSV headers/layout are retained, keep their existing SOLAR-MASS
  meaning and increase precision. Do NOT switch an ambiguous `mass` column to
  Earth masses without an explicit schema/version change.
- JSON must have unambiguous unit-bearing field names; useful Earth-relative
  conversions can be included alongside native units.
- Audit mass formatting in TEXT, HTML, SSC, SVG labels/tooltips, and verbose
  diagnostics. Human-readable defaults may remain rounded, but a finite nonzero
  tiny mass must not be presented as literal zero. Use adaptive significant-digit
  or scientific formatting when fixed decimals would erase it.
- Actual zero is still zero. Do not introduce arbitrary epsilon floors, fabricate
  mass, or change accretion calculations to repair a formatting problem.
- Diagram pixel sizes may legitimately be rounded; do not confuse drawing
  coordinates with exported physical values.

### BUG-02: sentinel/nonfinite values leak into exported data

**Non-JSON numeric tokens observed in executable 1.34; source sentinel handling
confirmed statically in 1.43.**

Legacy CSV contains values such as `1.#IO` in gas-giant surface fields. In source
1.43, `INCREDIBLY_LARGE_NUMBER` is normally `HUGE_VAL`, with a large finite fallback.
It is assigned to several quantities used as unavailable/unbounded placeholders.
This is not evidence that every such field suffered an unexpected numerical
failure: some are intentional model sentinels leaking through presentation.

Relevant source: `const.h:99-103`; `stargen.c:660-680`;
`enviro.c:698-719`; `display.c` numeric emitters.

Required correction in ALL output paths:

- Introduce a shared, FIELD-SPECIFIC value/status policy. Distinguish a legitimate
  finite value, `not_computed`, `not_applicable`, `above_model_limit`/unbounded,
  and `numerical_error`.
- Recognize both nonfinite values and explicit large finite sentinels. Checking
  only `isfinite()` is insufficient for the fallback implementation.
- JSON: valid numeric value or `null`, accompanied by a documented reason/status.
  Never emit NaN, Infinity, `1.#IO`, or an enormous placeholder as a real result.
- CSV: document empty/unavailable fields and their statuses; do not export opaque
  CRT-specific tokens as numbers.
- TEXT/HTML: display meaningful `N/A`/not-computed explanations instead of raw
  sentinels or invented zeroes.
- SSC/SVG: omit an unsupported optional physical property or explicitly reject/
  skip an invalid record when a required value is unavailable. Keep export syntax
  valid; report why something was omitted.
- Do not classify every field as unavailable merely from the final planet type.
  A late gas-dwarf classification can retain valid surface computations.
- Do not label placeholder water/cloud fractions or untouched temperature
  extremes as measured/calculated surface properties.
- Fix genuine causes of invalid arithmetic, including BUG-04 below. Normalizing
  a numerical failure is NOT a substitute for repairing its cause.

### BUG-03: inconsistent pressure units in text output

**Confirmed in source 1.43. The legacy executable's text/CSV values show the same
1000-based presentation conversion in the tested case.**

The text emitter divides millibars by `1000.0`, then labels the result
`Earth atmospheres`. That computes bars, not standard atmospheres. The HTML
emitter divides by `EARTH_SURF_PRES_IN_MILLIBARS` (1013.25).

Relevant source: `display.c:131,1420-1421`; `structs.h:61`; `const.h`.

Required correction:

- Use named, shared conversion helpers/constants. One standard atmosphere is
  1013.25 mbar; one bar is 1000 mbar; one mbar is 100 Pa.
- Either correct the text conversion to atmospheres or explicitly label bars.
  Prefer matching the existing atmosphere label with the correct conversion.
- Audit labels/conversions across every emitter: mass, radii, velocities,
  acceleration, Kelvin/Celsius, temperature differences, ages, periods, and
  percentages versus fractions.
- Correct presentation units without silently changing the simulation constants
  or numerical model. Document approved output changes.

### BUG-04: Callisto fixture has a negative dust mass

**Concrete source-1.43 typo; not established in the original executable.**

At `stargen.c:113`, total mass is `EM(1.62e-2)`, but dust mass is
`EM(1.62-2)`. The latter evaluates **1.62 minus 2**, producing **-0.38 Earth
masses** of dust instead of **0.0162 Earth masses**.

```c
/* Existing erroneous dust-mass initializer */
EM(1.62-2)

/* Intended dust-mass initializer, matching the total rocky-body mass */
EM(1.62e-2)
```

`generate_planet()` passes dust mass to `kothari_radius()` (`stargen.c:583`).
That function uses fractional powers of mass (`enviro.c:125,129`), so a negative
input can yield NaNs. This can affect computed properties and downstream output,
not just JSON formatting.

Required correction:

- Fix the initializer explicitly in a separately identifiable bug-fix change.
- Audit the other named Solar System and known-planet fixture initializers for
  similar exponent/subtraction typos and inconsistent component masses.
- Validate nonnegative physical component masses before domain-sensitive math.
  Accretion SEEDS that deliberately start with zero mass are a different stage;
  do not reject legitimate zero-mass seeds as though they were completed planets.
- Add a fixture test for positive Callisto dust mass and a Solar-template-with-
  moons test proving finite valid radius/core properties where the model computes
  them. Record this as an intentional change from the supplied source baseline.
- Do not update the whole historical catalog to modern astronomical measurements
  as part of this typo fix.

### BUG-05: malformed/incomplete presentation contracts

Source 1.43 also has:

- CSV-like single-quoted strings with no proper escaping.
- Moon records shorter than the planet header: `minor_moons` is omitted.
- Separate star and body table blocks, not one rectangular CSV file.
- Atmospheric constituents rounded/filtered into a descriptive string.
- A text `-g` section containing only `//gases?`.
- Text output that does not provide the requested moon detail.

Relevant source: `display.c:158-164,168-445,1518-1559`.

Required correction:

- Implement proper CSV string escaping and consistent row width WITHIN each
  documented legacy table block, including moon rows. Preserve column meanings
  unless introducing an explicitly versioned format.
- Document that corrected legacy CSV is a multi-table layout; do not call it one
  rectangular relation. A normalized/versioned CSV with system/body/gas rows is
  a useful OPTIONAL follow-up, not a prerequisite for MCP.
- Make text `-g` and `-M` meaningful: show atmosphere and moon data/statuses without
  requiring consumers to parse debug stderr.
- Full JSON includes all calculated atmospheric constituents. A human summary
  may suppress traces if it clearly says so. Avoid fixed 2000-byte intermediate
  strings as the authoritative representation.
- Escape strings according to each format: JSON, CSV, HTML attributes/text, and
  SSC quoted strings. Never use caller-provided text as a printf format string.
- An ineligible SSC export must report its reason rather than appear to have
  succeeded with unexplained empty output. Do not invent a Celestia star entry.

## 4. Source architecture and mandatory safety boundaries

### Generation and emitter seam

The current path is:

```text
stargen()
  -> request/catalog/star setup
  -> generate_stellar_system()
  -> generate_planets()/environment calculations
  -> output switch
  -> free_atmosphere()/free_generations()
```

There is no owning public system result. `stargen()` returns an integer;
`innermost_planet` is global; the star and its name are local/borrowed.

Add the completed-model export/callback AFTER generation and BEFORE cleanup.
Pass the star, body root, effective request/provenance context, and per-system
statistics explicitly. Do not infer the star solely from the first planet;
empty systems must be safe.

Relevant source: `stargen.h:14-40`; `stargen.c:347-401,1240,1722-1730,1832-1918`.

**Adding `ffJSON` to the old output switch alone is insufficient.** Path checks,
thumbnail creation, stream closing, and error exits surround that switch.
Create a genuinely headless one-job path retaining generation/chemistry setup
but bypassing presentation setup and finalization.

### State and ownership defects to address

- Process-global PRNG, scratch state, chemistry, statistics, and planet roots
  mean the engine is not thread-safe/reentrant.
- Positive `ratio_arg` multiplies the global dust coefficient across calls;
  it is not restored (`stargen.c:53,1445-1446`).
- Only some counters reset. Totals/extrema can persist across calls.
- Solar/catalog fixtures are adopted and modified in place, including gas-loss
  and moon-orbit fields. Clone templates before enrichment; do not mutate the
  immutable baseline fixtures.
- Borrowed star/name pointers and generated roots become invalid after the call.
  Serialize or deep-copy while their scope is valid; never retain raw pointers
  in the adapter or assume a guessed FFI struct ABI.
- `free_planet()` frees siblings but not `first_moon`; atmosphere cleanup does
  recurse, so allocated moon records can leak (`accrete.c:681-745`). Audit
  collision/capture/history ownership before introducing recursive freeing to
  avoid double-freeing shared/transferred nodes. Clear freed roots/counts.
- Heap bodies/newly exposed members are not comprehensively initialized.
  Initialize fields and computation status deliberately; do not serialize
  uninitialized `moon_a`/`moon_e` or unknown padding/data.
- Allocation failures, unchecked buffers, domain errors, and unbounded accretion
  loops need controlled failure paths. External worker timeouts remain mandatory.

Use a fresh process per system initially. Repair request reset, template ownership,
initialization, and cleanup as correctness work, but do not advertise concurrent
in-process engine use or persistent workers before a separate reentrancy audit.

### Streams and errors

- JSON worker stdout contains ONLY one complete UTF-8 JSON result, with no BOM,
  banners, debug lines, HTML, or environmental error messages.
- Route diagnostics through a shared stderr/logger interface. Some existing code
  prints directly to stderr and at least one environmental error uses stdout;
  audit these rather than trusting only the `sgErr` parameter.
- Never close caller-owned stdout/stderr. `close_html_file()` currently closes
  its supplied stream (`display.c:810-823`).
- File-opening failures currently call `exit(0)` in parts of the driver; `main`
  ignores the return from `stargen()`. Propagate failures and meaningful nonzero
  exit codes instead.
- Guard thumbnail finalization; the source CSV-download multisystem path can
  finalize a null thumbnail stream (`stargen.c:1468-1481,1952-1963`).
- A valid no-match search is not an execution failure. Distinguish it explicitly
  from invalid input, failed generation, timeout, and malformed output.

## 5. Proposed headless CLI and data contract

### Native interface

Implement explicit, documented headless modes, for example:

```text
StarGen.exe --json --seed 42 --mass-solar 1.0 --gases --no-moons
StarGen.exe --json --seed 48 --catalog solstation --star-index 5 --gases
StarGen.exe --list-stars-json --catalog solstation
```

These are PROPOSED NEW commands, not commands available in the original binary.
Keep supported legacy short options working; do not rely on the historical `--`
streaming convention. The new worker must not create files, require `html/` or
`ref/`, or silently generate an entire catalog when one star was requested.

Initial JSON generation is ONE SYSTEM per invocation. Reject conflicting sources,
invalid indices, and multi-system/catalog-wide requests in this worker mode.
The adapter owns bounded search/batching. Catalog listing must come from the
same compiled catalog data as generation, not a separately hard-coded Python list.

Use robust full-token numeric parsing (`strto*` with range/end checks), explicit
argument bounds, and finite input validation. Avoid `atoi`, unchecked `sscanf`,
unbounded `strcpy`/`sprintf`, and silently truncated names/paths. On Windows,
handle Unicode arguments/paths deliberately; do not label ACP bytes as UTF-8.

For explicit stellar mass, document the current model's 0.2-1.5 solar-mass range
and reject unsupported explicit values in the strict interface. Preserve a
deliberate automatic-mass mode when no mass is specified. Source 1.43 otherwise
silently randomizes out-of-range mass to 0.7-1.4; report effective inputs and warn
about any legacy catalog/input adjustment. Do not silently change catalog-mass
policy without documenting it as a behavior change.

### JSON contents

Define and version success/partial/error schemas BEFORE implementing exporters.
Suggested success structure:

```text
schema_version
status                         ok | partial | error
engine                         upstream revision, fork version, build identity,
                               compiler/runtime, PRNG identity, numeric precision
request                        requested parameters
effective                      resolved seed, effective star/configuration,
                               generation mode, dust coefficient, catalog identity
star                           name, designation, mass/luminosity, age/lifetime,
                               ecosphere, relevant binary context and orbit limit
summary                        separate planet/moon/type/habitability counts
planets[]                      stable body IDs, parentage, physical/environmental
                               fields, classifications/statuses, atmospheres, moons
warnings[]                     code, body/field context, actionable explanation
error                          structured error detail when applicable
```

Implementation rules:

- A practical shared normalized export view/value-status layer should feed
  JSON and corrected presentation helpers. It must not change RNG consumption.
- Keep machine values at round-trip precision; rounding is for presentation.
- Prefer unit-bearing names such as `mass_solar`, `mass_earth`, `radius_km`,
  `surface_pressure_mbar`, `surface_temperature_k`, `rotation_period_hours`,
  and `water_fraction`. Avoid ambiguous scalar `mass`/`temperature` in new schemas.
- Source units: mass/dust/gas in solar masses; orbits AU; radii km; velocities
  cm/s; acceleration cm/s2; gravity Earth gees; pressure mbar; temperatures K;
  differences K; stellar times years; heliocentric periods Earth days; days hours;
  surface coverage fractions. Use the model's named conversion constants.
- IDs can be deterministic hierarchical strings such as `p1` and `p1/m1`.
- Atmospheres contain chemical ID, plain symbol/name, partial pressure, fraction,
  inspired partial pressure, and toxicity information/status. Resolve chemistry
  by chemical ID, NOT array position: the chemistry table is sorted.
- `gases == 0` does not automatically mean physical vacuum. Gas computation is
  gated by options and conditions. Represent not-computed versus computed-empty.
- Breathability, model habitability, and Earth-likeness are separate decisions.
  Do not infer all three from an HTML description saying "Earth-like". Refactor
  their predicates into side-effect-free helpers where needed; do not count a
  body twice while building summaries.
- Count planets and moons separately; legacy statistics can include moon checks.
  Expose keyed type counts, not an unlabeled raw enum/histogram array.
- Moon `a/e` become HELIOCENTRIC values during enrichment. Planet-centric values
  are `moon_a/moon_e`; zero can mean no permitted modeled orbit. Existing
  `orb_period` is not a planet-centric lunar orbital period. Name these correctly;
  do not present one as the other or invent a computed lunar period.
- Some binary metadata/outer-orbit limits are local driver values, absent from
  the `sun` struct. Pass them in the export context.
- Transient collision histories, Roche/Hill intermediates, gas lifetimes, and
  temperature convergence history are not fully retained in the final model.
  Do not promise them as existing fields; instrumentation is a separate extension.

### Reproducibility

- Keep the legacy `rand()`/`srand()` algorithm and draw order initially. Do not
  switch to another PRNG, `powl` family, fast-math, or newer physical constants
  just because the build is being modernized.
- Source `enviro.c:389-392` consumes a random draw even where its result is later
  discarded. Removing apparently redundant code can change systems.
- Use an explicit validated seed representation and document how it maps into
  the CRT PRNG. Return the actual seed. Handle automatic selection and overflow
  deliberately; do not rely on platform-dependent `long` width or negative casts.
- Preserve/document legacy batch seed behavior where legacy batches remain:
  each system reseeds; the first Solar-template system can skip the increment.
- New adapter searches generate independent one-job workers with their own
  explicit seed sequence; report that sequence rather than claiming equivalence
  to the old all-catalog driver loop.
- Include moon/gas/template/known-planet/dust options and effective stellar inputs
  in reproducibility metadata. Enabling moons can change the planets themselves.
- Record `sizeof(long double)`, `LDBL_MANT_DIG`, and decimal precision, not just
  storage size. Compiler/CRT and source revision matter. Cross-platform or
  cross-version identical systems are NOT a requirement without a new PRNG/
  numerical compatibility project.

## 6. Modern Microsoft/portable build plan

### Build targets and tooling

- Add CMake and presets in the new fork. Minimum CMake version should be declared
  explicitly; 3.24+ is a reasonable starting baseline.
- Build a static `stargen_core` library and the CLI entry point from `.c` sources,
  plus a C test target. Initially the library may still include `display.c`
  because the old orchestration references it; do not pretend it is already
  presentation-independent.
- Add JSON/value-status/unit/format helpers in separate focused C modules.
- Use C17 (`C_STANDARD 17`, required; extensions disabled where supported).
  Configure modern MSVC C compilation, not `/TP` for all sources.
- Required initial Windows profiles: x64 Debug and Release under Visual Studio
  2022 or newer, with a current compatible Windows SDK/UCRT. x86 compatibility
  is useful but should be declared optional unless the owner requires it.
- Prefer compiler-version-agnostic presets using Ninja from a Visual Studio
  Developer Shell, or document the appropriate installed Visual Studio generator.
  Do not hard-code a generator for an unavailable toolset.
- Link `m` on platforms that require it, not unconditionally on MSVC.
- Set/document the MSVC runtime policy consistently across targets. Do not rely
  on the installation's old `msvcr80.dll` or MFC DLLs for the new console engine.
- The supplied `StarGen.dsp/.dsw` are VS6-era historical files. The `.dsp` omits
  `main.c`, references absent `propert.h`, and contains obsolete flags. CMake
  should be the supported build; historical files need not remain operational.
- Rebuilding `WinStarGen.exe`'s GUI and obsolete Macintosh projects is outside
  this task. Do not add GUI dependencies to the CLI/MCP engine.

### Source modernization checklist

- Convert K&R definitions and empty-parameter declarations into explicit ISO C
  prototypes (`function(void)` when appropriate).
- Make public headers self-contained and guarded; add `extern "C"` guards where
  they are intended for C++ consumers. A C++ header/link smoke test is useful;
  compiling the simulation implementation as C++ is not required.
- Fix pointer/array type mismatches instead of retaining broad warning suppression
  such as the `4048` pragma. Preserve catalog contents and lookup semantics.
- Replace implementation-defined four-character output selectors with explicit
  enums/internal constants; maintain CLI behavior and stable serialized names.
- Replace fragile positional `ZEROES` fixture initialization with named/designated
  initialization or well-tested constructors as practical. Verify every field's
  old intended value before changing layout/initializers.
- Standardize `_WIN32` and platform-specific I/O/path handling. Isolate classic
  Mac compatibility headers and optional Dumas code from production targets.
- Use `size_t` and fixed-width request/ID types appropriately; audit varargs
  formats (`%zu`, `<inttypes.h>` macros) and checked narrowing on x64.
- Replace unchecked fixed-size path/name/atmosphere buffers with bounds-checked
  helpers or owned dynamic strings, reporting overflow instead of truncating.
- Check allocation, file, conversion, serializer, and output-stream failures.
- Ensure calculations receive valid domains before division, square root,
  fractional powers, and logarithms; distinguish sentinels from real errors.
- Ensure names and generated text are safely escaped/encoded in every format.
- Do not declare the migration complete by globally suppressing CRT/compiler
  warnings. Any unavoidable temporary suppression must be narrow and explained.
- Enable `/W4` and fix actionable warnings. Require project-owned code to be
  warning-clean, enabling `/WX` once the compatibility cleanup is complete.
- Avoid `/fp:fast` and equivalent flags; document the chosen floating-point policy.
- Use AddressSanitizer where the installed MSVC toolchain supports it, in a
  separate compatible build profile. On a portable CI/toolchain, add ASan/UBSan.
  Keep unsupported/missing tooling explicit rather than claiming it was tested.

## 7. MCP adapter requirements

### Initial tools

1. `list_stars(catalog)` returns compiled catalog IDs/indices, names/designations,
   available parameters, and relevant experimental/model limitations.
2. `generate_system(...)` accepts typed stellar source, explicit or automatic
   seed, generation options, and `detail = summary | full`. Gas calculations
   should be enabled by default for useful agent-facing habitability results;
   moons should be opt-in and clearly marked experimental.
3. `find_systems(...)` accepts explicit criteria, seed sequence, `max_attempts`,
   `max_results`, and a time budget. Return attempts, matched systems, effective
   seeds, warnings, and the termination reason. No match is a valid result.

Do not expose `run_stargen(raw_args)` or model-selected arbitrary filesystem
paths. Star source and options must be cross-validated, not only individually
typed. Unsupported/private catalogs or option combinations must fail clearly.

Search criteria should distinguish planet-only matches from matches including
moons. Preserve/document the old CLI's counting semantics; do not silently apply
them to a differently described new tool.

### Process management and limits

- Spawn the configured absolute fork executable directly, without a shell.
- Use independent worker processes, not concurrent calls into global C state.
- Read stdout and stderr concurrently. Bound result/diagnostic size and process
  concurrency; avoid pipe deadlocks and unbounded memory.
- Enforce a worker timeout, search deadline, attempt/result limits, and cancellation
  cleanup. Kill/reap the owned worker and close pipes when a call is cancelled.
- Suggested starting configuration: 10-second worker limit, 30-second search
  budget, at most 1000 attempts/10 returned systems, and two workers concurrently.
  Benchmark the rebuilt engine and adjust these documented defaults to fit the
  target host's tool deadline. Never make searches implicitly unlimited.
- Check both exit status and result schema. Reject empty/malformed/truncated JSON
  and invalid numeric values. Do not silently retry with a different seed.
- Expected null/status values are not execution errors. Unexpected numerical
  failures must have body/field context and an actionable result.
- Native worker errors map to MCP tool errors with `isError`; make error envelopes
  conform to the declared output schema/SDK conventions for the selected protocol.

### Agent-facing results and packaging

- Use MCP `structuredContent` and explicit output schemas. Include serialized
  JSON text for compatible clients according to the SDK/protocol guidance.
- Add a concise Markdown/text overview for readability, not instead of the
  authoritative structured data.
- Default to compact per-system/per-planet summaries. Full atmospheric, thermal,
  and moon details must be opt-in so normal calls do not dump every struct field.
- If later adding `get_planet`/stored-system lookup, return explicit opaque handles
  with bounded lifetime/cache size, or require full reproducibility parameters.
  Do not assume implicit connection state or change moon options during a detail
  lookup and pretend it is the same generated system.
- MCP server stdout is protocol-only. Python logging and child diagnostics go to
  stderr; no startup `print()` banners.
- Package with `pyproject.toml`, pinned dependencies/lockfile, a console entry point,
  a configurable executable location, and a Windows-friendly launch example.
- Do not edit the owner's existing host/MCP configuration automatically. Provide
  a documented sample configuration for the target host.
- Describe the generated systems as synthetic/model-based. Format modernization
  does not turn the historical atmosphere/moon/planet models into modern
  astronomical predictions or validate them scientifically.

## 8. Implementation sequence and checkpoints

### Phase A - establish an auditable fork baseline

- [ ] Inspect the new fork's layout/instructions and preserve existing user work.
- [ ] Confirm upstream source revision and record a source manifest/build identity.
- [ ] Add a minimal modern CMake build using only necessary compatibility edits.
- [ ] Build source 1.43 with the selected MSVC toolchain; record compiler/CRT,
      precision, flags, and deterministic fixtures before substantive changes.
- [ ] Separate observations from original executable 1.34 from the new baseline.

### Phase B - modernize and fix model/lifecycle defects

- [ ] Finish C17/MSVC warning/type/prototype/buffer/error modernization.
- [ ] Fix and test Callisto's initializer; audit the other fixtures.
- [ ] Initialize body fields/computation statuses and validate arithmetic domains.
- [ ] Establish template and moon/history ownership; clone templates and repair
      cleanup/reset without double frees or dangling pointers.
- [ ] Route diagnostics correctly and fix exit-code/stream/null-thumbnail handling.
- [ ] Keep justified behavior-changing fixes distinct from mechanical portability
      work, with regression evidence and documented numerical changes.

### Phase C - correct all supported exporters

- [ ] Add shared unit, precision, value-status, and escaping helpers.
- [ ] Fix CSV mass precision, escaping, and moon row width.
- [ ] Correct pressure conversions/labels and audit the other units.
- [ ] Apply zero-safe tiny-value and unavailable-value rendering to text/HTML/SSC/
      SVG/diagnostics; ensure text gas/moon switches produce useful data.
- [ ] Test single/multiple systems, gases, moons, filters, and stream/file output.

### Phase D - add headless structured output

- [ ] Write versioned generation/catalog JSON schemas and the status policy.
- [ ] Add one-system `--json` and structured catalog listing with no filesystem
      presentation setup or stdout contamination.
- [ ] Export full computed data, context/provenance, classifications, and parentage
      while model lifetimes are valid.
- [ ] Prove that changing only output mode does not consume RNG or change the
      underlying same-build physical system.

### Phase E - implement and test the MCP adapter

- [ ] Implement the typed tools using the current official Python SDK.
- [ ] Implement direct spawning, bounded concurrent pipe reads, deadlines, limits,
      cancellation, structured errors, and schema validation.
- [ ] Add summary/full results, seeded bounded searches, packaging, and examples.
- [ ] Test with an MCP client/Inspector and the intended host. If both modern and
      legacy protocol eras are supported, test both rather than inferring this
      from a successful legacy handshake.

### Phase F - delivery

- [ ] Provide build/test instructions and reproducibility/output-schema docs.
- [ ] Record completed tests, unsupported toolchains/features, and known numerical
      model limitations accurately.
- [ ] Provide supported-format examples and intentional compatibility changes.
- [ ] Provide local CI configuration where appropriate; do not publish releases
      or alter global configuration as an incidental step.

## 9. Required tests and acceptance criteria

### Numerical/output unit tests

- A constructed 1-Earth-mass body exports a nonzero correct solar mass and useful
  Earth mass; machine values round-trip at the chosen precision.
- Tiny nonzero bodies/component masses do not become zero in any published mass
  label/value. True zero remains zero. Check SSC and SVG tooltips as well as CSV.
- 1013.25 mbar is 1 standard atmosphere; 1000 mbar is 1 bar, not 1 atmosphere.
- Test velocity, radius, temperature, age/period, and fraction/percentage units.
- Test finite fallback sentinels, infinity, NaN, invalid arithmetic, and each
  unavailable/status category. No invalid numeric tokens leak into exports.
- Validate Callisto and other applicable physical fixtures; completed-body
  component masses are nonnegative and appropriately consistent.
- JSON/CSV/HTML/SSC escaping: quotes, apostrophes, commas, ampersands, angle
  brackets, backslashes, newlines/control characters, and Unicode names.
- CSV planet and moon rows agree with their header width; table-block boundaries
  and legacy units are documented.
- Test complete atmosphere arrays, trace gases/toxicity, unknown chemical IDs,
  not-computed atmospheres, and computed-empty/vacuum states.

### Engine/CLI regression tests

- Fixed same-build seed/options produce identical physical results repeatedly.
- JSON versus other output selection does not change generation/RNG draws.
- Automatic seed can be replayed using the returned effective seed/parameters.
- Explicitly verify/document that enabling moons can change planet generation.
- Repeated template generation does not mutate the stored templates; repeated
  dust-ratio requests do not compound process-global state.
- Solar-template generation with moons exercises Callisto and ownership paths.
- One/multiple-system legacy runs with gases/moons and CSV stdout/download modes
  cannot finalize null streams or close caller-owned streams unexpectedly.
- Empty systems, no matches, invalid indices/options/numbers, zero/negative
  counts, excessive strings, unwritable destinations, allocation failure where
  testable, and broken output streams fail cleanly or return valid empty results.
- No unexpected leaks, double frees, use-after-free, or uninitialized reads in
  repeated generation and supported sanitizer/test profiles.

### Headless/MCP integration tests

- Worker output is exactly one valid schema-conforming UTF-8 JSON result.
- Headless generation/catalog listing works from an empty working directory with
  no `html/`, `ref/`, images, or helper files, and creates no output files there.
- Metadata identifies the rebuilt fork and effective parameters correctly.
- Tool listing, successful calls, input errors, worker errors, schema errors,
  output limits, timeouts, and cancellation cleanup work through a real MCP client.
- Parallel calls use isolated processes; the adapter survives child failure.
- Search reports attempt count, matched seeds, and termination/no-match status;
  it honors result/attempt/time limits and criterion scope.
- Summary versus full mode obeys the documented output/token-size policy.
- Required Windows x64 Debug/Release builds and tests pass with current MSVC.
  Report portable/x86/sanitizer coverage separately and honestly.

**Definition of done:** the fork builds under the supported modern MSVC setup;
the documented bugs are fixed across the applicable outputs; machine JSON is
accurate, valid, and versioned; a schema-validated SDK MCP server exposes bounded
generation/search tools; same-build reproducibility and worker cleanup are tested;
and the original installation has not been overwritten or repurposed.

## 10. References for the implementation agent

Local source navigation:

- `main.c`: legacy option parser and ignored engine return code.
- `stargen.h`: public one-shot API, flags, output selectors, global statistics.
- `structs.h:22-85,98-139`: units, body/star/chemistry structures and relationships.
- `stargen.c:332-401`: PRNG initialization and effective stellar inputs.
- `stargen.c:113,563-855`: Callisto typo, enrichment, sentinels, moon orbit meanings.
- `stargen.c:919-930,1082-1107`: habitability/Earth-likeness predicates.
- `stargen.c:1297-1328,1445-1565,1722-1963`: headless seam, state, presentation,
  output failures, cleanup, and batch finalization.
- `display.c:87-449,749-823,1304-1619,1718-1850`: existing exporters and ownership.
- `accrete.c:410-431,531-550,681-745`: allocation/initialization and cleanup.
- `enviro.c:125-129,384-392,698-719,914-980`: numerical domains, diagnostics,
  RNG draw order, sentinels, and iteration/inspired-pressure calculations.
- `utils.c:11-39`: CRT PRNG wrappers and eccentricity.
- `Makefile`, `StarGen.dsp`: old build configuration; do not run legacy packaging
  targets that move/delete installation directories during modernization.

External documentation (verify current versions before using examples):

- StarGen overview: https://www.eldacur.com/~brons/NerdCorner/StarGen/StarGen.html
- Historical manual: https://www.eldacur.com/~brons/NerdCorner/StarGen/Manual.html
- MCP server/SDK guidance: https://modelcontextprotocol.io/docs/develop/build-server
- MCP tool results/schemas: https://modelcontextprotocol.io/specification/latest/server/tools
- Official Python SDK: https://github.com/modelcontextprotocol/python-sdk
- MSVC C17 and C compilation: https://learn.microsoft.com/en-us/cpp/build/reference/std-specify-language-standard-version
- MSVC C11/C17 prerequisites: https://learn.microsoft.com/en-us/cpp/overview/install-c17-support

The owner's new fork and its own instructions take precedence over suggested
module names/layout here. Preserve the architectural and correctness requirements,
and explicitly document any agreed deviation.
