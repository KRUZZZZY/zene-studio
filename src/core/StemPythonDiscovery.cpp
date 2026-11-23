/*
 * StemPythonDiscovery.cpp - Python interpreter discovery for the stem backend
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
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
 */

#include "StemSeparation/ExternalProcessStemSeparator.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <system_error>
#include <thread>
#include <vector>

#include <QProcess>
#include <QStringList>

namespace lmms
{

namespace
{

//! How long one interpreter may take to start before it counts as missing.
constexpr int PythonProbeStartMs = 5000;
//! How long a killed probe may take to be reaped.
constexpr int PythonProbeKillGraceMs = 2000;

/*! Upper bound for ONE interpreter probe, and therefore for a whole discovery:
 *  every candidate is probed concurrently and gets this full deadline (there is
 *  no shared budget to starve a later candidate). 5 s is several times the
 *  ~0.45 s a cold reference CLI needs to start, import and reach inference, and
 *  covers a cold `import onnxruntime` that takes a couple of seconds. */
constexpr int PythonDiscoveryBudgetMs = 5000;

//! A failed discovery is not repeated sooner than this (the service caches it).
constexpr qint64 PythonDiscoveryRetryDelayMs = 10000;

/*! The budget for this discovery run. LMMS_STEM_PYTHON_PROBE_MS is a test hook
 *  (like LMMS_STEM_CHUNK_DELAY_MS) that can only make discovery stricter: a
 *  value outside [0, PythonDiscoveryBudgetMs] keeps the default. Zero is the
 *  boundary the regression test uses to observe "no process is started"
 *  without waiting. */
int pythonDiscoveryBudgetMs()
{
	bool ok = false;
	const int requested = qEnvironmentVariableIntValue("LMMS_STEM_PYTHON_PROBE_MS", &ok);
	if (!ok || requested < 0 || requested > PythonDiscoveryBudgetMs)
	{
		return PythonDiscoveryBudgetMs;
	}
	return requested;
}

//! True when @a candidate starts and imports onnxruntime within @a timeoutMs.
//! On timeout the child is killed, so no probe outlives its deadline.
bool probePython(const QString& candidate, int timeoutMs)
{
	if (timeoutMs <= 0)
	{
		return false;
	}
	const auto started = std::chrono::steady_clock::now();
	const auto remainingMs = [&started, timeoutMs]()
	{
		const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started).count();
		return timeoutMs - static_cast<int>(elapsed);
	};

