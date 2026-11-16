#include <string.h>

#include "sg_jsonw.h"
#include "sg_numfmt.h"

#define SG_JSON_MAX_DEPTH	((int)(sizeof(((sg_json_writer *)0)->need_comma) / \
								   sizeof(((sg_json_writer *)0)->need_comma[0])))

static void sg_json_raw(sg_json_writer *w, const char *s)
{
	if (fputs(s, w->out) == EOF)
		w->error = 1;
}

static void sg_json_sep(sg_json_writer *w)
{
	if (w->depth > 0)
	{
		if (w->need_comma[w->depth - 1])
			sg_json_raw(w, ",");
		else
			w->need_comma[w->depth - 1] = 1;
	}
}

static void sg_json_key(sg_json_writer *w, const char *key)
{
	sg_json_sep(w);
	sg_json_quoted_string(w, key);
	sg_json_raw(w, ":");
}

void sg_json_quoted_string(sg_json_writer *w, const char *s)
{
	sg_json_raw(w, "\"");

	for (; (s != NULL) && (*s != '\0'); ++s)
	{
		unsigned char c = (unsigned char) *s;

		switch (c)
		{
			case '"':	sg_json_raw(w, "\\\"");		break;
			case '\\':	sg_json_raw(w, "\\\\");		break;
			case '\b':	sg_json_raw(w, "\\b");		break;
			case '\f':	sg_json_raw(w, "\\f");		break;
			case '\n':	sg_json_raw(w, "\\n");		break;
			case '\r':	sg_json_raw(w, "\\r");		break;
			case '\t':	sg_json_raw(w, "\\t");		break;
			default:
				if (c < 0x20)
				{
					char	esc[8];

					snprintf(esc, sizeof(esc), "\\u%04x", c);
					sg_json_raw(w, esc);
				}
				else
				{
					char	ch[2];

					ch[0] = (char) c;
					ch[1] = '\0';
					sg_json_raw(w, ch);
				}
				break;
		}
	}

	sg_json_raw(w, "\"");
}

void sg_json_begin(sg_json_writer *w, FILE *out)
{
	w->out = out;
	w->depth = 0;
	w->error = 0;
	memset(w->need_comma, 0, sizeof(w->need_comma));
}

int sg_json_ok(const sg_json_writer *w)
{
	return (w->error == 0) && (w->depth == 0);
}

void sg_json_object_begin(sg_json_writer *w, const char *key)
{
	if (key != NULL)
		sg_json_key(w, key);
	else
		sg_json_sep(w);

	sg_json_raw(w, "{");

	if (w->depth < SG_JSON_MAX_DEPTH)
	{
		w->need_comma[w->depth] = 0;
		++w->depth;
	}
	else
		w->error = 1;			/* document too deeply nested		*/
}

void sg_json_object_end(sg_json_writer *w)
{
	if (w->depth > 0)
	{
		--w->depth;
		sg_json_raw(w, "}");
	}
	else
		w->error = 1;
}

void sg_json_array_begin(sg_json_writer *w, const char *key)
{
	if (key != NULL)
		sg_json_key(w, key);
	else
		sg_json_sep(w);

	sg_json_raw(w, "[");

	if (w->depth < SG_JSON_MAX_DEPTH)
	{
		w->need_comma[w->depth] = 0;
		++w->depth;
	}
	else
		w->error = 1;
}

void sg_json_array_end(sg_json_writer *w)
{
	if (w->depth > 0)
	{
		--w->depth;
		sg_json_raw(w, "]");
	}
	else
		w->error = 1;
}

void sg_json_string(sg_json_writer *w, const char *key, const char *value)
{
	sg_json_key(w, key);
	sg_json_quoted_string(w, value);
}

void sg_json_int(sg_json_writer *w, const char *key, int value)
{
	char	buf[32];

	sg_json_key(w, key);
	snprintf(buf, sizeof(buf), "%d", value);
	sg_json_raw(w, buf);
}

void sg_json_long(sg_json_writer *w, const char *key, long value)
{
	char	buf[32];

	sg_json_key(w, key);
	snprintf(buf, sizeof(buf), "%ld", value);
	sg_json_raw(w, buf);
}

void sg_json_bool(sg_json_writer *w, const char *key, int value)
{
	sg_json_key(w, key);
	sg_json_raw(w, value ? "true" : "false");
}

void sg_json_null(sg_json_writer *w, const char *key)
{
	sg_json_key(w, key);
	sg_json_raw(w, "null");
}

void sg_json_number(sg_json_writer *w, const char *key, long double value)
{
	char	buf[64];

	sg_json_key(w, key);
	sg_json_raw(w, sg_fmt_machine(value, buf, sizeof(buf)));
}

void sg_json_number_or_null(sg_json_writer *w, const char *key,
							long double value, int available)
{
	if (available)
		sg_json_number(w, key, value);
	else
		sg_json_null(w, key);
}
