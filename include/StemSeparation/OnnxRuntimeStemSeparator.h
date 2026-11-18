/*
 * OnnxRuntimeStemSeparator.h - in-process ONNX Runtime backend (optional)
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

#ifndef LMMS_ONNX_RUNTIME_STEM_SEPARATOR_H
#define LMMS_ONNX_RUNTIME_STEM_SEPARATOR_H

#include <QString>

#include "StemSeparation/StemSeparator.h"

class OnnxRuntimeStemSeparatorTest;

namespace lmms
{

// In-process ONNX Runtime backend. Only compiled when find_package(ONNXRuntime)
// succeeded; see doc/STEM-SPLIT.md, "CMake gate". It speaks the same model
// contract and uses the same overlap-add segmentation as the external-process
// backend, so results are interchangeable.
class LMMS_EXPORT OnnxRuntimeStemSeparator : public StemSeparator
{
public:
	explicit OnnxRuntimeStemSeparator(QString modelPath = QString());

	Status separate(const SampleBuffer& mix,
		int sampleRate,
		int segmentFrames,
		StemQuality quality,
		const ProgressFn& progress,
		StemSet& out,
		QString& error) override;

	QString backendName() const override;

	static QString runtimeVersion();

private:
	friend class ::OnnxRuntimeStemSeparatorTest;
	using PostRunTestHook = void (*)();
	// Whether a segment of \a segment frames over \a frames input frames is indexed safely
	// by the int arithmetic of separate(); false, with \a error, when it is not.
	static bool segmentFits(int frames, int segment, QString* error);
	// The segment separate() runs at: the model's own when \a segmentFrames is 1 or less. 0, with \a error,
	// when segmentFits() refuses it.
	static int segmentFor(int frames, int segmentFrames, QString* error);
	PostRunTestHook m_postRunTestHook = nullptr;
	QString m_modelPath;
};

} // namespace lmms

#endif // LMMS_ONNX_RUNTIME_STEM_SEPARATOR_H
