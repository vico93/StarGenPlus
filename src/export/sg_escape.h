/*
 *	sg_escape.h -- string escaping helpers for the supported output
 *	formats (PLAN.MD BUG-05). Callers pass a destination buffer with an
 * explicit size; content that does not fit is truncated safely rather
 * than overrun, and the helpers never emit invalid syntax.
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 *	Legacy CSV fields are single-quoted. Embedded single quotes are
 *	doubled (''), control characters (including newlines, which would
 *	break the row structure) are replaced by spaces. The field is always
 *	wrapped in single quotes.
 */
void sg_escape_csv_quoted(char *out, size_t out_size, const char *src);

/* HTML text and attribute values: & < > " ' become entities. */
void sg_escape_html(char *out, size_t out_size, const char *src);

/* Celestia SSC double-quoted strings: backslash and double quote are
 * backslash-escaped, control characters become spaces. */
void sg_escape_ssc_string(char *out, size_t out_size, const char *src);

#ifdef __cplusplus
}
#endif
