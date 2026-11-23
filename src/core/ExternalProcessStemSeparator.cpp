/*
 * ExternalProcessStemSeparator.cpp - python/onnxruntime backend
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
#include <cstdint>
#include <cstring>
#include <limits>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include "SampleBuffer.h"
#include "SampleFrame.h"
#include "StemSeparation/StemModelStore.h"

namespace lmms
{

namespace
{

constexpr int PollIntervalMs = 100;
constexpr int ProgressPollIntervalMs = 200;
constexpr int KillGraceMs = 30000;

//! How long one interpreter may take to start before it counts as missing.
constexpr int PythonProbeStartMs = 5000;
//! How long a killed probe may take to be reaped (it never gets a new candidate
//! started in that time - locatePython() checks the deadline first).
constexpr int PythonProbeKillGraceMs = 2000;

/*! Upper bound for the WHOLE interpreter discovery sequence.
 *
 *  Discovery runs on whichever thread asked for it - the GUI invokes
 *  `stem.get_state` synchronously, and its handler constructs this backend and
 *  asks isAvailable() - so it must never hold that thread for long. The old
 *  per-candidate waits (5 s to start + 30 s to import) cost four timeouts in a
 *  row when an interpreter hung importing onnxruntime: minutes of freeze.
 *  5 s for the sequence, shared by its candidates (about 1.25 s each when all
 *  four are tried), is still several times the ~0.45 s a cold reference CLI
 *  needs to start, import and reach inference; a share that is too short only
 *  delays discovery, because the next call probes again. */
constexpr int PythonDiscoveryBudgetMs = 5000;

void setError(QString* error, const QString& text)
{
	if (error != nullptr)
	{
		*error = text;
	}
}

/*! The budget for this discovery call. LMMS_STEM_PYTHON_PROBE_MS is a test hook
 *  (like LMMS_STEM_CHUNK_DELAY_MS) that can only make discovery stricter: a
 *  value outside [0, PythonDiscoveryBudgetMs] keeps the default. Zero is the
 *  boundary the regression test uses to observe "no candidate is started after
 *  the deadline" without waiting. */
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
//! On timeout the child is killed, so no probe outlives the discovery call.
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
ExternalProcessStemSeparator::DiscoveryHooks& discoveryHooks()
{
	static ExternalProcessStemSeparator::DiscoveryHooks hooks;
	return hooks;
}
#endif

//! The monotonic clock the discovery deadline reads (milliseconds since an
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
//! replaces the child process, not the decision.
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

bool writeMixFile(const QString& path, const SampleBuffer& mix)
{
	std::vector<float> interleaved(mix.size() * 2);
	const SampleFrame* mixFrames = mix.data();
	for (size_t i = 0; i < mix.size(); ++i)
	{
		interleaved[2 * i] = mixFrames[i].left();
		interleaved[2 * i + 1] = mixFrames[i].right();
	}
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		return false;
	}
	const auto bytes = static_cast<qint64>(interleaved.size() * sizeof(float));
	return file.write(reinterpret_cast<const char*>(interleaved.data()), bytes) == bytes;
}

std::shared_ptr<const SampleBuffer> readStemFile(
	const QString& path, int sampleRate, std::size_t expectedFrames)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly))
	{
		return nullptr;
	}
	constexpr qint64 BytesPerFrame = 2 * static_cast<qint64>(sizeof(float));
	const auto maxFrames = static_cast<std::uintmax_t>(std::numeric_limits<qint64>::max() / BytesPerFrame);
	if (static_cast<std::uintmax_t>(expectedFrames) > maxFrames)
	{
		return nullptr;
	}
	const auto expectedBytes = static_cast<qint64>(expectedFrames) * BytesPerFrame;
	if (file.size() != expectedBytes)
	{
		return nullptr;
	}
	const QByteArray raw = file.read(expectedBytes);
	if (raw.isEmpty() || raw.size() != expectedBytes || raw.size() % BytesPerFrame != 0)
	{
		return nullptr;
	}
	std::vector<float> interleaved(raw.size() / static_cast<int>(sizeof(float)));
	std::memcpy(interleaved.data(), raw.constData(), interleaved.size() * sizeof(float));
	std::vector<SampleFrame> frames(interleaved.size() / 2);
	for (size_t i = 0; i < frames.size(); ++i)
	{
		frames[i] = SampleFrame(interleaved[2 * i], interleaved[2 * i + 1]);
	}
	return std::make_shared<const SampleBuffer>(std::move(frames), sampleRate);
}

