#include <stdio.h>
#include <string.h>

#include "sg_escape.h"

/*
 *	Append one character to out, honoring the bound. Truncation is
 *	safe: the result is always NUL-terminated valid syntax for the
 *	target format (possibly shorter than ideal).
 */
static void put_char(char *out, size_t out_size, size_t *used, char c)
{
	if ((*used) + 1 < out_size)
		out[(*used)++] = c;
}

static void put_str(char *out, size_t out_size, size_t *used, const char *s)
{
	while (*s != '\0')
	{
		put_char(out, out_size, used, *s);
		++s;
	}
}

static void put_escaped_char(char *out, size_t out_size, size_t *used,
							 char c)
{
	if ((*used) + 2 < out_size)
	{
		out[(*used)++] = '\\';
		out[(*used)++] = c;
	}
}

static void finish(char *out, size_t out_size, size_t used)
{
	if (out_size > 0)
		out[(used < out_size) ? used : (out_size - 1)] = '\0';
}

void sg_escape_csv_quoted(char *out, size_t out_size, const char *src)
{
	size_t used = 0;

	if ((out == NULL) || (out_size == 0))
		return;

	put_char(out, out_size, &used, '\'');

	for (; (src != NULL) && (*src != '\0'); ++src)
	{
		unsigned char c = (unsigned char) *src;

		if (c == '\'')
			put_str(out, out_size, &used, "''");	/* CSV quote doubling	*/
		else if (c < 0x20)
			put_char(out, out_size, &used, ' ');	/* controls -> space	*/
		else
			put_char(out, out_size, &used, (char) c);
	}

	put_char(out, out_size, &used, '\'');
	finish(out, out_size, used);
}

void sg_escape_html(char *out, size_t out_size, const char *src)
{
	size_t used = 0;

	if ((out == NULL) || (out_size == 0))
		return;

	for (; (src != NULL) && (*src != '\0'); ++src)
	{
		unsigned char c = (unsigned char) *src;

		switch (c)
		{
			case '&':	put_str(out, out_size, &used, "&amp;");		break;
			case '<':	put_str(out, out_size, &used, "&lt;");		break;
			case '>':	put_str(out, out_size, &used, "&gt;");		break;
			case '"':	put_str(out, out_size, &used, "&quot;");	break;
			case '\'':	put_str(out, out_size, &used, "&#39;");		break;
			default:
				if (c < 0x20)
					put_char(out, out_size, &used, ' ');
				else
					put_char(out, out_size, &used, (char) c);
				break;
		}
	}

	finish(out, out_size, used);
}

void sg_escape_ssc_string(char *out, size_t out_size, const char *src)
{
	size_t used = 0;

	if ((out == NULL) || (out_size == 0))
		return;

	put_char(out, out_size, &used, '"');

	for (; (src != NULL) && (*src != '\0'); ++src)
	{
		unsigned char c = (unsigned char) *src;

		if ((c == '\\') || (c == '"'))
			put_escaped_char(out, out_size, &used, (char) c);
		else if (c < 0x20)
			put_char(out, out_size, &used, ' ');
		else
			put_char(out, out_size, &used, (char) c);
	}

	put_char(out, out_size, &used, '"');
	finish(out, out_size, used);
}
