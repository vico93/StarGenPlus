/*
 *	sg_mcp.h -- Model Context Protocol stdio server (PLAN.MD Phase E).
 *
 *	Dual-era implementation:
 *	  - Modern revision 2026-07-28: stateless per-request versioning via
 *	    params._meta["io.modelcontextprotocol/protocolVersion"]; mandatory
 *	    `server/discover`; UnsupportedProtocolVersionError (-32022) lists
 *	    the supported versions.
 *	  - Legacy revisions (<= 2025-11-25): the `initialize` handshake;
 *	    requests before initialization are rejected with -32002.
 *
 *	Transport: newline-delimited JSON-RPC 2.0 on stdin/stdout. stdout is
 *	protocol-only; all diagnostics go to stderr. Generation runs in
 *	fresh worker subprocesses (sg_worker.h) with bounded time/output.
 */

#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Run the server until stdin EOF. Returns the process exit code
 * (0 on clean shutdown). */
int sg_mcp_server_run(FILE *in, FILE *out, FILE *err);

#ifdef __cplusplus
}
#endif