//! Reads every stem the child wrote in @a outDir into @a out. True only when all of them are
//! there and each is @a frames long - the input's length: a truncated or mismatched stem
//! would be misaligned with the mix and with the others.
bool readStems(const QString& outDir, int sampleRate, std::size_t frames, StemSet& out)
{
	bool haveAll = true;
	for (int i = 0; i < NumStems; ++i)
	{
		const auto path = QDir(outDir).filePath(
			QString::fromLatin1(stemName(static_cast<Stem>(i))) + QStringLiteral(".f32"));
		auto buffer = readStemFile(path, sampleRate, frames);
		if (buffer == nullptr || buffer->size() != frames)
		{
			haveAll = false;
		}
		out[static_cast<size_t>(i)] = buffer;
	}
	return haveAll;
}

} // namespace

#ifdef LMMS_TESTING
void ExternalProcessStemSeparator::setDiscoveryHooksForTest(DiscoveryHooks hooks)
{
	discoveryHooks() = std::move(hooks);
}
#endif




ExternalProcessStemSeparator::ExternalProcessStemSeparator(QString pythonExecutable,
	QString cliPath,
	QString modelPath) :
	// The interpreter is resolved by separate() (see the header): probing here
	// let a hanging `import onnxruntime` freeze the thread that constructed the
	// backend, which can be the GUI thread.
	m_python(std::move(pythonExecutable)),
	m_cli(cliPath.isEmpty() ? locateCli() : std::move(cliPath)),
	m_model(modelPath.isEmpty() ? defaultModelPath() : std::move(modelPath))
{
}




QString ExternalProcessStemSeparator::locatePython(QString* error)
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

	// One deadline for the whole sequence. Each candidate may spend only its
	// share of what is left, so a slow or hanging one cannot consume the budget
	// a later candidate needs; a candidate that fails fast leaves its unused
	// share to the rest. No process is started once the deadline is reached.
	const int budgetMs = pythonDiscoveryBudgetMs();
	const auto deadline = discoveryNowMs() + budgetMs;
	QStringList tried;
	bool outOfTime = false;
	for (qsizetype index = 0; index < candidates.size(); ++index)
	{
		const auto left = deadline - discoveryNowMs();
		if (left <= 0)
		{
			outOfTime = true;
			break;
		}
		const auto candidatesLeft = static_cast<qint64>(candidates.size() - index);
		const int slice = static_cast<int>(std::max<qint64>(1, left / candidatesLeft));
		tried << candidates.at(index);
		if (probeInterpreter(candidates.at(index), slice))
		{
			return candidates.at(index);
		}
	}

	if (outOfTime)
	{
		setError(error, QStringLiteral("Python discovery ran out of time (%1 ms) "
			"before an interpreter answered (tried: %2). Probing is bounded so a "
			"hanging interpreter cannot stall the caller; retry, or set "
			"LMMS_STEM_PYTHON to a working interpreter.")
			.arg(budgetMs)
			.arg(tried.isEmpty() ? QStringLiteral("none")
				: tried.join(QStringLiteral(", "))));
	}
	else
	{
		setError(error, QStringLiteral("No Python interpreter with the 'onnxruntime' "
			"module found (tried: %1). Install onnxruntime or set LMMS_STEM_PYTHON.")
			.arg(tried.join(QStringLiteral(", "))));
	}
	return QString();
}




