/*
 * StemPythonDiscoveryTest.cpp - the external stem backend's interpreter discovery
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
 * Cluster new-p15-64. The default ExternalProcessStemSeparator constructor used
 * to probe every candidate interpreter synchronously (four candidates, each
 * allowed 5 s to start and 30 s to import onnxruntime). The GUI invokes
 * `stem.get_state` synchronously, which constructs this backend, so a hanging
 * `import onnxruntime` could freeze the interface for minutes.
 *
 * These tests observe interpreter launches without timing anything: a
 * `sitecustomize.py` on PYTHONPATH appends one line to a marker file every time
 * an interpreter starts. One marker line = one probe. Nothing here sleeps.
 * Candidates are probed in parallel, each with the full deadline; the
 * non-blocking, cached discovery service is tested in
 * StemPythonDiscoveryServiceTest.cpp.
 */

#include <QtTest>

#include <algorithm>
#include <atomic>
#include <initializer_list>
#include <mutex>
#include <vector>

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>

#include "SampleBuffer.h"
#include "StemSeparation/ExternalProcessStemSeparator.h"
#include "StemSeparation/StemTypes.h"

using namespace lmms;

namespace
{

//! Restores the named environment variables on scope exit, so a failing test
//! cannot leak its fake interpreter into the next one.
class EnvironmentGuard
{
public:
	explicit EnvironmentGuard(std::initializer_list<const char*> names)
	{
		for (const char* name : names)
		{
			m_names.emplace_back(QString::fromLatin1(name));
			m_values.emplace_back(qgetenv(name));
		}
	}

	~EnvironmentGuard()
	{
		for (std::size_t i = 0; i < m_names.size(); ++i)
		{
			if (m_values[i].isNull())
			{
				qunsetenv(m_names[i].toUtf8().constData());
			}
			else
			{
				qputenv(m_names[i].toUtf8().constData(), m_values[i]);
			}
		}
	}

	EnvironmentGuard(const EnvironmentGuard&) = delete;
	EnvironmentGuard& operator=(const EnvironmentGuard&) = delete;

private:
	std::vector<QString> m_names;
	std::vector<QByteArray> m_values;
};

bool writeFile(const QString& path, const QByteArray& contents)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		return false;
	}
	return file.write(contents) == contents.size();
}

//! The interpreter the probes will run. Empty when this machine has none, in
//! which case the caller skips: discovery has nothing to discover.
QString findPython()
{
	for (const char* name : { "python3", "python" })
	{
		const QString path = QStandardPaths::findExecutable(QString::fromLatin1(name));
		if (!path.isEmpty())
		{
			return path;
		}
	}
	return QString();
}

/*! Writes a sitecustomize.py into @a dir that appends one line to @a marker on
 *  every interpreter start, and puts @a dir on PYTHONPATH (prepended). */
bool installProbeMarker(const QString& dir, const QString& marker)
{
	const QByteArray script =
		"import os\n"
		"path = os.environ.get('LMMS_STEM_PROBE_MARKER')\n"
		"if path:\n"
		"    with open(path, 'a', encoding='utf-8') as handle:\n"
		"        handle.write('probe\\n')\n";
	if (!writeFile(QDir(dir).filePath(QStringLiteral("sitecustomize.py")), script))
	{
		return false;
	}
	return true;
}

//! @a moduleDir's onnxruntime either imports cleanly (@a imports) or raises
//! ImportError, so discovery has a deterministic outcome on any machine.
bool installStubOnnxRuntime(const QString& moduleDir, bool imports)
{
	const QByteArray module = imports
		? QByteArrayLiteral("# Stub: the probe only needs this import to succeed.\n")
		: QByteArrayLiteral("raise ImportError('stub: onnxruntime is not installed')\n");
	return writeFile(QDir(moduleDir).filePath(QStringLiteral("onnxruntime.py")), module);
}

QByteArray pythonPath(const QStringList& dirs)
{
	return dirs.join(QDir::listSeparator()).toUtf8();
}

} // namespace

