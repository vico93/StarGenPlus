# StarGen MCP server (`StarGen.exe --mcp`)

`StarGen.exe --mcp` speaks the Model Context Protocol over stdio
(newline-delimited JSON-RPC 2.0) and exposes StarGen's planetary-system
generator to MCP hosts/agents. All output is **synthetic, model-based
fiction** from the historical StarGen accretion model — not astronomy.

## Protocol support (dual-era)

- **Modern revision `2026-07-28`**: every request carries
  `params._meta["io.modelcontextprotocol/protocolVersion"]`; the server
  implements the mandatory `server/discover` and answers
  `-32022 UnsupportedProtocolVersionError` (with its supported list) for
  other versions.
- **Legacy revisions (≤ `2025-11-25`)**: the `initialize` handshake; the
  server echoes the client's requested protocol version and rejects
  requests that arrive before `initialize` with `-32002`.

Tools results carry `structuredContent` plus a human-readable `content`
text block. Input-validation problems are **tool execution errors**
(`isError: true`, model-correctable); structural problems (unknown tool,
malformed request) are JSON-RPC protocol errors.

## Tools

### `list_stars(catalog)`
Lists a built-in catalog: indices (0-based; index 0 is always Sol, the
Solar System template), designations, names, masses, luminosities
(`derived_from_mass` when 0), binary-companion data when present.
`catalog` ∈ `solstation | dole | jimb`.

### `generate_system(...)`
Generates ONE system. Parameters:
- star source: `catalog` + `star_index` **or** `mass_solar` in
  [0.2, 1.5] (mutually exclusive);
- `seed` (≥ 0; omit or 0 = automatic, returned as `effective.seed`);
- `gases` (default true — needed for habitability results);
- `moons` (default false — **experimental**: moon accretion can change
  planet properties);
- `detail`: `summary` (default, compact) or `full` (complete model data).

Returns `structuredContent` = the schema_version 1 document
(`docs/JSON-SCHEMA.md`; summary mode keeps `request`/`effective`/`star`/
`summary`/`warnings` and compact planets, and omits full surface detail).

### `find_systems(...)`
Bounded seeded search. Parameters: the same star source + `gases`/`moons`,
plus `seed_start` (default 1; each attempt increments it), `criteria`
(`min_planets`, `min_habitable`, `min_earthlike`, `requires_moons`,
`planet_type` — e.g. `Terrestrial`), `max_attempts` (≤ 1000),
`max_results` (≤ 10), `time_budget_ms` (≤ 30000).

Returns `{attempts, seed_start, terminated_by: completed|time_limit,
matches[]}` where each match embeds the reproducibility metadata
(`effective.seed` replays the system via `generate_system`). **No match
is a valid result** (`matches: []`), not an error.

## Limits and configuration

Defaults (PLAN.MD §7): worker timeout 10 s, search budget 30 s,
max 1000 attempts, max 10 results. Override via environment:

| Variable | Default | Meaning |
| --- | --- | --- |
| `STARGEN_MCP_WORKER_TIMEOUT_MS` | 10000 | per-generation worker limit |
| `STARGEN_MCP_SEARCH_BUDGET_MS` | 30000 | whole-search budget |
| `STARGEN_MCP_MAX_ATTEMPTS` | 1000 | hard cap on attempts |
| `STARGEN_MCP_MAX_RESULTS` | 10 | hard cap on returned systems |

Each generation runs in a fresh `StarGen --json` child process (crash
isolation, enforceable timeouts); the worker is killed on timeout and on
server shutdown (kill-on-close Job Object). Worker stdout is capped at
64 MiB; worker stderr is inherited into the server's stderr (host logs).

## Known limitations (documented, per PLAN.MD)

- **Cancellation**: `notifications/cancelled` aborts an in-flight
  `find_systems` between attempts (a single worker step of ≤ 10 s may
  still complete first) and no response is sent for a cancelled request.
  A long `generate_system` cannot be interrupted mid-worker.
- **Worker subprocesses are Windows-implemented**; on other platforms
  the tools report an error instead of spawning (the CLI itself is
  portable).
- The server processes requests sequentially per connection; matches the
  stdio transport's single-channel design.
- Moon generation is experimental and changes results (recorded in
  `effective.moons_enabled`).

## Host configuration samples

### Claude Desktop / generic stdio host (`claude_desktop_config.json`)
```json
{
  "mcpServers": {
    "stargen": {
      "command": "D:\\vico\\Projetos\\Diversos\\StarGenPlus\\build\\msvc2026-x64\\Release\\StarGen.exe",
      "args": ["--mcp"]
    }
  }
}
```
(Adjust the path to your build; do not edit host configs automatically —
this is a sample.)

### MCP Inspector (manual testing)
```powershell
npx @modelcontextprotocol/inspector D:\...\StarGen.exe --mcp
```

### Official Python SDK client test (dev-only)
```powershell
python -m venv tests\tmp\mcp_venv
tests\tmp\mcp_venv\Scripts\pip install mcp
tests\tmp\mcp_venv\Scripts\python tests\test_mcp_client.py <path\to\StarGen.exe>
```
The test exercises the legacy handshake era end-to-end (initialize,
tools/list, all three tools, isError validation path).
