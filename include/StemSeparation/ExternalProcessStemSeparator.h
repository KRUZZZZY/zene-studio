/*
 * ExternalProcessStemSeparator.h - python/onnxruntime backend
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

#ifndef LMMS_EXTERNAL_PROCESS_STEM_SEPARATOR_H
#define LMMS_EXTERNAL_PROCESS_STEM_SEPARATOR_H

//! Defined when the non-blocking discovery service below exists (tests use it).
#define LMMS_STEM_PYTHON_DISCOVERY_SERVICE 1
//! Defined when discovery publishes the winner before lower-priority probes end
//! (tests then settle those probes before counting them).
#define LMMS_STEM_PYTHON_EARLY_PUBLISH 1

#include <functional>

#include <QString>
#include <QtGlobal>

#include "StemSeparation/StemSeparator.h"

namespace lmms
{

// Runs tools/stem_split_cli.py in a child process. This is the always-available
// backend: it works on any system with a Python interpreter that imports
// onnxruntime, needs no C++ ORT SDK, and is fully isolated from the LMMS
// process (a crashing model cannot take the DAW down).
//
// The child speaks the same model contract as the optional C++ backend
// (src/core/OnnxRuntimeStemSeparator.cpp); see doc/STEM-SPLIT.md.
class LMMS_EXPORT ExternalProcessStemSeparator : public StemSeparator
{
public:
	// Empty arguments fall back to the corresponding finder. The interpreter is
	// NOT probed here: with an empty @a pythonExecutable, separate() discovers it
	// on the backend's worker thread. Probing can launch child processes and wait
	// for them, so it must not run on the constructing thread (the GUI drives
	// `stem.get_state` synchronously, which constructs this backend).
	explicit ExternalProcessStemSeparator(QString pythonExecutable = QString(),
		QString cliPath = QString(),
		QString modelPath = QString());

	Status separate(const SampleBuffer& mix,
		int sampleRate,
		int segmentFrames,
		StemQuality quality,
		const ProgressFn& progress,
		StemSet& out,
		QString& error) override;

	QString backendName() const override;

	// Discovery order (priority): LMMS_STEM_PYTHON, /usr/bin/python3, python3,
	// /usr/local/bin/python3. All candidates are probed CONCURRENTLY, each with
	// the full deadline (5 s), and the highest-priority one that imports
	// onnxruntime wins - a slow or hanging candidate cannot starve a later one.
	// BLOCKS only until the winner is settled, i.e. every higher-priority
	// candidate has failed; lower-priority probes are then cancelled (child
	// killed) and reaped in the background. Each call runs a fresh discovery (no
	// cache). For the CLI and tests: never call it from the GUI thread.
	static QString locatePython(QString* error = nullptr);

	// Non-blocking, cached, per-process discovery for threads that must not wait.
	enum class PythonDiscoveryPhase { Pending, Found, Failed };
	struct PythonDiscoveryState
	{
		PythonDiscoveryPhase phase = PythonDiscoveryPhase::Pending;
		QString python; //!< the interpreter, when Found
		QString error;  //!< why not, when Pending or Failed
	};

	// Starts a background discovery unless one is running, a result for the
	// current LMMS_STEM_PYTHON value is cached, or a failure is younger than 10 s.
	// Never blocks.
	static void requestPythonDiscovery();

	// The current state; calls requestPythonDiscovery() first. With the default
	// @a waitMs of 0 it never blocks. A positive @a waitMs waits at most that
	// long for a run in flight to finish (the control surface passes
	// pythonDiscoveryWaitBoundMs(), so a caller's first question is answered
	// with the result instead of "pending", and a hanging interpreter still
	// cannot hold it longer than one bounded run).
	static PythonDiscoveryState pythonDiscoveryState(int waitMs = 0);

	// The longest one discovery run can take: the probe deadline plus the grace
	// a killed probe gets to be reaped.
	static int pythonDiscoveryWaitBoundMs();

	// The interpreter for the control surface (`stem.get_state`, `stem.start`),
	// which runs on the GUI thread: pythonDiscoveryState(pythonDiscoveryWaitBoundMs()),
	// so it waits at most one bounded run and only while nothing is cached. Empty
	// with @a error set while still pending or after a failure.
	static QString discoveredPython(QString* error = nullptr);

	// BLOCKS until the running discovery (started if needed) finished and returns
	// its interpreter, or an empty string with @a error set. Worker threads and
	// tests only, never the GUI thread.
	static QString waitForPythonDiscovery(QString* error = nullptr);

#ifdef LMMS_TESTING
	//! Test seam (compiled into test builds only, see AudioResampler's
	//! *ForTest members): replaces the interpreter probe and the monotonic clock
	//! the failure-retry delay reads, so discovery is exercised without real
	//! processes or waiting. An empty function restores the real QProcess probe /
	//! steady_clock. The probe runs on several threads at once (one per
	//! candidate), so a test probe must be thread-safe.
	struct DiscoveryHooks
	{
		std::function<bool(const QString& candidate, int timeoutMs)> probe;
		std::function<qint64()> nowMs;
	};
	//! Waits for any running discovery, forgets the cached result, then swaps the
	//! hooks (so no probe thread reads them while they change).
	static void setDiscoveryHooksForTest(DiscoveryHooks hooks);
	//! Waits for any running discovery and its background probes, then clears
	//! the cached result.
	static void resetPythonDiscoveryForTest();
	//! Waits until no probe thread is running (cancelled losers included),
	//! without resetting anything.
	static void waitForDiscoveryProbesForTest();
#endif

	// Discovery order: LMMS_STEM_CLI, the build-time source path, then
	// <appdir>/tools/ and <appdir>/../share/zene/tools/.
	static QString locateCli(QString* error = nullptr);

	// LMMS_STEM_MODEL, else <AppDataLocation>/models/stems/htdemucs-fp16.onnx.
	static QString defaultModelPath();

	// Availability of THIS instance: python + CLI + the configured model path.
	// An instance built with an interpreter uses it without waiting; one without
	// waits for the discovery service (waitForPythonDiscovery), so it may block:
	// worker threads and tests only. GUI-thread callers construct it with the
	// interpreter taken from pythonDiscoveryState().
	bool isAvailable(QString* error = nullptr) const;

private:
	QString m_python;
	QString m_cli;
	QString m_model;
};

} // namespace lmms

#endif // LMMS_EXTERNAL_PROCESS_STEM_SEPARATOR_H
