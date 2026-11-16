/*
 *	sg_json_dom.h -- a minimal JSON DOM on top of the vendored jsmn
 *	tokenizer (used by the MCP server to parse requests and to carry
 *	worker results as structured content).
 *
 *	Numbers keep their ORIGINAL text token: re-emitting a parsed
 *	worker document reproduces the exact round-trip number formatting
 *	the exporter produced. Strings are decoded (including \uXXXX /
 *	surrogate pairs) into owned UTF-8 buffers.
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum sg_dom_type {
	sgDomObject,
	sgDomArray,
	sgDomString,
	sgDomNumber,
	sgDomBool,
	sgDomNull
} sg_dom_type;

typedef struct sg_dom_value sg_dom_value;

struct sg_dom_value {
	sg_dom_type		type;
	sg_dom_value	*first_child;	/* object members / array items		*/
	sg_dom_value	*next_sibling;
	char			*key;			/* member name (object members)	*/
	char			*str;			/* sgDomString payload			*/
	char			*num;			/* sgDomNumber raw token		*/
	int				 bval;			/* sgDomBool payload			*/
};

/* Parse `text[0..len)`; returns NULL on malformed input or OOM. */
sg_dom_value *sg_dom_parse(const char *text, size_t len);

void sg_dom_free(sg_dom_value *v);

/* Object member lookup (NULL when absent or not an object). */
const sg_dom_value *sg_dom_get(const sg_dom_value *obj, const char *key);

const sg_dom_value *sg_dom_index(const sg_dom_value *arr, int index);
int sg_dom_count(const sg_dom_value *arr);
sg_dom_type sg_dom_type_of(const sg_dom_value *v);

/* Accessors: NULL/0 when the value has a different type. */
const char *sg_dom_string(const sg_dom_value *v);
const char *sg_dom_number_token(const sg_dom_value *v);
int sg_dom_bool(const sg_dom_value *v);
long sg_dom_long(const sg_dom_value *v);
double sg_dom_double(const sg_dom_value *v);

/* Re-emit a parsed value verbatim (raw number tokens preserved). */
void sg_dom_emit(const sg_dom_value *v, void *writer /* sg_json_writer* */);

#ifdef __cplusplus
}
#endif
