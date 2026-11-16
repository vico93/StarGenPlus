/*
 *	sg_export.h -- normalized, versioned JSON export of a generated
 *	system (PLAN.MD Phase D, schema_version 1).
 *
 *	The exporter walks the star and the completed body chain and writes
 *	ONE complete JSON document to the caller-owned stream. It never
 *	touches the RNG, never reads or writes files, and shares its body
 *	classification predicates with the CLI statistics (stargen.h).
 *
 *	Value/status policy (see sg_value_status.h): every modeled field is
 *	emitted as a number or null; fields that do not exist for a class of
 *	body (gas-giant surface quantities) or that were not computed carry
 *	an explicit status in the body's "statuses" object. Sentinel values
 *	are never serialized as numbers.
 */

#pragma once

#include <stdio.h>

#include "structs.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SG_JSON_SCHEMA_VERSION	1

typedef struct sg_export_context {
	const sun		*star;			/* resolved primary					*/
	planet_pointer	planets;		/* innermost planet (may be NULL)	*/
	long			seed;			/* effective seed					*/
	int				seed_automatic;	/* nonzero if the seed was chosen	*/
	int				requested_gases;
	int				requested_moons;
	int				effective_gases;
	int				effective_moons;
	const catalog	*cat;			/* NULL when generated from a mass	*/
	int				sys_no;			/* 0-based index within catalog		*/
	long double		requested_mass;	/* 0 = automatic					*/
	long double		outer_limit;	/* binary companion limit (AU, 0)	*/
} sg_export_context;

/* 0 on success (including clean writes), nonzero on write failure or
 * invalid context. */
int sg_export_system_json(FILE *out, const sg_export_context *ctx);

/* Catalog listing: indices, names, parameters, model limitations. */
int sg_export_catalog_json(FILE *out, const catalog *cat);

#ifdef __cplusplus
}
#endif