class StemPythonDiscoveryTest : public QObject
{
	Q_OBJECT
private slots:
	//! The discovery service caches per process: every test starts from a clean
	//! one and leaves neither its hooks nor its result behind.
	void init()
	{
#ifdef LMMS_STEM_PYTHON_DISCOVERY_SERVICE
		ExternalProcessStemSeparator::resetPythonDiscoveryForTest();
#endif
	}
	void cleanup() { ExternalProcessStemSeparator::setDiscoveryHooksForTest({}); }

	/*! The constructor must not launch a probe. It runs on whichever thread
	 *  asked for the backend - the GUI calls `stem.get_state` synchronously -
	 *  and a hanging interpreter must not hold that thread. */
	void constructorDoesNotProbeForAnInterpreter()
	{
		const QString python = findPython();
		if (python.isEmpty())
		{
			QSKIP("no python3/python interpreter on PATH");
		}
		EnvironmentGuard guard({ "LMMS_STEM_PYTHON", "PYTHONPATH",
			"LMMS_STEM_PROBE_MARKER", "LMMS_STEM_PYTHON_PROBE_MS" });
		QTemporaryDir site;
		QVERIFY(site.isValid());
		const QString marker = QDir(site.path()).filePath(QStringLiteral("probes.txt"));
		QVERIFY(installProbeMarker(site.path(), marker));
		qputenv("PYTHONPATH", site.path().toUtf8());
		qputenv("LMMS_STEM_PROBE_MARKER", marker.toUtf8());
		qputenv("LMMS_STEM_PYTHON", python.toUtf8());
		qunsetenv("LMMS_STEM_PYTHON_PROBE_MS");

		{
			ExternalProcessStemSeparator separator;
			QVERIFY2(!QFile::exists(marker),
				"constructing the external backend launched an interpreter probe: the "
				"constructor runs on the caller's thread, so a hanging "
				"`import onnxruntime` froze it for the length of the probe");
		}

		// Discovery still happens where a caller asks for it, and only there.
		(void) ExternalProcessStemSeparator::locatePython();
		QVERIFY2(QFile::exists(marker),
			"locatePython() must probe the configured interpreter");
	}

	/*! An exhausted discovery budget starts no process at all: a zero budget is
	 *  the boundary of the deadline and needs no waiting to observe. */
	void exhaustedBudgetLaunchesNoProbe()
	{
		const QString python = findPython();
		if (python.isEmpty())
		{
			QSKIP("no python3/python interpreter on PATH");
		}
		EnvironmentGuard guard({ "LMMS_STEM_PYTHON", "PYTHONPATH",
			"LMMS_STEM_PROBE_MARKER", "LMMS_STEM_PYTHON_PROBE_MS" });
		QTemporaryDir site;
		QVERIFY(site.isValid());
		const QString marker = QDir(site.path()).filePath(QStringLiteral("probes.txt"));
		QVERIFY(installProbeMarker(site.path(), marker));
		qputenv("PYTHONPATH", site.path().toUtf8());
		qputenv("LMMS_STEM_PROBE_MARKER", marker.toUtf8());
		qputenv("LMMS_STEM_PYTHON", python.toUtf8());
		qputenv("LMMS_STEM_PYTHON_PROBE_MS", QByteArrayLiteral("0"));

		QString error;
		const QString located = ExternalProcessStemSeparator::locatePython(&error);
		QVERIFY2(located.isEmpty(), "an exhausted budget cannot discover an interpreter");
		QVERIFY2(!QFile::exists(marker),
			"an exhausted discovery budget launched a probe: a zero "
			"budget must refuse before any candidate starts a process");
		QVERIFY2(!error.isEmpty(), "an exhausted budget must be reported");

		// With the budget back at its default the same candidates are probed,
		// so the deadline is a bound, not a permanent refusal.
		qunsetenv("LMMS_STEM_PYTHON_PROBE_MS");
		(void) ExternalProcessStemSeparator::locatePython();
		QVERIFY(QFile::exists(marker));
	}

