# StarGen JSON output schema (schema_version 1)

Emitted by `StarGen --json ...` (one system) and used as the
`structuredContent` payload of the MCP `generate_system` tool (summary
projection) and `find_systems` (per-match summaries).

Everything produced by this schema is **synthetic, model-based fiction**
(the historical Dole/Fogg accretion model), not modern astronomical
prediction. See "Reproducibility" below for what "the same result"
means.

## Top-level document

| Field | Type | Meaning |
| --- | --- | --- |
| `schema_version` | int | Always `1` for this format. |
| `status` | string | `ok` or `partial` (partial = warnings present). |
| `error` | null/object | null on success; object with details on failure. |
| `engine` | object | Build identity (below). |
| `request` | object | Echo of the requested parameters. |
| `effective` | object | Resolved parameters actually used. |
| `star` | object | The primary star. |
| `summary` | object | Counts and classification totals. |
| `planets` | array | Bodies; moons are NESTED inside their planet. |
| `warnings` | array of strings | Non-fatal anomalies. |

`engine`: `name`, `fork`, `upstream_revision` (e.g. `1.43`),
`json_schema_version`, `compiler`, `stdc_version`,
`long_double {bytes, mantissa_bits, decimal_digits}`, `prng`
(`CRT rand/srand`, `rand_max`), `sizeof_long`.

`request`: `source` (`mass` or `catalog`), `catalog`, `star_index`,
`mass_solar` (null = automatic), `seed` (null = automatic), `gases`,
`moons`.

`effective`: `seed`, `seed_source` (`requested`/`automatic`), `mass_solar`,
`luminosity`, `ecosphere_radius_au`, `star_lifetime_years`,
`star_age_years`, `dust_density_coeff`, `outer_orbit_limit_au` (null when
no binary companion), `gases_enabled`, `moons_enabled`.

## Body objects (planets and moons)

Bodies share one shape. Moons add `parent` (the planet id, e.g. `p6`)
and `parent_orbit`; planets have `parent: null` and no `parent_orbit`.

| Group | Fields |
| --- | --- |
| identity | `id` (`p1`, `p1/m1`), `parent`, `planet_no`, `moon_no`, `type` |
| `orbit` | `a_au` (AU), `e`, `orbit_zone` (1-3), `orb_period_days` |
| `parent_orbit` | `semi_major_axis_au`, `eccentricity` — planet-centric moon orbit; null + status `no_permitted_modeled_orbit` when the model permits none |
| `mass` | `solar`, `earth`, `dust_solar`, `dust_earth`, `gas_solar`, `gas_earth` |
| `physical` | `core_radius_km`, `radius_km`, `radius_earth`, `density_g_cc`, `escape_velocity_cm_s`, `surf_accel_cm_s2`, `surf_grav_earth`, `rms_velocity_cm_s`, `molec_weight_retained` |
| `rotation` | `day_hours`, `resonant`, `tidally_locked`, `axial_tilt_deg` |
| `surface` | `pressure_mbar`, `pressure_atm`, `temperature_k`, `greenhouse_rise_k`, `boil_point_k`, `high_temp_k`, `low_temp_k`, `max_temp_k`, `min_temp_k`, `hydrosphere_fraction`, `cloud_fraction`, `ice_fraction`, `volatile_gas_inventory`, `greenhouse_effect` |
| `thermal_estimates` | `estimated_temp_k`, `estimated_terr_temp_k`, `albedo`, `exospheric_temp_k` |
| `atmosphere` | `computed`, `not_computed_reason`, `gases[]`, `breathability` |
| `classification` | `gas_giant`, `habitable`, `earthlike`, `habitable_jovian` (gas giants only, else null), `minor_moons` |
| `moons` | nested array of moon objects |
| `statuses` | only when non-ok: dotted field path -> status |

### Value/status policy

- Every modeled quantity is a number or `null`; **sentinel values
  (`INCREDIBLY_LARGE_NUMBER`, inf, NaN) are never serialized as
  numbers.**
- Gas-giant surfaces are **not modeled**: `surface` fields are null with
  `statuses: {"surface": "not_applicable", "surface.temps_range":
  "not_computed"}`. Placeholder water/cloud fractions are NOT exported
  as measurements.
- `atmosphere.computed=false` carries `not_computed_reason`:
  - `surface_pressure_zero` — physically airless;
  - `not_applicable` — gas giant;
  - `not_computed` — gases requested but the liquid-range gating
    suppressed computation (NOT the same as vacuum).
- `atmosphere.gases[]`: `id` (chemical number), `symbol`, `name`,
  `partial_pressure_mbar`, `max_ipp_mbar` (null when unlimited),
  `inspired_partial_pressure_mbar`, `fraction`, `poisonous`. Gases are
  resolved by chemical id against the (sorted) chemistry table, never by
  array position.
- `breathability`: `none` / `breathable` / `unbreathable` / `poisonous`.

## Units

mass (solar masses; Earth equivalents alongside), orbits (AU), radii
(km), velocities (cm/s), acceleration (cm/s²), gravity (Earth gees),
pressure (millibars; 1 atm = 1013.25 mbar), temperatures (Kelvin),
lifetimes/ages (years), `orb_period` (Earth days), `day` (hours),
hydrosphere/cloud/ice (fractions 0..1).

## Reproducibility

Results are reproducible **for the same compiled build only** (same
compiler, CRT, and source revision). `engine.long_double` records the
precision facts (MSVC: 8 bytes, 53-bit mantissa; GCC/Clang x86-64 use
80-bit long doubles and WILL produce different systems). The PRNG is the
CRT `rand()`/`srand()` with a 32767 `RAND_MAX`; seeds map directly to
`srand()`. Enabling moons changes planet generation (moon accretion
feeds back); the `effective` block records exactly what ran.
