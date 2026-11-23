/*
 * StemPythonDiscoveryServiceTest.cpp - the non-blocking Python discovery service
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

/*
 * Cluster new-p15-64. GUI-thread callers (stem.get_state) must never wait for an
 * interpreter probe, so the external stem backend has a per-process discovery
 * service: it runs the (parallel) discovery on a background thread, reports
 * "pending" while it runs, caches a success per LMMS_STEM_PYTHON value and a
 * failure for 10 s.
 *
 * Everything here is deterministic: the probe is an injected hook (no process),
 * a gate the test opens decides when it finishes, and the retry delay is read
 * from a fake clock the test advances. Nothing sleeps and no wall clock is read.
 */

#include <QtTest>

#include <atomic>
#include <future>
#include <initializer_list>
#include <memory>
#include <mutex>
#include <vector>

#include <QByteArray>
#include <QString>

#include "StemSeparation/ExternalProcessStemSeparator.h"

using namespace lmms;

namespace
{

//! Restores the named environment variables on scope exit.
class EnvironmentGuard
{
public:
	explicit EnvironmentGuard(std::initializer_list<const char*> names)
	{
		for (const char* name : names)
		{
			m_names.emplace_back(name);
			m_values.emplace_back(qgetenv(name));
		}
	}

	~EnvironmentGuard()
	{
		for (std::size_t i = 0; i < m_names.size(); ++i)
		{
			if (m_values[i].isNull()) { qunsetenv(m_names[i].constData()); }
			else { qputenv(m_names[i].constData(), m_values[i]); }
		}
	}

	EnvironmentGuard(const EnvironmentGuard&) = delete;
	EnvironmentGuard& operator=(const EnvironmentGuard&) = delete;

private:
	std::vector<QByteArray> m_names;
	std::vector<QByteArray> m_values;
};

//! A closed gate probes block on until the test opens it. Opened on scope exit
//! too, so a failing assertion cannot leave a probe thread blocked forever.
class Gate
{
public:
	Gate() : m_open(m_promise.get_future().share()) {}
	~Gate() { open(); }

	void wait() const { m_open.wait(); }

	void open()
	{
		std::call_once(m_once, [this]() { m_promise.set_value(); });
	}

private:
	std::promise<void> m_promise;
	std::shared_future<void> m_open;
	std::once_flag m_once;
};

constexpr int CandidatesWithoutEnv = 3; // /usr/bin/python3, python3, /usr/local/bin/python3

//! Discovery publishes the winner while lower-priority probes may still run
//! (cancelled in the background). Wait for them before counting probe calls.
void settleProbes()
{
#ifdef LMMS_STEM_PYTHON_EARLY_PUBLISH
	ExternalProcessStemSeparator::waitForDiscoveryProbesForTest();
#endif
}

//! A probe held open by a closed gate; shared with the hook so a probe thread
//! that outlives a failing assertion still has valid state to touch.
struct HeldProbe
{
	Gate gate;
	std::atomic<bool> returned{ false };
};

} // namespace

class StemPythonDiscoveryServiceTest : public QObject
{
	Q_OBJECT
private slots:
	void init()
	{
#ifdef LMMS_STEM_PYTHON_DISCOVERY_SERVICE
		ExternalProcessStemSeparator::resetPythonDiscoveryForTest();
#endif
	}
	void cleanup() { ExternalProcessStemSeparator::setDiscoveryHooksForTest({}); }

