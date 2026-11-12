/*
 * SampleClipTransientRepeatSafetyTest.cpp - Repeat / Gate render safety (Q050-162)
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public
 * License along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 *
 */

/*! The review-r1 regressions of the transient Repeat / Gate modes, on the
 *  same fixture as SampleClipTransientRepeatTest:
 *   * the published transient table is never written while a reader holds
 *     it - deterministically through a pinned slot, and under a reader thread
 *     copying while the control thread publishes;
 *   * the render applies the Sample's amplification and reads a reversed
 *     source backwards, as every other render path does;
 *   * a pass that starts mid-clip renders what the whole pass renders.
 */

#include <QtTest>

#include <QDomDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"
#include "TransientRepeat.h"
#include "WarpMarkers.h"

using namespace lmms;

namespace
{

constexpr int kBursts = 4;
constexpr double kBurstSpacing = 0.5;
constexpr double kBurstDecay = 0.45;
constexpr f_cnt_t kManyBase = 10000;

//! The fixture of SampleClipTransientRepeatTest: four decaying 1 kHz bursts, one every 0.5 s.
SampleClip* makeTransientClip(SampleTrack& track, int rate)
{
	auto* clip = dynamic_cast<SampleClip*>(track.createClip(TimePos(0)));
	Q_ASSERT(clip != nullptr);
	std::vector<SampleFrame> data(static_cast<std::size_t>(kBursts * kBurstSpacing * rate), SampleFrame(0.0f, 0.0f));
	for (std::size_t f = 0; f < data.size(); ++f)
	{
		const double t = std::fmod(static_cast<double>(f) / rate, kBurstSpacing);
		if (t >= kBurstDecay) { continue; }
		const auto value = static_cast<sample_t>(0.8 * (1.0 - t / kBurstDecay) * std::sin(6.28318530717958647692 * 1000.0 * t));
		data[f] = SampleFrame(value, value);
	}
	clip->setSampleBuffer(std::make_shared<SampleBuffer>(data.data(), data.size(), rate));
	return clip;
}

void applyHalfSpeedWarp(SampleClip& clip, int sourceRate)
{
	const auto frames = static_cast<f_cnt_t>(clip.sample().sampleSize());
	const auto ticks = static_cast<tick_t>(std::llround(2.0 * frames / Engine::framesPerTick(sourceRate)));
	const std::array<WarpMarker, 2> markers{WarpMarker{0, 0}, WarpMarker{frames, ticks}};
	QVERIFY(clip.setWarpMarkers(std::span<const WarpMarker>(markers.data(), markers.size())));
}

std::vector<SampleFrame> renderClip(SampleClip* clip)
{
	SamplePlayHandle handle(clip, clip->sampleWindow());
	const auto total = static_cast<int>(handle.totalFrames());
	std::vector<SampleFrame> rendered;
	std::vector<SampleFrame> buffer(512);
	for (int guard = 0; static_cast<int>(handle.framesDone()) < total && guard < total / 512 + 16; ++guard)
	{
		std::fill(buffer.begin(), buffer.end(), SampleFrame(0.0f, 0.0f));
		handle.play(std::span<SampleFrame>(buffer));
		rendered.insert(rendered.end(), buffer.begin(), buffer.end());
	}
	rendered.resize(static_cast<std::size_t>(total));
	return rendered;
}

double peakBetween(const std::vector<SampleFrame>& data, int rate, double from, double to)
{
	double peak = 0.0;
	for (auto f = static_cast<std::size_t>(from * rate); f < static_cast<std::size_t>(to * rate) && f < data.size(); ++f)
	{
		peak = std::max(peak, static_cast<double>(std::fabs(data[f][0])));
	}
	return peak;
}

//! One of the two tables the concurrency test publishes, whole - or the empty
//! table a reader may report when every attempt raced a publish.
bool isWholeTable(const TransientTable& table)
{
	const bool few = table.size() == 3 && table[0] == 1 && table[2] == 3;
	const int last = TransientTable::MaxTransients - 1;
	const bool many = table.size() == TransientTable::MaxTransients && table[0] == kManyBase
		&& table[last] == kManyBase + static_cast<f_cnt_t>(last);
	return few || many || table.size() == 0;
}

void evidence(const char* label, double a, double b = 0.0, double c = 0.0)
{
	std::printf("TRANSIENT_REPEAT_EVIDENCE %-48s %10.4f %10.4f %10.4f\n", label, a, b, c);
	std::fflush(stdout);
}

} // namespace


class SampleClipTransientRepeatSafetyTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		m_rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
	}

	void init() { Engine::getSong()->setTempo(120); }

	void cleanupTestCase() { Engine::destroy(); }

	//! REGRESSION (review r1): a slot a reader has pinned is never written, however
	//! many analyses publish meanwhile - the interleaving "reader loads the slot,
	//! is preempted, two publishes reuse it" made deterministic through pin().
	void aPinnedTableIsNeverOverwrittenByLaterPublishes()
	{
		PublishedTransients published;
		const std::vector<f_cnt_t> first{100, 200, 300};
		published.publish(first);
		const int slot = published.pin();
		QVERIFY(slot >= 0);
		for (f_cnt_t round = 1; round <= 6; ++round)
		{
			const std::vector<f_cnt_t> next{round, round + 1000};
			published.publish(next);
			QVERIFY(published.publishedSlot() != slot);
			QCOMPARE(published.read().size(), 2);
			QCOMPARE(published.read()[0], round);
		}
		const TransientTable& held = published.pinned(slot);
		QCOMPARE(held.size(), 3);
		QCOMPARE(held[0], f_cnt_t{100});
		QCOMPARE(held[2], f_cnt_t{300});
		published.unpin(slot);
	}

	//! REGRESSION (review r1): a reader thread copying the table while the
	//! control thread publishes as fast as it can only ever sees a whole table -
	//! one of the two patterns, never a mix - and is never left waiting.
	void concurrentPublishesNeverHandAReaderAMixedTable()
	{
		PublishedTransients published;
		const std::vector<f_cnt_t> few{1, 2, 3};
		std::vector<f_cnt_t> many;
		for (f_cnt_t f = 0; f < TransientTable::MaxTransients; ++f) { many.push_back(kManyBase + f); }
		published.publish(few);
		std::atomic<bool> stop{false};
		std::atomic<long> reads{0};
		std::atomic<long> mixed{0};
		std::thread reader([&] {
			while (!stop.load())
			{
				const TransientTable table = published.read();
				reads.fetch_add(1);
				if (!isWholeTable(table)) { mixed.fetch_add(1); }
			}
		});
		for (int pass = 0; pass < 20000; ++pass) { published.publish(pass % 2 == 0 ? many : few); }
		stop.store(true);
		reader.join();
		evidence("concurrent reads / mixed tables", static_cast<double>(reads.load()), static_cast<double>(mixed.load()));
		QVERIFY(reads.load() > 0);
		QCOMPARE(mixed.load(), 0L);
	}

	//! REGRESSION (review r1): the Sample's amplification scales the Repeat render
	//! exactly as it scales every other render path.
	void theAmplificationScalesTheRender()
	{
		SampleTrack track(Engine::getSong());
		auto* unity = makeTransientClip(track, m_rate);
		applyHalfSpeedWarp(*unity, m_rate);
		unity->setWarpStretchMode(WarpStretchMode::Repeat);
		auto* half = makeTransientClip(track, m_rate);
		applyHalfSpeedWarp(*half, m_rate);
		half->sample().setAmplification(0.5f);
		half->setWarpStretchMode(WarpStretchMode::Repeat);
		const std::vector<SampleFrame> a = renderClip(unity);
		const std::vector<SampleFrame> b = renderClip(half);
		QCOMPARE(a.size(), b.size());
		double worst = 0.0;
		for (std::size_t f = 0; f < a.size(); ++f) { worst = std::max(worst, std::fabs(0.5 * a[f][0] - b[f][0])); }
		evidence("max |0.5 * unity - amplified|", worst, peakBetween(b, m_rate, 0.0, 4.0));
		QVERIFY(worst < 1e-6);
		QVERIFY(peakBetween(b, m_rate, 0.0, 4.0) > 0.3);
	}

	//! REGRESSION (review r1): a reversed clip plays its audio backwards in the
	//! transient modes too - play frame p is buffer frame size - 1 - p - and the
	//! transients are those of the reversed audio.
	void aReversedClipPlaysBackwards()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeTransientClip(track, m_rate);
		applyHalfSpeedWarp(*clip, m_rate);
		clip->sample().setReversed(true);
		clip->setTransientRepeatRate(TransientRepeatRate::freeInterval(TransientRepeatRate::MaxIntervalMs));
		clip->setWarpStretchMode(WarpStretchMode::Gate);
		const TransientTable table = clip->transientTable();
		const auto size = static_cast<f_cnt_t>(clip->sample().sampleSize());
		// The first segment plays from the window head up to the first transient.
		const f_cnt_t firstSegment = table.size() > 0 ? table[0] : size;
		const std::vector<SampleFrame> rendered = renderClip(clip);
		const SampleFrame* buffer = clip->sample().data();
		double worst = 0.0;
		double forward = 0.0;
		for (f_cnt_t p = 0; p < firstSegment; ++p)
		{
			worst = std::max(worst, static_cast<double>(std::fabs(rendered[p][0] - buffer[size - 1 - p][0])));
			forward = std::max(forward, static_cast<double>(std::fabs(rendered[p][0] - buffer[p][0])));
		}
		evidence("reversed: first segment frames / max |render - reversed| / max |render - forward|",
			static_cast<double>(firstSegment), worst, forward);
		QVERIFY(firstSegment > 1000);
		QVERIFY(worst < 1e-6);
		QVERIFY(forward > 0.1);
	}

	//! A pass that starts mid-clip (a seek, a loop) renders what the whole-clip
	//! pass renders from that point: the segment it starts inside reads from its
	//! own transient, before the pass's first frame.
	void aPassStartingMidClipMatchesTheWholeRender()
	{
		SampleTrack track(Engine::getSong());
		auto* clip = makeTransientClip(track, m_rate);
		applyHalfSpeedWarp(*clip, m_rate);
		clip->setTransientRepeatRate(TransientRepeatRate::freeInterval(300.0));
		clip->setWarpStretchMode(WarpStretchMode::Repeat);
		const std::vector<SampleFrame> whole = renderClip(clip);
		const auto in = static_cast<f_cnt_t>(0.75 * m_rate);  // inside the second burst's segment
		SamplePlayHandle handle(clip, SampleWindow{in, clip->sampleWindow().sourceOut});
		std::vector<SampleFrame> part(512 * 64);
		for (std::size_t from = 0; from < part.size(); from += 512)
		{
			handle.play(std::span<SampleFrame>(part.data() + from, 512));
		}
		const std::size_t offset = 2 * in;  // the half-speed warp
		double worst = 0.0;
		std::size_t firstOff = part.size();
		for (std::size_t f = 0; f < part.size(); ++f)
		{
			const double d = std::fabs(part[f][0] - whole[offset + f][0]);
			if (d > 1e-4 && firstOff == part.size()) { firstOff = f; }
			worst = std::max(worst, d);
		}
		std::printf("TRANSIENT_REPEAT_EVIDENCE mid-clip first difference at frame %zu (of %zu)\n", firstOff, part.size());
		evidence("mid-clip pass: max |part - whole|", worst, peakBetween(part, m_rate, 0.0, 0.7));
		QVERIFY(worst < 1e-4);
		QVERIFY(peakBetween(part, m_rate, 0.0, 0.7) > 0.5);
	}

private:
	int m_rate = 44100;
};

QTEST_GUILESS_MAIN(SampleClipTransientRepeatSafetyTest)
#include "SampleClipTransientRepeatSafetyTest.moc"