QString ExternalProcessStemSeparator::locateCli(QString* error)
{
	const auto env = qEnvironmentVariable("LMMS_STEM_CLI");
	if (!env.isEmpty())
	{
		if (QFileInfo::exists(env))
		{
			return env;
		}
		setError(error, QStringLiteral("LMMS_STEM_CLI does not exist: %1").arg(env));
		return QString();
	}

	QStringList candidates;
#ifdef LMMS_STEM_CLI_DEFAULT
	candidates << QStringLiteral(LMMS_STEM_CLI_DEFAULT);
#endif
	const auto appDir = QCoreApplication::applicationDirPath();
	candidates << appDir + QStringLiteral("/tools/stem_split_cli.py")
		<< appDir + QStringLiteral("/../share/zene/tools/stem_split_cli.py");

	for (const auto& candidate : candidates)
	{
		if (QFileInfo::exists(candidate))
		{
			return QDir::cleanPath(candidate);
		}
	}
	setError(error, QStringLiteral("stem_split_cli.py not found (tried: %1). "
		"Set LMMS_STEM_CLI to its location.").arg(candidates.join(QStringLiteral(", "))));
	return QString();
}




QString ExternalProcessStemSeparator::defaultModelPath()
{
	return StemModelStore::defaultModelPath();
}




bool ExternalProcessStemSeparator::isAvailable(QString* error) const
{
	QString localError;
	const auto python = locatePython(&localError);
	if (python.isEmpty())
	{
		setError(error, localError);
		return false;
	}
	const auto cli = locateCli(&localError);
	if (cli.isEmpty())
	{
		setError(error, localError);
		return false;
	}
	if (!StemModelStore::isModelPresent(m_model, &localError))
	{
		setError(error, localError);
		return false;
	}
	return true;
}




QString ExternalProcessStemSeparator::backendName() const
{
	return QStringLiteral("external-process (python onnxruntime)");
}




