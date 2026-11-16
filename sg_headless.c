#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <errno.h>

#include "const.h"
#include "structs.h"
#include "stargen.h"
#include "sg_headless.h"
#include "sg_export.h"

/*
 *	Strict full-token numeric parsing (PLAN.MD §5): the entire token
 *	must be consumed, the result must be finite and in range.
 */
static int parse_long(const char *s, long *out)
{
	char	*end	= NULL;
	long	 v;

	errno = 0;
	v = strtol(s, &end, 10);

	if ((end == NULL) || (end == s) || (*end != '\0') || (errno == ERANGE))
		return 0;

	*out = v;
	return 1;
}

static int parse_double(const char *s, double *out)
{
	char	*end	= NULL;
	double	 v;

	errno = 0;
	v = strtod(s, &end);

	if ((end == NULL) || (end == s) || (*end != '\0') || (errno == ERANGE))
		return 0;

	if (!isfinite(v))
		return 0;

	*out = v;
	return 1;
}

static const catalog *lookup_catalog(const char *name)
{
	if (strcmp(name, "solstation") == 0)
		return &solstation;
	if (strcmp(name, "dole") == 0)
		return &dole;
	if (strcmp(name, "jimb") == 0)
		return &jimb;

	return NULL;
}

static void headless_usage(FILE *err, const char *prognam)
{
	fprintf(err,
			"Usage: %s --json [options]\n"
			"       %s --list-stars-json --catalog <name>\n"
			"\n"
			"Headless modes write exactly one JSON document to stdout;\n"
			"diagnostics go to stderr; no files are created.\n"
			"\n"
			"Options:\n"
			"  --catalog <name>      solstation | dole | jimb\n"
			"  --star-index <n>      0-based index into the catalog\n"
			"                        (required with --catalog for --json)\n"
			"  --mass-solar <x>      primary mass in solar masses [0.2, 1.5]\n"
			"                        (conflicts with --catalog)\n"
			"  --seed <n>            PRNG seed (0 or omitted: automatic)\n"
			"  --gases | --no-gases  atmosphere computation (default: on)\n"
			"  --moons | --no-moons  experimental moon generation (default: off)\n"
			"\n"
			"Exit codes: 0 success, 2 invalid arguments, 3 failure.\n",
			prognam, prognam);
}

/* ------------------------------------------------------------------ */

long double catalog_outer_limit(const catalog *cat, int sys_no,
								long double primary_mass)
{
	if ((cat != NULL) && (cat->stars != NULL)
		&& (sys_no >= 0) && (cat->stars[sys_no].m2 > .001))
	{
		/*
		 *	Holman & Wiegert's equation 1 from "Long-Term Stability of
		 *	Planets in Binary Systems", The Astronomical Journal,
		 *	117:621-628, Jan 1999.
		 */
		long double m1 = primary_mass;
		long double m2 = cat->stars[sys_no].m2;
		long double mu = m2 / (m1 + m2);
		long double e = cat->stars[sys_no].e;
		long double a = cat->stars[sys_no].a;

		return (0.464 + (-0.380 * mu) + (-0.631 * e)
			   + (0.586 * mu * e) + (0.150 * pow2(e))
			   + (-0.198 * mu * pow2(e))) * a;
	}

	return 0.0;
}

int sg_run_list_stars_json(FILE *out, FILE *err, const catalog *cat)
{
	if (cat == NULL)
	{
		fprintf(err, "--list-stars-json requires --catalog <name>\n");
		return 2;
	}

	return sg_export_catalog_json(out, cat) ? 3 : 0;
}

