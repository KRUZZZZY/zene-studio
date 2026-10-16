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
#include <QSaveFile>

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

//! Room past the count read before the snapshot began, for the periods the producer can still
//! push before it acknowledges: copyOut() copies the OLDEST frames that fit, so a destination
//! sized to the earlier count would leave out the newest. 16384 frames is several periods.
constexpr std::size_t SnapshotSlackFrames = 16384;

//! The retained window and the single rate it was recorded at, published as
//! ONE value: a discard generation (high half) and the sample rate (low half).
//! The producer bumps the generation each time a rate transition drops a stale
//! window, so the consumer can require the value to be unchanged around its
//! copy - frames and label as one validated snapshot (Luna cluster
//! new-p12-200 rework).
constexpr int RateTagGenerationBits = 32;

//! Reads of tag/copy/tag before the snapshot is declared unsettled. Each
//! attempt is bounded work off the audio thread; a transition that keeps
//! landing across the copy is rarer than one, and refusing after a few is
//! honest where relabelling never would be.
constexpr int RateTagValidationAttempts = 4;

constexpr std::uint64_t packTag(std::uint32_t generation, int sampleRate)
{
	return (static_cast<std::uint64_t>(generation) << RateTagGenerationBits)
		| static_cast<std::uint32_t>(sampleRate);
}

constexpr int rateOfTag(std::uint64_t tag)
{
	return static_cast<int>(static_cast<std::uint32_t>(tag));
}

constexpr std::uint32_t generationOfTag(std::uint64_t tag)
{
	return static_cast<std::uint32_t>(tag >> RateTagGenerationBits);
}

//! Copy the retained frames so that they and the rate tag labelling them are
//! one validated snapshot (Luna cluster new-p12-200 rework): read the tag,
//! copy under the ring's publication protocol, read the tag again. A tag that
//! moved between the reads means the producer re-segmented the window across
//! the copy - reachable exactly when the quiesce wait timed out and the engine
//! is still live through the read - so retry, and let the caller refuse a read
//! that never settles rather than pair copied old-rate frames with a rate they
//! were not recorded at.
bool copyTagValidated(const std::atomic<std::uint64_t>& rateTag, RetroAudioRing& ring,
	std::vector<SampleFrame>& dst, std::size_t& copied, std::uint64_t& tag)
{
	for (int attempt = 0; attempt < RateTagValidationAttempts; ++attempt)
	{
		tag = rateTag.load(std::memory_order_acquire);
		copied = ring.copyOut(dst.data(), dst.size());
		if (rateTag.load(std::memory_order_acquire) == tag)
		{
			return true;
		}
	}
	return false;
}

} // namespace


#ifdef LMMS_TESTING
std::atomic<RetroAudioCapture::PushPauseTestHook> RetroAudioCapture::s_pushPauseTestHook{nullptr};
std::atomic<void*> RetroAudioCapture::s_pushPauseTestContext{nullptr};
#endif

RetroAudioCapture::RetroAudioCapture() :
	m_ring(DefaultCapacityFrames)
{
}


void RetroAudioCapture::push(const SampleFrame* frames, f_cnt_t count, int sampleRate) noexcept
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
	// A frame cannot be labelled with a non-positive rate; skip rather than
	// poison the window's rate bookkeeping.
	if (sampleRate <= 0)
	{
		return;
	}
	// The window must hold frames recorded at ONE sample rate (Luna cluster
	// new-p12-200, BUG 5). A transition drops the stale frames through the
	// ring's producer-side discard - discardRetained() is realtime-safe and
	// coordinated through the publication sequence; the consumer-only reset()
	// must not run here - and publishes the new rate together with a fresh
	// generation in ONE atomic tag, so takeWindow() can validate the frames it
	// copies against the label it gives them. While a snapshot is in flight the
	// transition is DEFERRED to the next period after the consumer releases the
	// ring (push() still drops and acknowledges this period): a consumer whose
	// quiesce wait timed out can be reading the window right now, and the
	// frames and rate it reads must be one snapshot (new-p12-200 rework).
	const std::uint64_t tag = m_rateTag.load(std::memory_order_relaxed);
	// Observe the snapshot state ONCE for this period's decision. A transition
	// that finds a snapshot in flight is deferred; the period is then dropped
	// (never admitted to the old-rate ring) even if the snapshot releases
	// before admission (Luna new-p12-200 r3).
	const bool pending = m_ring.snapshotInFlight();

	if (transitionRateIfNeeded(sampleRate, pending, tag))
	{
#ifdef LMMS_TESTING
		notifyPushPauseTestHook();
#endif
		m_ring.dropPeriod(static_cast<std::size_t>(count));
		return;
	}
	m_ring.push(frames, static_cast<std::size_t>(count));
}

