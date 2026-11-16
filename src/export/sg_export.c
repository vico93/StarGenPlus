#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <float.h>
#include <stdarg.h>

#include "const.h"
#include "structs.h"
#include "stargen.h"
#include "enviro.h"
#include "display.h"
#include "sg_jsonw.h"
#include "sg_numfmt.h"
#include "sg_value_status.h"
#include "sg_units.h"
#include "sg_export.h"

#define SG_MAX_STATUSES_PER_BODY	24
#define SG_MAX_WARNINGS				16
#define SG_WARNING_LEN				192

#define SG_STR2_(x)	#x
#define SG_STR_(x)	SG_STR2_(x)

typedef struct status_list {
	char	path[48];
	char	status[24];
	int		used;
} status_entry;

typedef struct status_table {
	status_entry	entries[SG_MAX_STATUSES_PER_BODY];
	int				count;
} status_table;

typedef struct warning_list {
	char	items[SG_MAX_WARNINGS][SG_WARNING_LEN];
	int		count;
	int		overflowed;
} warning_list;

static void warn(warning_list *wl, const char *fmt, ...)
{
	va_list	ap;

	if (wl->count >= SG_MAX_WARNINGS)
	{
		wl->overflowed = 1;
		return;
	}

	va_start(ap, fmt);
	vsnprintf(wl->items[wl->count], SG_WARNING_LEN, fmt, ap);
	va_end(ap);
	++wl->count;
}

static void status_add(status_table *st, const char *path, const char *status)
{
	if (st->count >= SG_MAX_STATUSES_PER_BODY)
		return;

	strncpy(st->entries[st->count].path, path,
			sizeof(st->entries[st->count].path) - 1);
	st->entries[st->count].path[sizeof(st->entries[st->count].path) - 1] = '\0';
	strncpy(st->entries[st->count].status, status,
			sizeof(st->entries[st->count].status) - 1);
	st->entries[st->count].status[sizeof(st->entries[st->count].status) - 1] = '\0';
	++st->count;
}

static int status_table_full(const status_table *st)
{
	return st->count >= SG_MAX_STATUSES_PER_BODY;
}

static int is_giant_class(planet_type type)
{
	return (type == tGasGiant)
		|| (type == tSubGasGiant)
		|| (type == tSubSubGasGiant);
}

static const ChemTable *find_chem(int num)
{
	int	n;

	for (n = 0; n < max_gas; n++)
	{
		if (gases[n].num == num)
			return &gases[n];
	}

	return NULL;
}

/* ------------------------------------------------------------------ */
/* Body export                                                         */
/* ------------------------------------------------------------------ */

