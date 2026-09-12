/*
 * abort-trace.so - a diagnostic-only LD_PRELOAD shim (NOT part of the tree's
 * build). It interposes abort() so an aborting process prints the stack that
 * reached qFatal/abort before it dies - the same information gdb would give,
 * without ptrace and without a core pattern, so it can run inside a test harness
 * that captures (and truncates) the child's stderr.
 *
 * The trace goes to stderr AND, when ABORT_TRACE_FILE is set, to that file
 * (a harness that dumps at most N bytes of stderr would truncate the symbols).
 *
 * Build:  gcc -shared -fPIC -O1 -g -o abort-trace.so abort-trace.c -ldl
 * Use:    ABORT_TRACE_FILE=/tmp/trace.txt LD_PRELOAD=$PWD/abort-trace.so <command>
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/syscall.h>
#include <unistd.h>

static void emit(int fd)
{
	void* frames[64];
	const int n = backtrace(frames, 64);
	char buf[256];
	const int len = snprintf(buf, sizeof buf,
		"\n=== ABORT TRACE (tid %ld, %d frames) ===\n",
		(long)syscall(SYS_gettid), n);
	if (write(fd, buf, (size_t)len) < 0) { return; }
	backtrace_symbols_fd(frames, n, fd);
	for (int i = 0; i < n; ++i)
	{
		const int l = snprintf(buf, sizeof buf, "  [%02d] %p\n", i, frames[i]);
		if (write(fd, buf, (size_t)l) < 0) { return; }
	}
	if (write(fd, "=== END ABORT TRACE ===\n", 24) < 0) { return; }
}

void abort(void)
{
	emit(2);

	const char* path = getenv("ABORT_TRACE_FILE");
	if (path != 0 && *path != '\0')
	{
		const int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
		if (fd >= 0)
		{
			emit(fd);
			close(fd);
		}
	}

	static void (*real_abort)(void) = 0;
	if (!real_abort) { real_abort = dlsym(RTLD_NEXT, "abort"); }
	if (real_abort) { real_abort(); }
	_exit(134);
}
