/*
 * WorkerAltStackTest.cpp - engine workers must have an alternate signal stack
 *                          before their own work can overflow
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of Zene Studio (an LMMS-derived product).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

// Acceptance proofs for Q050-224.1, stage 1: the crash reporter installs its
// handlers with SA_ONSTACK, but sigaltstack() is a PER-THREAD attribute, so a
// worker that never registers cannot run the handler on an exhausted stack.
//
// This binary proves the LV2 worker end to end, because its work() callback is
// the one in-scope site a test can inject into:
//
//   1. a live Lv2Worker's first work() call already sees an enabled alternate
//      stack on ITS thread (registration happens before any work runs), and
//      the callback itself still runs on the ordinary stack;
//   2. a forked child makes that same work() callback overflow the worker's
//      ordinary stack: the child still dies by SIGSEGV, a report is written,
//      it stays under kMaxReportBytes, and its thread= is the worker's tid,
//      not pid= - so the report really came from the non-main thread.
//
// The other three in-scope launch sites (RubberBandStretch's recycler,
// StemJobManager's inference loop, WasmWorkerPool's lanes) cannot have a
// callback injected into them without shipping test hooks in the product, so
// their coverage proof is the source gate
// tests/worker-altstack-registration-check.py, which reads all four launch
// lambdas and has two negative probes. That is stated in docs/050-features/
// Q050-224.1.md rather than dressed up as a runtime test.
//
// Under ASan this is skipped: ASan intercepts stack overflow itself, and the
// project's ASan recipe sets use_sigaltstack=0 (Q050-157). On Windows the
// reporter is the documented no-op.

#include <QtTest>

#include "lmmsconfig.h"

#include "CrashReporter.h"

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#ifndef Q_OS_WIN
#include <csignal>
#include <cstdint>
#include <cstring>
#include <signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#endif

#ifdef LMMS_HAVE_LV2
#include "LmmsSemaphore.h"
#include "Lv2Worker.h"
#endif

// GCC/Clang AddressSanitizer only (MSVC's /fsanitize=address has no LeakSanitizer).
#if !defined(_MSC_VER) && defined(__SANITIZE_ADDRESS__)
#define WORKER_ALTSTACK_TEST_ASAN 1
#elif !defined(_MSC_VER) && defined(__has_feature)
#if __has_feature(address_sanitizer)
#define WORKER_ALTSTACK_TEST_ASAN 1
#endif
#endif

namespace
{

using namespace lmms::crashreporter;

#ifndef Q_OS_WIN

// ---------------------------------------------------------------------------
// The report helpers, the same shape CrashReporterTest uses.
// ---------------------------------------------------------------------------

QByteArray slurp(const QString& path)
{
	QFile f(path);
	if (!f.open(QIODevice::ReadOnly)) { return QByteArray(); }
	return f.readAll();
}

QString reportPathIn(const QTemporaryDir& dir)
{
	return dir.filePath(QStringLiteral("%1/%2")
		.arg(QString::fromUtf8(kReportDirName), QString::fromUtf8(kReportFileName)));
}

//! A decimal field from the report body (`\nkey=value`), or 0 when absent.
unsigned long long reportField(const QByteArray& report, const char* key)
{
	const QByteArray prefix = QByteArray("\n") + key + '=';
	const int at = report.indexOf(prefix);
	if (at < 0) { return 0; }
	const int valueAt = at + prefix.size();
	int end = report.indexOf('\n', valueAt);
	if (end < 0) { end = report.size(); }
	return report.mid(valueAt, end - valueAt).toULongLong();
}

//! Wait for a child, bounded; a timeout is a HANG, which the reporter must
//! never cause, and every caller asserts on it.
bool waitBounded(pid_t pid, int timeoutMs, int& status)
{
	for (int waited = 0; waited < timeoutMs; waited += 20)
	{
		const pid_t r = ::waitpid(pid, &status, WNOHANG);
		if (r == pid) { return true; }
		if (r < 0) { return false; }
		::usleep(20000);
	}
	return false;
}

#endif // !Q_OS_WIN

#ifdef LMMS_HAVE_LV2

// ---------------------------------------------------------------------------
// A fake LV2 worker interface: no plug-in, no module, just the one callback
// whose thread the claim is about.
// ---------------------------------------------------------------------------

struct AltStackProbe
{
	std::atomic<bool> ran{false};
	bool queried = false;
	bool enabled = false;
	unsigned long long stackBase = 0;
	unsigned long long stackSize = 0;
	bool callbackOnAltStack = false;
};

struct FakeInstance
{
	AltStackProbe* probe = nullptr;
	bool overflow = false;
};

//! Consumes the stack without bound. The volatile frame and the use of the
//! recursive result keep the compiler from turning this into a loop or a tail
//! call, so the guard page is reached for real.
#if defined(__GNUC__)
__attribute__((noinline))
#endif
int overflowNow(unsigned depth) noexcept
{
	volatile char frame[4096];
	frame[0] = static_cast<char>(depth);
	if (depth > (1u << 20)) { return 0; }
	return overflowNow(depth + 1) + frame[0];
}

LV2_Worker_Status fakeWork(LV2_Handle handle,
	LV2_Worker_Respond_Function /*respond*/,
	LV2_Worker_Respond_Handle /*respondHandle*/,
	uint32_t /*size*/,
	const void* /*data*/) noexcept
{
	auto* self = static_cast<FakeInstance*>(handle);
	if (self->overflow)
	{
		overflowNow(0);
		return LV2_WORKER_SUCCESS; // unreachable: the guard page faults first
	}

	// Query THIS thread's alternate stack from inside the first work() call:
	// if registration ran after workerFunc() started, this would be disabled.
	stack_t stack;
	std::memset(&stack, 0, sizeof(stack));
	const bool queried = ::sigaltstack(nullptr, &stack) == 0;
	AltStackProbe* probe = self->probe;
	probe->queried = queried;
	probe->enabled = queried && (stack.ss_flags & SS_DISABLE) == 0;
	probe->stackBase = reinterpret_cast<unsigned long long>(stack.ss_sp);
	probe->stackSize = static_cast<unsigned long long>(stack.ss_size);
	char here;
	const unsigned long long address = reinterpret_cast<unsigned long long>(&here);
	probe->callbackOnAltStack = probe->enabled
		&& address >= probe->stackBase
		&& address < probe->stackBase + probe->stackSize;
	probe->ran.store(true, std::memory_order_release);
	return LV2_WORKER_SUCCESS;
}

