#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define JSMN_STATIC
#define JSMN_STRICT
#include "jsmn.h"

#include "sg_json_dom.h"
#include "sg_jsonw.h"

static void emit_dom_value(const sg_dom_value *v, sg_json_writer *w);
static void emit_dom_children(const sg_dom_value *container, sg_json_writer *w);

static sg_dom_value *dom_new(sg_dom_type type)
{
	sg_dom_value *v = (sg_dom_value *) calloc(1, sizeof(sg_dom_value));

	if (v != NULL)
		v->type = type;

	return v;
}

void sg_dom_free(sg_dom_value *v)
{
	while (v != NULL)
	{
		sg_dom_value *next = v->next_sibling;

		sg_dom_free(v->first_child);
		free(v->key);
		free(v->str);
		free(v->num);
		free(v);

		v = next;
	}
}

/* ------------------------------------------------------------------ */
/* String decoding (with \uXXXX and surrogate pairs)                   */
/* ------------------------------------------------------------------ */

static int hex4(const char *s, unsigned *out)
{
	unsigned	value	= 0;
	int			i;

	for (i = 0; i < 4; i++)
	{
		char c = s[i];

		value <<= 4;

		if ((c >= '0') && (c <= '9'))
			value |= (unsigned) (c - '0');
		else if ((c >= 'a') && (c <= 'f'))
			value |= (unsigned) (c - 'a' + 10);
		else if ((c >= 'A') && (c <= 'F'))
			value |= (unsigned) (c - 'A' + 10);
		else
			return 0;
	}

	*out = value;
	return 1;
}

static size_t utf8_encode(char *out, unsigned cp)
{
	if (cp < 0x80)
	{
		out[0] = (char) cp;
		return 1;
	}

	if (cp < 0x800)
	{
		out[0] = (char) (0xC0 | (cp >> 6));
		out[1] = (char) (0x80 | (cp & 0x3F));
		return 2;
	}

	if (cp < 0x10000)
	{
		out[0] = (char) (0xE0 | (cp >> 12));
		out[1] = (char) (0x80 | ((cp >> 6) & 0x3F));
		out[2] = (char) (0x80 | (cp & 0x3F));
		return 3;
	}

	out[0] = (char) (0xF0 | (cp >> 18));
	out[1] = (char) (0x80 | ((cp >> 12) & 0x3F));
	out[2] = (char) (0x80 | ((cp >> 6) & 0x3F));
	out[3] = (char) (0x80 | (cp & 0x3F));
	return 4;
}

/*
 *	Decode the raw JSON string slice [start, end) into a fresh UTF-8
 *	buffer. Returns NULL on invalid escapes. `text` must stay valid
 *	(controls can appear inside strings as escapes only -- raw control
 *	characters are rejected by jsmn strict mode).
 */
static char *decode_string(const char *text, int start, int end)
{
	size_t	cap = (size_t) (end - start) + 1;
	char	*out = (char *) malloc(cap + 4);
	size_t	used = 0;
	int		i = start;

	if (out == NULL)
		return NULL;

	while (i < end)
	{
		char c = text[i];

		if (c != '\\')
		{
			out[used++] = c;
			++i;
			continue;
		}

		if ((i + 1) >= end)
			goto fail;

		switch (text[i + 1])
		{
			case '"':	out[used++] = '"';	break;
			case '\\':	out[used++] = '\\';	break;
			case '/':	out[used++] = '/';	break;
			case 'b':	out[used++] = '\b';	break;
			case 'f':	out[used++] = '\f';	break;
			case 'n':	out[used++] = '\n';	break;
			case 'r':	out[used++] = '\r';	break;
			case 't':	out[used++] = '\t';	break;
			case 'u':
			{
				unsigned	cp = 0;

				if ((i + 6) > end)
					goto fail;

				if (!hex4(text + i + 2, &cp))
					goto fail;

				i += 6;

				/* Surrogate pair handling. */
				if ((cp >= 0xD800) && (cp <= 0xDBFF))
				{
					unsigned lo = 0;

					if (((i + 1) < end) && (text[i] == '\\') && (text[i + 1] == 'u')
						&& ((i + 6) <= end) && hex4(text + i + 2, &lo)
						&& (lo >= 0xDC00) && (lo <= 0xDFFF))
					{
						cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
						i += 6;
					}
					else
					{
						cp = 0xFFFD;		/* lone surrogate: replacement	*/
					}
				}
				else if ((cp >= 0xDC00) && (cp <= 0xDFFF))
				{
					cp = 0xFFFD;
				}

				used += utf8_encode(out + used, cp);
				continue;					/* i already advanced			*/
			}
			default:
				goto fail;
		}

		i += 2;
	}

	out[used] = '\0';
	return out;

fail:
	free(out);
	return NULL;
}

