/*
 * RetroAudioCapture.cpp - retrospective AUDIO capture: the ring's producer and
 *                         consumer sides, and the take writer (0.3.0, feature
 *                         row 16).
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

#include "RetroAudioCapture.h"

#include <algorithm>

#include <QElapsedTimer>

#include <sndfile.h>

namespace lmms
{

namespace
{

//! How long the consumer waits for the audio thread to acknowledge a snapshot
//! request before it reads the window anyway. copyOut() is publication-sequence
//! guarded (include/RetroAudioRing.h), so an acknowledgement that arrives late -
//! or never, because a silent engine pushes nothing - cannot tear the copy. The
//! bound is what keeps a control handler from waiting on the audio thread: the
//! same 5 ms the MIDI capture's consumer uses for the same handshake.
constexpr int QuiesceTimeoutMs = 5;

} // namespace


RetroAudioCapture::RetroAudioCapture() :
	m_ring(DefaultCapacityFrames)
{
}


void RetroAudioCapture::push(const SampleFrame* frames, f_cnt_t count) noexcept
{
	// The whole cost of a DISARMED engine, once per rendered period.
	if (!m_armed.load(std::memory_order_relaxed))
	{
		return;
	}
	if (frames == nullptr || count == 0)
	{
		return;
	}
	m_ring.push(frames, static_cast<std::size_t>(count));
}


RetroAudioCapture::Window RetroAudioCapture::takeWindow() const
{
	// const_cast: the consumer side of the ring moves the read-side indices -
	// taking a window is a consumer action, and this object is being read
	// through a const accessor by a command handler that is allowed to consume.
	RetroAudioRing& ring = const_cast<RetroAudioRing&>(m_ring);

	Window window;
	window.frames.resize(ring.bufferedCount());

	ring.beginSnapshot();
	QElapsedTimer timer;
	timer.start();
	while (!ring.writerIdle() && timer.elapsed() < QuiesceTimeoutMs)
	{
		// Bounded spin: the producer acknowledges inside its next push().
	}
	window.quiesced = ring.writerIdle();
	const std::size_t copied = ring.copyOut(window.frames.data(), window.frames.size());
	ring.endSnapshot();

	window.frames.resize(copied);
	if (copied == 0 && ring.bufferedCount() > 0)
	{
		// Frames retained that no consistent window could be read for. Reported
		// rather than silently turned into "nothing was captured".
		window.refused = ring.bufferedCount();
	}
	return window;
}


namespace
{

//! Why a take request cannot be honoured, or an empty string when it can. Split
//! out of writeRetroAudioTake() so neither function carries both the validation
//! and the write (the complexity ratchet measures a function).
QString takeRequestProblem(const QString& filePath, std::size_t frames, int sampleRate)
{
	if (filePath.isEmpty())
	{
		return QStringLiteral("no file path");
	}
	if (frames == 0)
	{
		// A take with no audio in it is not a take. Refused rather than written
		// as a zero-length WAV a user would then have to diagnose.
		return QStringLiteral("the retained window holds no frames: nothing was captured "
			"while the capture was armed");
	}
	if (sampleRate <= 0)
	{
		return QStringLiteral("invalid sample rate %1").arg(sampleRate);
	}
	return QString();
}

//! The write itself: clamp, encode, flush, close, and report what libsndfile
//! accepted (measured, never assumed).
bool writeWindow(const QString& filePath, const std::vector<SampleFrame>& window,
	int sampleRate, std::uint64_t* framesWritten, QString* error)
{
	SF_INFO info{};
	info.samplerate = sampleRate;
	// The engine's input path is the stereo bus (SampleFrame is two floats,
	// include/SampleFrame.h), so a retrospective take is a stereo file. The
	// forward recorder demuxes ONE channel into a mono file because it records a
	// route; this records what the input path held.
	info.channels = 2;
	info.format = SF_FORMAT_WAV | SF_FORMAT_PCM_24;

	SNDFILE* sf = sf_open(filePath.toUtf8().constData(), SFM_WRITE, &info);
	if (sf == nullptr)
	{
		*error = QStringLiteral("cannot open %1 for writing: %2")
			.arg(filePath, QString::fromUtf8(sf_strerror(nullptr)));
		return false;
	}

	// Clamped in C++, not through SFC_SET_CLIPPING, for the reason
	// TrackRecorder::writerLoop documents at length: libsndfile's own clipping
	// changes the conversion of in-range negative samples too, and a take that
	// was never out of range must come back bit-identical.
	std::vector<sample_t> interleaved(window.size() * 2);
	for (std::size_t i = 0; i < window.size(); ++i)
	{
		interleaved[i * 2] = std::clamp(window[i].left(), sample_t{-1}, sample_t{1});
		interleaved[i * 2 + 1] = std::clamp(window[i].right(), sample_t{-1}, sample_t{1});
	}

	const auto written = sf_writef_float(sf, interleaved.data(),
		static_cast<sf_count_t>(window.size()));
	sf_write_sync(sf);
	sf_close(sf);

	if (written < 0 || static_cast<std::size_t>(written) != window.size())
	{
		*error = QStringLiteral("wrote %1 of %2 frames to %3")
			.arg(written).arg(window.size()).arg(filePath);
		return false;
	}
	*framesWritten = static_cast<std::uint64_t>(written);
	return true;
}

} // namespace


bool writeRetroAudioTake(const QString& filePath, const std::vector<SampleFrame>& window,
	int sampleRate, std::uint64_t* framesWritten, QString* error)
{
	if (framesWritten != nullptr) { *framesWritten = 0; }
	if (error != nullptr) { error->clear(); }

	const QString problem = takeRequestProblem(filePath, window.size(), sampleRate);
	if (!problem.isEmpty())
	{
		if (error != nullptr) { *error = problem; }
		return false;
	}
	return writeWindow(filePath, window, sampleRate, framesWritten, error);
}


} // namespace lmms
