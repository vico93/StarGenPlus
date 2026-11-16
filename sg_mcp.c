#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#endif

#include "stargen.h"
#include "sg_headless.h"
#include "sg_export.h"
#include "sg_jsonw.h"
#include "sg_json_dom.h"
#include "sg_worker.h"
#include "sg_mcp.h"

/* ------------------------------------------------------------------ */
/* Configuration (environment-overridable, documented defaults)         */
/* ------------------------------------------------------------------ */

#define MCP_PROTOCOL_VERSION	"2026-07-28"
#define MCP_SERVER_NAME			"stargen"
#define MCP_SERVER_VERSION		"1.43.0"
#define MCP_MAX_LINE			(1u << 20)		/* 1 MiB request cap		*/
#define MCP_MAX_OUTPUT			(64u << 20)		/* 64 MiB worker output cap	*/

typedef struct mcp_config {
	unsigned	worker_timeout_ms;
	unsigned	search_budget_ms;
	unsigned	max_attempts;
	unsigned	max_results;
} mcp_config;

static unsigned env_uint(const char *name, unsigned fallback)
{
	const char	*v = getenv(name);
	char		*end = NULL;
	unsigned long	 n;

	if ((v == NULL) || (*v == '\0'))
		return fallback;

	n = strtoul(v, &end, 10);

	if ((end == NULL) || (*end != '\0') || (n == 0) || (n > 3600000u))
		return fallback;

	return (unsigned) n;
}

static void config_load(mcp_config *cfg)
{
	cfg->worker_timeout_ms	= env_uint("STARGEN_MCP_WORKER_TIMEOUT_MS", 10000);
	cfg->search_budget_ms	= env_uint("STARGEN_MCP_SEARCH_BUDGET_MS", 30000);
	cfg->max_attempts		= env_uint("STARGEN_MCP_MAX_ATTEMPTS", 1000);
	cfg->max_results		= env_uint("STARGEN_MCP_MAX_RESULTS", 10);
}

/* ------------------------------------------------------------------ */
/* Input queue (reader thread feeds lines; main thread drains)          */
/* ------------------------------------------------------------------ */

typedef struct line_node {
	struct line_node	*next;
	char				*text;
} line_node;

typedef struct line_queue {
	line_node		*head;
	line_node		*tail;
	int				 eof;
	int				 cancelled_id_valid;
	char			 cancelled_id[64];	/* raw JSON of the id being cancelled */
#if defined(_WIN32)
	CRITICAL_SECTION	lock;
#endif
} line_queue;

typedef struct mcp_session {
	int				legacy_initialized;	/* legacy handshake completed		*/
	line_queue		queue;				/* by value							*/
} mcp_session;

static void queue_init(line_queue *q)
{
	memset(q, 0, sizeof(*q));
#if defined(_WIN32)
	InitializeCriticalSection(&q->lock);
#endif
}

static void queue_push(line_queue *q, char *text)
{
	line_node *n = (line_node *) malloc(sizeof(line_node));

	if (n == NULL)
	{
		free(text);
		return;
	}

	n->next = NULL;
	n->text = text;

#if defined(_WIN32)
	EnterCriticalSection(&q->lock);

	if (q->eof)
	{
		free(text);
		free(n);
	}
	else if (q->tail == NULL)
	{
		q->head = n;
		q->tail = n;
	}
	else
	{
		q->tail->next = n;
		q->tail = n;
	}

	LeaveCriticalSection(&q->lock);
#else
	/* Single-threaded fallback: no reader thread, nothing queued. */
	free(text);
	free(n);
#endif
}

static char *queue_pop(line_queue *q)
{
#if defined(_WIN32)
	char *text = NULL;

	EnterCriticalSection(&q->lock);

	if (q->head != NULL)
	{
		line_node *n = q->head;

		q->head = n->next;
		if (q->head == NULL)
			q->tail = NULL;

		text = n->text;
		free(n);
	}

	LeaveCriticalSection(&q->lock);
	return text;
#else
	return NULL;
#endif
}

static void queue_set_cancel(line_queue *q, const char *id_json)
{
#if defined(_WIN32)
	EnterCriticalSection(&q->lock);
	strncpy(q->cancelled_id, id_json, sizeof(q->cancelled_id) - 1);
	q->cancelled_id[sizeof(q->cancelled_id) - 1] = '\0';
	q->cancelled_id_valid = 1;
	LeaveCriticalSection(&q->lock);
#else
	(void) q;
	(void) id_json;
#endif
}

static int queue_take_cancel(line_queue *q, char *id_json, size_t size)
{
#if defined(_WIN32)
	int valid = 0;

	EnterCriticalSection(&q->lock);
	valid = q->cancelled_id_valid;
	if (valid)
	{
		q->cancelled_id_valid = 0;
		strncpy(id_json, q->cancelled_id, size - 1);
		id_json[size - 1] = '\0';
	}
	LeaveCriticalSection(&q->lock);
	return valid;
#else
	(void) q;
	(void) id_json;
	(void) size;
	return 0;
#endif
}

typedef struct reader_ctx {
	FILE		*in;
	line_queue	*q;
	volatile LONG	 stop;
} reader_ctx;

static void read_line_growing(FILE *in, char **buf, size_t *cap)
{
	size_t	used = 0;
	int		c;

	for (;;)
	{
		c = fgetc(in);

		if (c == EOF)
			break;

		if (c == '\n')
			break;

		if (c == '\r')
			continue;

		if ((used + 1) >= *cap)
		{
			size_t	 newcap = (*cap) * 2;
			char	*nb;

			while ((used + 1) > newcap)
				newcap *= 2;

			nb = (char *) realloc(*buf, newcap);
			if (nb == NULL)
				return;						/* OOM: drop the line			*/

			*buf = nb;
			*cap = newcap;
		}

		(*buf)[used++] = (char) c;
	}

	(*buf)[used] = '\0';
}

#if defined(_WIN32)
static unsigned __stdcall reader_thread(void *arg)
{
	reader_ctx	*ctx = (reader_ctx *) arg;
	char		*buf = NULL;
	size_t		 cap = 4096;

	buf = (char *) malloc(cap);
	if (buf == NULL)
	{
		queue_push(ctx->q, NULL);			/* signal EOF (empty)			*/
		InterlockedExchange(&ctx->stop, 1);
		return 0;
	}

	while (InterlockedCompareExchange(&ctx->stop, 0, 0) == 0)
	{
		size_t used = 0;
		int	   c;
		int	   got_any = 0;

		buf[0] = '\0';

		for (;;)
		{
			c = fgetc(ctx->in);

			if (c == EOF)
			{
				InterlockedExchange(&ctx->stop, 1);
				break;
			}

			if (c == '\r')
				continue;

			if (c == '\n')
				break;

			if ((used + 1) >= cap)
			{
				size_t	 newcap = cap * 2;
				char	*nb;

				while ((used + 1) > newcap)
					newcap *= 2;

				if (newcap > MCP_MAX_LINE)
					newcap = MCP_MAX_LINE;

				nb = (char *) realloc(buf, newcap);
				if (nb == NULL)
				{
					InterlockedExchange(&ctx->stop, 1);
					break;
				}

				buf = nb;
				cap = newcap;
			}

			if (used + 1 < cap)
			{
				buf[used++] = (char) c;
				buf[used] = '\0';
			}

			got_any = 1;
		}

		if (got_any && (used > 0))
			queue_push(ctx->q, _strdup(buf));

		if (ctx->stop)
			break;
	}

	/* Mark EOF so the main loop can shut down. */
	queue_push(ctx->q, NULL);
	free(buf);
	return 0;
}
#endif