/* ------------------------------------------------------------------ */
/* Tree building                                                       */
/* ------------------------------------------------------------------ */

static sg_dom_value *build(const char *text, const jsmntok_t *tokens,
						   int *tok);

static int add_child(sg_dom_value *parent, sg_dom_value *child, char *key)
{
	sg_dom_value *tail = parent->first_child;

	child->key = key;

	if (tail == NULL)
		parent->first_child = child;
	else
	{
		while (tail->next_sibling != NULL)
			tail = tail->next_sibling;

		tail->next_sibling = child;
	}

	return 1;
}

static char *slice_string(const char *text, const jsmntok_t *tok)
{
	return decode_string(text, tok->start, tok->end);
}

static sg_dom_value *build(const char *text, const jsmntok_t *tokens,
						   int *tok)
{
	const jsmntok_t	*t = &tokens[*tok];
	sg_dom_value	*v = NULL;

	switch (t->type)
	{
		case JSMN_OBJECT:
		{
			int members = t->size;
			int i;

			v = dom_new(sgDomObject);
			if (v == NULL)
				return NULL;

			++(*tok);

			for (i = 0; i < members; i++)
			{
				const jsmntok_t	*keytok = &tokens[*tok];
				char			*key;
				sg_dom_value	*val;

				if (keytok->type != JSMN_STRING)
					goto fail;

				key = slice_string(text, keytok);
				if (key == NULL)
					goto fail;

				++(*tok);

				val = build(text, tokens, tok);
				if (val == NULL)
				{
					free(key);
					goto fail;
				}

				add_child(v, val, key);
			}

			return v;
		}

		case JSMN_ARRAY:
		{
			int items = t->size;
			int i;

			v = dom_new(sgDomArray);
			if (v == NULL)
				return NULL;

			++(*tok);

			for (i = 0; i < items; i++)
			{
				sg_dom_value *val = build(text, tokens, tok);

				if (val == NULL)
					goto fail;

				add_child(v, val, NULL);
			}

			return v;
		}

		case JSMN_STRING:
			v = dom_new(sgDomString);
			if (v == NULL)
				return NULL;
			v->str = slice_string(text, t);
			if (v->str == NULL)
				goto fail;
			++(*tok);
			return v;

		case JSMN_PRIMITIVE:
		{
			int len = t->end - t->start;

			if ((len == 4) && (strncmp(text + t->start, "true", 4) == 0))
			{
				v = dom_new(sgDomBool);
				if (v == NULL)
					return NULL;
				v->bval = 1;
			}
			else if ((len == 5) && (strncmp(text + t->start, "false", 5) == 0))
			{
				v = dom_new(sgDomBool);
				if (v == NULL)
					return NULL;
				v->bval = 0;
			}
			else if ((len == 4) && (strncmp(text + t->start, "null", 4) == 0))
			{
				v = dom_new(sgDomNull);
				if (v == NULL)
					return NULL;
			}
			else
			{
				v = dom_new(sgDomNumber);
				if (v == NULL)
					return NULL;
				v->num = (char *) malloc((size_t) len + 1);
				if (v->num == NULL)
					goto fail;
				memcpy(v->num, text + t->start, (size_t) len);
				v->num[len] = '\0';
			}

			++(*tok);
			return v;
		}

		default:
			return NULL;
	}

fail:
	sg_dom_free(v);
	return NULL;
}

sg_dom_value *sg_dom_parse(const char *text, size_t len)
{
	jsmn_parser	parser;
	int			ntok;
	int			tok_index = 0;

	if ((text == NULL) || (len == 0))
		return NULL;

	jsmn_init(&parser);
	ntok = jsmn_parse(&parser, text, len, NULL, 0);

	if (ntok <= 0)
		return NULL;

	{
		jsmntok_t *tokens = (jsmntok_t *) malloc(sizeof(jsmntok_t) * (size_t) ntok);
		sg_dom_value *root;

		if (tokens == NULL)
			return NULL;

		jsmn_init(&parser);
		ntok = jsmn_parse(&parser, text, len, tokens, (unsigned) ntok);

		if (ntok <= 0)
		{
			free(tokens);
			return NULL;
		}

		root = build(text, tokens, &tok_index);
		free(tokens);
		return root;
	}
}

