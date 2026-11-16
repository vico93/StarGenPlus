#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sg_worker.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>

typedef struct pipe_reader_ctx {
	HANDLE	pipe;				/* read end										*/
	char	*buf;
	size_t	len;
	size_t	cap;
	size_t	limit;				/* max_output_bytes								*/
	int		truncated;
	int		oom;
} pipe_reader_ctx;

static unsigned __stdcall pipe_reader(void *arg)
{
	pipe_reader_ctx	*ctx = (pipe_reader_ctx *) arg;
	char			 chunk[8192];
	DWORD			 n;

	for (;;)
	{
		if (!ReadFile(ctx->pipe, chunk, sizeof(chunk), &n, NULL))
			break;					/* EOF or broken pipe					*/

		if (n == 0)
			break;

		if ((ctx->len + n) > ctx->limit)
		{
			size_t room = (ctx->cap > ctx->len) ? (ctx->cap - ctx->len) : 0;

			if (room > 0)
			{
				memcpy(ctx->buf + ctx->len, chunk, room);
				ctx->len += room;
			}

			ctx->truncated = 1;
			/* Keep draining so the child does not block on a full pipe. */
			continue;
		}

		if (ctx->len + (size_t) n + 1 > ctx->cap)
		{
			size_t	 newcap = ctx->cap * 2;
			char	*nb;

			while (ctx->len + (size_t) n + 1 > newcap)
				newcap *= 2;

			if (newcap > ctx->limit + 1)
				newcap = ctx->limit + 1;

			nb = (char *) realloc(ctx->buf, newcap);
			if (nb == NULL)
			{
				ctx->oom = 1;
				break;
			}

			ctx->buf = nb;
			ctx->cap = newcap;
		}

		memcpy(ctx->buf + ctx->len, chunk, n);
		ctx->len += (size_t) n;
	}

	if (ctx->buf != NULL)
		ctx->buf[ctx->len] = '\0';

	return 0;
}

static wchar_t *utf8_to_utf16(const char *s)
{
	int			wlen = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
	wchar_t		*w;

	if (wlen <= 0)
		return NULL;

	w = (wchar_t *) malloc(sizeof(wchar_t) * (size_t) wlen);
	if (w == NULL)
		return NULL;

	if (MultiByteToWideChar(CP_UTF8, 0, s, -1, w, wlen) <= 0)
	{
		free(w);
		return NULL;
	}

	return w;
}