/* ------------------------------------------------------------------ */
/* Response helpers                                                     */
/* ------------------------------------------------------------------ */

/*
 *	Standard JSON-RPC response envelope, fully tracked by the writer
 *	(no raw object interleaving -- that caused separator bugs).
 *	Usage:
 *	    reply_begin(&w, out, id, modern);
 *	    ... tracked members of the result object ...
 *	    reply_end(&w, out);
 */
static void reply_begin(sg_json_writer *w, FILE *out, const sg_dom_value *id,
						int modern)
{
	sg_json_begin(w, out);
	sg_json_object_begin(w, NULL);
	sg_json_string(w, "jsonrpc", "2.0");
	sg_json_key_raw(w, "id");
	if (id != NULL)
		sg_dom_emit(id, w);
	else
		sg_json_raw_null(w);
	w->pending_key = 0;

	sg_json_object_begin(w, "result");
	if (modern)
		sg_json_string(w, "resultType", "complete");
}

static void reply_end(sg_json_writer *w, FILE *out)
{
	sg_json_object_end(w);			/* result	*/
	sg_json_object_end(w);			/* root		*/
	fputc('\n', out);
	fflush(out);
}

static void send_error(FILE *out, const sg_dom_value *id,
					   int code, const char *message, const char *data_json)
{
	sg_json_writer w;
	char			buf[32];

	sg_json_begin(&w, out);
	sg_json_object_begin(&w, NULL);
	sg_json_string(&w, "jsonrpc", "2.0");
	sg_json_key_raw(&w, "id");
	if (id != NULL)
		sg_dom_emit(id, &w);
	else
		sg_json_raw_null(&w);

	sg_json_object_begin(&w, "error");
	snprintf(buf, sizeof(buf), "%d", code);
	sg_json_raw_member(&w, "code", buf);
	sg_json_string(&w, "message", message);

	if (data_json != NULL)
	{
		sg_json_key_raw(&w, "data");
		sg_json_raw_token(&w, data_json);
		w.pending_key = 0;
	}

	sg_json_object_end(&w);			/* error	*/
	sg_json_object_end(&w);			/* root		*/
	fputc('\n', out);
	fflush(out);
}

static int modern_version_ok(const sg_dom_value *params, char *requested,
							 size_t size)
{
	const sg_dom_value	*meta = sg_dom_get(params, "_meta");
	const sg_dom_value	*ver;

	if (meta == NULL)
		return -1;						/* not a modern request			*/

	ver = sg_dom_get(meta, "io.modelcontextprotocol/protocolVersion");

	if (ver == NULL)
		return -1;

	{
		const char *v = sg_dom_string(ver);

		if (v == NULL)
			return 0;

		strncpy(requested, v, size - 1);
		requested[size - 1] = '\0';

		return (strcmp(v, MCP_PROTOCOL_VERSION) == 0);
	}
}

/* ------------------------------------------------------------------ */
/* Tool metadata (input/output schemas as literal JSON, parsed once)    */
/* ------------------------------------------------------------------ */

static const char *SCHEMA_LIST_STARS =
	"{\"type\":\"object\",\"properties\":{\"catalog\":{\"type\":\"string\","
	"\"enum\":[\"solstation\",\"dole\",\"jimb\"],\"description\":\"Built-in "
	"star catalog to list\"}},\"required\":[\"catalog\"],"
	"\"additionalProperties\":false}";

static const char *SCHEMA_GENERATE =
	"{\"type\":\"object\",\"properties\":{"
	"\"catalog\":{\"type\":\"string\",\"enum\":[\"solstation\",\"dole\",\"jimb\"],"
	"\"description\":\"Built-in catalog; index 0 (Sol) uses the Solar System template\"},"
	"\"star_index\":{\"type\":\"integer\",\"minimum\":0,"
	"\"description\":\"0-based index into the catalog (required with catalog)\"},"
	"\"mass_solar\":{\"type\":\"number\",\"minimum\":0.2,\"maximum\":1.5,"
	"\"description\":\"Primary mass in solar masses (conflicts with catalog)\"},"
	"\"seed\":{\"type\":\"integer\",\"minimum\":0,"
	"\"description\":\"PRNG seed; omit for an automatic seed (returned in the result)\"},"
	"\"gases\":{\"type\":\"boolean\",\"default\":true,"
	"\"description\":\"Compute atmospheres (recommended for habitability)\"},"
	"\"moons\":{\"type\":\"boolean\",\"default\":false,"
	"\"description\":\"Experimental moon generation; can change planet properties\"},"
	"\"detail\":{\"type\":\"string\",\"enum\":[\"summary\",\"full\"],\"default\":\"summary\","
	"\"description\":\"summary = compact overview; full = complete model data\"}},"
	"\"additionalProperties\":false}";

static const char *SCHEMA_FIND =
	"{\"type\":\"object\",\"properties\":{"
	"\"catalog\":{\"type\":\"string\",\"enum\":[\"solstation\",\"dole\",\"jimb\"]},"
	"\"star_index\":{\"type\":\"integer\",\"minimum\":0},"
	"\"mass_solar\":{\"type\":\"number\",\"minimum\":0.2,\"maximum\":1.5},"
	"\"seed_start\":{\"type\":\"integer\",\"minimum\":1,\"default\":1,"
	"\"description\":\"First seed; each attempt increments it\"},"
	"\"criteria\":{\"type\":\"object\",\"properties\":{"
	"\"min_planets\":{\"type\":\"integer\",\"minimum\":0,\"default\":1},"
	"\"min_habitable\":{\"type\":\"integer\",\"minimum\":0,\"default\":0},"
	"\"min_earthlike\":{\"type\":\"integer\",\"minimum\":0,\"default\":0},"
	"\"requires_moons\":{\"type\":\"boolean\",\"default\":false},"
	"\"planet_type\":{\"type\":\"string\","
	"\"description\":\"A planet of this type must be present (e.g. Terrestrial)\"}}},"
	"\"max_attempts\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":1000,\"default\":100},"
	"\"max_results\":{\"type\":\"integer\",\"minimum\":1,\"maximum\":10,\"default\":10},"
	"\"time_budget_ms\":{\"type\":\"integer\",\"minimum\":100,\"maximum\":30000,\"default\":30000},"
	"\"gases\":{\"type\":\"boolean\",\"default\":true},"
	"\"moons\":{\"type\":\"boolean\",\"default\":false}},"
	"\"additionalProperties\":false}";

/* ------------------------------------------------------------------ */
/* Worker plumbing                                                      */
/* ------------------------------------------------------------------ */

