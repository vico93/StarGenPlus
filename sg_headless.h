/*
 *	sg_headless.h -- one-job headless modes (PLAN.MD Phase D).
 *
 *	These entry points retain the generation and chemistry setup of the
 *	legacy driver but bypass ALL presentation setup/finalization: they
 *	create no files, require no html/ or ref/ data, and write exactly
 *	one JSON document to the output stream. Diagnostics go to stderr.
 *
 *	Exit-code contract (returned to the caller / process):
 *	  0  success (the document is complete and valid)
 *	  2  invalid usage / arguments (usage message on stderr)
 *	  3  generation or export failure (diagnostic on stderr)
 */

#pragma once

#include <stdio.h>

#include "structs.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum sg_headless_command {
	sg_cmd_none = 0,
	sg_cmd_json,			/* --json: generate one system			*/
	sg_cmd_list_stars_json,	/* --list-stars-json: catalog listing	*/
	sg_cmd_mcp				/* --mcp: stdio MCP server (Phase E)	*/
} sg_headless_command;

typedef struct sg_headless_request {
	int				from_catalog;	/* nonzero: generate from catalog	*/
	const catalog	*cat;
	int				star_index;		/* 0-based index into the catalog	*/
	long double		mass_solar;		/* 0 = automatic (legacy behavior)	*/
	long			seed;			/* 0 = automatic					*/
	int				seed_automatic;
	int				do_gases;
	int				do_moons;
} sg_headless_request;

/* Parse the new-style arguments and run the requested headless command.
 * Returns the process exit code (0/2/3). */
int sg_headless_main(int argc, char *argv[], sg_headless_command command);

/* The two headless operations, also used by the MCP server (Phase E). */
int sg_run_json(FILE *out, FILE *err, const sg_headless_request *req);
int sg_run_list_stars_json(FILE *out, FILE *err, const catalog *cat);

#ifdef __cplusplus
}
#endif
