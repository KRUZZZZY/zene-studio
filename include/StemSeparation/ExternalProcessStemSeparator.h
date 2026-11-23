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

#include <functional>

#include <QString>

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

	// Discovery order: LMMS_STEM_PYTHON, /usr/bin/python3, python3,
	// /usr/local/bin/python3; the first one that imports onnxruntime wins.
	// The WHOLE sequence is bounded by one short deadline, and each candidate
	// may spend only its share of what is left, so neither a hanging interpreter
	// nor a slow one can hold the caller or starve a later candidate. The next
	// call probes again.
	static QString locatePython(QString* error = nullptr);

#ifdef LMMS_TESTING
	//! Test seam (compiled into test builds only, see AudioResampler's
	//! *ForTest members): replaces the interpreter probe and the monotonic clock
	//! the discovery deadline reads, so a candidate that consumes its whole
	//! share and an exhausted budget are exercised without real waiting. An
	//! empty function restores the real QProcess probe / steady_clock.
	struct DiscoveryHooks
	{
		std::function<bool(const QString& candidate, int timeoutMs)> probe;
		std::function<qint64()> nowMs;
	};
	static void setDiscoveryHooksForTest(DiscoveryHooks hooks);
#endif

	// Discovery order: LMMS_STEM_CLI, the build-time source path, then
	// <appdir>/tools/ and <appdir>/../share/zene/tools/.
	static QString locateCli(QString* error = nullptr);

	// LMMS_STEM_MODEL, else <AppDataLocation>/models/stems/htdemucs-fp16.onnx.
	static QString defaultModelPath();

	// Availability of THIS instance: python + CLI + the configured model path.
	bool isAvailable(QString* error = nullptr) const;

private:
	QString m_python;
	QString m_cli;
	QString m_model;
};

} // namespace lmms

#endif // LMMS_EXTERNAL_PROCESS_STEM_SEPARATOR_H