/* ------------------------------------------------------------------ */
/* Accessors                                                           */
/* ------------------------------------------------------------------ */

const sg_dom_value *sg_dom_get(const sg_dom_value *obj, const char *key)
{
	const sg_dom_value *c;

	if ((obj == NULL) || (obj->type != sgDomObject) || (key == NULL))
		return NULL;

	for (c = obj->first_child; c != NULL; c = c->next_sibling)
	{
		if ((c->key != NULL) && (strcmp(c->key, key) == 0))
			return c;
	}

	return NULL;
}

const sg_dom_value *sg_dom_index(const sg_dom_value *arr, int index)
{
	const sg_dom_value *c;

	if ((arr == NULL) || (arr->type != sgDomArray) || (index < 0))
		return NULL;

	for (c = arr->first_child; (c != NULL) && (index > 0); c = c->next_sibling)
		--index;

	return c;
}

int sg_dom_count(const sg_dom_value *arr)
{
	const sg_dom_value *c;
	int					n = 0;

	if ((arr == NULL) || (arr->type != sgDomArray))
		return 0;

	for (c = arr->first_child; c != NULL; c = c->next_sibling)
		++n;

	return n;
}

sg_dom_type sg_dom_type_of(const sg_dom_value *v)
{
	return (v != NULL) ? v->type : sgDomNull;
}

const char *sg_dom_string(const sg_dom_value *v)
{
	return ((v != NULL) && (v->type == sgDomString)) ? v->str : NULL;
}

const char *sg_dom_number_token(const sg_dom_value *v)
{
	return ((v != NULL) && (v->type == sgDomNumber)) ? v->num : NULL;
}

int sg_dom_bool(const sg_dom_value *v)
{
	return ((v != NULL) && (v->type == sgDomBool)) ? v->bval : 0;
}

long sg_dom_long(const sg_dom_value *v)
{
	const char	*tok = sg_dom_number_token(v);
	char		*end = NULL;
	long		 value;

	if (tok == NULL)
		return 0;

	value = strtol(tok, &end, 10);
	(void) end;

	return value;
}

double sg_dom_double(const sg_dom_value *v)
{
	const char *tok = sg_dom_number_token(v);

	if (tok == NULL)
		return 0.0;

	return strtod(tok, NULL);
}

/* Emit one value (containers include their begin/end). */
static void emit_dom_value(const sg_dom_value *v, sg_json_writer *w)
{
	if (v == NULL)
	{
		sg_json_raw_null(w);
		return;
	}

	switch (v->type)
	{
		case sgDomObject:
			sg_json_object_begin(w, NULL);
			emit_dom_children(v, w);
			sg_json_object_end(w);
			break;

		case sgDomArray:
			sg_json_array_begin(w, NULL);
			emit_dom_children(v, w);
			sg_json_array_end(w);
			break;

		case sgDomString:
			sg_json_quoted_string(w, v->str);
			break;

		case sgDomNumber:
			sg_json_raw_token(w, (v->num != NULL) ? v->num : "0");
			break;

		case sgDomBool:
			sg_json_raw_token(w, v->bval ? "true" : "false");
			break;

		case sgDomNull:
		default:
			sg_json_raw_null(w);
			break;
	}
}

void sg_dom_emit(const sg_dom_value *v, void *writer_arg)
{
	sg_json_writer *w = (sg_json_writer *) writer_arg;

	if (v == NULL)
	{
		sg_json_raw_null(w);
		return;
	}

	emit_dom_value(v, w);
}

/*
 *	Emit the members/items of a container (keys and separators handled
 *	per the sg_json_writer contract).
 */
static void emit_dom_children(const sg_dom_value *container, sg_json_writer *w)
{
	const sg_dom_value *c;

	for (c = container->first_child; c != NULL; c = c->next_sibling)
	{
		if (container->type == sgDomObject)
		{
			if (c->key == NULL)
				continue;

			if (c->type == sgDomObject)
			{
				sg_json_object_begin(w, c->key);
				emit_dom_children(c, w);
				sg_json_object_end(w);
			}
			else if (c->type == sgDomArray)
			{
				sg_json_array_begin(w, c->key);
				emit_dom_children(c, w);
				sg_json_array_end(w);
			}
			else
			{
				sg_json_key_raw(w, c->key);
				emit_dom_value(c, w);
			}
		}
		else	/* array item */
		{
			if ((c->type != sgDomObject) && (c->type != sgDomArray))
				sg_json_sep_raw(w);		/* containers self-separate via begin */

			emit_dom_value(c, w);
		}
	}
}
