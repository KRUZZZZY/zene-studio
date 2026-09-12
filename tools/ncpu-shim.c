/*
 * ncpu-shim.c - measurement instrument for docs/RENDER-DETERMINISM.md, NOT product code.
 *
 * AudioEngine's worker pool is sized by QThread::idealThreadCount() - 1
 * (src/core/AudioEngine.cpp:85), so the number of threads that process a render's
 * play-handles, audio buses and mixer channels is a property of the *host*, not of the
 * render request. To test whether that count is what makes renders vary run to run we
 * need to change it for one binary without rebuilding it:
 *
 *   gcc -shared -fPIC -O2 -o /tmp/ncpu.so tools/ncpu-shim.c -ldl
 *   ZENE_FAKE_NCPU=1 LD_PRELOAD=/tmp/ncpu.so \
 *     QT_QPA_PLATFORM=offscreen build/lmms render data/projects/shorties/DirtyLove.mmpz -o /tmp/a.wav -f wav
 *
 * It interposes sysconf(_SC_NPROCESSORS_ONLN|_SC_NPROCESSORS_CONF), which is what Qt,
 * libstdc++'s std::thread::hardware_concurrency() and nproc all consult. Verify it took
 * effect by counting /proc/<pid>/task during a render (see the report's §5 recipe).
 * Unset ZENE_FAKE_NCPU and the shim is a pass-through.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <sched.h>
#include <stdlib.h>
#include <sys/sysinfo.h>
#include <unistd.h>

static long s_forced = -1;

static long forced_value(void)
{
	if (s_forced < 0)
	{
		const char *env = getenv("ZENE_FAKE_NCPU");
		s_forced = env ? strtol(env, NULL, 10) : 0;
	}
	return s_forced;
}

long sysconf(int name)
{
	static long (*real_sysconf)(int) = NULL;
	if (real_sysconf == NULL) { real_sysconf = dlsym(RTLD_NEXT, "sysconf"); }

	if (real_sysconf == NULL) { return -1; }

	const long forced = forced_value();
	if (forced > 0 && (name == _SC_NPROCESSORS_ONLN || name == _SC_NPROCESSORS_CONF))
	{
		return forced;
	}
	return real_sysconf(name);
}

int get_nprocs(void)
{
	static int (*real_get_nprocs)(void) = NULL;
	if (real_get_nprocs == NULL) { real_get_nprocs = dlsym(RTLD_NEXT, "get_nprocs"); }

	const long forced = forced_value();
	if (forced > 0) { return (int) forced; }
	return real_get_nprocs ? real_get_nprocs() : (int) sysconf(_SC_NPROCESSORS_ONLN);
}

int get_nprocs_conf(void)
{
	static int (*real_get_nprocs_conf)(void) = NULL;
	if (real_get_nprocs_conf == NULL) { real_get_nprocs_conf = dlsym(RTLD_NEXT, "get_nprocs_conf"); }

	const long forced = forced_value();
	if (forced > 0) { return (int) forced; }
	return real_get_nprocs_conf ? real_get_nprocs_conf() : (int) sysconf(_SC_NPROCESSORS_CONF);
}

/* Qt 6's QThread::idealThreadCount() asks the kernel for the *affinity mask* rather than
 * for the online CPU count, so interposing sysconf alone changes nothing (verified: with
 * ZENE_FAKE_NCPU=3 the shim moved python's os.cpu_count() to 3 and left Qt at 20). Clear
 * the mask bits at and above the forced count instead. */
int sched_getaffinity(pid_t pid, size_t cpusetsize, cpu_set_t *mask)
{
	static int (*real_getaffinity)(pid_t, size_t, cpu_set_t *) = NULL;
	if (real_getaffinity == NULL) { real_getaffinity = dlsym(RTLD_NEXT, "sched_getaffinity"); }

	if (real_getaffinity == NULL) { return -1; }

	const int rc = real_getaffinity(pid, cpusetsize, mask);
	const long forced = forced_value();
	if (forced > 0 && rc == 0)
	{
		unsigned int cpu = 0;
		for (; cpu < (unsigned int) forced && cpu < 8 * sizeof(cpu_set_t); ++cpu) { CPU_SET(cpu, mask); }
		for (; cpu < 8 * sizeof(cpu_set_t); ++cpu) { CPU_CLR(cpu, mask); }
	}
	return rc;
}