	/*! While a probe is still running the state is Pending and asking for it
	 *  never waits (the gate is closed, so a blocking call would hang here). The
	 *  probe ran once per candidate for the whole run, however many instances and
	 *  state queries there were meanwhile. */
	void stateIsPendingWhileAProbeRunsAndNeverBlocks()
	{
#ifdef LMMS_STEM_PYTHON_DISCOVERY_SERVICE
		EnvironmentGuard guard({ "LMMS_STEM_PYTHON", "LMMS_STEM_PYTHON_PROBE_MS" });
		qunsetenv("LMMS_STEM_PYTHON");
		qunsetenv("LMMS_STEM_PYTHON_PROBE_MS");
		const QString python = QStringLiteral("/usr/bin/python3");

		Gate gate;
		std::atomic<int> calls{ 0 };
		ExternalProcessStemSeparator::setDiscoveryHooksForTest({
			[&](const QString& candidate, int)
			{
				++calls;
				gate.wait();
				return candidate == python;
			},
			{}
		});

		ExternalProcessStemSeparator::requestPythonDiscovery();
		auto state = ExternalProcessStemSeparator::pythonDiscoveryState();
		QVERIFY2(state.phase == ExternalProcessStemSeparator::PythonDiscoveryPhase::Pending,
			"a probe that has not finished must read as Pending");
		QVERIFY2(!state.error.isEmpty(), "a pending state explains itself");
		QVERIFY(state.python.isEmpty());

		{
			// Constructing the backend while discovery runs probes nothing.
			ExternalProcessStemSeparator separator;
			state = ExternalProcessStemSeparator::pythonDiscoveryState();
			QVERIFY(state.phase == ExternalProcessStemSeparator::PythonDiscoveryPhase::Pending);
		}

		// A bounded wait (what the control surface does) returns when its bound
		// runs out, still Pending, instead of being held by the probe. The gate is
		// closed, so the outcome does not depend on timing.
		state = ExternalProcessStemSeparator::pythonDiscoveryState(1);
		QVERIFY(state.phase == ExternalProcessStemSeparator::PythonDiscoveryPhase::Pending);

		gate.open();
		QString error;
		QCOMPARE(ExternalProcessStemSeparator::waitForPythonDiscovery(&error), python);
		QVERIFY2(error.isEmpty(), qPrintable(error));

		state = ExternalProcessStemSeparator::pythonDiscoveryState();
		QVERIFY(state.phase == ExternalProcessStemSeparator::PythonDiscoveryPhase::Found);
		QCOMPARE(state.python, python);
		settleProbes();
		QCOMPARE(calls.load(), CandidatesWithoutEnv);
#else
		QFAIL("no non-blocking discovery service");
#endif
	}

	/*! A failure is remembered (no re-probe on every poll) and retried once the
	 *  retry delay has passed on the injected clock. */
	void failureIsCachedThenRetriedAfterTheDelay()
	{
#ifdef LMMS_STEM_PYTHON_DISCOVERY_SERVICE
		EnvironmentGuard guard({ "LMMS_STEM_PYTHON", "LMMS_STEM_PYTHON_PROBE_MS" });
		qunsetenv("LMMS_STEM_PYTHON");
		qunsetenv("LMMS_STEM_PYTHON_PROBE_MS");

		std::atomic<int> calls{ 0 };
		std::atomic<qint64> clockMs{ 1000 };
		ExternalProcessStemSeparator::setDiscoveryHooksForTest({
			[&](const QString&, int) { ++calls; return false; },
			[&clockMs]() { return clockMs.load(); }
		});

		QString error;
		QVERIFY(ExternalProcessStemSeparator::waitForPythonDiscovery(&error).isEmpty());
		QVERIFY2(!error.isEmpty(), "a failed discovery says why");
		settleProbes();
		const int firstRun = calls.load();
		QCOMPARE(firstRun, CandidatesWithoutEnv);

		// Same clock: the failure is served from the cache.
		auto state = ExternalProcessStemSeparator::pythonDiscoveryState();
		QVERIFY(state.phase == ExternalProcessStemSeparator::PythonDiscoveryPhase::Failed);
		QCOMPARE(state.error, error);
		QVERIFY(ExternalProcessStemSeparator::waitForPythonDiscovery().isEmpty());
		settleProbes();
		QCOMPARE(calls.load(), firstRun);

		// Just short of the delay: still cached.
		clockMs += 9999;
		QVERIFY(ExternalProcessStemSeparator::waitForPythonDiscovery().isEmpty());
		settleProbes();
		QCOMPARE(calls.load(), firstRun);

		// Past the 10 s delay: discovered again.
		clockMs += 2;
		QVERIFY(ExternalProcessStemSeparator::waitForPythonDiscovery().isEmpty());
		settleProbes();
		QCOMPARE(calls.load(), 2 * firstRun);
#else
		QFAIL("no non-blocking discovery service");
#endif
	}

