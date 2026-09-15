/*
 * RecordingInputPathTest.cpp - the ARBITRARY INPUT COUNT proved with real takes
 *                               (0.3.0, feature rows 14 and 64)
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

/*
 * WHAT THIS PROVES, AND WITHOUT A SOUND CARD. Feature row 64 asks for "arbitrary
 * input count / multiple simultaneous inputs" and the audit's own measurement of
 * the gap was that a record route took ZERO inputs: multi-track recording existed,
 * but nothing carried more than the stereo bus into it. The property that
 * replaces that is measurable on any box, because it is a property of the ENGINE
 * rather than of a device:
 *
 *   - a recorder built for N routes and N input channels records N takes at once,
 *     and route k's take holds CHANNEL k of the interleaved block it was fed,
 *     sample-exactly (this file reads the WAVs back and compares them);
 *   - the stereo entry point the engine has always used still records the bus,
 *     unchanged, so the wide path is additive rather than a replacement;
 *   - a route pointed outside the width is refused BEFORE a take file is created,
 *     and the refusal is what a caller can act on (armTrack returns false and no
 *     file appears).
 *
 * WHAT IT DOES NOT PROVE, said here rather than implied: that a real interface
 * opens, that a driver grants N channels, or that AudioAlsa's capture thread
 * delivers them. Those are hardware, and the honest report of them is the
 * `capture_capable` / `capture_open` / `capture_reason` triple of
 * record.input_get_state (proved over the socket by tests/control-record-inputs.py)
 * and the sentence in docs/KNOWN-LIMITATIONS.md.
 */

#include "MultiTrackRecorder.h"
#include "AudioInputPath.h"
#include "SampleFrame.h"

#include <QtTest>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include <QFileInfo>
#include <QTemporaryDir>

#include <sndfile.h>

using lmms::AudioInputPath;
using lmms::MultiTrackRecorder;
using lmms::SampleFrame;
using lmms::sample_t;

namespace
{

constexpr int SampleRate = 48000;
constexpr int Routes = 6;
constexpr int Channels = 6;
constexpr lmms::f_cnt_t BlockFrames = 512;
constexpr int Blocks = 3;
constexpr lmms::f_cnt_t TotalFrames = BlockFrames * Blocks;

//! One 24-bit LSB: the tolerance the take read-back is compared with. The signal
//! below is exact dyadic values (multiples of 1/64), so the real round trip is
//! bit-exact; the tolerance is there so a future libsndfile rounding change is a
//! tolerance question rather than a false alarm - and it is a million times
//! tighter than the channel separation, so a route that recorded the WRONG
//! channel cannot pass by accident.
constexpr double OneLsb24 = 1.0 / 16777216.0;

/*! Channel \a channel, frame \a frame, as an exact dyadic value.
 *
 * The channel term separates the channels by 1/8 (three bits) and dominates the
 * frame term, which varies by at most 4/64 - so a take that recorded the wrong
 * channel differs from the expected signal by at least 1/16, which is six orders
 * of magnitude above the tolerance. Every value is a multiple of 1/64 and stays
 * inside [-0.5, 0.5], i.e. exactly representable in 24-bit PCM and untouched by
 * the encoder's clipping (TrackRecorder clamps at +-1).
 */
sample_t channelValue(int channel, lmms::f_cnt_t frame)
{
	const auto channelTerm = static_cast<sample_t>(channel - 3) * 0.125f;
	const auto frameTerm = static_cast<sample_t>(static_cast<int>(frame % 8) - 4) * 0.015625f;
	return channelTerm + frameTerm;
}

//! Read \a path back as mono floats; false when it cannot be read.
bool readTake(const QString& path, std::vector<sample_t>* samples)
{
	SF_INFO info{};
	SNDFILE* file = sf_open(path.toUtf8().constData(), SFM_READ, &info);
	if (file == nullptr)
	{
		return false;
	}
	// Read with the file's own frame count: a take that holds fewer frames than
	// it should must not be compared against a zero-padded buffer.
	std::vector<sample_t> buffer(static_cast<std::size_t>(info.frames > 0 ? info.frames : 0), 0.f);
	const auto read = sf_readf_float(file, buffer.data(),
		static_cast<sf_count_t>(buffer.size()));
	sf_close(file);
	buffer.resize(static_cast<std::size_t>(read > 0 ? read : 0));
	*samples = std::move(buffer);
	return true;
}

} // namespace


class RecordingInputPathTest : public QObject
{
	Q_OBJECT

private slots:

	//! THE FEATURE: N routes, N input channels, N takes, each holding its own
	//! channel of the block that was fed to the engine's audio-thread entry point.
	void everyRouteRecordsItsOwnInputChannel()
	{
		MultiTrackRecorder recorder(Routes, Channels);
		QCOMPARE(recorder.trackCount(), Routes);
		QCOMPARE(recorder.inputChannelCapacity(), Channels);

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		for (int route = 0; route < Routes; ++route)
		{
			const auto path = dir.filePath(QStringLiteral("route%1.wav").arg(route)).toStdString();
			// The prototype's default mapping is exercised by giving each route
			// an EXPLICIT channel here: route k reads channel k.
			QVERIFY2(recorder.armTrack(route, path, SampleRate, route),
				qPrintable(QStringLiteral("route %1 could not be armed").arg(route)));
			QVERIFY(recorder.track(route).isArmed());
		}

		// One interleaved block per period, at the recorder's own width, fed
		// through the audio-thread entry point the engine calls.
		std::vector<sample_t> interleaved(static_cast<std::size_t>(BlockFrames) * Channels);
		for (lmms::f_cnt_t block = 0; block < Blocks; ++block)
		{
			for (lmms::f_cnt_t frame = 0; frame < BlockFrames; ++frame)
			{
				const auto absolute = block * BlockFrames + frame;
				for (int channel = 0; channel < Channels; ++channel)
				{
					interleaved[static_cast<std::size_t>(frame) * Channels + channel] =
						channelValue(channel, absolute);
				}
			}
			recorder.processInputInterleaved(interleaved.data(), Channels, BlockFrames);
		}

		for (int route = 0; route < Routes; ++route)
		{
			QCOMPARE(recorder.track(route).framesPushed(), static_cast<std::uint64_t>(TotalFrames));
			QCOMPARE(recorder.track(route).inputChannel(), route);
		}

		// A clean stop flushes and closes every take.
		recorder.disarmAll();
		for (int route = 0; route < Routes; ++route)
		{
			QVERIFY(!recorder.track(route).isArmed());
			QCOMPARE(recorder.track(route).framesRecorded(),
				static_cast<std::uint64_t>(TotalFrames));
			QCOMPARE(recorder.track(route).overflowCount(), std::uint64_t{0});
			QCOMPARE(recorder.track(route).writeErrorCount(), std::uint64_t{0});

			const QString path = dir.filePath(QStringLiteral("route%1.wav").arg(route));
			QVERIFY2(QFileInfo::exists(path), qPrintable(path));

			std::vector<sample_t> recorded;
			QVERIFY2(readTake(path, &recorded), qPrintable(QStringLiteral("cannot read %1").arg(path)));
			QCOMPARE(static_cast<lmms::f_cnt_t>(recorded.size()), TotalFrames);

			for (lmms::f_cnt_t frame = 0; frame < TotalFrames; ++frame)
			{
				const auto expected = channelValue(route, frame);
				const auto measured = static_cast<double>(recorded[static_cast<std::size_t>(frame)]);
				if (std::fabs(measured - static_cast<double>(expected)) > OneLsb24)
				{
					QFAIL(qPrintable(QStringLiteral("route %1 frame %2 holds %3, expected %4 "
						"(the take holds a DIFFERENT channel's samples)")
						.arg(route).arg(frame).arg(measured).arg(expected)));
				}
			}
		}
	}

	//! The path the engine has always used is unchanged: the stereo entry point
	//! still demuxes the bus, and DEFAULT_CHANNELS is still the width it means.
	void theStereoBusEntryPointStillRecords()
	{
		MultiTrackRecorder recorder;
		QCOMPARE(recorder.trackCount(), MultiTrackRecorder::NumTracks);
		QCOMPARE(recorder.inputChannelCapacity(), 2);

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QVERIFY(recorder.armTrack(0, dir.filePath(QStringLiteral("left.wav")).toStdString(),
			SampleRate, 0));
		QVERIFY(recorder.armTrack(1, dir.filePath(QStringLiteral("right.wav")).toStdString(),
			SampleRate, 1));

		std::vector<SampleFrame> bus(512);
		for (std::size_t i = 0; i < bus.size(); ++i)
		{
			bus[i] = SampleFrame(0.25f, -0.125f);
		}
		recorder.processInput(bus.data(), static_cast<lmms::f_cnt_t>(bus.size()));

		QCOMPARE(recorder.track(0).framesPushed(), std::uint64_t{512});
		QCOMPARE(recorder.track(1).framesPushed(), std::uint64_t{512});
		recorder.disarmAll();
		QCOMPARE(recorder.track(0).framesRecorded(), std::uint64_t{512});
		QCOMPARE(recorder.track(1).framesRecorded(), std::uint64_t{512});

		std::vector<sample_t> left;
		std::vector<sample_t> right;
		QVERIFY(readTake(dir.filePath(QStringLiteral("left.wav")), &left));
		QVERIFY(readTake(dir.filePath(QStringLiteral("right.wav")), &right));
		QCOMPARE(left.size(), std::size_t{512});
		QCOMPARE(right.size(), std::size_t{512});
		QVERIFY(std::fabs(static_cast<double>(left[7]) - 0.25) <= OneLsb24);
		QVERIFY(std::fabs(static_cast<double>(right[7]) + 0.125) <= OneLsb24);
	}