int sg_worker_run(const char *const *args,
				  unsigned timeout_ms,
				  size_t max_output_bytes,
				  sg_worker_result *result)
{
	wchar_t				self[MAX_PATH];
	size_t				clen = 0;
	wchar_t			*cmdline = NULL;
	wchar_t			*argw[64];
	int				 nargs = 0;
	SECURITY_ATTRIBUTES	sa;
	STARTUPINFOW		si;
	PROCESS_INFORMATION	pi;
	HANDLE				stdout_rd = NULL;
	HANDLE				stdout_wr = NULL;
	HANDLE				job = NULL;
	HANDLE				reader_thread = NULL;
	pipe_reader_ctx		rctx;
	DWORD				wait_rc;
	int					rc = 0;

	memset(result, 0, sizeof(*result));
	memset(&rctx, 0, sizeof(rctx));

	if (GetModuleFileNameW(NULL, self, MAX_PATH) == 0)
	{
		result->launch_failed = 1;
		snprintf(result->reason, sizeof(result->reason),
				 "cannot locate own executable");
		return 1;
	}

	/* Command line: quoted argv[0] + quoted args. */
	argw[nargs++] = self;
	while ((args[nargs - 1] != NULL) && (nargs < 63))
	{
		argw[nargs] = utf8_to_utf16(args[nargs - 1]);
		if (argw[nargs] == NULL)
		{
			result->launch_failed = 1;
			snprintf(result->reason, sizeof(result->reason), "out of memory");
			rc = 1;
			goto cleanup_args;
		}
		nargs++;
	}

	argw[nargs] = NULL;					/* terminate the array				*/

	for (nargs = 0; argw[nargs] != NULL; nargs++)
	{
		size_t l = wcslen(argw[nargs]) + 4;		/* quotes + separating space */

		clen += l;
	}

	cmdline = (wchar_t *) malloc(sizeof(wchar_t) * (clen + 1));
	if (cmdline == NULL)
	{
		result->launch_failed = 1;
		snprintf(result->reason, sizeof(result->reason), "out of memory");
		rc = 1;
		goto cleanup_args;
	}

	cmdline[0] = '\0';
	for (nargs = 0; argw[nargs] != NULL; nargs++)
	{
		size_t used = wcslen(cmdline);

		swprintf(cmdline + used, (clen + 1) - used, L"\"%s\" ", argw[nargs]);
	}

	cmdline[clen] = L'\0';

	memset(&sa, 0, sizeof(sa));
	sa.nLength = sizeof(sa);
	sa.bInheritHandle = TRUE;

	if (!CreatePipe(&stdout_rd, &stdout_wr, &sa, 0))
	{
		result->launch_failed = 1;
		snprintf(result->reason, sizeof(result->reason), "pipe creation failed");
		rc = 1;
		goto cleanup_args;
	}

	SetHandleInformation(stdout_rd, HANDLE_FLAG_INHERIT, 0);

	memset(&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdOutput = stdout_wr;
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);	/* inherit server stderr	*/
	si.hStdInput = NULL;

	memset(&pi, 0, sizeof(pi));

	if (!CreateProcessW(NULL, cmdline, NULL, NULL, TRUE,
						CREATE_NO_WINDOW, NULL, NULL, &si, &pi))
	{
		result->launch_failed = 1;
		snprintf(result->reason, sizeof(result->reason),
				 "CreateProcess failed (%lu)", (unsigned long) GetLastError());
		rc = 1;
		goto cleanup_pipe;
	}

	/* Kill-on-close job: the worker never outlives the server. */
	job = CreateJobObjectW(NULL, NULL);
	if (job != NULL)
	{
		JOBOBJECT_EXTENDED_LIMIT_INFORMATION jl;

		memset(&jl, 0, sizeof(jl));
		jl.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
		SetInformationJobObject(job, JobObjectExtendedLimitInformation, &jl,
								sizeof(jl));
		AssignProcessToJobObject(job, pi.hProcess);
	}

	/* The write end must be closed in the parent before reading EOF. */
	CloseHandle(stdout_wr);
	stdout_wr = NULL;

	rctx.pipe = stdout_rd;
	rctx.limit = max_output_bytes;
	rctx.cap = 65536;
	if (rctx.cap > max_output_bytes + 1)
		rctx.cap = max_output_bytes + 1;
	rctx.buf = (char *) malloc(rctx.cap);
	if (rctx.buf == NULL)
	{
		TerminateProcess(pi.hProcess, (UINT) -1);
		WaitForSingleObject(pi.hProcess, 5000);
		result->launch_failed = 1;
		snprintf(result->reason, sizeof(result->reason), "out of memory");
		rc = 1;
		goto cleanup_process;
	}

	reader_thread = (HANDLE) _beginthreadex(NULL, 0, pipe_reader, &rctx, 0, NULL);
	if (reader_thread == NULL)
	{
		TerminateProcess(pi.hProcess, (UINT) -1);
		WaitForSingleObject(pi.hProcess, 5000);
		result->launch_failed = 1;
		snprintf(result->reason, sizeof(result->reason), "reader thread failed");
		rc = 1;
		goto cleanup_process;
	}

	wait_rc = WaitForSingleObject(pi.hProcess, timeout_ms);

	if (wait_rc == WAIT_TIMEOUT)
	{
		result->timed_out = 1;
		TerminateProcess(pi.hProcess, (UINT) -1);
		WaitForSingleObject(pi.hProcess, 5000);
		snprintf(result->reason, sizeof(result->reason),
				 "worker timed out after %u ms", timeout_ms);
	}
	else
	{
		DWORD exit_code = 0;

		GetExitCodeProcess(pi.hProcess, &exit_code);
		result->exit_code = (int) exit_code;
		snprintf(result->reason, sizeof(result->reason), "exit code %lu",
				 (unsigned long) exit_code);
	}

	/* Drain: the reader stops at EOF, which happens when the process and
	 * all inheriting handles are gone. */
	WaitForSingleObject(reader_thread, 10000);

	result->stdout_len = rctx.len;
	result->truncated = rctx.truncated;

	if (rctx.oom)
	{
		free(rctx.buf);
		result->stdout_text = NULL;
		result->launch_failed = 1;
		snprintf(result->reason, sizeof(result->reason), "out of memory reading output");
		rc = 1;
		goto cleanup_process;
	}

	if ((rctx.len == 0) && (rctx.buf != NULL))
	{
		/* Keep the empty string (valid JSON-missing marker for callers). */
	}

	result->stdout_text = rctx.buf;
	rctx.buf = NULL;					/* ownership transferred			*/

cleanup_process:
	if (reader_thread != NULL)
		CloseHandle(reader_thread);
	if (rctx.buf != NULL)
		free(rctx.buf);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	if (job != NULL)
		CloseHandle(job);
	if (stdout_wr != NULL)
		CloseHandle(stdout_wr);
cleanup_pipe:
	if (stdout_rd != NULL)
		CloseHandle(stdout_rd);
cleanup_args:
	{
		int k;

		for (k = 1; k < nargs; k++)
			free(argw[k]);
	}
	free(cmdline);

	return rc;
}

void sg_worker_result_free(sg_worker_result *result)
{
	if (result != NULL)
	{
		free(result->stdout_text);
		result->stdout_text = NULL;
		result->stdout_len = 0;
	}
}

#else /* !_WIN32 */

/* POSIX implementation is a documented follow-up; the MCP server on
 * non-Windows platforms reports tool errors instead of spawning. */
int sg_worker_run(const char *const *args,
				  unsigned timeout_ms,
				  size_t max_output_bytes,
				  sg_worker_result *result)
{
	(void) args;
	(void) timeout_ms;
	(void) max_output_bytes;

	memset(result, 0, sizeof(*result));
	result->launch_failed = 1;
	snprintf(result->reason, sizeof(result->reason),
			 "worker subprocesses are not implemented on this platform yet");
	return 1;
}

void sg_worker_result_free(sg_worker_result *result)
{
	(void) result;
}

#endif
