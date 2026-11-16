/*
 *	test_escape.c -- unit tests for the format escaping helpers
 *	(PLAN.MD BUG-05 acceptance criteria).
 */

#include <stdio.h>
#include <string.h>

#include "sg_escape.h"

static int failures = 0;

static void expect_str(const char *what, const char *got, const char *want)
{
	if (strcmp(got, want) != 0)
	{
		fprintf(stderr, "FAIL: %s: got '%s', want '%s'\n", what, got, want);
		++failures;
	}
}

int main(void)
{
	char buf[64];

	sg_escape_csv_quoted(buf, sizeof(buf), "plain");
	expect_str("csv plain", buf, "'plain'");

	sg_escape_csv_quoted(buf, sizeof(buf), "O'Brien's");
	expect_str("csv quote doubling", buf, "'O''Brien''s'");

	sg_escape_csv_quoted(buf, sizeof(buf), "a\nb\tc");
	expect_str("csv controls become spaces", buf, "'a b c'");

	/* Truncation stays valid syntax. */
	{
		char small[8];
		sg_escape_csv_quoted(small, sizeof(small), "abcdefghijklmnop");
		expect_str("csv truncation keeps quotes", small, "'abcdef");
	}

	sg_escape_html(buf, sizeof(buf), "a<b>&\"c\"");
	expect_str("html entities", buf, "a&lt;b&gt;&amp;&quot;c&quot;");

	sg_escape_html(buf, sizeof(buf), "it's");
	expect_str("html apostrophe", buf, "it&#39;s");

	sg_escape_ssc_string(buf, sizeof(buf), "say \"hi\" \\");
	expect_str("ssc escapes", buf, "\"say \\\"hi\\\" \\\\\"");

	sg_escape_ssc_string(buf, sizeof(buf), "a\nb");
	expect_str("ssc controls become spaces", buf, "\"a b\"");

	if (failures > 0)
	{
		fprintf(stderr, "%d escape check(s) failed\n", failures);
		return 1;
	}

	printf("escape checks passed\n");
	return 0;
}