	/*! Deferral moves discovery to separate(), which runs on the backend's
	 *  worker; the interpreter must still be resolved there, not reported
	 *  missing because the constructor no longer found it. */
	void separateResolvesTheInterpreterOnDemand()
	{
		const QString python = findPython();
		if (python.isEmpty())
		{
			QSKIP("no python3/python interpreter on PATH");
		}
		EnvironmentGuard guard({ "LMMS_STEM_PYTHON", "PYTHONPATH",
			"LMMS_STEM_PROBE_MARKER", "LMMS_STEM_PYTHON_PROBE_MS",
			"LMMS_STEM_MODEL", "LMMS_STEM_CLI" });
		QTemporaryDir site;
		QTemporaryDir module;
		QVERIFY(site.isValid());
		QVERIFY(module.isValid());
		const QString marker = QDir(site.path()).filePath(QStringLiteral("probes.txt"));
		QVERIFY(installProbeMarker(site.path(), marker));
		QVERIFY(installStubOnnxRuntime(module.path(), true));
		qputenv("PYTHONPATH", pythonPath({ site.path(), module.path() }));
		qputenv("LMMS_STEM_PROBE_MARKER", marker.toUtf8());
		qputenv("LMMS_STEM_PYTHON", python.toUtf8());
		qunsetenv("LMMS_STEM_PYTHON_PROBE_MS");
		qputenv("LMMS_STEM_CLI", QByteArrayLiteral("/nonexistent/stem_split_cli.py"));
		qputenv("LMMS_STEM_MODEL", QByteArrayLiteral("/nonexistent/model.onnx"));

		ExternalProcessStemSeparator separator;
		QVERIFY2(!QFile::exists(marker), "the constructor must not probe");

		// The separation fails later (no CLI), but only after discovering the
		// interpreter - which is exactly the deferred work.
		SampleBuffer mix;
		StemSet stems{};
		QString error;
		const auto status = separator.separate(mix, StemModelSampleRate,
			HTDemucsSegmentFrames, DefaultStemQuality, StemSeparator::ProgressFn(),
			stems, error);
		QVERIFY2(status == StemSeparator::Status::Failed, qPrintable(error));
		QVERIFY2(QFile::exists(marker),
			"separate() must resolve the interpreter it deferred from the constructor");
		QVERIFY2(!error.isEmpty(),
			"a separation that cannot start must report why");
	}

	/*! A slow failing candidate must not starve a later candidate that works.
	 *  LMMS_STEM_PYTHON names an interpreter that is slow and fails (6 s, longer
	 *  than the budget), and /usr/bin/python3 needs 2 s to cold-import
	 *  onnxruntime. Probing sequentially with a shared deadline handed the
	 *  fallback 5000 / 4 = 1250 ms, so it failed; every candidate now runs
	 *  concurrently with the FULL budget. The cost model is an injected probe
	 *  (a candidate succeeds when it works and its cost fits the budget it was
	 *  handed) and a fake clock - no real time passes. */
	void slowFailingCandidateDoesNotStarveAColdFallback()
	{
		EnvironmentGuard guard({ "LMMS_STEM_PYTHON", "LMMS_STEM_PYTHON_PROBE_MS" });
		const QString slow = QStringLiteral("/slow/failing/interpreter");
		const QString fallback = QStringLiteral("/usr/bin/python3");
		qputenv("LMMS_STEM_PYTHON", slow.toUtf8());
		qunsetenv("LMMS_STEM_PYTHON_PROBE_MS"); // a positive (default) budget

		std::atomic<qint64> clockMs{ 0 };
		std::mutex mutex; // the probe runs on one thread per candidate
		QStringList probed;
		QList<int> budgets;
		ExternalProcessStemSeparator::setDiscoveryHooksForTest({
			[&](const QString& candidate, int timeoutMs)
			{
				const qint64 cost = candidate == slow ? 6000 : candidate == fallback ? 2000 : 0;
				const bool works = candidate == fallback;
				{
					std::lock_guard lock(mutex);
					probed << candidate;
					budgets << timeoutMs;
				}
				clockMs += std::min<qint64>(cost, timeoutMs);
				return timeoutMs >= cost && works;
			},
			[&clockMs]() { return clockMs.load(); }
		});

		QString error;
		const QString located = ExternalProcessStemSeparator::locatePython(&error);
		QCOMPARE(located, fallback);
		QVERIFY2(error.isEmpty(), qPrintable(error));
		QCOMPARE(probed.size(), 4);
		QVERIFY2(probed.contains(slow) && probed.contains(fallback),
			"every candidate must be probed, not only those before the first success");
		for (const int budget : budgets)
		{
			QCOMPARE(budget, 5000); // the full budget, never a share of it
		}
	}