const LV2_Worker_Interface kFakeInterface = { &fakeWork, nullptr, nullptr };

#endif // LMMS_HAVE_LV2

} // namespace

class WorkerAltStackTest : public QObject
{
	Q_OBJECT

private slots:
	//! Registration happens at thread startup, before the first work() call.
	void lv2_worker_has_alternate_stack_before_first_work();

	//! The second acceptance criterion: a non-main-thread overflow produces a
	//! bounded report and the child still terminates by the signal.
	void forked_worker_overflow_reports_and_dies_by_signal();
};

void WorkerAltStackTest::lv2_worker_has_alternate_stack_before_first_work()
{
#ifdef LMMS_HAVE_LV2
	AltStackProbe probe;
	FakeInstance instance;
	instance.probe = &probe;

	const char request[4] = {'w', 'o', 'r', 'k'};
	lmms::Semaphore workLock(1);
	{
		lmms::Lv2Worker worker(&workLock, true);
		worker.setHandle(&instance);
		worker.setInterface(&kFakeInterface);
		// A non-empty request: workerFunc() consumes sizeof(size) before it
		// has a body, and skips when only the size itself is queued.
		QCOMPARE(worker.scheduleWork(sizeof(request), request), LV2_WORKER_SUCCESS);

		QTRY_VERIFY_WITH_TIMEOUT(probe.ran.load(std::memory_order_acquire), 10000);
		QVERIFY2(probe.queried,
			"the work() callback could not read its thread's sigaltstack()");
		QVERIFY2(probe.enabled,
			"the Lv2Worker thread reached its first work() call with NO alternate "
			"signal stack: a worker stack overflow there could not be reported");
		QVERIFY2(probe.stackSize > 0, "an enabled alternate stack must have a size");
		QVERIFY2(!probe.callbackOnAltStack,
			"work() itself must run on the ordinary stack; the alternate stack is "
			"for the handler (a callback running on it would prove the wrong thing)");
	}
#else
	QSKIP("built without LV2: there is no Lv2Worker thread to exercise");
#endif
}

