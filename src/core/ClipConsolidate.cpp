/*
 * ClipConsolidate.cpp - R3.2: render the clips of a sample-track region to one audio file
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
 *
 */

#include "ClipConsolidate.h"

#include <algorithm>
#include <cmath>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <sndfile.h>

#include "AudioEngine.h"
#include "Engine.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"

namespace lmms
{

namespace
{

constexpr f_cnt_t BlockFrames = 512;

//! Adds \a clip's audio into \a mix, which starts at tick \a start. The clip starts where
//! the arrangement starts it (SampleTrack::play): at its start, or later by a positive
//! content offset, from the source frame under that tick to the one under its end.
void mixClip(SampleClip* clip, tick_t start, double framesPerTick, std::vector<SampleFrame>* mix)
{
	const tick_t clipStart = clip->startPosition().getTicks();
	const tick_t from = std::max(clipStart, clipStart + clip->startTimeOffset().getTicks());
	const tick_t to = clip->endPosition().getTicks();
	if (from >= to) { return; }
	const SampleWindow window{clip->sourceFrameAt(TimePos(from)), clip->sourceFrameAt(clip->endPosition())};
	if (window.sourceIn >= window.sourceOut) { return; }

	SamplePlayHandle handle(clip, window);
	handle.setIgnoresTrackMute(true);
	const auto at = static_cast<f_cnt_t>(std::llround((from - start) * framesPerTick));
	const auto span = static_cast<f_cnt_t>(std::llround((to - from) * framesPerTick));
	const f_cnt_t room = static_cast<f_cnt_t>(mix->size()) > at ? static_cast<f_cnt_t>(mix->size()) - at : 0;
	const f_cnt_t length = std::min({span, room, handle.totalFrames()});

	std::vector<SampleFrame> block(BlockFrames);
	for (f_cnt_t done = 0; done < length; done += BlockFrames)
	{
		std::fill(block.begin(), block.end(), SampleFrame(0.0f, 0.0f));
		handle.play(std::span<SampleFrame>(block));
		const f_cnt_t count = std::min(BlockFrames, length - done);
		for (f_cnt_t f = 0; f < count; ++f)
		{
			(*mix)[at + done + f][0] += block[f][0];
			(*mix)[at + done + f][1] += block[f][1];
		}
	}
}

bool writeWav(const QString& path, const std::vector<SampleFrame>& mix, int sampleRate, QString* error)
{
	if (!QDir().mkpath(QFileInfo(path).absolutePath()))
	{
		*error = QStringLiteral("cannot create the directory for %1").arg(path);
		return false;
	}
	SF_INFO info{};
	info.samplerate = sampleRate;
	info.channels = 2;
	info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	SNDFILE* file = sf_open(path.toLocal8Bit().constData(), SFM_WRITE, &info);
	if (file == nullptr)
	{
		*error = QStringLiteral("cannot write %1: %2").arg(path, QString::fromLocal8Bit(sf_strerror(nullptr)));
		return false;
	}
	std::vector<float> interleaved(mix.size() * 2);
	for (std::size_t i = 0; i < mix.size(); ++i)
	{
		interleaved[2 * i] = mix[i][0];
		interleaved[2 * i + 1] = mix[i][1];
	}
	const auto frames = static_cast<sf_count_t>(mix.size());
	const bool written = sf_writef_float(file, interleaved.data(), frames) == frames;
	const bool closed = sf_close(file) == 0;
	if (!written || !closed)
	{
		QFile::remove(path);
		*error = QStringLiteral("writing %1 failed").arg(path);
		return false;
	}
	return true;
}

} // namespace

ClipConsolidate::Result ClipConsolidate::render(const std::vector<SampleClip*>& clips, tick_t start,
	tick_t end, const QString& outPath)
{
	Result result;
	if (end <= start)
	{
		result.error = QStringLiteral("an empty range has nothing to consolidate");
		return result;
	}
	result.sampleRate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	const double framesPerTick = Engine::framesPerTick(result.sampleRate);
	std::vector<SampleFrame> mix(static_cast<std::size_t>(std::llround((end - start) * framesPerTick)),
		SampleFrame(0.0f, 0.0f));
	for (SampleClip* clip : clips) { mixClip(clip, start, framesPerTick, &mix); }
	if (!writeWav(outPath, mix, result.sampleRate, &result.error)) { return result; }
	result.ok = true;
	result.path = outPath;
	result.frames = static_cast<qint64>(mix.size());
	return result;
}

} // namespace lmms
