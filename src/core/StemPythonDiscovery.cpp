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
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <optional>
#include <system_error>
#include <thread>
#include <vector>

#include <QProcess>
#include <QStringList>

namespace lmms
{

namespace
{

//! Time a probe may take to start before it counts as missing.
constexpr int PythonProbeStartMs = 5000;
//! How long a killed probe may take to be reaped.
constexpr int PythonProbeKillGraceMs = 2000;
//! Cancellation poll interval of a running probe.
constexpr int PythonProbeCancelPollMs = 50;

//! Upper bound for ONE probe (and a whole discovery): every candidate gets this
//! full deadline, so none starves another. Covers a cold `import onnxruntime`.
constexpr int PythonDiscoveryBudgetMs = 5000;

//! A failed discovery is not repeated sooner than this.
constexpr qint64 PythonDiscoveryRetryDelayMs = 10000;

//! The budget for this run. LMMS_STEM_PYTHON_PROBE_MS is a test hook that can
//! only make discovery stricter: a value outside [0, PythonDiscoveryBudgetMs]
//! keeps the default (zero starts no process).
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

//! Waits for the started @a probe until @a deadline; true when it exited with
//! status 0. On timeout or cancellation the child is killed and reaped.
bool awaitProbe(QProcess& probe, std::chrono::steady_clock::time_point deadline,
	const std::atomic<bool>& cancelled)
{
	while (!cancelled.load())
	{
		const auto remainingMs = std::chrono::duration_cast<std::chrono::milliseconds>(
			deadline - std::chrono::steady_clock::now()).count();
		if (remainingMs <= 0)
		{
			break;
		}
		const int slice = static_cast<int>(std::min<long long>(remainingMs, PythonProbeCancelPollMs));
		if (probe.waitForFinished(slice))
		{
			return probe.exitStatus() == QProcess::NormalExit && probe.exitCode() == 0;
		}
		if (probe.state() == QProcess::NotRunning)
		{
			return false;
		}
	}
	probe.kill();
	probe.waitForFinished(PythonProbeKillGraceMs);
	return false;
}

//! True when @a candidate starts and imports onnxruntime within @a timeoutMs
//! and the run is not cancelled meanwhile (the child is then killed).
bool probePython(const QString& candidate, int timeoutMs, const std::atomic<bool>& cancelled)
{
	if (timeoutMs <= 0 || cancelled.load())
	{
		return false;
	}
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
	QProcess probe;
	probe.start(candidate, { QStringLiteral("-c"), QStringLiteral("import onnxruntime") });
	if (!probe.waitForStarted(std::min(timeoutMs, PythonProbeStartMs)))
	{
		return false;
	}
	return awaitProbe(probe, deadline, cancelled);
}

#ifdef LMMS_TESTING
//! Read by the probe threads; replaced only while none runs.
ExternalProcessStemSeparator::DiscoveryHooks& discoveryHooks()
{
	static ExternalProcessStemSeparator::DiscoveryHooks hooks;
	return hooks;
}
#endif

//! Monotonic ms clock for the failure-retry delay (replaceable by a test hook).
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
//! replaces the child process, not the decision (and ignores @a cancelled).
//! Runs on a probe thread.
bool probeInterpreter(const QString& candidate, int timeoutMs, const std::atomic<bool>& cancelled)
{
#ifdef LMMS_TESTING
	if (discoveryHooks().probe)
	{
		return discoveryHooks().probe(candidate, timeoutMs);
	}
#endif
	return probePython(candidate, timeoutMs, cancelled);
}

void setError(QString* error, const QString& text)
{
	if (error != nullptr) { *error = text; }
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

enum class ProbeResult { Running, Works, Fails };

//! Shared by the decider and its probe threads (shared_ptr: outlives the decider).
struct DiscoveryRun
{
	std::mutex mutex;
	std::condition_variable settled;
	std::vector<ProbeResult> results; //!< per candidate, in priority order
	std::atomic<bool> cancelled{false};
};

//! Probe threads still running (tells a test/reset that none reads the hooks).
struct LiveProbes
{
	std::mutex mutex;
	std::condition_variable idle;
	int live = 0;
};

LiveProbes& liveProbes()
{
	static auto* instance = new LiveProbes;
	return *instance;
}

void addLiveProbes(int delta)
{
	{
		std::lock_guard lock(liveProbes().mutex);
		liveProbes().live += delta;
	}
	liveProbes().idle.notify_all();
}

#ifdef LMMS_TESTING
void waitForIdleProbes()
{
	std::unique_lock lock(liveProbes().mutex);
	liveProbes().idle.wait(lock, []() { return liveProbes().live == 0; });
}
#endif

//! The winner index once decided (-1: all failed); nullopt while a Running entry
//! precedes the first Works (that probe may still win).
std::optional<int> settledWinner(const std::vector<ProbeResult>& results)
{
	for (std::size_t index = 0; index < results.size(); ++index)
	{
		if (results[index] == ProbeResult::Running)
		{
			return std::nullopt;
		}
		if (results[index] == ProbeResult::Works)
		{
			return static_cast<int>(index);
		}
	}
	return -1;
}

void runProbe(const std::shared_ptr<DiscoveryRun>& run, std::size_t index, const QString& candidate, int budgetMs)
{
	bool worked = false;
	try { worked = probeInterpreter(candidate, budgetMs, run->cancelled); }
	catch (...) { worked = false; }
	{
		std::lock_guard lock(run->mutex);
		run->results[index] = worked ? ProbeResult::Works : ProbeResult::Fails;
	}
	run->settled.notify_all();
	addLiveProbes(-1);
}

//! Starts one probe on a detached thread (on the caller's if none can be made).
void startProbe(const std::shared_ptr<DiscoveryRun>& run, std::size_t index, const QString& candidate, int budgetMs)
{
	addLiveProbes(1);
	try { std::thread([run, index, candidate, budgetMs]() { runProbe(run, index, candidate, budgetMs); }).detach(); }
	catch (const std::system_error&) { runProbe(run, index, candidate, budgetMs); }
}

/*! One discovery run: every candidate is probed concurrently with the FULL
 *  budget. The highest-priority working candidate is returned as soon as every
 *  higher-priority one has failed; lower-priority probes are cancelled (child
 *  killed) and reaped by their own threads in the background. Ends within the
 *  budget. Blocking: never call this from the GUI thread. */
QString runParallelDiscovery(QString* error)
{
	const QStringList candidates = discoveryCandidates();
	const int budgetMs = pythonDiscoveryBudgetMs();

	auto run = std::make_shared<DiscoveryRun>();
	run->results.assign(static_cast<std::size_t>(candidates.size()), ProbeResult::Running);
	for (qsizetype index = 0; index < candidates.size(); ++index)
	{
		startProbe(run, static_cast<std::size_t>(index), candidates.at(index), budgetMs);
	}

	std::optional<int> decision;
	{
		std::unique_lock lock(run->mutex);
		run->settled.wait(lock, [&run, &decision]()
		{
			decision = settledWinner(run->results);
			return decision.has_value();
		});
	}
	run->cancelled.store(true);

	if (*decision >= 0)
	{
		return candidates.at(*decision);
	}
	setError(error, QStringLiteral("No Python interpreter with the 'onnxruntime' "
		"module found (tried: %1). Install onnxruntime or set LMMS_STEM_PYTHON.")
		.arg(candidates.join(QStringLiteral(", "))));
	return QString();
}

enum class Phase { Idle, Pending, Found, Failed };

//! The per-process discovery cache; leaked so detached threads never outlive it.
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

//! Forgets the cached result when its LMMS_STEM_PYTHON key changed (a run in
//! flight for the old key becomes stale: its result is dropped).
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
	try { std::thread([&s, generation]() { runDiscoveryThread(s, generation); }).detach(); }
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
	{
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
	// Losing probes are reaped after publication: wait for them (unlocked).
	waitForIdleProbes();
}

void ExternalProcessStemSeparator::waitForDiscoveryProbesForTest()
{
	waitForIdleProbes();
}

void ExternalProcessStemSeparator::setDiscoveryHooksForTest(DiscoveryHooks hooks)
{
	// No probe thread may be reading the hooks while they are swapped.
	resetPythonDiscoveryForTest();
	discoveryHooks() = std::move(hooks);
}
#endif

} // namespace lmms