StemSeparator::Status ExternalProcessStemSeparator::separate(const SampleBuffer& mix,
	int sampleRate,
	int segmentFrames,
	StemQuality quality,
	const ProgressFn& progress,
	StemSet& out,
	QString& error)
{
	if (m_python.isEmpty())
	{
		// Deferred discovery: the constructor must stay cheap (it can run on the
		// GUI thread), so the single probe per instance runs here, on the
		// backend's worker thread.
		m_python = locatePython(&error);
		if (m_python.isEmpty())
		{
			if (error.isEmpty())
			{
				error = QStringLiteral("No Python interpreter with onnxruntime available");
			}
			return Status::Failed;
		}
	}
	if (m_cli.isEmpty())
	{
		error = QStringLiteral("stem_split_cli.py not found");
		return Status::Failed;
	}
	if (!StemModelStore::isModelPresent(m_model, &error))
	{
		return Status::Failed;
	}
	if (sampleRate != StemModelSampleRate)
	{
		error = QStringLiteral("Stem separation needs %1 Hz input, got %2 Hz "
			"(resampling is not implemented yet, SPEC-stem-split.md OQ-1)")
			.arg(StemModelSampleRate).arg(sampleRate);
		return Status::Failed;
	}
	if (mix.empty())
	{
		error = QStringLiteral("Input is empty");
		return Status::Failed;
	}

	QTemporaryDir tmp;
	if (!tmp.isValid())
	{
		error = QStringLiteral("Cannot create temporary directory");
		return Status::Failed;
	}
	const auto mixPath = tmp.filePath(QStringLiteral("mix.f32"));
	if (!writeMixFile(mixPath, mix))
	{
		error = QStringLiteral("Cannot write %1").arg(mixPath);
		return Status::Failed;
	}
	const auto outDir = tmp.filePath(QStringLiteral("out"));
	if (!QDir().mkpath(outDir))
	{
		error = QStringLiteral("Cannot create %1").arg(outDir);
		return Status::Failed;
	}
	const auto cancelPath = tmp.filePath(QStringLiteral("cancel"));

	QProcess proc;
	proc.setProcessChannelMode(QProcess::SeparateChannels);
	QStringList args = {
		m_cli,
		QStringLiteral("--model"), m_model,
		QStringLiteral("--input"), mixPath,
		QStringLiteral("--in-format"), QStringLiteral("f32"),
		QStringLiteral("--in-rate"), QString::number(sampleRate),
		QStringLiteral("--out-dir"), outDir,
		QStringLiteral("--out-format"), QStringLiteral("f32"),
		QStringLiteral("--segment"), QString::number(segmentFrames),
		QStringLiteral("--quality"), QString::fromLatin1(stemQualityName(quality)),
		QStringLiteral("--progress-json"),
		QStringLiteral("--cancel-file"), cancelPath,
	};
	// Test hook: slow the reference CLI down so the cancellation path can be
	// exercised deterministically (mirrors the CLI's own --chunk-delay-ms).
	const auto chunkDelay = qEnvironmentVariable("LMMS_STEM_CHUNK_DELAY_MS");
	if (!chunkDelay.isEmpty())
	{
		args << QStringLiteral("--chunk-delay-ms") << chunkDelay;
	}
	proc.start(m_python, args);
	if (!proc.waitForStarted(10000))
	{
		error = QStringLiteral("Cannot start %1: %2").arg(m_python, proc.errorString());
		return Status::Failed;
	}

	QByteArray stdoutBuf;
	QByteArray stderrBuf;
	QByteArray lineBuf;
	QString cliError;
	bool cancelRequested = false;
	bool cancelFileWritten = false;
	float lastFraction = 0.0f;
	auto lastProgressPoll = std::chrono::steady_clock::now();
	auto cancelTime = std::chrono::steady_clock::now();

	auto handleLine = [&](const QByteArray& line)
	{
		if (line.trimmed().isEmpty())
		{
			return;
		}
		QJsonParseError parseError;
		const auto doc = QJsonDocument::fromJson(line, &parseError);
		if (parseError.error != QJsonParseError::NoError || !doc.isObject())
		{
			return; // diagnostics may print non-JSON lines
		}
		const auto obj = doc.object();
		const auto type = obj.value(QStringLiteral("type")).toString();
		if (type == QLatin1String("progress"))
		{
			lastFraction = static_cast<float>(obj.value(QStringLiteral("fraction")).toDouble());
			if (progress && !progress(lastFraction))
			{
				cancelRequested = true;
			}
		}
		else if (type == QLatin1String("error"))
		{
			cliError = obj.value(QStringLiteral("error")).toString();
		}
		else if (type == QLatin1String("cancelled"))
		{
			cancelRequested = true;
		}
	};

	auto consumeStdout = [&]()
	{
		stdoutBuf += proc.readAllStandardOutput();
		int nl = -1;
		while ((nl = stdoutBuf.indexOf('\n')) >= 0)
		{
			handleLine(stdoutBuf.left(nl));
			stdoutBuf.remove(0, nl + 1);
		}
	};

	while (proc.state() != QProcess::NotRunning)
	{
		if (proc.waitForReadyRead(PollIntervalMs))
		{
			consumeStdout();
			stderrBuf += proc.readAllStandardError();
		}

		const auto now = std::chrono::steady_clock::now();
		if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastProgressPoll).count()
			>= ProgressPollIntervalMs)
		{
			lastProgressPoll = now;
			// Give the manager a chance to observe cancellation even when the
			// child produces no progress line for a while.
			if (progress && !progress(lastFraction))
			{
				cancelRequested = true;
			}
		}
		if (cancelRequested && !cancelFileWritten)
		{
			cancelFileWritten = true;
			cancelTime = std::chrono::steady_clock::now();
			QFile cancelFile(cancelPath);
			if (cancelFile.open(QIODevice::WriteOnly))
			{
				cancelFile.write("cancel");
			}
		}
		if (cancelRequested && proc.state() == QProcess::Running
			&& std::chrono::duration_cast<std::chrono::milliseconds>(now - cancelTime).count() > KillGraceMs)
		{
			proc.kill();
			proc.waitForFinished(2000);
			break;
		}
	}
	consumeStdout();
	stderrBuf += proc.readAllStandardError();

	const int exitCode = (proc.exitStatus() == QProcess::NormalExit) ? proc.exitCode() : -1;

	// Read whatever stems the child managed to write (partial on cancellation).
	const bool haveAll = readStems(outDir, sampleRate, mix.size(), out);

	if (exitCode == 0 && haveAll)
	{
		if (progress)
		{
			progress(1.0f);
		}
		return Status::Success;
	}
	if (exitCode == 3 || (cancelRequested && exitCode != 0))
	{
		return Status::Cancelled;
	}

	error = cliError;
	if (error.isEmpty())
	{
		error = QStringLiteral("stem_split_cli.py failed (exit %1): %2")
			.arg(exitCode)
			.arg(QString::fromUtf8(stderrBuf.right(2000)).trimmed());
	}
	return Status::Failed;
}

} // namespace lmms
