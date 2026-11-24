/*
 * SampleClipSourceSwapTest.cpp - replacing a playing clip's source restarts its playback
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

// BUG new-p9-402: a live SamplePlayHandle snapshots the clip's window and length at construction
// but reads the clip's Sample through a pointer. SampleClip::setSampleBuffer() and replaceSample()
// swap that Sample in place, so without retiring the track's handles (and clearing the playing
// latch, which keeps SampleTrack::play from building a new one) the stale handle renders the new
// buffer through the old window - wrong audio, silence, or (pitch-preserving path) reads past the
// end of a shorter buffer. setSampleFile() already retires them; these two must as well.
//
// Deterministic: no render thread interaction is waited on. The playing latch is the observable
// SampleTrack::play itself consults, and the removal sweeps the not-yet-adopted handle list too.

#include <QtTest>

#include <memory>
#include <vector>

#include "AudioEngine.h"
#include "Engine.h"
#include "PlayHandle.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "SampleWindow.h"
#include "Song.h"
#include "TimePos.h"

using namespace lmms;

namespace
{

constexpr f_cnt_t kPeriod = 1024;

std::shared_ptr<SampleBuffer> constantBuffer(f_cnt_t frames, int rate, float value)
{
	const std::vector<SampleFrame> data(static_cast<std::size_t>(frames), SampleFrame(value, value));
	return std::make_shared<SampleBuffer>(data.data(), data.size(), rate);
}

} // namespace

class SampleClipSourceSwapTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void setSampleBufferRestartsLivePlayback()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::getSong());
		auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setSampleBuffer(constantBuffer(4 * rate, rate, 0.5f));

		QVERIFY(track.play(TimePos(0), kPeriod, 0, -1));
		QVERIFY(clip->isPlaying());
		clip->setSampleBuffer(constantBuffer(rate / 2, rate, 0.25f));
		QVERIFY2(!clip->isPlaying(), "setSampleBuffer left the stale handle playing");
		QVERIFY(track.play(TimePos(0), kPeriod, 0, -1));  // the next pass starts a fresh handle

		Engine::audioEngine()->removePlayHandlesOfTypes(&track, PlayHandle::Type::SamplePlayHandle);
	}

	void replaceSampleRestartsLivePlayback()
	{
		const int rate = Engine::audioEngine()->outputSampleRate();
		SampleTrack track(Engine::getSong());
		auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->setSampleBuffer(constantBuffer(4 * rate, rate, 0.5f));

		QVERIFY(track.play(TimePos(0), kPeriod, 0, -1));
		QVERIFY(clip->isPlaying());
		clip->replaceSample(constantBuffer(rate / 4, rate, 0.25f), SampleWindow());
		QVERIFY2(!clip->isPlaying(), "replaceSample left the stale handle playing");
		QVERIFY(track.play(TimePos(0), kPeriod, 0, -1));

		Engine::audioEngine()->removePlayHandlesOfTypes(&track, PlayHandle::Type::SamplePlayHandle);
	}
};

QTEST_GUILESS_MAIN(SampleClipSourceSwapTest)
#include "SampleClipSourceSwapTest.moc"