static void export_body(sg_json_writer		*w,
						planet_pointer		 body,
						const char			*id,
						const char			*parent_id,
						int					 planet_no,
						int					 moon_no,
						planet_pointer		 first_moon,
						warning_list		*wl)
{
	status_table	 statuses;
	int				 giant = is_giant_class(body->type);
	char			 path[64];

	memset(&statuses, 0, sizeof(statuses));

	sg_json_object_begin(w, NULL);

	sg_json_string(w, "id", id);
	if (parent_id != NULL)
		sg_json_string(w, "parent", parent_id);
	else
		sg_json_null(w, "parent");
	sg_json_int(w, "planet_no", planet_no);
	if (moon_no > 0)
		sg_json_int(w, "moon_no", moon_no);
	sg_json_string(w, "type", type_string(body->type));

	/* --- heliocentric orbit ----------------------------------------- */
	sg_json_object_begin(w, "orbit");
	sg_json_number_or_null(w, "a_au", body->a, sg_num_available(body->a));
	sg_json_number_or_null(w, "e", body->e, sg_num_available(body->e));
	sg_json_int(w, "orbit_zone", body->orbit_zone);
	sg_json_number_or_null(w, "orb_period_days", body->orb_period,
						   sg_num_available(body->orb_period));
	sg_json_object_end(w);

	/* --- planet-centric lunar orbit (moons only) --------------------- */
	if (parent_id != NULL)
	{
		sg_json_object_begin(w, "parent_orbit");

		if (sg_num_available(body->moon_a) && (body->moon_a > 0.0))
		{
			/* moon_a is carried in AU here (converted from km during
			 * enrichment); moon_e is the eccentricity. */
			sg_json_number_or_null(w, "semi_major_axis_au", body->moon_a,
								   sg_num_available(body->moon_a));
			sg_json_number_or_null(w, "eccentricity", body->moon_e,
								   sg_num_available(body->moon_e));
		}
		else
		{
			sg_json_null(w, "semi_major_axis_au");
			sg_json_null(w, "eccentricity");
			snprintf(path, sizeof(path), "parent_orbit");
			status_add(&statuses, path, "no_permitted_modeled_orbit");
		}

		sg_json_object_end(w);
	}

	/* --- mass --------------------------------------------------------- */
	sg_json_object_begin(w, "mass");
	sg_json_number_or_null(w, "solar", body->mass, sg_num_available(body->mass));
	sg_json_number_or_null(w, "earth",
						   body->mass * SUN_MASS_IN_EARTH_MASSES,
						   sg_num_available(body->mass));
	sg_json_number_or_null(w, "dust_solar", body->dust_mass,
						   sg_num_available(body->dust_mass));
	sg_json_number_or_null(w, "dust_earth",
						   body->dust_mass * SUN_MASS_IN_EARTH_MASSES,
						   sg_num_available(body->dust_mass));
	sg_json_number_or_null(w, "gas_solar", body->gas_mass,
						   sg_num_available(body->gas_mass));
	sg_json_number_or_null(w, "gas_earth",
						   body->gas_mass * SUN_MASS_IN_EARTH_MASSES,
						   sg_num_available(body->gas_mass));
	sg_json_object_end(w);

	/* --- physical ------------------------------------------------------ */
	sg_json_object_begin(w, "physical");
	sg_json_number_or_null(w, "core_radius_km", body->core_radius,
						   sg_num_available(body->core_radius));
	sg_json_number_or_null(w, "radius_km", body->radius,
						   sg_num_available(body->radius));
	sg_json_number_or_null(w, "radius_earth",
						   body->radius / KM_EARTH_RADIUS,
						   sg_num_available(body->radius));
	sg_json_number_or_null(w, "density_g_cc", body->density,
						   sg_num_available(body->density));
	sg_json_number_or_null(w, "escape_velocity_cm_s", body->esc_velocity,
						   sg_num_available(body->esc_velocity));
	sg_json_number_or_null(w, "surf_accel_cm_s2", body->surf_accel,
						   sg_num_available(body->surf_accel));
	sg_json_number_or_null(w, "surf_grav_earth", body->surf_grav,
						   sg_num_available(body->surf_grav));
	sg_json_number_or_null(w, "rms_velocity_cm_s", body->rms_velocity,
						   sg_num_available(body->rms_velocity));
	sg_json_number_or_null(w, "molec_weight_retained", body->molec_weight,
						   sg_num_available(body->molec_weight));
	sg_json_object_end(w);

	/* --- rotation ------------------------------------------------------- */
	sg_json_object_begin(w, "rotation");
	sg_json_number_or_null(w, "day_hours", body->day,
						   sg_num_available(body->day));
	sg_json_bool(w, "resonant", body->resonant_period ? 1 : 0);
	sg_json_bool(w, "tidally_locked",
				 (sg_num_available(body->day)
				  && ((int)body->day == (int)(body->orb_period * 24.0))) ? 1 : 0);
	sg_json_number_or_null(w, "axial_tilt_deg", body->axial_tilt,
						   sg_num_available(body->axial_tilt));
	sg_json_object_end(w);

	/* --- surface (gas giants: not modeled) ------------------------------ */
	sg_json_object_begin(w, "surface");

	if (giant)
	{
		sg_json_null(w, "pressure_mbar");
		sg_json_null(w, "pressure_atm");
		sg_json_null(w, "temperature_k");
		sg_json_null(w, "greenhouse_rise_k");
		sg_json_null(w, "boil_point_k");
		sg_json_null(w, "high_temp_k");
		sg_json_null(w, "low_temp_k");
		sg_json_null(w, "max_temp_k");
		sg_json_null(w, "min_temp_k");
		sg_json_null(w, "hydrosphere_fraction");
		sg_json_null(w, "cloud_fraction");
		sg_json_null(w, "ice_fraction");
		sg_json_null(w, "volatile_gas_inventory");
		sg_json_bool(w, "greenhouse_effect", 0);

		snprintf(path, sizeof(path), "surface");
		status_add(&statuses, path, "not_applicable");	/* surface not modeled */
		snprintf(path, sizeof(path), "surface.temps_range");
		status_add(&statuses, path, "not_computed");
	}
	else
	{
		sg_json_number_or_null(w, "pressure_mbar", body->surf_pressure,
							   sg_num_available(body->surf_pressure));
		sg_json_number_or_null(w, "pressure_atm",
							   sg_mbar_to_atm(body->surf_pressure),
							   sg_num_available(body->surf_pressure));
		sg_json_number_or_null(w, "temperature_k", body->surf_temp,
							   sg_num_available(body->surf_temp));
		sg_json_number_or_null(w, "greenhouse_rise_k", body->greenhs_rise,
							   sg_num_available(body->greenhs_rise));
		sg_json_number_or_null(w, "boil_point_k", body->boil_point,
							   sg_num_available(body->boil_point));
		sg_json_number_or_null(w, "high_temp_k", body->high_temp,
							   sg_num_available(body->high_temp));
		sg_json_number_or_null(w, "low_temp_k", body->low_temp,
							   sg_num_available(body->low_temp));
		sg_json_number_or_null(w, "max_temp_k", body->max_temp,
							   sg_num_available(body->max_temp));
		sg_json_number_or_null(w, "min_temp_k", body->min_temp,
							   sg_num_available(body->min_temp));
		sg_json_number_or_null(w, "hydrosphere_fraction", body->hydrosphere,
							   sg_num_available(body->hydrosphere));
		sg_json_number_or_null(w, "cloud_fraction", body->cloud_cover,
							   sg_num_available(body->cloud_cover));
		sg_json_number_or_null(w, "ice_fraction", body->ice_cover,
							   sg_num_available(body->ice_cover));
		sg_json_number_or_null(w, "volatile_gas_inventory",
							   body->volatile_gas_inventory,
							   sg_num_available(body->volatile_gas_inventory));
		sg_json_bool(w, "greenhouse_effect",
					 body->greenhouse_effect ? 1 : 0);

		/* Any sentinel on a non-giant body is unexpected: report it as a
		 * status (and let the driver add a warning) rather than leaking
		 * it as a number. */
		if (!sg_num_available(body->day))
		{
			snprintf(path, sizeof(path), "rotation.day_hours");
			status_add(&statuses, path,
					   isnan((double) body->day) ? "numerical_error"
												 : "unbounded");
			warn(wl, "%s: rotation day is a sentinel value", id);
		}
	}

	sg_json_object_end(w);

	/* --- thermal estimates (all body classes) ---------------------------- */
	sg_json_object_begin(w, "thermal_estimates");
	sg_json_number_or_null(w, "estimated_temp_k", body->estimated_temp,
						   sg_num_available(body->estimated_temp));
	sg_json_number_or_null(w, "estimated_terr_temp_k", body->estimated_terr_temp,
						   sg_num_available(body->estimated_terr_temp));
	sg_json_number_or_null(w, "albedo", body->albedo,
						   sg_num_available(body->albedo));
	sg_json_number_or_null(w, "exospheric_temp_k", body->exospheric_temp,
						   sg_num_available(body->exospheric_temp));
	sg_json_object_end(w);

	/* --- atmosphere ------------------------------------------------------- */
	sg_json_object_begin(w, "atmosphere");

	if ((body->gases > 0) && (body->atmosphere != NULL))
	{
		int	i;

		sg_json_bool(w, "computed", 1);
		sg_json_null(w, "not_computed_reason");
		sg_json_array_begin(w, "gases");

		for (i = 0; i < body->gases; i++)
		{
			const ChemTable	*chem = find_chem(body->atmosphere[i].num);
			long double		 ipp;

			sg_json_object_begin(w, NULL);

			sg_json_int(w, "id", body->atmosphere[i].num);
			sg_json_string(w, "symbol", (chem != NULL) ? chem->symbol : "?");
			sg_json_string(w, "name", (chem != NULL) ? chem->name : "unknown");
			sg_json_number_or_null(w, "partial_pressure_mbar",
								   body->atmosphere[i].surf_pressure,
								   sg_num_available(body->atmosphere[i].surf_pressure));

			if ((chem != NULL) && (chem->max_ipp < SG_SENTINEL_LIMIT))
				sg_json_number(w, "max_ipp_mbar", chem->max_ipp);
			else
				sg_json_null(w, "max_ipp_mbar");

			ipp = inspired_partial_pressure(body->surf_pressure,
											body->atmosphere[i].surf_pressure);
			sg_json_number_or_null(w, "inspired_partial_pressure_mbar", ipp,
								   sg_num_available(ipp));
			sg_json_number_or_null(w, "fraction",
								   body->atmosphere[i].surf_pressure /
								   body->surf_pressure,
								   sg_num_available(body->surf_pressure) &&
								   (body->surf_pressure > 0.0));

			sg_json_bool(w, "poisonous",
						 (ipp > ((chem != NULL) ? chem->max_ipp : 0.0)) ? 1 : 0);

			sg_json_object_end(w);
		}

		sg_json_array_end(w);

		{
			unsigned int breathe = breathability(body);

			sg_json_string(w, "breathability",
						   (breathe <= POISONOUS) ? breathability_phrase[breathe]
												  : "unknown");
		}
	}
	else
	{
		sg_json_bool(w, "computed", 0);

		if (giant)
		{
			sg_json_string(w, "not_computed_reason", "not_applicable");
			sg_json_null(w, "breathability");
		}
		else if (body->surf_pressure <= 0.0)
		{
			sg_json_string(w, "not_computed_reason", "surface_pressure_zero");
			sg_json_string(w, "breathability", "none");
		}
		else
		{
			sg_json_string(w, "not_computed_reason", "not_computed");
			sg_json_null(w, "breathability");
		}

		sg_json_array_begin(w, "gases");
		sg_json_array_end(w);
	}

	sg_json_object_end(w);

	/* --- classification ---------------------------------------------------- */
	sg_json_object_begin(w, "classification");
	sg_json_bool(w, "gas_giant", body->gas_giant ? 1 : 0);
	sg_json_bool(w, "habitable", sg_is_habitable_body(body) ? 1 : 0);
	sg_json_bool(w, "earthlike", sg_is_earthlike_body(body) ? 1 : 0);
	if (giant)
		sg_json_bool(w, "habitable_jovian", sg_is_habitable_jovian(body) ? 1 : 0);
	else
		sg_json_null(w, "habitable_jovian");
	sg_json_int(w, "minor_moons", body->minor_moons);
	sg_json_object_end(w);

	/* --- statuses (only when non-ok) ---------------------------------------- */
	if (statuses.count > 0)
	{
		int	i;

		sg_json_object_begin(w, "statuses");

		for (i = 0; i < statuses.count; i++)
			sg_json_string(w, statuses.entries[i].path,
						   statuses.entries[i].status);

		if (status_table_full(&statuses))
			sg_json_string(w, "truncated", "too many statuses to report");

		sg_json_object_end(w);
	}

	/* --- moons (nested; moons never carry moons of their own) --------------- */
	if (first_moon != NULL)
	{
		planet_pointer	moon;
		int				mno = 0;

		sg_json_array_begin(w, "moons");

		for (moon = first_moon; moon != NULL; moon = moon->next_planet)
		{
			char moon_id[32];

			++mno;
			snprintf(moon_id, sizeof(moon_id), "%s/m%d", id, mno);
			export_body(w, moon, moon_id, id, planet_no, mno, NULL, wl);
		}

		sg_json_array_end(w);
	}

	sg_json_object_end(w);
}