	/*! A success is remembered for the LMMS_STEM_PYTHON value it was found for;
	 *  a different value is a different configuration and is discovered anew. */
	void successIsCachedPerLmmsStemPythonValue()
	{
#ifdef LMMS_STEM_PYTHON_DISCOVERY_SERVICE
		EnvironmentGuard guard({ "LMMS_STEM_PYTHON", "LMMS_STEM_PYTHON_PROBE_MS" });
		qunsetenv("LMMS_STEM_PYTHON_PROBE_MS");
		const QString first = QStringLiteral("/custom/first");
		const QString second = QStringLiteral("/custom/second");

		std::atomic<int> calls{ 0 };
		ExternalProcessStemSeparator::setDiscoveryHooksForTest({
			[&](const QString& candidate, int)
			{
				++calls;
				return candidate.startsWith(QStringLiteral("/custom/"));
			},
			{}
		});

		qputenv("LMMS_STEM_PYTHON", first.toUtf8());
		QCOMPARE(ExternalProcessStemSeparator::waitForPythonDiscovery(), first);
		settleProbes();
		const int firstRun = calls.load();
		QCOMPARE(firstRun, CandidatesWithoutEnv + 1);

		QCOMPARE(ExternalProcessStemSeparator::waitForPythonDiscovery(), first);
		QVERIFY(ExternalProcessStemSeparator::pythonDiscoveryState().phase
			== ExternalProcessStemSeparator::PythonDiscoveryPhase::Found);
		settleProbes();
		QCOMPARE(calls.load(), firstRun); // served from the cache

		qputenv("LMMS_STEM_PYTHON", second.toUtf8());
		QCOMPARE(ExternalProcessStemSeparator::waitForPythonDiscovery(), second);
		settleProbes();
		QCOMPARE(calls.load(), 2 * firstRun); // a new value, a new discovery
#else
		QFAIL("no non-blocking discovery service");
#endif
	}

	/*! The preferred interpreter is published as soon as it is settled: with
	 *  /usr/bin/python3 working at once and the lower-priority `python3` held
	 *  open, readiness (what stem.get_state waits on) must not wait for the held
	 *  probe. Before the fix the held probe held the run for its whole deadline,
	 *  so discoveredPython() ran into its own bound and returned empty. The
	 *  closed gate (not a clock) is what keeps the loser running. */
	void readinessDoesNotWaitForALosingLowerPriorityProbe()
	{
#ifdef LMMS_STEM_PYTHON_DISCOVERY_SERVICE
		EnvironmentGuard guard({ "LMMS_STEM_PYTHON", "LMMS_STEM_PYTHON_PROBE_MS" });
		qunsetenv("LMMS_STEM_PYTHON");
		qunsetenv("LMMS_STEM_PYTHON_PROBE_MS");
		const QString preferred = QStringLiteral("/usr/bin/python3");
		const QString held = QStringLiteral("python3");

		const auto heldProbe = std::make_shared<HeldProbe>();
		ExternalProcessStemSeparator::setDiscoveryHooksForTest({
			[heldProbe, preferred, held](const QString& candidate, int)
			{
				if (candidate == preferred) { return true; }
				if (candidate == held)
				{
					heldProbe->gate.wait();
					heldProbe->returned = true;
				}
				return false;
			},
			{}
		});

		QString error;
		const QString python = ExternalProcessStemSeparator::discoveredPython(&error);
		const bool heldReturnedAtPublish = heldProbe->returned.load();

		// Release the loser before asserting, so a failure cannot strand it.
		heldProbe->gate.open();
		settleProbes();

		QVERIFY2(python == preferred,
			"readiness waited for a losing lower-priority probe instead of publishing "
			"the preferred interpreter");
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QVERIFY2(!heldReturnedAtPublish,
			"the preferred interpreter was only published after the held probe returned");
		QVERIFY(ExternalProcessStemSeparator::pythonDiscoveryState().phase
			== ExternalProcessStemSeparator::PythonDiscoveryPhase::Found);
#else
		QFAIL("no non-blocking discovery service");
#endif
	}
};

QTEST_GUILESS_MAIN(StemPythonDiscoveryServiceTest)
#include "StemPythonDiscoveryServiceTest.moc"
