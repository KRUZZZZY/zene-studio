/*
 * StemSeparator.h - inference backend interface of the stem-separation feature
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

#ifndef LMMS_STEM_SEPARATOR_H
#define LMMS_STEM_SEPARATOR_H

#include <functional>

#include <QString>

#include "StemSeparation/StemTypes.h"
#include "lmms_export.h"

namespace lmms
{

class SampleBuffer;

// A StemSeparator runs one whole-file 4-stem separation. Implementations are
// used exclusively by StemJobManager's worker thread - never by the audio
// thread and never by the GUI thread (see doc/STEM-SPLIT.md, "Threading").
class LMMS_EXPORT StemSeparator
{
public:
	enum class Status
	{
		Success,
		Cancelled,
		Failed
	};

	// Called by the backend from the worker thread with a fraction in [0, 1].
	// Returning false requests cancellation; the backend must return
	// Status::Cancelled as soon as it can do so safely.
	using ProgressFn = std::function<bool(float fraction)>;

	virtual ~StemSeparator() = default;

	// mix: stereo source at sampleRate. On success `out` holds one buffer per
	// stem, all at sampleRate and of the same length as `mix`. On cancellation
	// `out` may hold partial stems (implementation defined, see each backend).
	virtual Status separate(const SampleBuffer& mix,
		int sampleRate,
		int segmentFrames,
		const ProgressFn& progress,
		StemSet& out,
		QString& error) = 0;

	// Human readable backend identifier, e.g. "onnxruntime-cpp" or
	// "external-process". Surfaced in logs and the progress dialog.
	virtual QString backendName() const = 0;
};

} // namespace lmms

#endif // LMMS_STEM_SEPARATOR_H