/* ------------------------------------------------------------------ */
/* Document assembly                                                   */
/* ------------------------------------------------------------------ */

static void export_engine_block(sg_json_writer *w)
{
	const char	*rev = stargen_revision;		/* "$Revision: 1.43 $"	*/
	char		 upstream[32] = "unknown";
	const char	*p;

	sg_json_object_begin(w, "engine");

	sg_json_string(w, "name", "StarGen");
	sg_json_string(w, "fork", "StarGenPlus");

	if ((p = strstr(rev, "Revision: ")) != NULL)
	{
		const char	*d = p + strlen("Revision: ");
		int			 i = 0;

		while ((*d != '\0') && (*d != ' ') && (i < (int) sizeof(upstream) - 1))
			upstream[i++] = *d++;

		upstream[i] = '\0';
	}

	sg_json_string(w, "upstream_revision", upstream);
	sg_json_int(w, "json_schema_version", SG_JSON_SCHEMA_VERSION);

#if defined(_MSC_VER)
	sg_json_string(w, "compiler", "msvc-" SG_STR_(_MSC_FULL_VER));
#elif defined(__clang__)
	sg_json_string(w, "compiler", "clang " __clang_version__);
#elif defined(__GNUC__)
	sg_json_string(w, "compiler", "gcc-" SG_STR_(__GNUC__) "." SG_STR_(__GNUC_MINOR__));
#else
	sg_json_string(w, "compiler", "unknown");
#endif

	sg_json_long(w, "stdc_version", (long) __STDC_VERSION__);

	sg_json_object_begin(w, "long_double");
	sg_json_int(w, "bytes", (int) sizeof(long double));
	sg_json_int(w, "mantissa_bits", LDBL_MANT_DIG);
	sg_json_int(w, "decimal_digits", SG_LDBL_DEC_DIG);
	sg_json_object_end(w);

	sg_json_object_begin(w, "prng");
	sg_json_string(w, "algorithm", "CRT rand/srand");
	sg_json_int(w, "rand_max", RAND_MAX);
	sg_json_object_end(w);

	sg_json_int(w, "sizeof_long", (int) sizeof(long));

	sg_json_object_end(w);
}