	/*! The normal path is untouched: the first candidate whose `import
	 *  onnxruntime` succeeds is the interpreter returned. */
	void discoveryFindsAnInterpreterThatImportsOnnxRuntime()
	{
		const QString python = findPython();
		if (python.isEmpty())
		{
			QSKIP("no python3/python interpreter on PATH");
		}
		EnvironmentGuard guard({ "LMMS_STEM_PYTHON", "PYTHONPATH",
			"LMMS_STEM_PROBE_MARKER", "LMMS_STEM_PYTHON_PROBE_MS" });
		QTemporaryDir site;
		QTemporaryDir module;
		QVERIFY(site.isValid());
		QVERIFY(module.isValid());
		const QString marker = QDir(site.path()).filePath(QStringLiteral("probes.txt"));
		QVERIFY(installProbeMarker(site.path(), marker));
		QVERIFY(installStubOnnxRuntime(module.path(), true));
		qputenv("PYTHONPATH", pythonPath({ site.path(), module.path() }));
		qputenv("LMMS_STEM_PROBE_MARKER", marker.toUtf8());
		qputenv("LMMS_STEM_PYTHON", python.toUtf8());
		qunsetenv("LMMS_STEM_PYTHON_PROBE_MS");

		QString error;
		const QString located = ExternalProcessStemSeparator::locatePython(&error);
		QCOMPARE(located, python);
		QVERIFY2(error.isEmpty(), qPrintable(QStringLiteral("unexpected error: ") + error));
		QVERIFY(QFile::exists(marker));
	}

	/*! A failing probe is not remembered across a reconfiguration: the same
	 *  process, told about another interpreter, discovers it. */
	void failedProbeIsRetriedAfterReconfiguration()
	{
		const QString python = findPython();
		if (python.isEmpty())
		{
			QSKIP("no python3/python interpreter on PATH");
		}
		EnvironmentGuard guard({ "LMMS_STEM_PYTHON", "PYTHONPATH",
			"LMMS_STEM_PROBE_MARKER", "LMMS_STEM_PYTHON_PROBE_MS" });
		QTemporaryDir site;
		QTemporaryDir brokenModule;
		QTemporaryDir goodModule;
		QVERIFY(site.isValid());
		QVERIFY(brokenModule.isValid());
		QVERIFY(goodModule.isValid());
		const QString marker = QDir(site.path()).filePath(QStringLiteral("probes.txt"));
		QVERIFY(installProbeMarker(site.path(), marker));
		QVERIFY(installStubOnnxRuntime(brokenModule.path(), false));
		QVERIFY(installStubOnnxRuntime(goodModule.path(), true));
		qputenv("LMMS_STEM_PROBE_MARKER", marker.toUtf8());
		qunsetenv("LMMS_STEM_PYTHON_PROBE_MS");

		// Every candidate fails: the configured path does not exist and the
		// real interpreters see an onnxruntime that refuses the import.
		qputenv("PYTHONPATH", pythonPath({ site.path(), brokenModule.path() }));
		qputenv("LMMS_STEM_PYTHON",
			QDir(brokenModule.path()).filePath(QStringLiteral("no-such-python")).toUtf8());
		QString error;
		QVERIFY2(ExternalProcessStemSeparator::locatePython(&error).isEmpty(),
			qPrintable(QStringLiteral("unexpected interpreter: ") + error));
		QVERIFY(!error.isEmpty());

		// Reconfigured in the same process: the next call must see it.
		qputenv("PYTHONPATH", pythonPath({ site.path(), goodModule.path() }));
		qputenv("LMMS_STEM_PYTHON", python.toUtf8());
		QCOMPARE(ExternalProcessStemSeparator::locatePython(), python);
	}
};

QTEST_GUILESS_MAIN(StemPythonDiscoveryTest)
#include "StemPythonDiscoveryTest.moc"
