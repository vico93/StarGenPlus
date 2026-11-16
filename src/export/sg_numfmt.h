/*
 *	sg_numfmt.h -- numeric formatting helpers shared by all exporters
 *	(PLAN.MD BUG-01, BUG-02).
 *
 *	Principles:
 *	  - Machine formats (CSV numeric fields, JSON) get locale-neutral,
 *	    round-trip significant digits. StarGen never calls setlocale(),
 *	    so the CRT "C" locale guarantees '.' as the decimal separator.
 *	  - Human formats get fixed decimals, EXCEPT that a finite nonzero
 *	    tiny value must never be displayed as literal zero: such values
 *	    fall back to significant-digit rendering.
 *	  - Sentinel/unavailable values render per the target format's
 *	    policy (empty CSV field, "N/A" in text, ...); see
 *	    sg_value_status.h.
 */

#pragma once

#include <float.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *	Decimal digits needed for round-trip of a long double in this build.
 *	MSVC's long double is 64-bit (DECIMAL_DIG == 17); GCC/Clang x86-64
 *	typically provide LDBL_DECIMAL_DIG == 21.
 */
#if defined(LDBL_DECIMAL_DIG)
#define SG_LDBL_DEC_DIG	LDBL_DECIMAL_DIG
#elif defined(DECIMAL_DIG)
#define SG_LDBL_DEC_DIG	DECIMAL_DIG
#else
#define SG_LDBL_DEC_DIG	(LDBL_DIG + 3)
#endif

/* Round-trip, locale-neutral rendering ("1.1660000000000001e+00" style
 * is NOT produced; %Lg strips the exponent when in range). Returns buf. */
char *sg_fmt_machine(long double v, char *buf, size_t size);

/*
 *	Human rendering with up to `decimals` fixed decimals, but never
 *	showing a nonzero value as zero: if the value is nonzero and would
 *	be displayed as "0.00...0", it is rendered with significant digits
 *	instead. Sentinel values render as "N/A".
 *	Returns buf.
 */
char *sg_fmt_human(char *buf, size_t size, long double v, int decimals);

/*
 *	CSV field rendering: sentinel values become an empty field
 *	(documented convention; see docs/CSV format notes), everything else
 *	is machine precision. Returns buf.
 */
char *sg_fmt_csv_field(long double v, char *buf, size_t size);

#ifdef __cplusplus
}
#endif