static void export_body_array(sg_json_writer *w, planet_pointer planets,
							  warning_list *wl)
{
	planet_pointer	planet;
	int				pno = 0;

	sg_json_array_begin(w, "planets");

	for (planet = planets; planet != NULL; planet = planet->next_planet)
	{
		char	id[24];

		++pno;
		snprintf(id, sizeof(id), "p%d", pno);

		export_body(w, planet, id, NULL, pno, 0, planet->first_moon, wl);
	}

	sg_json_array_end(w);
}

static void export_summary(sg_json_writer *w, planet_pointer planets)
{
	planet_pointer	planet;
	int				planet_count = 0;
	int				moon_count = 0;
	int				habitable_planets = 0;
	int				habitable_moons = 0;
	int				earthlike_planets = 0;
	int				habitable_jovians = 0;
	int				type_counts[12];
	int				i;

	for (i = 0; i < 12; i++)
		type_counts[i] = 0;

	for (planet = planets; planet != NULL; planet = planet->next_planet)
	{
		planet_pointer moon;

		++planet_count;
		++type_counts[planet->type];
		if (sg_is_habitable_body(planet))
			++habitable_planets;
		if (sg_is_earthlike_body(planet))
			++earthlike_planets;
		if (is_giant_class(planet->type) && sg_is_habitable_jovian(planet))
			++habitable_jovians;

		for (moon = planet->first_moon; moon != NULL; moon = moon->next_planet)
		{
			++moon_count;
			++type_counts[moon->type];
			if (sg_is_habitable_body(moon))
				++habitable_moons;
		}
	}

	sg_json_object_begin(w, "summary");
	sg_json_int(w, "planet_count", planet_count);
	sg_json_int(w, "moon_count", moon_count);

	sg_json_object_begin(w, "counts_by_type");
	for (i = 0; i < 12; i++)
	{
		if (type_counts[i] > 0)
			sg_json_int(w, type_string((planet_type) i), type_counts[i]);
	}
	sg_json_object_end(w);

	sg_json_int(w, "habitable_planets", habitable_planets);
	sg_json_int(w, "habitable_moons", habitable_moons);
	sg_json_int(w, "earthlike_planets", earthlike_planets);
	sg_json_int(w, "habitable_jovians", habitable_jovians);
	sg_json_object_end(w);
}

