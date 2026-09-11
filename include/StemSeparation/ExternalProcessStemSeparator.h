/*
 * ExternalProcessStemSeparator.h - python/onnxruntime backend
 *
 * Copyright (c) 2026 LMMS Developers
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
	// Empty arguments fall back to the corresponding locate*() helper.
	explicit ExternalProcessStemSeparator(QString pythonExecutable = QString(),
		QString cliPath = QString(),
		QString modelPath = QString());

	Status separate(const SampleBuffer& mix,
		int sampleRate,
		int segmentFrames,
	const ProgressFn& progress,
		StemSet& out,
		QString& error) override;

	QString backendName() const override;

	// Discovery order: LMMS_STEM_PYTHON, /usr/bin/python3, python3,
	// /usr/local/bin/python3; the first one that imports onnxruntime wins.
	static QString locatePython(QString* error = nullptr);

	// Discovery order: LMMS_STEM_CLI, the build-time source path, then
	// <appdir>/tools/ and <appdir>/../share/zene/tools/.
	static QString locateCli(QString* error = nullptr);

	// LMMS_STEM_MODEL, else <AppDataLocation>/models/stems/htdemucs-fp16.onnx.
	static QString defaultModelPath();

	static bool isAvailable(QString* error = nullptr);

private:
	QString m_python;
	QString m_cli;
	QString m_model;
};

} // namespace lmms

#endif // LMMS_EXTERNAL_PROCESS_STEM_SEPARATOR_H
