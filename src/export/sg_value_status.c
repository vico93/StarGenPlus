#include <math.h>
#include <stdio.h>

#include "sg_value_status.h"

int sg_num_is_sentinel(long double v)
{
	if (isnan(v))
		return 1;						/* numerical error class		*/
	if (isinf(v))
		return 1;						/* INCREDIBLY_LARGE_NUMBER path	*/
	if (fabs(v) >= SG_SENTINEL_LIMIT)
		return 1;						/* finite fallback sentinel		*/

	return 0;
}

int sg_num_available(long double v)
{
	return !sg_num_is_sentinel(v);
}

const char *sg_value_status_name(sg_value_status status)
{
	switch (status)
	{
		case sgvs_ok:				return "ok";
		case sgvs_not_computed:		return "not_computed";
		case sgvs_not_applicable:	return "not_applicable";
		case sgvs_unbounded:		return "unbounded";
		case sgvs_numerical_error:	return "numerical_error";
	}

	return "unknown";
}