int sg_export_catalog_json(FILE *out, const catalog *cat)
{
	sg_json_writer	w;
	int				i;

	if ((out == NULL) || (cat == NULL) || (cat->stars == NULL))
		return 1;

	sg_json_begin(&w, out);

	sg_json_object_begin(&w, NULL);
	sg_json_int(&w, "schema_version", SG_JSON_SCHEMA_VERSION);
	sg_json_string(&w, "status", "ok");
	sg_json_string(&w, "kind", "catalog");
	sg_json_string(&w, "catalog", cat->arg);
	sg_json_int(&w, "star_count", cat->count);

	sg_json_array_begin(&w, "stars");

	for (i = 0; i < cat->count; i++)
	{
		const star	*s = &cat->stars[i];

		sg_json_object_begin(&w, NULL);
		sg_json_int(&w, "index", i);
		sg_json_string(&w, "designation", (s->desig != NULL) ? s->desig : "");
		sg_json_string(&w, "name", (s->name != NULL) ? s->name : "");

		/* A catalog luminosity of 0 means "derive from mass". */
		if (s->luminosity > 0.0)
		{
			sg_json_number(&w, "luminosity", s->luminosity);
			sg_json_null(&w, "luminosity_source");
		}
		else
		{
			sg_json_null(&w, "luminosity");
			sg_json_string(&w, "luminosity_source", "derived_from_mass");
		}

		sg_json_number(&w, "mass_solar", s->mass);
		sg_json_bool(&w, "in_celestia", s->in_celestia ? 1 : 0);
		sg_json_bool(&w, "has_known_planets", (s->known_planets != NULL) ? 1 : 0);

		if (s->m2 > 0.0)
		{
			sg_json_object_begin(&w, "binary_companion");
			sg_json_number(&w, "mass_solar", s->m2);
			sg_json_number(&w, "eccentricity", s->e);
			sg_json_number(&w, "semi_major_axis_au", s->a);
			sg_json_object_end(&w);
		}
		else
		{
			sg_json_null(&w, "binary_companion");
		}

		sg_json_object_end(&w);
	}

	sg_json_array_end(&w);

	sg_json_object_begin(&w, "notes");
	sg_json_string(&w, "model",
				   "Synthetic systems from the historical StarGen accretion "
				   "model (Dole/Fogg lineage); not modern astronomical "
				   "predictions.");
	sg_json_string(&w, "mass_range",
				   "Generation supports stellar masses in [0.2, 1.5] "
				   "solar masses; the Solar System (index 0) is a template.");
	sg_json_object_end(&w);

	sg_json_null(&w, "error");
	sg_json_object_end(&w);

	return sg_json_ok(&w) ? 0 : 1;
}

