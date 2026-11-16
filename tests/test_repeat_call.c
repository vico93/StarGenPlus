/*
 *	test_repeat_call.c -- in-process regression checks (PLAN.MD Phase B)
 *
 *	1. Same-build determinism: two identical stargen() calls produce
 *	   byte-identical text output (seed/PRNG sequencing is per call).
 *	2. Template immutability: repeated adoption of the Solar System
 *	   fixture with moons enabled produces identical output both times
 *	   (generation must not mutate the static template; the historical
 *	   code accumulated gas-loss into the fixtures across calls).
 *	3. Dust-ratio restore: a run with the experimental dust ratio
 *	   followed by a plain run yields the same output as a plain run
 *	   preceded by nothing (the global dust coefficient must not
 *	   compound across calls).
 *
 *	stdout/stderr are redirected per run and RESTORED afterwards via
 *	duplicated file descriptors, so the engine's captured output and the
 *	test's own diagnostics never mix.
 *
 *	Text mode needs a path argument ending in the platform DIRSEP but
 *	writes no files.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#define SG_DUP(fd)			_dup(fd)
#define SG_DUP2(old, new)	_dup2(old, new)
#define SG_CLOSE(fd)		_close(fd)
#define SG_FILENO(stream)	_fileno(stream)
#else
#include <unistd.h>
#define SG_DUP(fd)			dup(fd)
#define SG_DUP2(old, new)	dup2(old, new)
#define SG_CLOSE(fd)		close(fd)
#define SG_FILENO(stream)	fileno(stream)
#endif

#include "const.h"
#include "structs.h"
#include "stargen.h"

static int failures = 0;

static void run_text(const char *capture_file,
					 long seed,
					 long double mass,
					 long double ratio_arg,
					 int flags_arg)
{
	FILE *out	= NULL;
	FILE *err	= NULL;
	int	 saved_out;
	int	 saved_err;

	saved_out = SG_DUP(SG_FILENO(stdout));
	saved_err = SG_DUP(SG_FILENO(stderr));

	if (freopen_s(&out, capture_file, "w", stdout) != 0)
		out = NULL;
	if (freopen_s(&err, "nul.err", "w", stderr) != 0)
		err = NULL;
	if ((out == NULL) || (err == NULL))
	{
		SG_DUP2(saved_out, SG_FILENO(stdout));
		SG_DUP2(saved_err, SG_FILENO(stderr));
		SG_CLOSE(saved_out);
		SG_CLOSE(saved_err);
		fprintf(stderr, "FAIL: cannot redirect output streams\n");
		exit(2);
	}

	(void)stargen(aGenerate,
				  'z',					// flag_char (unused without a catalog)
				  "unused" DIRSEP,		// path (must end in DIRSEP; unused by text mode)
				  "",					// url_path
				  "",					// filename
				  "",					// system name
				  NULL,					// sgOut: text mode writes to stdout
				  stderr,				// sgErr
				  "test_repeat_call",
				  mass,
				  seed,
				  1,					// count
				  1,					// increment
				  NULL,					// catalog
				  0,					// sys_no
				  ratio_arg,
				  flags_arg,
				  ffTEXT,
				  gfGIF);

	fflush(stdout);
	fflush(stderr);

	/* Restore the original stdout/stderr so test diagnostics go to the
	 * real console/pipe, not into the capture files. */
	SG_DUP2(saved_out, SG_FILENO(stdout));
	SG_DUP2(saved_err, SG_FILENO(stderr));
	SG_CLOSE(saved_out);
	SG_CLOSE(saved_err);
}

static int file_differs(const char *a, const char *b)
{
	FILE *fa = fopen(a, "rb");
	FILE *fb = fopen(b, "rb");
	int differs = 0;

	if ((fa == NULL) || (fb == NULL))
	{
		if (fa) fclose(fa);
		if (fb) fclose(fb);
		return 1;
	}

	for (;;)
	{
		char ba[4096];
		char bb[4096];
		size_t na = fread(ba, 1, sizeof(ba), fa);
		size_t nb = fread(bb, 1, sizeof(bb), fb);

		if ((na != nb) || ((na > 0) && (memcmp(ba, bb, na) != 0)))
		{
			differs = 1;
			break;
		}
		if (na == 0)
			break;
	}

	fclose(fa);
	fclose(fb);
	return differs;
}

static void expect_same(const char *what, const char *a, const char *b)
{
	if (file_differs(a, b))
	{
		fprintf(stderr, "FAIL: %s: %s and %s differ\n", what, a, b);
		++failures;
	}
	else
		printf("ok: %s\n", what);
}

int main(void)
{
	/* 1. Determinism of two identical plain runs. */
	run_text("rep1a.txt", 42, 1.0L, 0.0L, fDoGases);
	run_text("rep1b.txt", 42, 1.0L, 0.0L, fDoGases);
	expect_same("deterministic repeat (plain)", "rep1a.txt", "rep1b.txt");

	/* 2. Template immutability with moons (Solar System fixture). */
	run_text("rep2a.txt", 42, 1.0L, 0.0L, fDoGases | fDoMoons | fUseSolarsystem);
	run_text("rep2b.txt", 42, 1.0L, 0.0L, fDoGases | fDoMoons | fUseSolarsystem);
	expect_same("solar template immutability (moons)", "rep2a.txt", "rep2b.txt");

	/* 3. Dust-ratio restore: ratio run then plain run must equal a
	 *    plain run preceded by nothing. */
	run_text("rep3a.txt", 42, 1.0L, 10.0L, fDoGases);
	run_text("rep3b.txt", 42, 1.0L, 0.0L, fDoGases);
	expect_same("dust-ratio restore", "rep3b.txt", "rep1a.txt");
	/* The ratio run itself must differ from the plain run (sanity). */
	if (!file_differs("rep3a.txt", "rep1a.txt"))
	{
		fprintf(stderr, "FAIL: dust ratio had no effect on generation\n");
		++failures;
	}
	else
		printf("ok: dust ratio changes generation (sanity)\n");

	if (failures > 0)
	{
		fprintf(stderr, "%d repeat-call check(s) failed\n", failures);
		return 1;
	}

	printf("repeat-call checks passed\n");
	return 0;
}