typedef struct worker_args {
	char *tokens[16];
} worker_args;

static void wa_init(worker_args *wa)
{
	memset(wa, 0, sizeof(*wa));
}

static void wa_push(worker_args *wa, const char *token)
{
	int i;

	for (i = 0; i < 15; i++)
	{
		if (wa->tokens[i] == NULL)
		{
			wa->tokens[i] = _strdup(token);
			return;
		}
	}
}

static void wa_free(worker_args *wa)
{
	int i;

	for (i = 0; i < 16; i++)
	{
		free(wa->tokens[i]);
		wa->tokens[i] = NULL;
	}
}

/*
 *	Run one worker and parse its output. Returns a DOM of the document,
 *	or NULL (with err_text describing the failure).
 */
static sg_dom_value *run_worker_json(const char *const *argv,
									 unsigned timeout_ms,
									 int *timed_out, int *exit_code,
									 char *err_text, size_t err_size)
{
	sg_worker_result	result;
	sg_dom_value		*doc;

	if (sg_worker_run(argv, timeout_ms, MCP_MAX_OUTPUT, &result) != 0)
	{
		snprintf(err_text, err_size, "worker launch failed: %s", result.reason);
		return NULL;
	}

	if (result.timed_out)
	{
		if (timed_out != NULL)
			*timed_out = 1;
		snprintf(err_text, err_size, "worker timed out after %u ms", timeout_ms);
		sg_worker_result_free(&result);
		return NULL;
	}

	if (result.exit_code != 0)
	{
		snprintf(err_text, err_size,
				 "worker failed (exit %d; see server log)", result.exit_code);
		sg_worker_result_free(&result);
		if (exit_code != NULL)
			*exit_code = result.exit_code;
		return NULL;
	}

	if (result.stdout_text == NULL)
	{
		snprintf(err_text, err_size, "worker produced no output");
		sg_worker_result_free(&result);
		return NULL;
	}

	doc = sg_dom_parse(result.stdout_text, result.stdout_len);

	if (doc == NULL)
	{
		snprintf(err_text, err_size,
				 "worker output was not valid JSON%s",
				 result.truncated ? " (output truncated)" : "");
		sg_worker_result_free(&result);
		return NULL;
	}

	{
		const sg_dom_value *schema = sg_dom_get(doc, "schema_version");

		if ((schema == NULL) || (sg_dom_type_of(schema) != sgDomNumber)
			|| (sg_dom_long(schema) != SG_JSON_SCHEMA_VERSION))
		{
			snprintf(err_text, err_size,
					 "worker output has an unexpected schema (no schema_version %d)",
					 SG_JSON_SCHEMA_VERSION);
			sg_dom_free(doc);
			sg_worker_result_free(&result);
			return NULL;
		}
	}

	sg_worker_result_free(&result);
	return doc;
}

/* Build worker argv for a generate-like request (shared by
 * generate_system and find_systems). Returns 0 on success, nonzero with
 * a message on validation failure. */
static int build_source_args(const sg_dom_value *args, worker_args *wa,
							 char *err, size_t err_size)
{
	const sg_dom_value	*cat = sg_dom_get(args, "catalog");
	const sg_dom_value	*idx = sg_dom_get(args, "star_index");
	const sg_dom_value	*mass = sg_dom_get(args, "mass_solar");

	wa_init(wa);

	if ((cat != NULL) && (mass != NULL))
	{
		snprintf(err, err_size, "catalog and mass_solar cannot be combined");
		return 1;
	}

	if (cat != NULL)
	{
		const char	*cat_name = sg_dom_string(cat);
		long		 index;

		if ((cat_name == NULL)
			|| ((strcmp(cat_name, "solstation") != 0)
				&& (strcmp(cat_name, "dole") != 0)
				&& (strcmp(cat_name, "jimb") != 0)))
		{
			snprintf(err, err_size,
					 "catalog must be one of: solstation, dole, jimb");
			return 1;
		}

		if (idx == NULL)
		{
			snprintf(err, err_size, "star_index is required with catalog");
			return 1;
		}

		if (sg_dom_type_of(idx) != sgDomNumber)
		{
			snprintf(err, err_size, "star_index must be an integer");
			return 1;
		}

		index = sg_dom_long(idx);

		if (index < 0)
		{
			snprintf(err, err_size, "star_index must be >= 0");
			return 1;
		}

		wa_push(wa, "--catalog");
		wa_push(wa, cat_name);
		wa_push(wa, "--star-index");

		{
			char buf[32];
			snprintf(buf, sizeof(buf), "%ld", index);
			wa_push(wa, buf);
		}
	}
	else if (mass != NULL)
	{
		char	buf[64];
		double	m;

		if (sg_dom_type_of(mass) != sgDomNumber)
		{
			snprintf(err, err_size, "mass_solar must be a number");
			return 1;
		}

		m = sg_dom_double(mass);

		if ((m < 0.2) || (m > 1.5))
		{
			snprintf(err, err_size,
					 "mass_solar %g is outside the supported range [0.2, 1.5]", m);
			return 1;
		}

		snprintf(buf, sizeof(buf), "--mass-solar");
		wa_push(wa, buf);
		snprintf(buf, sizeof(buf), "%g", m);
		wa_push(wa, buf);
	}

	return 0;
}

static void push_flag_arg(worker_args *wa, const sg_dom_value *args,
						  const char *key, const char *on, const char *off,
						  int default_on)
{
	const sg_dom_value *v = sg_dom_get(args, key);
	int value = default_on;

	if ((v != NULL) && (sg_dom_type_of(v) == sgDomBool))
		value = sg_dom_bool(v);

	wa_push(wa, value ? on : off);
}

/* ------------------------------------------------------------------ */
/* Tool: list_stars                                                     */
/* ------------------------------------------------------------------ */

typedef struct tool_ctx {
	mcp_config	 cfg;
	FILE		*err;
} tool_ctx;

/* ------------------------------------------------------------------ */
/* tools/list                                                           */
/* ------------------------------------------------------------------ */

static void emit_tool_items(sg_json_writer *w, void **schemas)
{
	{
		static const char *names[]			= {"list_stars", "generate_system", "find_systems"};
		static const char *titles[]			= {"List catalog stars", "Generate a planetary system", "Search generated systems"};
		static const char *descriptions[]	= {
			"List the stars of a built-in catalog with their parameters. "
			"Index 0 of every catalog is the Solar System template.",
			"Generate one fictional planetary system with the StarGen "
			"accretion model (synthetic, model-based output). Specify the "
			"star either by catalog+star_index or by mass_solar [0.2, 1.5]. "
			"Gases are computed by default; moons are experimental and can "
			"change planet properties.",
			"Run a bounded seeded search for systems matching criteria "
			"(planet counts, habitability, earthlikeness, moon presence, "
			"planet type). Returns attempts, effective seeds and matches. "
			"No match is a valid result."
		};
		int	i;

		for (i = 0; i < 3; i++)
		{
			sg_json_object_begin(w, NULL);
			sg_json_string(w, "name", names[i]);
			sg_json_string(w, "title", titles[i]);
			sg_json_string(w, "description", descriptions[i]);
			sg_json_key_raw(w, "inputSchema");
			sg_dom_emit(schemas[i], w);
			w->pending_key = 0;
			sg_json_object_end(w);
		}
	}
}