int sg_run_json(FILE *out, FILE *err, const sg_headless_request *req)
{
	sun					primary;
	char				system_name[80];
	long double			outer_limit		= 0.0;
	int					use_seed_system	= FALSE;
	planet_pointer		seed_planets	= NULL;
	sg_export_context	ctx;

	if ((out == NULL) || (err == NULL) || (req == NULL))
		return 2;

	if (req->from_catalog)
	{
		if ((req->cat == NULL)
			|| (req->star_index < 0)
			|| (req->star_index >= req->cat->count))
		{
			fprintf(err, "star index out of range\n");
			return 2;
		}

		if ((req->mass_solar != 0.0)
			&& ((req->mass_solar < 0.2) || (req->mass_solar > 1.5)))
		{
			fprintf(err,
					"explicit stellar mass %g is outside the supported "
					"range [0.2, 1.5]\n", (double) req->mass_solar);
			return 2;
		}
	}
	else
	{
		if ((req->mass_solar != 0.0)
			&& ((req->mass_solar < 0.2) || (req->mass_solar > 1.5)))
		{
			fprintf(err,
					"explicit stellar mass %g is outside the supported "
					"range [0.2, 1.5]\n", (double) req->mass_solar);
			return 2;
		}
	}

	memset(&primary, 0, sizeof(primary));

	flag_seed = req->seed;

	init_chemistry();
	reset_run_statistics();
	init();

	if (req->from_catalog)
	{
		const star	*cs = &req->cat->stars[req->star_index];

		primary.mass		= cs->mass;
		primary.luminosity	= cs->luminosity;

		snprintf(system_name, sizeof(system_name), "%s",
				 (cs->name != NULL) ? cs->name : "unnamed");

		outer_limit = catalog_outer_limit(req->cat, req->star_index,
										  primary.mass);

		if (req->star_index == 0)
		{
			/* Catalog entry 0 is always Sol, generated from the Solar
			 * System template (legacy driver behavior for the first
			 * catalog system). */
			seed_planets	= solar_system;
			use_seed_system	= TRUE;
		}
	}
	else
	{
		primary.mass = req->mass_solar;		/* 0 = legacy automatic mode */

		snprintf(system_name, sizeof(system_name), "StarGen %ld-%LG",
				 flag_seed, primary.mass);
	}

	primary.name = system_name;

	generate_stellar_system(&primary,
							use_seed_system,
							seed_planets,
							'z',				// flag_char (unused here)
							0,					// sys_no (unused here)
							system_name,
							outer_limit,
							req->do_gases,
							req->do_moons,
							0.0);

	if (innermost_planet == NULL)
	{
		fprintf(err, "generation failed (out of memory)\n");
		free_generations();
		return 3;
	}

	memset(&ctx, 0, sizeof(ctx));
	ctx.star				= &primary;
	ctx.planets				= innermost_planet;
	ctx.seed				= flag_seed;
	ctx.seed_automatic		= req->seed_automatic;
	ctx.requested_gases		= req->do_gases;
	ctx.requested_moons		= req->do_moons;
	ctx.effective_gases		= req->do_gases;
	ctx.effective_moons		= req->do_moons;
	ctx.cat					= req->from_catalog ? req->cat : NULL;
	ctx.sys_no				= req->star_index;
	ctx.requested_mass		= req->mass_solar;
	ctx.outer_limit			= outer_limit;

	{
		int rc = sg_export_system_json(out, &ctx);

		free_atmosphere(innermost_planet);
		free_planet(innermost_planet);
		innermost_planet = NULL;
		free_generations();

		if (rc != 0)
		{
			fprintf(err, "JSON export failed (write error)\n");
			return 3;
		}
	}

	return 0;
}

/* ------------------------------------------------------------------ */

