/*
 * OnnxRuntimeStemSeparator.h - in-process ONNX Runtime backend (optional)
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

#ifndef LMMS_ONNX_RUNTIME_STEM_SEPARATOR_H
#define LMMS_ONNX_RUNTIME_STEM_SEPARATOR_H

#include <QString>

#include "StemSeparation/StemSeparator.h"

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
		const ProgressFn& progress,
		StemSet& out,
		QString& error) override;

	QString backendName() const override;

	static QString runtimeVersion();

private:
	QString m_modelPath;
};

} // namespace lmms

#endif // LMMS_ONNX_RUNTIME_STEM_SEPARATOR_H
