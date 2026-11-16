# Vendored third-party code

## jsmn

- **Source**: https://github.com/zserge/jsmn (single-header JSON tokenizer)
- **License**: MIT -- see the header comment in `jsmn.h`
- **Pinned**: upstream master at the time of vendoring (2026-11)
- **Used by**: `src/mcp/sg_json_dom.c` (JSON DOM for the MCP server's request
  parsing) and the MCP request path.

Compiled with `JSMN_STATIC` and `JSMN_STRICT` (strict primitives,
no non-std extensions).