	QProcess probe;
	probe.start(candidate, { QStringLiteral("-c"), QStringLiteral("import onnxruntime") });
	if (!probe.waitForStarted(std::min(timeoutMs, PythonProbeStartMs)))
	{
		return false;
	}
	const int finishMs = remainingMs();
	if (finishMs <= 0 || !probe.waitForFinished(finishMs))
	{
		probe.kill();
		probe.waitForFinished(PythonProbeKillGraceMs);
		return false;
	}
	return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
}

#ifdef LMMS_TESTING
//! Read concurrently by the probe threads; only replaced while none is running
//! (setDiscoveryHooksForTest waits for that).
ExternalProcessStemSeparator::DiscoveryHooks& discoveryHooks()
{
	static ExternalProcessStemSeparator::DiscoveryHooks hooks;
	return hooks;
}
#endif

//! The monotonic clock the failure-retry delay reads (milliseconds since an
//! arbitrary epoch). A test can replace it through the DiscoveryHooks seam.
qint64 discoveryNowMs()
{
#ifdef LMMS_TESTING
	if (discoveryHooks().nowMs)
	{
		return discoveryHooks().nowMs();
	}
#endif
	return std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count();
}

//! True when @a candidate imports onnxruntime within @a timeoutMs; the test seam
//! replaces the child process, not the decision. Runs on a probe thread.
bool probeInterpreter(const QString& candidate, int timeoutMs)
{
#ifdef LMMS_TESTING
	if (discoveryHooks().probe)
	{
		return discoveryHooks().probe(candidate, timeoutMs);
	}
#endif
	return probePython(candidate, timeoutMs);
}

void setError(QString* error, const QString& text)
{
	if (error != nullptr)
	{
		*error = text;
	}
}

QStringList discoveryCandidates()
{
	QStringList candidates;
	const auto env = qEnvironmentVariable("LMMS_STEM_PYTHON");
	if (!env.isEmpty())
	{
		candidates << env;
	}
	candidates << QStringLiteral("/usr/bin/python3")
		<< QStringLiteral("python3")
		<< QStringLiteral("/usr/local/bin/python3");
	return candidates;
}

//! Starts one probe on its own thread. If no thread can be created the probe
//! runs on the first get() instead, so discovery still completes.
std::future<bool> startProbe(const QString& candidate, int budgetMs)
{
	try
	{
		return std::async(std::launch::async, probeInterpreter, candidate, budgetMs);
	}
	catch (const std::system_error&)
	{
		return std::async(std::launch::deferred, probeInterpreter, candidate, budgetMs);
	}
}

/*! One discovery run: every candidate is probed concurrently with the FULL
 *  budget, then the futures are read in priority order, so the winner is the
 *  highest-priority candidate that works and is only chosen once every
 *  higher-priority one has finished. A slow failing candidate therefore cannot
 *  starve a working fallback. Every probe is bounded by the budget (the child
 *  is killed at its deadline), so the run ends within it. Blocking: never call
 *  this from the GUI thread. */
QString runParallelDiscovery(QString* error)
{
	const QStringList candidates = discoveryCandidates();
	const int budgetMs = pythonDiscoveryBudgetMs();

	std::vector<std::future<bool>> probes;
	probes.reserve(static_cast<std::size_t>(candidates.size()));
	for (const auto& candidate : candidates)
	{
		probes.push_back(startProbe(candidate, budgetMs));
	}

	QString winner;
	for (qsizetype index = 0; index < candidates.size(); ++index)
	{
		const bool worked = probes[static_cast<std::size_t>(index)].get();
		if (worked && winner.isEmpty())
		{
			winner = candidates.at(index);
		}
	}
	if (winner.isEmpty())
	{
		setError(error, QStringLiteral("No Python interpreter with the 'onnxruntime' "
			"module found (tried: %1). Install onnxruntime or set LMMS_STEM_PYTHON.")
			.arg(candidates.join(QStringLiteral(", "))));
	}
	return winner;
}

enum class Phase { Idle, Pending, Found, Failed };

//! The per-process discovery cache. Heap-allocated and never destroyed so a
//! detached discovery thread never touches a destroyed object at exit.
struct Service
{
	std::mutex mutex;
	std::condition_variable changed;
	Phase phase = Phase::Idle;
	QString python;
	QString error;
	QString key; //!< the LMMS_STEM_PYTHON value the cached result belongs to
	qint64 failedAtMs = 0;
	bool running = false; //!< a run for the CURRENT generation is in flight
	quint64 generation = 0;
	int activeThreads = 0; //!< all runs in flight, stale generations included
};

Service& service()
{
	static auto* instance = new Service;
	return *instance;
}

//! Forgets the cached result when the configuration it belongs to changed. A
//! run still in flight for the old key becomes stale: its result is dropped.
void forgetIfKeyChanged(Service& s, const QString& key)
{
	if (key == s.key)
	{
		return;
	}
	s.key = key;
	s.phase = Phase::Idle;
	s.python.clear();
	s.error.clear();
	s.running = false;
	++s.generation;
}

bool shouldStartRun(const Service& s)
{
	if (s.running || s.phase == Phase::Found)
	{
		return false;
	}
	if (s.phase == Phase::Failed && discoveryNowMs() - s.failedAtMs < PythonDiscoveryRetryDelayMs)
	{
		return false;
	}
	return true;
}

void storeResult(Service& s, const QString& python, const QString& error)
{
	s.python = python;
	s.error = error;
	s.phase = python.isEmpty() ? Phase::Failed : Phase::Found;
	if (python.isEmpty())
	{
		s.failedAtMs = discoveryNowMs();
	}
}

void runDiscoveryThread(Service& s, quint64 generation)
{
	QString error;
	const QString python = runParallelDiscovery(&error);
	{
		std::lock_guard lock(s.mutex);
		if (generation == s.generation)
		{
			storeResult(s, python, error);
			s.running = false;
		}
		--s.activeThreads;
	}
	s.changed.notify_all();
}

//! Caller holds the lock.
void startRun(Service& s)
{
	s.phase = Phase::Pending;
	s.running = true;
	++s.activeThreads;
	const quint64 generation = s.generation;
	try
	{
		std::thread([&s, generation]() { runDiscoveryThread(s, generation); }).detach();
	}
	catch (const std::system_error&)
	{
		--s.activeThreads;
		s.running = false;
		s.phase = Phase::Failed;
		s.error = QStringLiteral("Cannot start the Python discovery thread");
		s.failedAtMs = discoveryNowMs();
	}
}

} // namespace