int sg_headless_main(int argc, char *argv[], sg_headless_command command)
{
	sg_headless_request	req;
	const char			*prognam = "StarGen";
	int					i;
	int					has_catalog = 0, has_index = 0, has_mass = 0;
	int					has_seed = 0;
	const catalog		*cat = NULL;

	memset(&req, 0, sizeof(req));
	req.do_gases = 1;			/* gases default ON for headless work	*/
	req.do_moons = 0;			/* moons are opt-in and experimental	*/

	if (argc > 0)
	{
		const char *arg0 = argv[0];
		const char *slash = strrchr(arg0, '\\');
		const char *slash2 = strrchr(arg0, '/');

		if ((slash2 != NULL) && (slash2 > slash))
			slash = slash2;
		prognam = (slash != NULL) ? (slash + 1) : arg0;
	}

	for (i = 1; i < argc; i++)
	{
		const char *arg = argv[i];

		if (strcmp(arg, "--json") == 0)
		{
			/* the command token itself; already dispatched			*/
		}
		else if (strcmp(arg, "--list-stars-json") == 0)
		{
			/* command token											*/
		}
		else if (strcmp(arg, "--mcp") == 0)
		{
			/* command token (Phase E)									*/
		}
		else if (strcmp(arg, "--catalog") == 0)
		{
			if (has_catalog)
			{
				fprintf(stderr, "--catalog given more than once\n");
				headless_usage(stderr, prognam);
				return 2;
			}

			if ((i + 1 >= argc)
				|| ((cat = lookup_catalog(argv[i + 1])) == NULL))
			{
				fprintf(stderr, "--catalog requires one of: solstation, dole, jimb\n");
				return 2;
			}

			has_catalog = 1;
			++i;
		}
		else if (strcmp(arg, "--star-index") == 0)
		{
			if (has_index)
			{
				fprintf(stderr, "--star-index given more than once\n");
				return 2;
			}

			if ((i + 1 >= argc) || !parse_long(argv[i + 1], &req.star_index))
			{
				fprintf(stderr, "--star-index requires an integer\n");
				return 2;
			}

			has_index = 1;
			++i;
		}
		else if (strcmp(arg, "--mass-solar") == 0)
		{
			double	m = 0.0;

			if (has_mass)
			{
				fprintf(stderr, "--mass-solar given more than once\n");
				return 2;
			}

			if ((i + 1 >= argc) || !parse_double(argv[i + 1], &m))
			{
				fprintf(stderr, "--mass-solar requires a number\n");
				return 2;
			}

			req.mass_solar = (long double) m;
			has_mass = 1;
			++i;
		}
		else if (strcmp(arg, "--seed") == 0)
		{
			if (has_seed)
			{
				fprintf(stderr, "--seed given more than once\n");
				return 2;
			}

			if ((i + 1 >= argc) || !parse_long(argv[i + 1], &req.seed))
			{
				fprintf(stderr, "--seed requires an integer\n");
				return 2;
			}

			if (req.seed < 0)
			{
				fprintf(stderr, "--seed must be nonnegative\n");
				return 2;
			}

			has_seed = 1;
			++i;
		}
		else if (strcmp(arg, "--gases") == 0)
		{
			req.do_gases = 1;
		}
		else if (strcmp(arg, "--no-gases") == 0)
		{
			req.do_gases = 0;
		}
		else if (strcmp(arg, "--moons") == 0)
		{
			req.do_moons = 1;
		}
		else if (strcmp(arg, "--no-moons") == 0)
		{
			req.do_moons = 0;
		}
		else
		{
			fprintf(stderr, "Unknown argument: %s\n", arg);
			headless_usage(stderr, prognam);
			return 2;
		}
	}

	req.seed_automatic = (req.seed == 0);

	switch (command)
	{
		case sg_cmd_list_stars_json:
			if (!has_catalog)
			{
				fprintf(stderr, "--list-stars-json requires --catalog <name>\n");
				return 2;
			}

			return sg_run_list_stars_json(stdout, stderr, cat);

		case sg_cmd_json:
			req.cat = cat;
			req.from_catalog = has_catalog;

			if (has_catalog && !has_index)
			{
				fprintf(stderr,
						"--catalog with --json requires --star-index "
						"(one system per invocation)\n");
				return 2;
			}

			if (!has_catalog && has_index)
			{
				fprintf(stderr, "--star-index requires --catalog\n");
				return 2;
			}

			if (has_catalog && has_mass)
			{
				fprintf(stderr, "--mass-solar conflicts with --catalog\n");
				return 2;
			}

			return sg_run_json(stdout, stderr, &req);

		default:
			fprintf(stderr, "internal error: no headless command\n");
			return 2;
	}
}
