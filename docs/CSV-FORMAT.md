# StarGen CSV output format (as exported by the StarGenPlus fork)

The CSV output (`-e`, file-based; `-C ... --`, stdout) is a **multi-table
legacy dialect**, not one rectangular relation:

1. **Star row** — header `seed, name, luminosity, mass, life, age, r_ecosphere`,
   one data row describing the primary star.
2. **Body table** — one 40-column header (from `planet_no` to
   `minor_moons`), followed by one row per planet **and one row per
   moon** (`<name> <planet#>.<moon#>` in the first column). Rows are
   narrower/wider only if a bug slips through; `tests/csv_columns_check`
   guards this.

## Conventions and corrections (PLAN.MD BUG-01/02/05)

- **Strings** are single-quoted; embedded single quotes are doubled
  (`O'Brien` → `'O''Brien'`); control characters become spaces.
- **Numeric fields** use locale-neutral, shortest **round-trip**
  significant digits (`0.305`, `3.5038622418395767e-06`). Values parse
  back bit-exactly.
- **Masses keep their documented SOLAR-MASS meaning** (`mass`,
  `dust_mass`, `gas_mass`). The historical `%5.3Lf` rendering destroyed
  planetary masses (1.166 Earth masses ≈ 3.5e-6 solar masses printed as
  `0.000`); Earth-mass equivalents are `mass × 332775.64`.
- **Sentinel/unavailable values render as EMPTY fields.** The model
  assigns `INCREDIBLY_LARGE_NUMBER` (+infinity with MSVC, 9.9999E37 as
  the finite fallback) to quantities that do not exist for a class of
  body — e.g. surface pressure/gravity/temperature of gas giants.
  Empty means "not applicable / unbounded", not zero.
- The first body-table column is labeled `planet_no` but carries the
  legacy display id `'systemname N'` (moons: `'systemname N.M'`).
  Preserved for compatibility; see the JSON schema for unambiguous ids.
- Atmosphere data is a descriptive string (`H2O 12.3% 123mb (ipp:100)`),
  not full constituent data; use the JSON output for the complete,
  unrounded atmosphere model (Phase D).
- Moon rows carry `minor_moons` = 0 to match the header width; the
  historical exporter omitted the column for moons.

## Out of scope / intentional legacy quirks

- `-e ... --` (file CSV with stdout requested) is a historical no-op in
  source revision 1.43 and is preserved as such; use `-e` (no `--`) for
  file output or `-C ... --` for stdout output.
- The layout is not RFC 4180 (single quotes, space-separated fields);
  a normalized, versioned CSV remains an optional future extension.
