/*
 *	sg_units.h -- canonical unit conversions for exporters (PLAN.MD
 *	BUG-03). One standard atmosphere is 1013.25 mbar; one bar is 1000
 *	mbar; one mbar is 100 Pa. The simulation's internal pressure unit is
 *	the millibar throughout, so exporters must never divide by 1000 and
 *	call the result "atmospheres".
 *
 *	Source units reference (structs.h / const.h):
 *	  mass, dust_mass, gas_mass   solar masses
 *	  a, e, moon_a (planet-centric)   AU / unitless
 *	  core_radius, radius, moon_a   km (moon_a converted from km)
 *	  esc_velocity, rms_velocity   cm/sec
 *	  surf_accel                  cm/sec2
 *	  surf_grav                   Earth gees
 *	  surf_pressure               millibars
 *	  temperatures                Kelvin (differences Kelvin)
 *	  life, age                   years
 *	  orb_period                  Earth days
 *	  day                         hours
 *	  hydrosphere/cloud/ice       fractions (0..1)
 */

#pragma once

#include "const.h"

#ifdef __cplusplus
extern "C" {
#endif

#define SG_MBAR_PER_ATM			EARTH_SURF_PRES_IN_MILLIBARS	/* 1013.25	*/
#define SG_MBAR_PER_BAR			MILLIBARS_PER_BAR				/* 1000.0	*/

static inline long double sg_mbar_to_atm(long double mbar)
{
	return mbar / SG_MBAR_PER_ATM;
}

static inline long double sg_mbar_to_bar(long double mbar)
{
	return mbar / SG_MBAR_PER_BAR;
}

static inline long double sg_em_to_solar(long double earth_masses)
{
	return earth_masses / SUN_MASS_IN_EARTH_MASSES;
}

static inline long double sg_solar_to_em(long double solar_masses)
{
	return solar_masses * SUN_MASS_IN_EARTH_MASSES;
}

#ifdef __cplusplus
}
#endif