	//! A route cannot be pointed past the width, and a refused arm leaves NO file:
	//! the check happens before the take is opened (which is the whole reason the
	//! refusal is useful - a caller must not have to clean up after it).
	void aRouteCannotSelectAChannelOutsideTheWidth()
	{
		MultiTrackRecorder recorder(2, 2);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = dir.filePath(QStringLiteral("never.wav"));
		QVERIFY(!recorder.armTrack(0, path.toStdString(), SampleRate, 2));
		QVERIFY(!recorder.armTrack(0, path.toStdString(), SampleRate, -1));
		QVERIFY(!recorder.track(0).isArmed());
		QVERIFY(!QFileInfo::exists(path));
		QVERIFY(recorder.track(0).inputChannelSelectable(0));
		QVERIFY(recorder.track(0).inputChannelSelectable(1));
		QVERIFY(!recorder.track(0).inputChannelSelectable(2));

		// Widening the capacity is what makes channel 2 selectable - the same
		// thing the engine does at construction from the configured count.
		recorder.setInputChannelCapacity(4);
		QVERIFY(recorder.track(0).inputChannelSelectable(2));
		QVERIFY(recorder.track(0).inputChannelSelectable(3));
		QVERIFY(!recorder.track(0).inputChannelSelectable(4));
		QVERIFY2(recorder.armTrack(0, path.toStdString(), SampleRate, 3),
			"a route inside the widened range must arm");
		recorder.disarmAll();
	}

	//! The input path's plan accepts an ARBITRARY count inside its stated bound and
	//! refuses what it cannot honour, with a message that names the bound.
	void theInputPlanAcceptsAnArbitraryCountAndRefusesWhatItCannotHonour()
	{
		AudioInputPath::Plan plan;
		plan.channels = 8;
		plan.left = 3;
		plan.right = 4;
		AudioInputPath::Plan normalised;
		QString error;
		QVERIFY2(AudioInputPath::validatePlan(plan, &normalised, &error), qPrintable(error));
		QCOMPARE(normalised.channels, 8);
		QCOMPARE(normalised.left, 3);
		QCOMPARE(normalised.right, 4);

		// The bound is stated, and both ends of it are refused with the range in
		// the message rather than a bare false.
		AudioInputPath::Plan tooFew;
		tooFew.channels = 0;
		QVERIFY(!AudioInputPath::validatePlan(tooFew, nullptr, &error));
		QVERIFY(!error.isEmpty());
		AudioInputPath::Plan tooMany;
		tooMany.channels = AudioInputPath::MaxChannels + 1;
		QVERIFY(!AudioInputPath::validatePlan(tooMany, nullptr, &error));
		QVERIFY(error.contains(QString::number(AudioInputPath::MaxChannels)));

		// A bus pair outside the captured width is refused: silence-on-one-side
		// would otherwise look like a working capture.
		AudioInputPath::Plan badPair;
		badPair.channels = 2;
		badPair.left = 5;
		badPair.right = 0;
		QVERIFY(!AudioInputPath::validatePlan(badPair, nullptr, &error));

		// The default plan (no configuration) is the stereo bus, and the route
		// bound is the engine's own - NOT the input count.
		QCOMPARE(AudioInputPath::DefaultChannels, 2);
		QCOMPARE(AudioInputPath::recordRouteCapacity(),
			static_cast<int>(MultiTrackRecorder::MaxRoutes));
		QVERIFY(AudioInputPath::recordRouteCapacity() >= MultiTrackRecorder::NumTracks);

		// With no capture device open, a route may select the stereo bus's two
		// channels: a box with no interface is not a box with no input path.
		if (!AudioInputPath::live().open)
		{
			QCOMPARE(AudioInputPath::recordableChannelCount(), AudioInputPath::DefaultChannels);
		}
	}
} ;

QTEST_GUILESS_MAIN(RecordingInputPathTest)

#include "RecordingInputPathTest.moc"