void WorkerAltStackTest::forked_worker_overflow_reports_and_dies_by_signal()
{
#if !defined(LMMS_HAVE_LV2)
	QSKIP("built without LV2: there is no Lv2Worker thread to overflow");
#elif defined(Q_OS_WIN)
	QSKIP("the crash reporter is a documented no-op on Windows");
#elif defined(WORKER_ALTSTACK_TEST_ASAN)
	QSKIP("AddressSanitizer intercepts a stack overflow itself and the ASan recipe "
		"sets use_sigaltstack=0 (Q050-157)");
#else
	QTemporaryDir dir;
	QVERIFY2(dir.isValid(), "could not create a temporary working directory");
	const std::string workDir = dir.path().toStdString();
	QVERIFY(lmms::crashreporter::install(workDir));

	::fflush(nullptr);
	const pid_t pid = ::fork();
	QVERIFY2(pid >= 0, "fork failed");

	if (pid == 0)
	{
		// CHILD: a live Lv2Worker whose one callback overflows on ITS thread.
		// Everything the child needs is created here, after the fork, so no
		// thread or lock state is inherited.
		lmms::crashreporter::install(workDir);
		AltStackProbe probe;
		FakeInstance instance;
		instance.probe = &probe;
		instance.overflow = true;

		const char request[4] = {'w', 'o', 'r', 'k'};
		lmms::Semaphore workLock(1);
		lmms::Lv2Worker worker(&workLock, true);
		worker.setHandle(&instance);
		worker.setInterface(&kFakeInterface);
		if (worker.scheduleWork(sizeof(request), request) != LV2_WORKER_SUCCESS) { ::_exit(75); }

		// The worker thread faults and kills the process; this thread only
		// keeps the child alive until it does.
		for (;;) { ::pause(); }
	}

	int status = 0;
	QVERIFY2(waitBounded(pid, 20000, status),
		"the child hung: a crash must not deadlock the process");
	QVERIFY2(WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV,
		"the child must still die by SIGSEGV, as it would without the reporter");

	const QString reportPath = reportPathIn(dir);
	const QByteArray report = slurp(reportPath);
	QVERIFY2(!report.isEmpty(),
		"no report was written for an overflow on the Lv2Worker thread: the "
		"handler had no alternate stack to run on");
	QVERIFY(report.startsWith("Zene Studio crash report v1\n"));
	QVERIFY2(report.contains("signal=SIGSEGV(11)\n"),
		"the report must name the signal that killed the child");

	const unsigned long long threadId = reportField(report, "thread");
	const unsigned long long processId = reportField(report, "pid");
	QVERIFY2(threadId != 0, "the report carries no thread id");
	QVERIFY2(processId != 0, "the report carries no process id");
	QVERIFY2(threadId != processId,
		"the report names the process id as its thread: the fault was reported "
		"from the main thread, not from the worker that overflowed");
	QVERIFY2(QFileInfo(reportPath).size() <= static_cast<qint64>(kMaxReportBytes),
		"the report exceeded kMaxReportBytes");
#endif
}

QTEST_GUILESS_MAIN(WorkerAltStackTest)

#include "WorkerAltStackTest.moc"
