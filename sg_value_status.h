/*
 *	sg_value_status.h -- shared value/status policy for all exporters
 *	(PLAN.MD BUG-02).
 *
 *	The model uses INCREDIBLY_LARGE_NUMBER (const.h) as an
 *	unavailable/unbounded placeholder. With MSVC (and any toolchain where
 *	HUGE_VAL is defined) it is +infinity; the fallback definition is the
 *	finite value 9.9999E37. Exporters must therefore classify a value as
 *	unavailable BOTH when it is non-finite and when it is an explicit
 *	large finite sentinel -- checking only isfinite() is insufficient.
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 *	Values at or beyond this magnitude are treated as sentinels. Every
 *	legitimately modeled physical quantity in StarGen is orders of
 *	magnitude below this bound (the largest are gas lifetimes in years,
 *	which are clamped to the sentinel); the finite fallback sentinel is
 *	9.9999E37.
 */
#define SG_SENTINEL_LIMIT	(1.0E30)

/* Nonzero when v is a finite, in-range value (i.e. publishable). */
int sg_num_available(long double v);

/* Nonzero when v is a sentinel (non-finite, or the large finite
 * fallback placeholder). */
int sg_num_is_sentinel(long double v);

/*
 *	Status categories for the structured (JSON) exporter, Phase D.
 *	The mapping from a raw value to a category is field-specific and
 *	lives with the exporters; these are the categories themselves.
 */
typedef enum sg_value_status {
	sgvs_ok,				/* finite, in-range computed value			*/
	sgvs_not_computed,		/* calculation disabled by options/gating	*/
	sgvs_not_applicable,	/* field has no meaning for this body		*/
	sgvs_unbounded,			/* value exceeds the model's range			*/
	sgvs_numerical_error	/* invalid arithmetic (NaN etc.)			*/
} sg_value_status;

const char *sg_value_status_name(sg_value_status status);

#ifdef __cplusplus
}
#endif
