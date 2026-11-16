/*
 *	sg_jsonw.h -- bounded, streaming JSON writer for the headless
 *	exporter (PLAN.MD Phase D). The writer is stack-driven (begin/end
 *	object/array) and emits:
 *	  - strings escaped per RFC 8259 (control chars as \u00XX; the
 *	    source strings are ASCII, UTF-8 bytes pass through untouched);
 *	  - long doubles as raw number tokens via sg_fmt_machine()
 *	    (shortest round-trip, locale-neutral);
 *	  - null for unavailable values, chosen by the caller.
 *
 *	All writes go to a caller-owned FILE*. Write failures are sticky:
 *	sg_json_ok() reports whether any write failed so the driver can
 *	treat a broken stdout as an error instead of emitting a truncated
 *	document silently.
 */

#pragma once

#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sg_json_writer {
	FILE	*out;
	int		depth;
	int		need_comma[16];		/* per-scope pending separator		*/
	int		error;				/* sticky write/encode failure		*/
} sg_json_writer;

void sg_json_begin(sg_json_writer *w, FILE *out);
int  sg_json_ok(const sg_json_writer *w);			/* nonzero = clean	*/

void sg_json_object_begin(sg_json_writer *w, const char *key);
void sg_json_object_end(sg_json_writer *w);
void sg_json_array_begin(sg_json_writer *w, const char *key);
void sg_json_array_end(sg_json_writer *w);

void sg_json_string(sg_json_writer *w, const char *key, const char *value);
void sg_json_quoted_string(sg_json_writer *w, const char *s);	/* raw, no key */
void sg_json_int(sg_json_writer *w, const char *key, int value);
void sg_json_long(sg_json_writer *w, const char *key, long value);
void sg_json_bool(sg_json_writer *w, const char *key, int value);
void sg_json_null(sg_json_writer *w, const char *key);

/* Finite number or null (when available == 0). */
void sg_json_number_or_null(sg_json_writer *w, const char *key,
							long double value, int available);

/* Raw number token, no formatting decisions. */
void sg_json_number(sg_json_writer *w, const char *key, long double value);

#ifdef __cplusplus
}
#endif
