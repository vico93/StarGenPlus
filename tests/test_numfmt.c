/*
 *	test_numfmt.c -- unit tests for the shared numeric/value-status
 *	helpers (PLAN.MD Phase C acceptance criteria).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "sg_numfmt.h"
#include "sg_value_status.h"
#include "sg_units.h"
#include "const.h"

static int failures = 0;

static void expect_str(const char *what, const char *got, const char *want)
{
	if (strcmp(got, want) != 0)
	{
		fprintf(stderr, "FAIL: %s: got '%s', want '%s'\n", what, got, want);
		++failures;
	}
}

static void expect_true(const char *what, int got)
{
	if (!got)
	{
		fprintf(stderr, "FAIL: %s: expected true\n", what);
		++failures;
	}
}

static void expect_false(const char *what, int got)
{
	if (got)
	{
		fprintf(stderr, "FAIL: %s: expected false\n", what);
		++failures;
	}
}

int main(void)
{
	char buf[64];
	long double v;

	/* --- value/status classification --------------------------------- */

	expect_true("0 is available", sg_num_available(0.0L));
	expect_true("1e-40 tiny is available", sg_num_available(1.0e-40L));
	expect_false("inf is a sentinel", sg_num_available(INCREDIBLY_LARGE_NUMBER));
	expect_false("finite fallback sentinel is a sentinel",
				 sg_num_available((long double)9.9999E37));
	expect_false("NaN is a sentinel", sg_num_available((long double)NAN));
	expect_true("1e29 is available", sg_num_available(1.0e29L));
	expect_false("1e31 is a sentinel", sg_num_available(1.0e31L));

	/* --- machine round-trip ------------------------------------------- */

	v = 1.0L / 3.0L;
	sg_fmt_machine(v, buf, sizeof(buf));
	expect_true("machine format round-trips",
				fabsl(v - strtold(buf, NULL)) == 0.0L);

	v = 1.166L / SUN_MASS_IN_EARTH_MASSES;		/* 1.166 EM in solar masses */
	sg_fmt_machine(v, buf, sizeof(buf));
	expect_true("EM(1.166) does not render as zero",
				(strcmp(buf, "0") != 0) && (strcmp(buf, "0.000") != 0));
	expect_true("EM(1.166) round-trips",
				fabsl(v - strtold(buf, NULL)) == 0.0L);

	/* --- human rendering: never show nonzero as zero ------------------- */

	sg_fmt_human(buf, sizeof(buf), 0.0L, 3);
	expect_str("exact zero stays zero", buf, "0.000");

	sg_fmt_human(buf, sizeof(buf), 1.166L, 3);
	expect_str("ordinary value fixed decimals", buf, "1.166");

	sg_fmt_human(buf, sizeof(buf), 1e-9L, 3);
	expect_str("tiny nonzero is not zero", buf, "1e-09");

	sg_fmt_human(buf, sizeof(buf), INCREDIBLY_LARGE_NUMBER, 3);
	expect_str("sentinel renders N/A", buf, "N/A");

	/* --- CSV field rendering ------------------------------------------- */

	sg_fmt_csv_field(192837.406L, buf, sizeof(buf));
	expect_str("csv finite keeps value (shortest round-trip)", buf, "192837.406");

	sg_fmt_csv_field((long double)INFINITY, buf, sizeof(buf));
	expect_str("csv sentinel becomes empty", buf, "");

	/* --- units (BUG-03 acceptance) -------------------------------------- */

	expect_true("1013.25 mbar == 1 atm",
				sg_mbar_to_atm(1013.25L) == 1.0L);
	expect_true("1000 mbar == 1 bar", sg_mbar_to_bar(1000.0L) == 1.0L);
	expect_true("1 bar is NOT 1 atm",
				fabsl(sg_mbar_to_atm(1000.0L) - 1.0L) > 0.01L);

	/* --- mass unit helpers ----------------------------------------------- */

	expect_true("EM conversion is consistent",
				sg_em_to_solar(sg_solar_to_em(0.5L)) == 0.5L);

	if (failures > 0)
	{
		fprintf(stderr, "%d numfmt check(s) failed\n", failures);
		return 1;
	}

	printf("numfmt checks passed\n");
	return 0;
}
