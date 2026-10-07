/*
 * SessionAudioCellPlaybackTest.cpp - a launched AUDIO cell sounds its clip (0.5.0 L02, WF-055 / WF-054)
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

/*! "Ready to launch": an audio cell on a sample track's column, launched through the agent surface on the
 *  Dummy device (the test drives the periods), plays its clip through the track - the engine's output is the
 *  clip, frame for frame, from its top, looping round its end - and a cell recorded with session.record_slot and
 *  landed plays back the take it recorded. Before this path existed a launched audio cell only silenced its
 *  column: the same binary built without it (the recording step alone) renders silence and fails both cases. */

#include <QtTest>

#include <QJsonObject>
#include <QTemporaryDir>

#include <cmath>
#include <cstdint>
#include <vector>

#include <sndfile.h>

#include "AllocationProbe.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "SampleFrame.h"
#include "SampleTrack.h"
#include "SessionAudioCells.h"
#include "SessionModel.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! Clip frame @a n: distinct for every frame of the clip, well inside full scale.
SampleFrame clipFrame(std::size_t n)
{
	return SampleFrame(static_cast<float>(n % 997) / 2048.0f, -static_cast<float>(n % 1009) / 2048.0f);
}

//! Two steps of a 24-bit file (libsndfile writes with 2^23 - 1 and reads with 2^23), plus float mixing.
constexpr float Tolerance = 4.0f / 8388608.0f;

bool writeFloatWav(const QString& path, const std::vector<SampleFrame>& frames, int rate)
{
	SF_INFO info{};
	info.channels = 2;
	info.samplerate = rate;
	info.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
	SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_WRITE, &info);
	if (file == nullptr) { return false; }
	const sf_count_t written = sf_writef_float(file, &frames[0][0], static_cast<sf_count_t>(frames.size()));
	sf_close(file);
	return written == static_cast<sf_count_t>(frames.size());
}

std::vector<SampleFrame> readWav(const QString& path)
{
	SF_INFO info{};
	SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
	if (file == nullptr) { return {}; }
	std::vector<SampleFrame> frames(static_cast<std::size_t>(info.frames));
	sf_readf_float(file, &frames[0][0], info.frames);
	sf_close(file);
	return frames;
}

} // namespace

class SessionAudioCellPlaybackTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test drives the periods itself
		ControlRegistry::setReady(true);
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Song* song = Engine::getSong();
		song->clearProject();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 2}}).ok);
		QVERIFY(run(QStringLiteral("session.set_quantisation"), {{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		for (int i = 0; i < 4; ++i) { period(); }
	}

	//! A file in a cell: launched, the output is the clip from its top and round its end again.
	void aLaunchedAudioCellPlaysItsClipThroughItsTrack()
	{
		const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		std::vector<SampleFrame> clip(1500);
		for (std::size_t i = 0; i < clip.size(); ++i) { clip[i] = clipFrame(i); }
		const QString path = m_dir.filePath(QStringLiteral("clip.wav"));
		QVERIFY(writeFloatWav(path, clip, rate));
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), path}}).ok);
		launchAndCompare(clip, 0);
	}

	//! The take a cell recorded, landed, is what the cell plays when it is launched again.
	void aRecordedCellPlaysBackItsTake()
	{
		QVERIFY(run(QStringLiteral("session.record_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 1},
			{QStringLiteral("bars"), 1}}).ok);
		for (int i = 0; i < 6000 && status() != QStringLiteral("done"); ++i) { period(); }
		QCOMPARE(status(), QStringLiteral("done"));
		const QString path = m_dir.filePath(QStringLiteral("take.wav"));
		const ControlResult landed = run(QStringLiteral("session.record_slot_land"), {{QStringLiteral("file"), path}});
		QVERIFY2(landed.ok, qPrintable(landed.errorMessage));
		for (int i = 0; i < 8; ++i) { period(); }  // the recorded cell has stopped; the column is silent again
		launchAndCompare(readWav(path), 1);
	}

	//! The realtime rule: the renderer's audio-thread entry point allocates nothing while a cell plays,
	//! across launches and the clip's wrap (AllocationProbe.h, the SessionSchedulerTest pattern).
	void theCellRendererDoesNotAllocate()
	{
		const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		std::vector<SampleFrame> clip(700);
		for (std::size_t i = 0; i < clip.size(); ++i) { clip[i] = clipFrame(i + 1); }
		const QString path = m_dir.filePath(QStringLiteral("probe.wav"));
		QVERIFY(writeFloatWav(path, clip, rate));
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("type"), QStringLiteral("audio")}, {QStringLiteral("source"), path}}).ok);
		QVERIFY(run(QStringLiteral("session.launch_slot"), {{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0},
			{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		for (int i = 0; i < 4; ++i) { period(); }
		auto* track = dynamic_cast<SampleTrack*>(Engine::getSong()->tracks().front());
		QVERIFY(track != nullptr);
		SessionCellPlayHandle probe(track);
		std::vector<SampleFrame> buffer(static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod()));
		lmms::test::tlCountAllocations = true;
		lmms::test::resetAllocationCount();
		for (int i = 0; i < 2000; ++i) { probe.play(buffer); }
		const std::uint64_t allocations = lmms::test::tlAllocationCount;
		lmms::test::tlCountAllocations = false;
		QVERIFY2(allocations == 0, qPrintable(QStringLiteral("the cell renderer allocated %1 times").arg(allocations)));
		QCOMPARE(probe.framesRendered(), std::uint64_t{2000} * buffer.size());
		QVERIFY(run(QStringLiteral("session.stop_all")).ok);
		period();
	}

private:
	//! One period of input (a ramp, so a take holds something), rendered; its output is appended to m_out.
	void period()
	{
		AudioEngine* engine = Engine::audioEngine();
		const f_cnt_t fpp = engine->framesPerPeriod();
		std::vector<SampleFrame> block(static_cast<std::size_t>(fpp));
		for (f_cnt_t f = 0; f < fpp; ++f) { block[static_cast<std::size_t>(f)] = clipFrame(m_fed++ * 7 + 3); }
		engine->pushInputFrames(block.data(), fpp);
		const std::span<const SampleFrame> out = engine->renderNextPeriod();
		m_out.insert(m_out.end(), out.begin(), out.end());
	}

	QString status() const
	{
		return run(QStringLiteral("session.record_slot_status")).result.value(QStringLiteral("state")).toString();
	}

	void launchAndCompare(const std::vector<SampleFrame>& clip, int scene)
	{
		QVERIFY(!clip.empty());
		m_out.clear();
		const ControlResult launched = run(QStringLiteral("session.launch_slot"), {{QStringLiteral("track"), 0},
			{QStringLiteral("scene"), scene}, {QStringLiteral("quantisation"), QStringLiteral("none")}});
		QVERIFY2(launched.ok, qPrintable(launched.errorMessage));
		const std::size_t enough = 2 * clip.size() + 8 * static_cast<std::size_t>(Engine::audioEngine()->framesPerPeriod());
		while (m_out.size() < enough) { period(); }
		std::size_t first = 0;
		while (first < m_out.size() && std::fabs(m_out[first].left()) + std::fabs(m_out[first].right()) == 0.0f) { ++first; }
		QVERIFY2(first < m_out.size(), "the launched audio cell rendered silence");
		std::size_t from = 0;  // the clip may open on silent frames: line the first sound up with the clip's
		while (from < clip.size() && std::fabs(clip[from].left()) + std::fabs(clip[from].right()) == 0.0f) { ++from; }
		QVERIFY(first >= from);
		first -= from;
		QVERIFY(first + 2 * clip.size() <= m_out.size());
		for (std::size_t i = 0; i < 2 * clip.size(); ++i)  // twice round: from the top, and looping
		{
			const SampleFrame& want = clip[i % clip.size()];
			const SampleFrame& got = m_out[first + i];
			if (std::fabs(got.left() - want.left()) > Tolerance || std::fabs(got.right() - want.right()) > Tolerance)
			{
				QFAIL(qPrintable(QStringLiteral("output frame %1 is (%2, %3), the clip's frame %4 is (%5, %6)").arg(i)
					.arg(got.left()).arg(got.right()).arg(i % clip.size()).arg(want.left()).arg(want.right())));
			}
		}
		QVERIFY(run(QStringLiteral("session.stop_all")).ok);
		period();
	}

	QTemporaryDir m_dir;
	std::uint64_t m_fed = 0;
	std::vector<SampleFrame> m_out;
};

QTEST_GUILESS_MAIN(SessionAudioCellPlaybackTest)
#include "SessionAudioCellPlaybackTest.moc"
