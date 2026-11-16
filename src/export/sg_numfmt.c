#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#include "sg_numfmt.h"
#include "sg_value_status.h"

/*
 *	Strip trailing zeros from a decimal representation produced by %Lg,
 *	e.g. "192837.406000000" -> "192837.406", "100.000000" -> "100".
 *	Exponential forms keep their exponent: "1.500000e-05" -> "1.5e-05".
 */
static void trim_trailing_zeros(char *buf)
{
	char	*dot	= strchr(buf, '.');

	if (dot != NULL)
	{
		char	*e		= strchr(dot, 'e');
		char	*last	= (e != NULL) ? e : (buf + strlen(buf));
		char	*p		= last - 1;

		while (p > dot && *p == '0')
			--p;

		if (*p == '.')
			--p;

		if ((p + 1) < last)
			memmove(p + 1, last, strlen(last) + 1);
	}
}

char *sg_fmt_machine(long double v, char *buf, size_t size)
{
	int	prec;

	/* Shortest representation that parses back to exactly this value
	 * (round-trip). StarGen never calls setlocale(), so the "C" locale
	 * guarantees '.' as the decimal separator. */
	for (prec = 1; prec <= SG_LDBL_DEC_DIG; prec++)
	{
		snprintf(buf, size, "%.*Lg", prec, v);
		trim_trailing_zeros(buf);

		if (strtold(buf, NULL) == v)
			return buf;
	}

	snprintf(buf, size, "%.*Lg", SG_LDBL_DEC_DIG, v);
	return buf;
}

char *sg_fmt_human(char *buf, size_t size, long double v, int decimals)
{
	if (!sg_num_available(v))
	{
		snprintf(buf, size, "N/A");
		return buf;
	}

	if (v != 0.0L)
	{
		long double threshold = 0.5L * powl(10.0L, (long double)(-decimals));

		if (fabsl(v) < threshold)
		{
			/* Would print as 0.00...0 although the value is nonzero:
			 * fall back to significant-digit rendering. */
			snprintf(buf, size, "%.*Lg", decimals + 1, v);
			return buf;
		}
	}

	snprintf(buf, size, "%.*Lf", decimals, v);
	return buf;
}

char *sg_fmt_csv_field(long double v, char *buf, size_t size)
{
	if (!sg_num_available(v))
	{
		buf[0] = '\0';				/* empty field: value unavailable	*/
		return buf;
	}

	return sg_fmt_machine(v, buf, size);
}
