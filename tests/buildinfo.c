/*
 *	buildinfo.c -- StarGenPlus build-identity probe (PLAN.MD Phase A)
 *
 *	Prints the compiler/CRT identity and the floating-point facts that
 *	govern reproducibility of generated systems, plus the upstream source
 *	revision compiled into the engine.
 *
 *	Reproducibility contract: generated systems are reproducible for the
 *	same compiled build only. In particular MSVC's long double is 64-bit
 *	(== double, LDBL_MANT_DIG == 53), while GCC/Clang on x86-64 typically
 *	provide an 80-bit long double, so cross-compiler outputs differ.
 */

#include <stdio.h>
#include <stdlib.h>
#include <float.h>
#include <limits.h>

#include "structs.h"	/* stargen.h is not yet self-contained (Phase B) */
#include "stargen.h"

int main(void)
{
	printf("stargen_revision: %s\n", stargen_revision);
	printf("compiler:\n");
#if defined(_MSC_VER)
	printf("  _MSC_VER: %d\n", _MSC_VER);
#if defined(_MSC_FULL_VER)
	printf("  _MSC_FULL_VER: %d\n", _MSC_FULL_VER);
#endif
#if defined(_MSVC_LANG)
	printf("  _MSVC_LANG: %ld\n", (long)_MSVC_LANG);
#endif
#elif defined(__clang__)
	printf("  clang %d.%d.%d\n", __clang_major__, __clang_minor__, __clang_patchlevel__);
#elif defined(__GNUC__)
	printf("  gcc %d.%d.%d\n", __GNUC__, __GNUC_MINOR__, __GNUC_PATCHLEVEL__);
#else
	printf("  unknown\n");
#endif
	printf("  __STDC_VERSION__: %ld\n", (long)__STDC_VERSION__);

	printf("long double:\n");
	printf("  sizeof: %zu\n", sizeof(long double));
	printf("  LDBL_MANT_DIG: %d\n", LDBL_MANT_DIG);
	printf("  LDBL_DIG: %d\n", LDBL_DIG);
#if defined(LDBL_DECIMAL_DIG)
	printf("  LDBL_DECIMAL_DIG: %d\n", LDBL_DECIMAL_DIG);
#else
	printf("  LDBL_DECIMAL_DIG: (not defined by this toolchain)\n");
#endif
#if defined(DECIMAL_DIG)
	printf("  DECIMAL_DIG: %d\n", DECIMAL_DIG);
#else
	printf("  DECIMAL_DIG: (not defined by this toolchain)\n");
#endif
	printf("  LDBL_MAX: %Lg\n", LDBL_MAX);
	printf("  LDBL_EPSILON: %Lg\n", LDBL_EPSILON);

	printf("double:\n");
	printf("  sizeof: %zu\n", sizeof(double));
	printf("  DBL_MANT_DIG: %d\n", DBL_MANT_DIG);
	printf("  DBL_DIG: %d\n", DBL_DIG);

	printf("PRNG:\n");
	printf("  RAND_MAX: %d\n", RAND_MAX);
	printf("  sizeof(long): %zu\n", sizeof(long));

	return 0;
}