QString ExternalProcessStemSeparator::locatePython(QString* error)
{
	return runParallelDiscovery(error);
}

void ExternalProcessStemSeparator::requestPythonDiscovery()
{
	auto& s = service();
	const QString key = qEnvironmentVariable("LMMS_STEM_PYTHON");
	std::lock_guard lock(s.mutex);
	forgetIfKeyChanged(s, key);
	if (shouldStartRun(s))
	{
		startRun(s);
	}
}

int ExternalProcessStemSeparator::pythonDiscoveryWaitBoundMs()
{
	return PythonDiscoveryBudgetMs + PythonProbeKillGraceMs;
}




ExternalProcessStemSeparator::PythonDiscoveryState ExternalProcessStemSeparator::pythonDiscoveryState(int waitMs)
{
	requestPythonDiscovery();
	auto& s = service();
	std::unique_lock lock(s.mutex);
	if (waitMs > 0)
	{
		s.changed.wait_for(lock, std::chrono::milliseconds(waitMs), [&s]() { return !s.running; });
	}
	PythonDiscoveryState state;
	switch (s.phase)
	{
	case Phase::Found:
		state.phase = PythonDiscoveryPhase::Found;
		state.python = s.python;
		break;
	case Phase::Failed:
		state.phase = PythonDiscoveryPhase::Failed;
		state.error = s.error;
		break;
	default:
		state.error = QStringLiteral("discovering a Python interpreter with onnxruntime; "
			"ask again in a moment");
		break;
	}
	return state;
}

QString ExternalProcessStemSeparator::discoveredPython(QString* error)
{
	const auto state = pythonDiscoveryState(pythonDiscoveryWaitBoundMs());
	if (state.phase != PythonDiscoveryPhase::Found)
	{
		setError(error, state.error);
	}
	return state.python;
}




QString ExternalProcessStemSeparator::waitForPythonDiscovery(QString* error)
{
	requestPythonDiscovery();
	auto& s = service();
	std::unique_lock lock(s.mutex);
	s.changed.wait(lock, [&s]() { return !s.running; });
	if (s.phase == Phase::Found)
	{
		return s.python;
	}
	setError(error, s.phase == Phase::Failed ? s.error
		: QStringLiteral("Python discovery was reset before it finished; retry"));
	return QString();
}

#ifdef LMMS_TESTING
void ExternalProcessStemSeparator::resetPythonDiscoveryForTest()
{
	auto& s = service();
	std::unique_lock lock(s.mutex);
	s.changed.wait(lock, [&s]() { return s.activeThreads == 0; });
	s.phase = Phase::Idle;
	s.python.clear();
	s.error.clear();
	s.key.clear();
	s.failedAtMs = 0;
	s.running = false;
	++s.generation;
}

void ExternalProcessStemSeparator::setDiscoveryHooksForTest(DiscoveryHooks hooks)
{
	// No probe thread may be reading the hooks while they are swapped.
	resetPythonDiscoveryForTest();
	discoveryHooks() = std::move(hooks);
}
#endif

} // namespace lmms