bool RetroAudioCapture::transitionRateIfNeeded(int sampleRate, bool pending, std::uint64_t tag) noexcept
{
	const int held = rateOfTag(tag);
	if (held == 0)
	{
		m_rateTag.store(packTag(generationOfTag(tag), sampleRate), std::memory_order_release);
		return false;
	}
	if (held == sampleRate)
	{
		return false;
	}
	if (pending)
	{
		// Deferred: the transition waits for the snapshot to release.
		return true;
	}
	m_ring.discardRetained();
	m_rateTag.store(packTag(generationOfTag(tag) + 1, sampleRate), std::memory_order_release);
	return false;
}


RetroAudioCapture::Window RetroAudioCapture::takeWindow() const
{
	// const_cast: the consumer side of the ring moves the read-side indices -
	// taking a window is a consumer action, and this object is being read
	// through a const accessor by a command handler that is allowed to consume.
	RetroAudioRing& ring = const_cast<RetroAudioRing&>(m_ring);

	Window window;
	window.frames.resize(std::min(ring.capacity(), ring.bufferedCount() + SnapshotSlackFrames));

	ring.beginSnapshot();
	QElapsedTimer timer;
	timer.start();
	while (!ring.writerIdle() && timer.elapsed() < QuiesceTimeoutMs)
	{
		// Bounded spin: the producer acknowledges inside its next push().
	}
	window.quiesced = ring.writerIdle();
	// Inside the handshake, so the count is the one that went with these frames:
	// a capture keeps running while the take is written, and a count read after
	// the write also counts frames this window never contained (Luna cluster 4901).
	window.overwritten = static_cast<std::size_t>(ring.overwrittenCount());
	std::size_t copied = 0;
	std::uint64_t tag = 0;
	const bool validated = copyTagValidated(m_rateTag, ring, window.frames, copied, tag);
	ring.endSnapshot();
	if (!validated)
	{
		// The rate tag moved across every read of the copy - a transition landed
		// inside the snapshot, which is only possible once the quiesce wait timed
		// out with the producer still live (Luna cluster new-p12-200 rework). The
		// frames and the rate are then NOT one validated snapshot: refuse the
		// read rather than label copied old-rate frames with a new rate.
		window.frames.clear();
		window.refused = ring.bufferedCount();
		window.sampleRate = rateOfTag(m_rateTag.load(std::memory_order_relaxed));
		return window;
	}
	// The tag read around the copy is unchanged: every frame this window holds
	// was published under this generation, and the window is homogeneous at the
	// tag's rate - frames and label as one validated snapshot (new-p12-200).
	window.frames.resize(copied);
	window.sampleRate = rateOfTag(tag);
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

	// QSaveFile gives this call a unique sibling temporary and replaces the
	// destination atomically only after the complete WAV has been written.
	QSaveFile output(filePath);
	output.setDirectWriteFallback(false);
	if (!output.open(QIODevice::WriteOnly))
	{
		*error = QStringLiteral("cannot open %1 for writing: %2")
			.arg(filePath, output.errorString());
		return false;
	}
	SNDFILE* sf = sf_open_fd(output.handle(), SFM_WRITE, &info, SF_FALSE);
	if (sf == nullptr)
	{
		*error = QStringLiteral("cannot encode %1: %2")
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
	if (!output.commit())
	{
		*error = QStringLiteral("could not commit the written take to %1: %2")
			.arg(filePath, output.errorString());
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
	// writeWindow() reports through both pointers; either may be null here.
	std::uint64_t written = 0;
	QString why;
	const bool ok = writeWindow(filePath, window, sampleRate, &written, &why);
	if (framesWritten != nullptr) { *framesWritten = written; }
	if (error != nullptr) { *error = why; }
	return ok;
}


} // namespace lmms