static void reply_tools_list(FILE *out, const sg_dom_value *id,
							 int modern, sg_dom_value **schemas)
{
	sg_json_writer w;

	reply_begin(&w, out, id, modern);
	sg_json_array_begin(&w, "tools");
	emit_tool_items(&w, schemas);
	sg_json_array_end(&w);
	reply_end(&w, out);
}

static int tool_list_stars(tool_ctx *tc, const sg_dom_value *args,
						   const sg_dom_value *id, FILE *out)
{
	worker_args			wa;
	char				err[256] = "";
	sg_dom_value		*doc;
	const sg_dom_value	*catv = sg_dom_get(args, "catalog");
	const char			*cat_name = sg_dom_string(catv);
	char				summary[256];

	if ((cat_name == NULL)
		|| ((strcmp(cat_name, "solstation") != 0)
			&& (strcmp(cat_name, "dole") != 0)
			&& (strcmp(cat_name, "jimb") != 0)))
	{
		send_error(out, id, -32602,
				   "catalog must be one of: solstation, dole, jimb", NULL);
		return 1;
	}

	wa_init(&wa);
	wa_push(&wa, "--list-stars-json");
	wa_push(&wa, "--catalog");
	wa_push(&wa, cat_name);

	{
		const char	*argv[8];
		int			 n;

		for (n = 0; (wa.tokens[n] != NULL) && (n < 15); n++)
			argv[n] = wa.tokens[n];
		argv[n] = NULL;

		doc = run_worker_json(argv, tc->cfg.worker_timeout_ms, NULL, NULL,
							  err, sizeof(err));
	}

	wa_free(&wa);

	if (doc == NULL)
	{
		char data[512];
		snprintf(data, sizeof(data), "{\"tool\":\"list_stars\",\"error\":\"%s\"}",
				 err);
		send_error(out, id, -32000, "worker error", data);
		return 1;
	}

	{
		const sg_dom_value *count = sg_dom_get(doc, "star_count");

		snprintf(summary, sizeof(summary),
				 "Catalog '%s': %ld stars (0-based indices). Index 0 is the "
				 "Solar System template; use star_index with generate_system.",
				 cat_name,
				 (count != NULL) ? sg_dom_long(count) : 0);
	}

	/* Content: text summary + the full listing as structured content. */
	{
		sg_json_writer w;

		reply_begin(&w, out, id, 1);		/* tools results use the modern shape */

		sg_json_array_begin(&w, "content");
		sg_json_object_begin(&w, NULL);
		sg_json_string(&w, "type", "text");
		sg_json_string(&w, "text", summary);
		sg_json_object_end(&w);
		sg_json_array_end(&w);

		sg_json_key_raw(&w, "structuredContent");
		sg_dom_emit(doc, &w);
		w.pending_key = 0;

		sg_json_bool(&w, "isError", 0);
		reply_end(&w, out);
	}

	sg_dom_free(doc);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Tool: generate_system                                                */
/* ------------------------------------------------------------------ */

static void summarize_planet_line(char *dst, size_t size,
								  const sg_dom_value *planet)
{
	const sg_dom_value	*mass_e = sg_dom_get(sg_dom_get(planet, "mass"), "earth");
	const sg_dom_value	*type = sg_dom_get(planet, "type");
	const sg_dom_value	*a = sg_dom_get(sg_dom_get(planet, "orbit"), "a_au");
	const sg_dom_value	*hab = sg_dom_get(sg_dom_get(planet, "classification"), "habitable");
	const sg_dom_value	*earthlike = sg_dom_get(sg_dom_get(planet, "classification"), "earthlike");
	int					moons = sg_dom_count(sg_dom_get(planet, "moons"));

	snprintf(dst, size, "  %s %s: %.3Lf AU, %s Earth masses%s%s",
			 sg_dom_string(sg_dom_get(planet, "id")),
			 (type != NULL) ? sg_dom_string(type) : "?",
			 (long double) ((a != NULL) ? sg_dom_double(a) : 0.0),
			 (mass_e != NULL) ? sg_dom_number_token(mass_e) : "?",
			 ((hab != NULL) && sg_dom_bool(hab)) ? ", HABITABLE" : "",
			 (moons > 0) ? (earthlike != NULL ? ", has moons" : "") : "");
}

static void emit_full_doc(sg_json_writer *w, void *ud)
{
	sg_dom_emit((sg_dom_value *) ud, w);
}

static void emit_summary_doc(sg_json_writer *w, void *ud)
{
	sg_dom_value *doc = (sg_dom_value *) ud;

	sg_json_object_begin(w, NULL);
	{
		const char *keys[] = {
			"schema_version", "status", "engine", "request", "effective",
			"star", "summary", "warnings", "error"
		};
		size_t i;

		for (i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
		{
			const sg_dom_value *v = sg_dom_get(doc, keys[i]);

			if (v == NULL)
				continue;

			sg_json_key_raw(w, keys[i]);
			sg_dom_emit(v, w);
		}

		/* Compact planet list. */
		sg_json_array_begin(w, "planets");
		{
			const sg_dom_value *planets = sg_dom_get(doc, "planets");
			const sg_dom_value *p;
			int					pi;

			for (pi = 0; ((p = sg_dom_index(planets, pi)) != NULL); pi++)
			{
				sg_json_object_begin(w, NULL);
				{
					const sg_dom_value *pv;

					pv = sg_dom_get(p, "id");
					if (pv != NULL) { sg_json_key_raw(w, "id"); sg_dom_emit(pv, w); }
					pv = sg_dom_get(p, "type");
					if (pv != NULL) { sg_json_key_raw(w, "type"); sg_dom_emit(pv, w); }
					pv = sg_dom_get(sg_dom_get(p, "orbit"), "a_au");
					if (pv != NULL) { sg_json_key_raw(w, "a_au"); sg_dom_emit(pv, w); }
					pv = sg_dom_get(sg_dom_get(p, "mass"), "earth");
					if (pv != NULL) { sg_json_key_raw(w, "mass_earth"); sg_dom_emit(pv, w); }
					pv = sg_dom_get(sg_dom_get(p, "physical"), "radius_earth");
					if (pv != NULL) { sg_json_key_raw(w, "radius_earth"); sg_dom_emit(pv, w); }
					pv = sg_dom_get(sg_dom_get(p, "surface"), "temperature_k");
					if (pv != NULL) { sg_json_key_raw(w, "temperature_k"); sg_dom_emit(pv, w); }
					pv = sg_dom_get(sg_dom_get(p, "surface"), "pressure_mbar");
					if (pv != NULL) { sg_json_key_raw(w, "pressure_mbar"); sg_dom_emit(pv, w); }
					pv = sg_dom_get(sg_dom_get(p, "classification"), "habitable");
					if (pv != NULL) { sg_json_key_raw(w, "habitable"); sg_dom_emit(pv, w); }
					pv = sg_dom_get(sg_dom_get(p, "classification"), "earthlike");
					if (pv != NULL) { sg_json_key_raw(w, "earthlike"); sg_dom_emit(pv, w); }
					pv = sg_dom_get(sg_dom_get(p, "atmosphere"), "breathability");
					if (pv != NULL) { sg_json_key_raw(w, "breathability"); sg_dom_emit(pv, w); }
				}

				sg_json_array_begin(w, "moons");
				{
					const sg_dom_value *moons = sg_dom_get(p, "moons");
					const sg_dom_value *m;
					int					mi;

					for (mi = 0; ((m = sg_dom_index(moons, mi)) != NULL); mi++)
					{
						sg_json_object_begin(w, NULL);
						{
							const sg_dom_value *mv = sg_dom_get(m, "id");

							if (mv != NULL) { sg_json_key_raw(w, "id"); sg_dom_emit(mv, w); }
							mv = sg_dom_get(m, "type");
							if (mv != NULL) { sg_json_key_raw(w, "type"); sg_dom_emit(mv, w); }
							mv = sg_dom_get(sg_dom_get(m, "mass"), "earth");
							if (mv != NULL) { sg_json_key_raw(w, "mass_earth"); sg_dom_emit(mv, w); }
						}
						sg_json_object_end(w);
					}
				}
				sg_json_array_end(w);

				sg_json_object_end(w);
			}
		}
		sg_json_array_end(w);

		sg_json_object_end(w);
	}
}

static int tool_generate_system(tool_ctx *tc, const sg_dom_value *args,
								const sg_dom_value *id, FILE *out)
{
	worker_args			wa;
	char				err[256] = "";
	sg_dom_value		*doc = NULL;
	int					timed_out = 0;
	const sg_dom_value	*detail = sg_dom_get(args, "detail");
	const char			*detail_str = sg_dom_string(detail);
	int					full = ((detail_str != NULL)
								&& (strcmp(detail_str, "full") == 0));
	char				seed_buf[32];
	const char			*argv[20];
	int					n;

	if (build_source_args(args, &wa, err, sizeof(err)) != 0)
	{
		send_error(out, id, -32602, err, NULL);
		return 1;
	}

	push_flag_arg(&wa, args, "gases", "--gases", "--no-gases", 1);
	push_flag_arg(&wa, args, "moons", "--moons", "--no-moons", 0);
	wa_push(&wa, "--json");

	/* Assemble the argv for THIS call: base tokens + explicit seed. */
	for (n = 0; (wa.tokens[n] != NULL) && (n < 15); n++)
		argv[n] = wa.tokens[n];

	snprintf(seed_buf, sizeof(seed_buf), "%ld",
			 sg_dom_long(sg_dom_get(args, "seed")));
	argv[n++] = "--seed";
	argv[n++] = seed_buf;
	argv[n] = NULL;

	doc = run_worker_json(argv, tc->cfg.worker_timeout_ms, &timed_out, NULL,
						  err, sizeof(err));
	wa_free(&wa);

	if (doc == NULL)
	{
		char data[512];

		snprintf(data, sizeof(data), "{\"tool\":\"generate_system\",\"timed_out\":%s}",
				 timed_out ? "true" : "false");
		send_error(out, id, timed_out ? -32001 : -32000,
				   timed_out ? "generation timed out" : "worker error", data);
		return 1;
	}

	/* Build the text overview. */
	{
		char					text[2048];
		const sg_dom_value	*star_name = sg_dom_get(sg_dom_get(doc, "star"), "name");
		const sg_dom_value	*eff = sg_dom_get(doc, "effective");
		const sg_dom_value	*summary = sg_dom_get(doc, "summary");
		const sg_dom_value	*warnings = sg_dom_get(doc, "warnings");
		const sg_dom_value	*planets = sg_dom_get(doc, "planets");
		size_t					used = 0;
		const sg_dom_value	*p;
		int					pi;

		used += (size_t) snprintf(text + used, sizeof(text) - used,
				"Generated system '%s' (seed %ld, %s mass, %ld planets + %ld moons; "
				"habitable bodies: %ld, earthlike: %ld).",
				(star_name != NULL) ? sg_dom_string(star_name) : "?",
				sg_dom_long(sg_dom_get(eff, "seed")),
				(sg_dom_string(sg_dom_get(sg_dom_get(doc, "request"), "source")) != NULL
					? sg_dom_string(sg_dom_get(sg_dom_get(doc, "request"), "source"))
					: "?"),
				sg_dom_long(sg_dom_get(summary, "planet_count")),
				sg_dom_long(sg_dom_get(summary, "moon_count")),
				sg_dom_long(sg_dom_get(summary, "habitable_planets"))
					+ sg_dom_long(sg_dom_get(summary, "habitable_moons")),
				sg_dom_long(sg_dom_get(summary, "earthlike_planets")));

		for (pi = 0; ((p = sg_dom_index(planets, pi)) != NULL); pi++)
		{
			char line[192];

			summarize_planet_line(line, sizeof(line), p);
			used += (size_t) snprintf(text + used, sizeof(text) - used,
									  "\n%s", line);
		}

		if (sg_dom_count(warnings) > 0)
		{
			const sg_dom_value *wv;
			int					wi;

			used += (size_t) snprintf(text + used, sizeof(text) - used,
									  "\nWarnings:");
			for (wi = 0; ((wv = sg_dom_index(warnings, wi)) != NULL); wi++)
				used += (size_t) snprintf(text + used, sizeof(text) - used,
										  "\n- %s", sg_dom_string(wv));
		}

		{
			sg_json_writer w;

			reply_begin(&w, out, id, 1);

			sg_json_array_begin(&w, "content");
			sg_json_object_begin(&w, NULL);
			sg_json_string(&w, "type", "text");
			sg_json_key_raw(&w, "text");
			sg_json_quoted_string(&w, text);
			w.pending_key = 0;
			sg_json_object_end(&w);
			sg_json_array_end(&w);

			sg_json_key_raw(&w, "structuredContent");
			if (full)
				emit_full_doc(&w, doc);
			else
				emit_summary_doc(&w, doc);
			w.pending_key = 0;

			sg_json_bool(&w, "isError", 0);
			reply_end(&w, out);
		}
	}

	sg_dom_free(doc);
	return 0;
}

/* ------------------------------------------------------------------ */
/* Tool: find_systems                                                   */
/* ------------------------------------------------------------------ */

/* Count chained match documents (they are root objects linked by
 * next_sibling, not a JSON array). */
static int find_match_count(const sg_dom_value *matches)
{
	const sg_dom_value *m;
	int					n = 0;

	for (m = matches; m != NULL; m = m->next_sibling)
		++n;

	return n;
}

static int criteria_match(const sg_dom_value *doc, const sg_dom_value *criteria)
{
	const sg_dom_value	*summary = sg_dom_get(doc, "summary");
	long					min_planets = 1;
	long					min_habitable = 0;
	long					min_earthlike = 0;
	int						requires_moons = 0;
	const char				*want_type = NULL;
	const sg_dom_value		*v;

	if (criteria == NULL)
		return 1;

	if ((v = sg_dom_get(criteria, "min_planets")) != NULL)
		min_planets = sg_dom_long(v);
	if ((v = sg_dom_get(criteria, "min_habitable")) != NULL)
		min_habitable = sg_dom_long(v);
	if ((v = sg_dom_get(criteria, "min_earthlike")) != NULL)
		min_earthlike = sg_dom_long(v);
	if ((v = sg_dom_get(criteria, "requires_moons")) != NULL)
		requires_moons = sg_dom_bool(v);
	if ((v = sg_dom_get(criteria, "planet_type")) != NULL)
		want_type = sg_dom_string(v);

	if (sg_dom_long(sg_dom_get(summary, "planet_count")) < min_planets)
		return 0;
	if (sg_dom_long(sg_dom_get(summary, "habitable_planets")) < min_habitable)
		return 0;
	if (sg_dom_long(sg_dom_get(summary, "earthlike_planets")) < min_earthlike)
		return 0;
	if (requires_moons
		&& (sg_dom_long(sg_dom_get(summary, "moon_count")) <= 0))
		return 0;

	if (want_type != NULL)
	{
		const sg_dom_value	*counts = sg_dom_get(summary, "counts_by_type");
		const sg_dom_value	*tv = sg_dom_get(counts, want_type);

		if ((tv == NULL) || (sg_dom_long(tv) <= 0))
			return 0;
	}

	return 1;
}

static int tool_find_systems(tool_ctx *tc, const sg_dom_value *args,
							 const sg_dom_value *id, FILE *out,
							 line_queue *queue)
{
	worker_args			wa;
	char				err[256] = "";
	long				seed_start = 1;
	long				max_attempts = 100;
	long				max_results = 10;
	unsigned			time_budget = tc->cfg.search_budget_ms;
	int					attempts = 0;
	int					terminated_by_time = 0;
	int					cancelled = 0;
	sg_dom_value		*matches = NULL;
	sg_dom_value		*matches_tail = NULL;
	const sg_dom_value	*criteria = sg_dom_get(args, "criteria");
	const sg_dom_value	*seed_start_v = sg_dom_get(args, "seed_start");
	const sg_dom_value	*max_attempts_v = sg_dom_get(args, "max_attempts");
	const sg_dom_value	*max_results_v = sg_dom_get(args, "max_results");
	const sg_dom_value	*time_budget_v = sg_dom_get(args, "time_budget_ms");
	char				cancel_id[64];
	char				*seed_list = NULL;
	size_t				 seed_list_cap = 0, seed_list_used = 0;

	if (build_source_args(args, &wa, err, sizeof(err)) != 0)
	{
		send_error(out, id, -32602, err, NULL);
		return 1;
	}

	/* Add the per-attempt parameters. */
	push_flag_arg(&wa, args, "gases", "--gases", "--no-gases", 1);
	push_flag_arg(&wa, args, "moons", "--moons", "--no-moons", 0);
	wa_push(&wa, "--json");

	if (seed_start_v != NULL)
	{
		if (sg_dom_type_of(seed_start_v) != sgDomNumber)
		{
			wa_free(&wa);
			send_error(out, id, -32602, "seed_start must be an integer", NULL);
			return 1;
		}

		seed_start = sg_dom_long(seed_start_v);

		if (seed_start < 1)
		{
			wa_free(&wa);
			send_error(out, id, -32602, "seed_start must be >= 1", NULL);
			return 1;
		}
	}

	if ((max_attempts_v != NULL) && (sg_dom_type_of(max_attempts_v) == sgDomNumber))
	{
		max_attempts = sg_dom_long(max_attempts_v);
		if (max_attempts < 1)
			max_attempts = 1;
		if (max_attempts > (long) tc->cfg.max_attempts)
			max_attempts = (long) tc->cfg.max_attempts;
	}

	if ((max_results_v != NULL) && (sg_dom_type_of(max_results_v) == sgDomNumber))
	{
		max_results = sg_dom_long(max_results_v);
		if (max_results < 1)
			max_results = 1;
		if (max_results > (long) tc->cfg.max_results)
			max_results = (long) tc->cfg.max_results;
	}

	if ((time_budget_v != NULL) && (sg_dom_type_of(time_budget_v) == sgDomNumber))
	{
		double tb = sg_dom_double(time_budget_v);

		if (tb >= 100.0)
		{
			unsigned want = (unsigned) tb;

			if (want > tc->cfg.search_budget_ms)
				want = tc->cfg.search_budget_ms;
			time_budget = want;
		}
	}

	seed_list_cap = 4096;
	seed_list = (char *) malloc(seed_list_cap);
	seed_list[0] = '\0';

	{
#if defined(_WIN32)
		unsigned long long start_tick = GetTickCount64();
#else
		unsigned long long start_tick = (unsigned long long) time(NULL) * 1000u;
#endif
		int stop = 0;

		while (!stop)
		{
			sg_dom_value	*doc;
			int				timed_out = 0;
			long			seed;
			char			seed_tok[32];

			/* Cancellation: a cancelled notification received while this
			 * search is in flight stops it (single-client server). */
			if (queue_take_cancel(queue, cancel_id, sizeof(cancel_id)))
			{
				cancelled = 1;
				break;
			}

			/* Time budget. */
			{
#if defined(_WIN32)
				unsigned long long now = GetTickCount64();
#else
				unsigned long long now = (unsigned long long) time(NULL) * 1000u;
#endif
				if ((attempts > 0) && ((now - start_tick) >= time_budget))
				{
					terminated_by_time = 1;
					break;
				}
			}

			if (attempts >= (int) max_attempts)
				break;

			seed = seed_start + attempts;

			{
				/* Assemble this attempt's argv: base tokens + this seed.
				 * The base tokens stay owned by `wa`; the seed buffers
				 * live in this block and outlive the worker call. */
				const char	*attempt_argv[20];
				char		 seed_buf[32];
				int			 n;

				for (n = 0; (wa.tokens[n] != NULL) && (n < 15); n++)
					attempt_argv[n] = wa.tokens[n];

				snprintf(seed_buf, sizeof(seed_buf), "%ld", seed);
				snprintf(seed_tok, sizeof(seed_tok), "%ld", seed);
				attempt_argv[n++] = "--seed";
				attempt_argv[n++] = seed_buf;
				attempt_argv[n] = NULL;

				doc = run_worker_json(attempt_argv, tc->cfg.worker_timeout_ms,
									  &timed_out, NULL, err, sizeof(err));

				if (doc == NULL)
				{
					if (timed_out)
					{
						/* A timed-out attempt is not fatal: count it and
						 * keep searching (PLAN.MD: bounded, not unlimited). */
						++attempts;
						continue;
					}

					wa_free(&wa);
					free(seed_list);
					{
						char data[512];
						snprintf(data, sizeof(data),
								 "{\"tool\":\"find_systems\",\"error\":\"%s\"}", err);
						send_error(out, id, -32000, "worker error", data);
					}
					return 1;
				}
			}

			++attempts;

			/* Track the searched seed sequence. */
			{
				size_t l = strlen(seed_tok);

				if (seed_list_used + l + 2 > seed_list_cap)
				{
					size_t		 newcap = seed_list_cap * 2;
					char		*nb = (char *) realloc(seed_list, newcap);

					if (nb != NULL)
					{
						seed_list = nb;
						seed_list_cap = newcap;
					}
				}

				if (seed_list_used + l + 2 <= seed_list_cap)
				{
					if (seed_list_used > 0)
						seed_list[seed_list_used++] = ',';
					memcpy(seed_list + seed_list_used, seed_tok, l);
					seed_list_used += l;
					seed_list[seed_list_used] = '\0';
				}
			}

			if (criteria_match(doc, criteria))
			{
				sg_dom_value *entry = doc;

				doc = NULL;
				entry->next_sibling = NULL;

				if (matches_tail == NULL)
				{
					matches = matches_tail = entry;
				}
				else
				{
					matches_tail->next_sibling = entry;
					matches_tail = entry;
				}

				if (find_match_count(matches) >= max_results)
					stop = 1;
			}

			sg_dom_free(doc);
			doc = NULL;
		}
	}

	wa_free(&wa);
	free(seed_list);

	/* A cancelled request gets no response (MCP: the server MUST NOT
	 * send further messages for a cancelled request). */
	if (cancelled)
	{
		sg_dom_free(matches);
		return 0;
	}

	/* Build the result document. */
	{
		char					text[1024];
		sg_json_writer			w;
		const char				*how = terminated_by_time ? "time_limit" : "completed";

		snprintf(text, sizeof(text),
				 "Searched %d seed(s) starting at %ld; %ld match(es) found "
				 "(terminated: %s).",
				 attempts, seed_start,
				 (long) find_match_count(matches), how);

		reply_begin(&w, out, id, 1);

		sg_json_array_begin(&w, "content");
		sg_json_object_begin(&w, NULL);
		sg_json_string(&w, "type", "text");
		sg_json_string(&w, "text", text);
		sg_json_object_end(&w);
		sg_json_array_end(&w);

		sg_json_object_begin(&w, "structuredContent");
		sg_json_int(&w, "attempts", attempts);
		sg_json_long(&w, "seed_start", seed_start);
		sg_json_string(&w, "terminated_by", how);
		sg_json_array_begin(&w, "matches");
		{
			sg_dom_value *m;

			for (m = matches; m != NULL; m = m->next_sibling)
				emit_summary_doc(&w, m);
		}
		sg_json_array_end(&w);
		sg_json_object_end(&w);

		sg_json_bool(&w, "isError", 0);
		reply_end(&w, out);
	}

	sg_dom_free(matches);

	return 0;
}

/* ------------------------------------------------------------------ */
/* Dispatch                                                             */
/* ------------------------------------------------------------------ */

typedef struct dispatch_ctx {
	FILE			*out;
	FILE			*err;
	mcp_config		 cfg;
	sg_dom_value	**schemas;
	line_queue		*queue;
	int				*legacy_initialized;
} dispatch_ctx;

static int dc_is_modern(int version_result)
{
	return version_result > 0;
}

static void send_unsupported_version(FILE *out, const sg_dom_value *id,
									 const char *requested)
{
	char data[160];

	snprintf(data, sizeof(data),
			 "{\"supported\":[\"%s\"],\"requested\":\"%s\"}",
			 MCP_PROTOCOL_VERSION,
			 (requested != NULL) ? requested : "?");

	send_error(out, id, -32022, "Unsupported protocol version", data);
}

static void reply_initialize(FILE *out, const sg_dom_value *id,
							 const sg_dom_value *params)
{
	const sg_dom_value	*pv = sg_dom_get(params, "protocolVersion");
	const char			*client_version = sg_dom_string(pv);
	sg_json_writer		 w;

	sg_json_begin(&w, out);
	sg_json_object_begin(&w, NULL);
	sg_json_string(&w, "jsonrpc", "2.0");
	sg_json_key_raw(&w, "id");
	sg_dom_emit(id, &w);
	w.pending_key = 0;

	sg_json_object_begin(&w, "result");
	sg_json_string(&w, "protocolVersion",
				   (client_version != NULL) ? client_version : "2025-06-18");
	sg_json_object_begin(&w, "capabilities");
	sg_json_object_begin(&w, "tools");
	sg_json_bool(&w, "listChanged", 0);
	sg_json_object_end(&w);
	sg_json_object_end(&w);
	sg_json_object_begin(&w, "serverInfo");
	sg_json_string(&w, "name", MCP_SERVER_NAME);
	sg_json_string(&w, "version", MCP_SERVER_VERSION);
	sg_json_object_end(&w);
	sg_json_object_end(&w);			/* result	*/

	sg_json_object_end(&w);			/* root		*/
	fputc('\n', out);
	fflush(out);
}

static void reply_discover(FILE *out, const sg_dom_value *id)
{
	sg_json_writer w;

	sg_json_begin(&w, out);
	sg_json_object_begin(&w, NULL);
	sg_json_string(&w, "jsonrpc", "2.0");
	sg_json_key_raw(&w, "id");
	sg_dom_emit(id, &w);
	w.pending_key = 0;

	sg_json_object_begin(&w, "result");
	sg_json_string(&w, "resultType", "complete");
	sg_json_array_begin(&w, "supportedVersions");
	sg_json_sep_raw(&w);
	sg_json_quoted_string(&w, MCP_PROTOCOL_VERSION);
	sg_json_array_end(&w);
	sg_json_object_begin(&w, "capabilities");
	sg_json_object_begin(&w, "tools");
	sg_json_object_end(&w);
	sg_json_object_end(&w);
	sg_json_object_begin(&w, "_meta");
	sg_json_object_begin(&w, "io.modelcontextprotocol/serverInfo");
	sg_json_string(&w, "name", MCP_SERVER_NAME);
	sg_json_string(&w, "version", MCP_SERVER_VERSION);
	sg_json_object_end(&w);
	sg_json_object_end(&w);
	sg_json_string(&w, "instructions",
				   "Synthetic planetary-system generator (StarGen accretion "
				   "model). Use list_stars to find catalog stars, "
				   "generate_system for one system, find_systems for bounded "
				   "seeded searches. All output is model-based fiction, not "
				   "astronomy.");
	sg_json_object_end(&w);			/* result	*/

	sg_json_object_end(&w);			/* root		*/
	fputc('\n', out);
	fflush(out);
}

static void reply_ping(FILE *out, const sg_dom_value *id, int modern)
{
	sg_json_writer w;

	reply_begin(&w, out, id, modern);
	reply_end(&w, out);
}

static void dispatch_message(dispatch_ctx *dc, sg_dom_value *msg)
{
	const sg_dom_value	*method = sg_dom_get(msg, "method");
	const sg_dom_value	*id = sg_dom_get(msg, "id");
	const sg_dom_value	*params = sg_dom_get(msg, "params");
	const char			*mname;
	char				 requested_version[48] = "";
	int					version_result;

	if (method == NULL)
		return;							/* response or garbage: ignore		*/

	mname = sg_dom_string(method);

	if (mname == NULL)
	{
		if (id != NULL)
			send_error(dc->out, id, -32600, "invalid request", NULL);
		return;
	}

	version_result = modern_version_ok(params, requested_version,
									   sizeof(requested_version));

	/* Notifications never produce responses. */
	if (id == NULL)
	{
		if (strcmp(mname, "notifications/cancelled") == 0)
			queue_set_cancel(dc->queue, "-");

		/* notifications/initialized and unknown notifications: no-op. */
		return;
	}

	if (version_result == 0)
	{
		send_unsupported_version(dc->out, id, requested_version);
		return;
	}

	if (strcmp(mname, "initialize") == 0)
	{
		reply_initialize(dc->out, id, params);
		*dc->legacy_initialized = 1;
		return;
	}

	if (strcmp(mname, "server/discover") == 0)
	{
		reply_discover(dc->out, id);
		return;
	}

	if (strcmp(mname, "ping") == 0)
	{
		reply_ping(dc->out, id, dc_is_modern(version_result));
		return;
	}

	/* Legacy requests must be preceded by initialize (PLAN.MD dual-era
	 * dispatch; -32002 is the historical "not initialized" code). */
	if (!dc_is_modern(version_result) && !*dc->legacy_initialized)
	{
		send_error(dc->out, id, -32002, "Server not initialized", NULL);
		return;
	}

	if (strcmp(mname, "tools/list") == 0)
	{
		reply_tools_list(dc->out, id, dc_is_modern(version_result), dc->schemas);
		return;
	}

	if (strcmp(mname, "tools/call") == 0)
	{
		const sg_dom_value	*name = sg_dom_get(params, "name");
		const sg_dom_value	*arguments = sg_dom_get(params, "arguments");
		const char			*tool = sg_dom_string(name);
		tool_ctx			 tc;

		tc.cfg = dc->cfg;
		tc.err = dc->err;

		if (tool == NULL)
		{
			send_error(dc->out, id, -32602, "tools/call requires a tool name", NULL);
			return;
		}

		if ((arguments != NULL) && (sg_dom_type_of(arguments) != sgDomObject))
		{
			send_error(dc->out, id, -32602, "arguments must be an object", NULL);
			return;
		}

		if (arguments == NULL)
		{
			send_error(dc->out, id, -32602, "tools/call requires arguments", NULL);
			return;
		}

		if (strcmp(tool, "list_stars") == 0)
		{
			tool_list_stars(&tc, arguments, id, dc->out);
			return;
		}

		if (strcmp(tool, "generate_system") == 0)
		{
			tool_generate_system(&tc, arguments, id, dc->out);
			return;
		}

		if (strcmp(tool, "find_systems") == 0)
		{
			tool_find_systems(&tc, arguments, id, dc->out, dc->queue);
			return;
		}

		{
			char msg2[128];
			snprintf(msg2, sizeof(msg2), "Unknown tool: %s", tool);
			send_error(dc->out, id, -32602, msg2, NULL);
		}
		return;
	}

	send_error(dc->out, id, -32601, "method not found", NULL);
}

/* ------------------------------------------------------------------ */
/* Server main loop                                                     */
/* ------------------------------------------------------------------ */

int sg_mcp_server_run(FILE *in, FILE *out, FILE *err)
{
	mcp_session		session;
	dispatch_ctx	dc;
	sg_dom_value	*schemas[3];
	int				exit_code = 0;

	config_load(&dc.cfg);
	dc.out = out;
	dc.err = err;
	dc.schemas = schemas;
	dc.legacy_initialized = &session.legacy_initialized;

	queue_init(&session.queue);
	session.legacy_initialized = 0;
	dc.queue = &session.queue;

	schemas[0] = sg_dom_parse(SCHEMA_LIST_STARS, strlen(SCHEMA_LIST_STARS));
	schemas[1] = sg_dom_parse(SCHEMA_GENERATE, strlen(SCHEMA_GENERATE));
	schemas[2] = sg_dom_parse(SCHEMA_FIND, strlen(SCHEMA_FIND));

	if ((schemas[0] == NULL) || (schemas[1] == NULL) || (schemas[2] == NULL))
	{
		fprintf(err, "internal error: tool schemas failed to parse\n");
		return 2;
	}

#if defined(_WIN32)
	{
		reader_ctx	rctx;
		HANDLE		thread;

		memset(&rctx, 0, sizeof(rctx));
		rctx.in = in;
		rctx.q = &session.queue;

		thread = (HANDLE) _beginthreadex(NULL, 0, reader_thread, &rctx, 0, NULL);

		if (thread == NULL)
		{
			fprintf(err, "internal error: reader thread failed\n");
			sg_dom_free(schemas[0]);
			sg_dom_free(schemas[1]);
			sg_dom_free(schemas[2]);
			return 2;
		}

		/* Main loop: pop one message at a time and dispatch it. */
		for (;;)
		{
			char *line = queue_pop(&session.queue);

			if (line == NULL)
			{
				if (InterlockedCompareExchange(&rctx.stop, 0, 0) != 0)
					break;				/* EOF signaled					*/

				/* The reader may still be between lines; brief pause. */
				Sleep(5);
				continue;
			}

			if (line[0] != '\0')
			{
				sg_dom_value *msg = sg_dom_parse(line, strlen(line));

				if (msg == NULL)
					send_error(out, NULL, -32700, "parse error", NULL);
				else
				{
					dispatch_message(&dc, msg);
					sg_dom_free(msg);
				}
			}

			free(line);
		}

		/* Shutdown: stdin is closed; the reader thread will exit on its
		 * own (fgetc returns EOF). The process exit reclaims it. */
		InterlockedExchange(&rctx.stop, 1);
		CloseHandle(thread);
	}
#else
	/* Fallback without a reader thread: synchronous single-threaded
	 * loop. Cancellation notifications are only observed between
	 * requests (documented limitation of the non-Windows build). */
	{
		char	*buf = NULL;
		size_t	 cap = 4096;

		buf = (char *) malloc(cap);
		if (buf == NULL)
			return 2;

		for (;;)
		{
			size_t used = 0;
			int	   c;

			buf[0] = '\0';

			for (;;)
			{
				c = fgetc(in);

				if (c == EOF)
					break;

				if (c == '\r')
					continue;

				if (c == '\n')
					break;

				if ((used + 1) < cap)
				{
					buf[used++] = (char) c;
					buf[used] = '\0';
				}
			}

			if ((c == EOF) && (used == 0))
				break;

			if (used > 0)
			{
				sg_dom_value *msg = sg_dom_parse(buf, used);

				if (msg == NULL)
					send_error(out, NULL, -32700, "parse error", NULL);
				else
				{
					dispatch_message(&dc, msg);
					sg_dom_free(msg);
				}
			}

			if (c == EOF)
				break;
		}

		free(buf);
	}
#endif

	fprintf(err, "stargen MCP server stopped\n");

	sg_dom_free(schemas[0]);
	sg_dom_free(schemas[1]);
	sg_dom_free(schemas[2]);

	return exit_code;
}
