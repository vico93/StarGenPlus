/*
 *	test_json_roundtrip.c -- sg_jsonw + sg_export + sg_json_dom integration
 *
 *	1. Generates a system through the headless path into a file.
 *	2. Parses that document into a DOM and re-emits it.
 *	3. Requires byte-identical output (numbers keep their exact tokens;
 *	   object key order is preserved by the DOM).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "stargen.h"
#include "sg_headless.h"
#include "sg_json_dom.h"
#include "sg_jsonw.h"

/* Read the whole (already flushed) "w+" stream back into a buffer. */
static char *slurp_stream(FILE *f, size_t *out_len)
{
	char	*buf;
	long	 size;

	fflush(f);

	if (fseek(f, 0, SEEK_END) != 0)
		return NULL;

	size = ftell(f);

	if ((size < 0) || (fseek(f, 0, SEEK_SET) != 0))
		return NULL;

	buf = (char *) malloc((size_t) size + 1);

	if (buf == NULL)
		return NULL;

	{
		size_t got = fread(buf, 1, (size_t) size, f);

		buf[got] = '\0';
		*out_len = got;
	}

	return buf;
}

int main(void)
{
	sg_headless_request	req;
	FILE				*out	= NULL;
	FILE				*err	= NULL;
	char				*text = NULL;
	size_t				 len = 0;
	sg_dom_value		*dom;
	char				*round = NULL;
	size_t				 round_len = 0;
	int				 failures = 0;

	memset(&req, 0, sizeof(req));
	req.do_gases = 1;
	req.mass_solar = 1.0;
	req.seed = 42;

	if (freopen_s(&out, "rt_original.json", "w+", stdout) != 0)
		return 2;
	if (freopen_s(&err, "rt_err.txt", "w+", stderr) != 0)
		return 2;

	if (sg_run_json(stdout, stderr, &req) != 0)
	{
		fprintf(stderr, "FAIL: sg_run_json failed\n");
		return 1;
	}

	text = slurp_stream(stdout, &len);

	if ((text == NULL) || (len == 0) || (text[0] != '{'))
	{
		fprintf(stderr, "FAIL: exported document is empty/malformed\n");
		return 1;
	}

	dom = sg_dom_parse(text, len);

	if (dom == NULL)
	{
		fprintf(stderr, "FAIL: DOM parse of the exported document failed\n");
		return 1;
	}

	/* Re-emit into a separate file and compare. */
	if (freopen_s(&out, "rt_roundtrip.json", "w+", stdout) != 0)
		return 2;

	{
		sg_json_writer w;

		sg_json_begin(&w, stdout);
		sg_dom_emit(dom, &w);
	}

	sg_dom_free(dom);
	round = slurp_stream(stdout, &round_len);

	if ((round == NULL) || (round_len != len) || (memcmp(text, round, len) != 0))
	{
		size_t i;
		size_t n = (len < round_len) ? len : round_len;

		fprintf(stderr, "FAIL: DOM round-trip differs (orig %zu, round %zu)\n",
				len, round_len);

		for (i = 0; i < n; i++)
		{
			if (text[i] != round[i])
			{
				size_t s = (i > 60) ? (i - 60) : 0;
				size_t e = (i + 60 < n) ? (i + 60) : n;
				size_t k;

				fprintf(stderr, "first diff at %zu\norig: ", i);
				for (k = s; k < e; k++)
					fputc(((unsigned char) text[k] >= 0x20) ? text[k] : '.', stderr);
				fprintf(stderr, "\nround: ");
				for (k = s; k < e; k++)
					fputc(((unsigned char) round[k] >= 0x20) ? round[k] : '.', stderr);
				fprintf(stderr, "\n");
				break;
			}
		}

		++failures;
	}

	if (failures == 0)
		fprintf(stderr, "json round-trip ok\n");

	return failures ? 1 : 0;
}
