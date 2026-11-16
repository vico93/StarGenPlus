/*
 *	test_fixtures.c -- physical sanity of the named Solar System fixtures
 *	(PLAN.MD Phase B / BUG-04).
 *
 *	Checks, over the whole Solar System template chain (planets and moons):
 *	  1. mass, dust_mass and gas_mass are nonnegative;
 *	  2. component masses are consistent with the gas_giant flag:
 *	       gas giant -> dust_mass == 0, gas_mass == mass
 *	       rocky     -> gas_mass  == 0, dust_mass == mass
 *	  3. Callisto specifically carries the corrected dust mass
 *	     EM(1.62e-2) -- the historical EM(1.62-2) typo evaluated to
 *	     -0.38 Earth masses and drove kothari_radius() to NaN.
 */

#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#include "const.h"
#include "structs.h"
#include "stargen.h"

static int failures = 0;

static void fail(const char *what, const char *name,
				 long double got, long double want)
{
	fprintf(stderr,
			"FAIL: %s: %s (got %.10Lg, want %.10Lg)\n",
			what, name, got, want);
	++failures;
}

static void check_body(const char *kind, const planets *b)
{
	const char *name = (b->planet_no >= 0) ? "fixture body" : "fixture body";

	(void)name;

	if (b->mass < 0.0)
		fail("negative mass", kind, b->mass * SUN_MASS_IN_EARTH_MASSES, 0.0);
	if (b->dust_mass < 0.0)
		fail("negative dust_mass", kind, b->dust_mass * SUN_MASS_IN_EARTH_MASSES, 0.0);
	if (b->gas_mass < 0.0)
		fail("negative gas_mass", kind, b->gas_mass * SUN_MASS_IN_EARTH_MASSES, 0.0);

	{
		const long double tol = 1e-9;
		const long double m		= b->mass * SUN_MASS_IN_EARTH_MASSES;
		const long double dust	= b->dust_mass * SUN_MASS_IN_EARTH_MASSES;
		const long double gas	= b->gas_mass * SUN_MASS_IN_EARTH_MASSES;

		if (b->gas_giant)
		{
			if (fabsl(dust) > tol)
				fail("gas giant with nonzero dust_mass", kind, dust, 0.0);
			if (fabsl(gas - m) > tol)
				fail("gas giant gas_mass != mass", kind, gas, m);
		}
		else
		{
			if (fabsl(gas) > tol)
				fail("rocky body with nonzero gas_mass", kind, gas, 0.0);
			if (fabsl(dust - m) > tol)
				fail("rocky body dust_mass != mass", kind, dust, m);
		}
	}
}

static void check_chain(const char *kind, planet_pointer head)
{
	planet_pointer node;

	for (node = head; node != NULL; node = node->next_planet)
	{
		check_body(kind, node);

		if (node->first_moon != NULL)
			check_chain("moon", node->first_moon);
	}
}

int main(void)
{
	planet_pointer node;
	int saw_callisto = 0;

	check_chain("solar_system", solar_system);

	/* Callisto: orbit 1.259e-2 AU, 4th moon of Jupiter. */
	for (node = solar_system; node != NULL; node = node->next_planet)
	{
		planet_pointer moon;

		for (moon = node->first_moon; moon != NULL; moon = moon->next_planet)
		{
			if (fabsl(moon->a - 1.259e-2) < 1e-9)
			{
				const long double want = 1.62e-2;
				const long double got = moon->dust_mass * SUN_MASS_IN_EARTH_MASSES;

				saw_callisto = 1;

				if (fabsl(got - want) > 1e-9)
					fail("Callisto dust_mass", "jupiter moon 4", got, want);
				else
					printf("Callisto dust_mass = %.6Lg EM (fixed)\n", got);
			}
		}
	}

	if (!saw_callisto)
	{
		fprintf(stderr, "FAIL: Callisto fixture not found in solar_system moons\n");
		++failures;
	}

	if (failures > 0)
	{
		fprintf(stderr, "%d fixture check(s) failed\n", failures);
		return 1;
	}

	printf("fixture checks passed\n");
	return 0;
}