int sg_export_system_json(FILE *out, const sg_export_context *ctx)
{
	sg_json_writer	w;
	warning_list	wl;

	memset(&wl, 0, sizeof(wl));

	if ((out == NULL) || (ctx == NULL) || (ctx->star == NULL))
		return 1;

	sg_json_begin(&w, out);

	sg_json_object_begin(&w, NULL);
	sg_json_int(&w, "schema_version", SG_JSON_SCHEMA_VERSION);

	/* "status" is emitted near the end of the document, after the
	 * warnings are known (a streaming writer cannot rewrite keys). */

	export_engine_block(&w);

	/* --- request ----------------------------------------------------- */
	sg_json_object_begin(&w, "request");
	if (ctx->cat != NULL)
	{
		sg_json_string(&w, "source", "catalog");
		sg_json_string(&w, "catalog", ctx->cat->arg);
		sg_json_int(&w, "star_index", ctx->sys_no);
		sg_json_null(&w, "mass_solar");
	}
	else
	{
		sg_json_string(&w, "source", "mass");
		sg_json_null(&w, "catalog");
		sg_json_null(&w, "star_index");
		if (ctx->requested_mass > 0.0)
			sg_json_number(&w, "mass_solar", ctx->requested_mass);
		else
			sg_json_null(&w, "mass_solar");
	}

	if (ctx->seed_automatic)
		sg_json_null(&w, "seed");
	else
		sg_json_long(&w, "seed", ctx->seed);

	sg_json_bool(&w, "gases", ctx->requested_gases ? 1 : 0);
	sg_json_bool(&w, "moons", ctx->requested_moons ? 1 : 0);
	sg_json_object_end(&w);

	/* --- effective ---------------------------------------------------- */
	sg_json_object_begin(&w, "effective");
	sg_json_long(&w, "seed", ctx->seed);
	sg_json_string(&w, "seed_source",
				   ctx->seed_automatic ? "automatic" : "requested");
	sg_json_number(&w, "mass_solar", ctx->star->mass);
	sg_json_number(&w, "luminosity", ctx->star->luminosity);
	sg_json_number(&w, "ecosphere_radius_au", ctx->star->r_ecosphere);
	sg_json_number(&w, "star_lifetime_years", ctx->star->life);
	sg_json_number(&w, "star_age_years", ctx->star->age);
	sg_json_number(&w, "dust_density_coeff", dust_density_coeff);
	if (ctx->outer_limit > 0.0)
		sg_json_number(&w, "outer_orbit_limit_au", ctx->outer_limit);
	else
		sg_json_null(&w, "outer_orbit_limit_au");
	sg_json_bool(&w, "gases_enabled", ctx->effective_gases ? 1 : 0);
	sg_json_bool(&w, "moons_enabled", ctx->effective_moons ? 1 : 0);
	sg_json_object_end(&w);

	/* --- star ----------------------------------------------------------- */
	sg_json_object_begin(&w, "star");
	sg_json_string(&w, "name", (ctx->star->name != NULL) ? ctx->star->name : "");
	if (ctx->cat != NULL)
		sg_json_string(&w, "designation", ctx->cat->stars[ctx->sys_no].desig);
	else
		sg_json_null(&w, "designation");
	sg_json_number(&w, "mass_solar", ctx->star->mass);
	sg_json_number(&w, "luminosity", ctx->star->luminosity);
	sg_json_number(&w, "age_years", ctx->star->age);
	sg_json_number(&w, "lifetime_years", ctx->star->life);
	sg_json_number(&w, "ecosphere_radius_au", ctx->star->r_ecosphere);
	sg_json_object_end(&w);

	/* --- summary + bodies ------------------------------------------------- */
	export_summary(&w, ctx->planets);
	export_body_array(&w, ctx->planets, &wl);

	/* --- warnings ------------------------------------------------------------ */
	sg_json_array_begin(&w, "warnings");
	{
		int	i;

		for (i = 0; i < wl.count; i++)
			sg_json_string(&w, NULL, wl.items[i]);

		if (wl.overflowed)
			sg_json_string(&w, NULL, "warning list truncated");

		if (ctx->requested_moons && !ctx->effective_moons)
			sg_json_string(&w, NULL, "moons requested but not enabled");
	}
	sg_json_array_end(&w);

	/* --- status + error ------------------------------------------------------ */
	sg_json_string(&w, "status", (wl.count > 0) ? "partial" : "ok");
	sg_json_null(&w, "error");
	sg_json_object_end(&w);

	return sg_json_ok(&w) ? 0 : 1;
}
