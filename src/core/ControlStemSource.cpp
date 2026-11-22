/*
 * ControlStemSource.cpp - what stem.job_start reads of its source, and the decode its worker runs (Q050-010)
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

#include "ControlStemSource.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include <QFile>
#include <QFileInfo>

#include <sndfile.h>

#include "SampleBuffer.h"
#include "SampleBufferResample.h"
#include "StemSeparation/StemTypes.h"

namespace lmms::control
{

namespace
{

//! Whether a source at the model's rate is at least one frame and still fits a job's int frame count - checked
//! before the resampler allocates for it, so a pathological source rate cannot ask for billions of frames.
bool fitsAtModelRate(qint64 sourceFrames, int sourceRate)
{
	const double frames = static_cast<double>(sourceFrames) * StemModelSampleRate / std::max(1, sourceRate);
	return frames >= 1.0 && frames <= static_cast<double>(std::numeric_limits<int>::max());
}

QString undecodableMessage(const QString& source)
{
	return QStringLiteral("could not decode '%1' as stereo audio (the decoders this "
		"build carries are the browser's own; a mono or unsupported file cannot be split)").arg(source);
}

QString unfitMessage(const QString& source)
{
	return QStringLiteral("'%1' is too long or too short to split at the model's %2 Hz").arg(source).arg(StemModelSampleRate);
}

bool probeHeader(const QString& path, qint64* frames, int* rate)
{
	SF_INFO info{};
	SNDFILE* file = sf_open(QFile::encodeName(path).constData(), SFM_READ, &info);
	if (file == nullptr) { return false; }
	sf_close(file);
	if (info.frames <= 0 || info.samplerate <= 0) { return false; }
	*frames = static_cast<qint64>(info.frames);
	*rate = info.samplerate;
	return true;
}

//! A format the browser's decoders read without libsndfile, whose header gives no length: DrumSynth (.ds) synthesises
//! the whole sample (SampleDecoder.cpp decodeSampleDS; its parser state is thread local), and Ogg Vorbis where
//! libsndfile was built without it.
bool decodedOnlyByTheWorker(const QString& source)
{
	const QString suffix = QFileInfo(source).suffix().toLower();
	return suffix == QLatin1String("ds") || suffix == QLatin1String("ogg") || suffix == QLatin1String("oga");
}

} // namespace

bool stemSourceLength(const QString& source, qint64* frames, int* rate, QString* error)
{
	if (!probeHeader(source, frames, rate))
	{
		// Nothing is decoded here: a format with a decoder of its own is the worker's to decode, and its length is
		// unknown until then; anything else libsndfile cannot open is refused now.
		if (!decodedOnlyByTheWorker(source))
		{
			*error = undecodableMessage(source);
			return false;
		}
		*frames = 0;
		*rate = StemModelSampleRate;
		return true;
	}
	if (!fitsAtModelRate(*frames, *rate))
	{
		*error = unfitMessage(source);
		return false;
	}
	return true;
}

int stemFramesAtModelRate(qint64 frames, int rate)
{
	return static_cast<int>(std::llround(static_cast<double>(frames) * StemModelSampleRate / std::max(1, rate)));
}

std::shared_ptr<const SampleBuffer> stemDecodeAtModelRate(const QString& source, const std::atomic<bool>& cancel, QString* error)
{
	const auto decoded = SampleBuffer::fromFile(source);
	if (cancel.load()) { return nullptr; }
	if (!decoded || decoded->empty())
	{
		*error = undecodableMessage(source);
		return nullptr;
	}
	if (!fitsAtModelRate(static_cast<qint64>(decoded->size()), static_cast<int>(decoded->sampleRate())))
	{
		*error = unfitMessage(source);
		return nullptr;
	}
	// 0.5.0 (engine audit E27): a source at another rate is resampled to the model's own, offline, instead of refused.
	return resampleBuffer(decoded, StemModelSampleRate);
}

} // namespace lmms::control
