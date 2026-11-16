/*
 *	sg_worker.h -- one-shot subprocess execution of the headless CLI
 *	(PLAN.MD Phase E). The MCP server runs every generation in a fresh
 *	child process (`StarGen --json ...`): crash isolation and enforceable
 *	timeouts, exactly as PLAN.MD's safety rules require.
 *
 *	The child's stdout is captured into a bounded buffer; its stderr is
 *	inherited from the server process (diagnostics appear as server
 *	logs and cannot deadlock a pipe).
 */

#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct sg_worker_result {
	char	*stdout_text;		/* malloc'd NUL-terminated text, or NULL	*/
	size_t	stdout_len;
	int		truncated;			/* output exceeded max_output_bytes			*/
	int		timed_out;
	int		launch_failed;
	int		exit_code;			/* meaningful when not timed_out/launch		*/
	char	reason[256];		/* human-readable status for diagnostics	*/
} sg_worker_result;

/*
 *	Run `<self> args...` in a fresh process. `args` is an argv-style
 *	NULL-terminated array of UTF-8 tokens (not including argv[0]).
 *	Returns 0 on a completed run (check the result fields), nonzero on
 *	local failures (OOM / OS errors).
 */
int sg_worker_run(const char *const *args,
				  unsigned timeout_ms,
				  size_t max_output_bytes,
				  sg_worker_result *result);

void sg_worker_result_free(sg_worker_result *result);

#ifdef __cplusplus
}
#endif
