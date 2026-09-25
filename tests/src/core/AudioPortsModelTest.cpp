/*
 * AudioPortsModelTest.cpp
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

#include <QtTest>
#include <QDomDocument>
#include <QDomElement>
#include <QSignalSpy>

#include <array>
#include <limits>
#include <stdexcept>
#include <string>

#include "AudioBufferView.h"
#include "AudioBus.h"
#include "AudioEngine.h"
#include "AudioPortsModel.h"
#include "Engine.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

//! AudioPortsModel is abstract (bufferPropertiesChanging); count the calls
class TestAudioPortsModel : public AudioPortsModel
{
public:
	using AudioPortsModel::AudioPortsModel;

	int bufferPropertiesChangingCalls = 0;
	ch_cnt_t lastInChannels = 0;
	ch_cnt_t lastOutChannels = 0;
	f_cnt_t lastFrames = 0;

protected:
	void bufferPropertiesChanging(ch_cnt_t inChannels, ch_cnt_t outChannels, f_cnt_t frames) override
	{
		++bufferPropertiesChangingCalls;
		lastInChannels = inChannels;
		lastOutChannels = outChannels;
		lastFrames = frames;
	}
};

//! Runs `fn`, reporting the exception message through `message`
template<typename Fn>
bool throws(Fn&& fn, QString* message)
{
	try
	{
		fn();
		return false;
	}
	catch (const std::exception& e)
	{
		if (message) { *message = QString::fromUtf8(e.what()); }
		return true;
	}
}

} // namespace


class AudioPortsModelTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
	}

	//! Dynamic channel counts leave the model uninitialized and unrouted
	void dynamicConstructor()
	{
		TestAudioPortsModel model{true};

		QVERIFY(model.isInstrument());
		QVERIFY(!model.initialized());
		QCOMPARE(model.in().channelCount(), ch_cnt_t(0));
		QCOMPARE(model.out().channelCount(), ch_cnt_t(0));
		QCOMPARE(model.trackChannelCount(), DEFAULT_CHANNELS);
		QVERIFY(!model.directRouting().has_value());

		// setProcessorChannelCountsImpl() rejects dynamic counts
		QSignalSpy spy(&model, &Model::propertiesChanged);
		model.setChannelCounts(DynamicChannelCount, 2);
		QCOMPARE(spy.count(), 0);
		QVERIFY(!model.initialized());
	}

	//! Instruments skip the default connections on their input matrix
	void instrumentInputSkipsDefaults()
	{
		TestAudioPortsModel model{2, 2, true};

		QVERIFY(model.isInstrument());
		QVERIFY(model.initialized());

		QVERIFY(!model.in().enabled(0, 0));
		QVERIFY(!model.in().enabled(1, 1));
		QVERIFY(!model.in().usedTrackChannels().any());
		QVERIFY(!model.in().usedChannels()[0]);
		QVERIFY(!model.in().usedChannels()[1]);

		// ... but the output matrix still gets the stereo diagonal
		QVERIFY(model.out().enabled(0, 0));
		QVERIFY(model.out().enabled(1, 1));
		QVERIFY(!model.out().enabled(0, 1));
		QVERIFY(!model.out().enabled(1, 0));
		QVERIFY(model.out().usedTrackChannels()[0]);
		QVERIFY(model.out().usedTrackChannels()[1]);
		QVERIFY(model.out().usedChannels()[0]);
		QVERIFY(model.out().usedChannels()[1]);

		// no input pins means the direct routing optimization is disabled
		QVERIFY(!model.directRouting().has_value());
	}

	//! Mono processors only route the first track channel
	void monoDefaultConnections()
	{
		// mono output
		{
			TestAudioPortsModel model{2, 1, false};
			QCOMPARE(model.out().channelCount(), ch_cnt_t(1));
			QVERIFY(model.out().enabled(0, 0));
			QVERIFY(model.out().enabled(1, 0));
			QCOMPARE(model.out().usedChannels().size(), std::size_t(1));
			QVERIFY(model.out().usedChannels()[0]);
			QVERIFY(model.out().usedTrackChannels()[0]);
			QVERIFY(model.out().usedTrackChannels()[1]);
			QVERIFY(!model.directRouting().has_value());
		}

		// mono input
		{
			TestAudioPortsModel model{1, 2, false};
			QCOMPARE(model.in().channelCount(), ch_cnt_t(1));
			QVERIFY(model.in().enabled(0, 0));
			QVERIFY(!model.in().enabled(1, 0));
			QCOMPARE(model.in().usedChannels().size(), std::size_t(1));
			QVERIFY(model.in().usedTrackChannels()[0]);
			QVERIFY(!model.in().usedTrackChannels()[1]);
			QVERIFY(!model.directRouting().has_value());
		}
	}

	//! setAllChannelCounts() updates the caches and emits propertiesChanged()
	void setAllChannelCountsUpdatesCaches()
	{
		TestAudioPortsModel model{4, 4, false};
		QSignalSpy spy(&model, &Model::propertiesChanged);
		QCOMPARE(model.trackChannelCount(), DEFAULT_CHANNELS);

		// grow the track channel count, shrink the processor counts
		model.setAllChannelCounts(4, 2, 2);
		QCOMPARE(spy.count(), 1);
		QCOMPARE(model.trackChannelCount(), ch_cnt_t(4));
		QCOMPARE(model.in().channelCount(), ch_cnt_t(2));
		QCOMPARE(model.out().channelCount(), ch_cnt_t(2));
		QVERIFY(model.in().usedTrackChannels()[0]);
		QVERIFY(model.in().usedTrackChannels()[1]);
		QVERIFY(!model.in().usedTrackChannels()[2]);
		QVERIFY(model.out().usedTrackChannels()[0]);
		QVERIFY(model.in().usedChannels()[1]);
		QVERIFY(model.out().usedChannels()[0]);

		// shrink both the track channel count and the processor counts
		model.setAllChannelCounts(2, 1, 1);
		QCOMPARE(spy.count(), 2);
		QCOMPARE(model.trackChannelCount(), ch_cnt_t(2));
		QCOMPARE(model.in().channelCount(), ch_cnt_t(1));
		QCOMPARE(model.out().channelCount(), ch_cnt_t(1));
		QVERIFY(model.in().usedTrackChannels()[0]);
		QVERIFY(!model.in().usedTrackChannels()[1]);
		QVERIFY(!model.directRouting().has_value());

		// nothing changed: early return without a signal
		model.setAllChannelCounts(2, 1, 1);
		QCOMPARE(spy.count(), 2);

		// shrink only the track channel count
		TestAudioPortsModel shrink{1, 1, false};
		QSignalSpy shrinkSpy(&shrink, &Model::propertiesChanged);
		shrink.setTrackChannelCount(4);
		QCOMPARE(shrinkSpy.count(), 1);
		shrink.setAllChannelCounts(2, 1, 1);
		QCOMPARE(shrinkSpy.count(), 2);
		QCOMPARE(shrink.trackChannelCount(), ch_cnt_t(2));
		QCOMPARE(shrink.in().pins().size(), std::size_t(2));
		QCOMPARE(shrink.out().pins().size(), std::size_t(2));
	}

	//! setTrackChannelCount() resizes the pin matrices and updates caches
	void setTrackChannelCountUpdatesCaches()
	{
		TestAudioPortsModel model{2, 2, false};
		QSignalSpy spy(&model, &Model::propertiesChanged);

		model.setTrackChannelCount(4);
		QCOMPARE(spy.count(), 1);
		QCOMPARE(model.trackChannelCount(), ch_cnt_t(4));
		QCOMPARE(model.in().pins().size(), std::size_t(4));
		QVERIFY(model.in().enabled(0, 0));
		QVERIFY(!model.in().enabled(2, 0));

		model.setTrackChannelCount(2);
		QCOMPARE(spy.count(), 2);
		QCOMPARE(model.trackChannelCount(), ch_cnt_t(2));
		QCOMPARE(model.out().pins().size(), std::size_t(2));

		// same count again: no-op
		model.setTrackChannelCount(2);
		QCOMPARE(spy.count(), 2);
	}

	//! Invalid track channel counts are rejected with a clear message
	void invalidTrackChannelCountsThrow()
	{
		TestAudioPortsModel model{2, 2, false};
		QString message;

		QVERIFY(throws([&] { model.setTrackChannelCount(0); }, &message));
		QCOMPARE(message, QStringLiteral("There must be at least 2 track channels"));

		QVERIFY(throws([&] { model.setTrackChannelCount(3); }, &message));
		QCOMPARE(message, QStringLiteral("There must be an even number of track channels"));

		QVERIFY(throws([&] { model.setTrackChannelCount(MaxTrackChannels + 2); }, &message));
		QCOMPARE(message, QStringLiteral("Only up to 256 track channels are allowed"));

		// the rejected calls must not have modified the model
		QCOMPARE(model.trackChannelCount(), DEFAULT_CHANNELS);
	}

	//! setChannelCounts() rejects dynamic, all-zero and unchanged counts
	void setChannelCountsRejections()
	{
		TestAudioPortsModel model{2, 2, false};
		QSignalSpy spy(&model, &Model::propertiesChanged);

		model.setChannelCounts(DynamicChannelCount, 2);
		QCOMPARE(spy.count(), 0);
		QCOMPARE(model.in().channelCount(), ch_cnt_t(2));

		model.setChannelCounts(0, 0);
		QCOMPARE(spy.count(), 0);
		QCOMPARE(model.in().channelCount(), ch_cnt_t(2));
		QCOMPARE(model.out().channelCount(), ch_cnt_t(2));

		model.setChannelCounts(2, 2);
		QCOMPARE(spy.count(), 0);
	}

	//! setChannelCountIn()/setChannelCountOut() wrap setChannelCounts()
	void channelCountInOutWrappers()
	{
		TestAudioPortsModel model{2, 2, false};
		QSignalSpy spy(&model, &Model::propertiesChanged);

		model.setChannelCountIn(4);
		QCOMPARE(spy.count(), 1);
		QCOMPARE(model.in().channelCount(), ch_cnt_t(4));
		QCOMPARE(model.out().channelCount(), ch_cnt_t(2));

		model.setChannelCountOut(4);
		QCOMPARE(spy.count(), 2);
		QCOMPARE(model.out().channelCount(), ch_cnt_t(4));
	}

	//! Shrinking processor channel counts updates the used-channel caches
	void setChannelCountsShrinkUpdatesCaches()
	{
		TestAudioPortsModel model{4, 4, false};
		QSignalSpy spy(&model, &Model::propertiesChanged);

		model.setChannelCounts(2, 2);
		QCOMPARE(spy.count(), 1);
		QCOMPARE(model.in().channelCount(), ch_cnt_t(2));
		QCOMPARE(model.out().channelCount(), ch_cnt_t(2));
		QVERIFY(model.in().usedTrackChannels()[0]);
		QVERIFY(model.in().usedTrackChannels()[1]);
		QVERIFY(model.in().usedChannels()[0]);
		QVERIFY(model.out().usedTrackChannels()[0]);
		QVERIFY(model.out().usedChannels()[1]);
	}

	//! directRouting() follows the pin layout of both matrices
	void directRoutingMatchesPinLayout()
	{
		// stereo effect with the default diagonal: pair 0 is direct
		{
			TestAudioPortsModel model{2, 2, false};
			QVERIFY(model.directRouting().has_value());
			QCOMPARE(model.directRouting().value(), ch_cnt_t(0));
		}

		// output-only processor: the output matrix decides
		{
			TestAudioPortsModel model{0, 2, false};
			QVERIFY(model.directRouting().has_value());
			QCOMPARE(model.directRouting().value(), ch_cnt_t(0));
		}

		// input-only processor: the input matrix decides
		{
			TestAudioPortsModel model{2, 0, false};
			QVERIFY(model.directRouting().has_value());
			QCOMPARE(model.directRouting().value(), ch_cnt_t(0));
		}

		// input matrix is not directly routed -> disabled
		{
			TestAudioPortsModel model{2, 2, false};
			QVERIFY(model.directRouting().has_value());
			model.in().setPin(1, 1, false);
			QVERIFY(!model.directRouting().has_value());
		}

		// two track channel pairs connected -> disabled
		{
			TestAudioPortsModel model{0, 2, false};
			QVERIFY(model.directRouting().has_value());
			model.setTrackChannelCount(4);
			model.out().setPin(2, 0, true);
			QVERIFY(!model.directRouting().has_value());
		}

		// input and output use different track channel pairs -> disabled
		{
			TestAudioPortsModel model{2, 2, false};
			QVERIFY(model.directRouting().has_value());
			model.setTrackChannelCount(4);
			model.out().setPin(0, 0, false);
			model.out().setPin(1, 1, false);
			model.out().setPin(2, 0, true);
			model.out().setPin(3, 1, true);
			QVERIFY(!model.directRouting().has_value());
		}
	}

	//! channel names are 1-based and direction-dependent
	void channelNames()
	{
		TestAudioPortsModel model{2, 2, false};

		QCOMPARE(model.out().channelName(0), QStringLiteral("Output 1"));
		QCOMPARE(model.out().channelName(1), QStringLiteral("Output 2"));
		QCOMPARE(model.in().channelName(0), QStringLiteral("Input 1"));
		QCOMPARE(model.in().channelName(1), QStringLiteral("Input 2"));
	}

	//! sampleRateChanged() notifies the processor of new buffer properties
	void sampleRateChangedNotifiesBuffers()
	{
		TestAudioPortsModel model{2, 2, false};

		// the constructor is silent
		QCOMPARE(model.bufferPropertiesChangingCalls, 0);

		emit Engine::audioEngine()->sampleRateChanged();

		QCOMPARE(model.bufferPropertiesChangingCalls, 1);
		QCOMPARE(model.lastInChannels, ch_cnt_t(2));
		QCOMPARE(model.lastOutChannels, ch_cnt_t(2));
		QCOMPARE(model.lastFrames, Engine::audioEngine()->framesPerPeriod());
	}

	//! Regression: the sampleRateChanged connection must not outlive the model.
	//! Before the fix the lambda had no context object, so a destroyed model's
	//! dangling `this` was called on the next sampleRateChanged emission.
	void sampleRateChangedDisconnectsOnDestruction()
	{
		{
			TestAudioPortsModel doomed{2, 2, false};
			QCOMPARE(doomed.bufferPropertiesChangingCalls, 0);
		}

		// Emitting now must not reach the destroyed model (would be a crash or
		// a write into freed memory before the fix).
		TestAudioPortsModel survivor{2, 2, false};
		emit Engine::audioEngine()->sampleRateChanged();
		QCOMPARE(survivor.bufferPropertiesChangingCalls, 1);
		QCOMPARE(survivor.lastInChannels, ch_cnt_t(2));
		QCOMPARE(survivor.lastOutChannels, ch_cnt_t(2));
	}

	//! BUG-PINBOUNDS: every persisted `cN_M` pin attribute is validated, in EVERY
	//! build. A project file is user-editable input and the loader used to index the
	//! pin matrix with whatever the file supplied, guarded only by `#ifndef NDEBUG`
	//! asserts - and this build is NDEBUG. On the pre-fix binary the element below
	//! SIGSEGVs: `c40000_1` walks off the row vector and the non-pin name `junk`
	//! lands at index -1. The malformed names must be ignored and the valid ones
	//! must still apply.
	void loadSettingsIgnoresMalformedAndOutOfRangePins()
	{
		TestAudioPortsModel model{2, 2, false};

		QDomDocument doc;
		QVERIFY(doc.setContent(QStringLiteral(
			"<audioengine><pins inputs=\"2\" outputs=\"2\">"
			"<in_matrix c1_1=\"true\" c40000_1=\"true\" c9_1=\"true\" junk=\"true\""
			" x1_1=\"true\" c0_1=\"true\" c1_0=\"true\" c_1=\"true\" c1_1_1=\"true\"/>"
			"<out_matrix c2_1=\"true\"/>"
			"</pins></audioengine>")));
		model.loadSettings(doc.documentElement());

		// the loader resets every pin first, so exactly one input pin is set: the
		// valid `c1_1`. Nothing the bad names "addressed" was written.
		for (ch_cnt_t tc = 0; tc < model.trackChannelCount(); ++tc)
		{
			for (ch_cnt_t pc = 0; pc < model.in().channelCount(); ++pc)
			{
				QCOMPARE(model.in().enabled(tc, pc), tc == 0 && pc == 0);
			}
		}
		// and the valid output pin applied: track channel 2 is the last one this
		// model has, so the range check must accept the bound itself
		QVERIFY(model.out().enabled(1, 0));
		QVERIFY(!model.out().enabled(0, 0));
		QCOMPARE(model.trackChannelCount(), DEFAULT_CHANNELS);
	}

	//! BUG-PINBOUNDS: the channel counts a file carries are validated before they
	//! size the matrices. 134 was applied verbatim - a 134-channel matrix built from
	//! the file, above the engine's own MaxChannelsPerAudioBuffer ceiling - and a
	//! negative count wrapped into the unsigned ch_cnt_t.
	void loadSettingsIgnoresOutOfRangeChannelCounts()
	{
		TestAudioPortsModel model{2, 2, false};

		QDomDocument doc;
		QVERIFY(doc.setContent(QStringLiteral(
			"<audioengine><pins inputs=\"134\" outputs=\"134\">"
			"<in_matrix c1_1=\"true\"/><out_matrix/></pins></audioengine>")));
		model.loadSettings(doc.documentElement());
		QCOMPARE(model.in().channelCount(), ch_cnt_t(2));
		QCOMPARE(model.out().channelCount(), ch_cnt_t(2));
		QVERIFY(model.in().enabled(0, 0));

		// a negative count is refused the same way
		QDomDocument negative;
		QVERIFY(negative.setContent(QStringLiteral(
			"<audioengine><pins inputs=\"-4\" outputs=\"2\">"
			"<in_matrix c1_1=\"true\"/><out_matrix/></pins></audioengine>")));
		model.loadSettings(negative.documentElement());
		QCOMPARE(model.in().channelCount(), ch_cnt_t(2));

		// ... and the boundary itself is still accepted, so the check is a range
		// and not a refusal of large-but-legal processors
		QDomDocument atLimit;
		QVERIFY(atLimit.setContent(QStringLiteral(
			"<audioengine><pins inputs=\"128\" outputs=\"2\">"
			"<in_matrix c1_1=\"true\"/><out_matrix/></pins></audioengine>")));
		model.loadSettings(atLimit.documentElement());
		QCOMPARE(model.in().channelCount(), MaxChannelsPerAudioBuffer);
		QVERIFY(model.in().enabled(0, 0));
	}

	//! BUG-CHBOUND: the cached track-channel upper bound follows the pins actually
	//! in use. It was initialised to DEFAULT_CHANNELS and only ever assigned
	//! std::min(...), so it never grew. AudioBus and AudioPorts loop [0, bound), so
	//! audio routed to a pair above the default one was never examined - and a bus
	//! carrying signal on track channels 2/3 was reported all-quiet.
	void trackChannelsUpperBoundFollowsPinUsage()
	{
		TestAudioPortsModel model{0, 2, false};
		QCOMPARE(model.trackChannelsUpperBound(), DEFAULT_CHANNELS);

		// growing the track channel count alone does not grow the bound
		model.setTrackChannelCount(4);
		QCOMPARE(model.trackChannelsUpperBound(), DEFAULT_CHANNELS);

		// route ONLY the second pair (track channels 2 and 3)
		model.out().setPin(0, 0, false);
		model.out().setPin(1, 1, false);
		model.out().setPin(2, 0, true);
		model.out().setPin(3, 1, true);
		QCOMPARE(model.trackChannelsUpperBound(), ch_cnt_t(4));

		// the bound is what AudioBus::update() loops over: the signal in the pair
		// above the default one is examined, so the bus is NOT all-quiet
		std::array<std::array<SampleFrame, 8>, 2> storage{};
		std::array<SampleFrame*, 2> pointers{storage[0].data(), storage[1].data()};
		AudioBus bus{pointers.data(), ch_cnt_t(2), f_cnt_t(8)};
		storage[1][0][0] = 0.5f; // track channel 2
		QVERIFY(!bus.update(model));

		// control: the same measurement on a model that routes the DEFAULT pair, so a
		// signal the router does examine is reported not-quiet too - the result above
		// is the bound, not the harness
		TestAudioPortsModel defaultPair{0, 2, false};
		std::array<std::array<SampleFrame, 8>, 2> controlStorage{};
		std::array<SampleFrame*, 2> controlPointers{controlStorage[0].data(), controlStorage[1].data()};
		AudioBus controlBus{controlPointers.data(), ch_cnt_t(2), f_cnt_t(8)};
		controlStorage[0][0][0] = 0.5f;
		QVERIFY(!controlBus.update(defaultPair));

		// and the pair is sanitized: a NaN left in it is cleared
		storage[1][0][0] = std::numeric_limits<float>::quiet_NaN();
		bus.sanitize(model);
		QCOMPARE(storage[1][0][0], 0.f);

		// shrink then grow again: the removed pair must not come back
		model.setTrackChannelCount(2);
		QCOMPARE(model.trackChannelsUpperBound(), DEFAULT_CHANNELS);
		model.setTrackChannelCount(4);
		QCOMPARE(model.trackChannelsUpperBound(), DEFAULT_CHANNELS);
		QVERIFY(!model.out().usedTrackChannels()[2]);
		QVERIFY(!model.out().usedTrackChannels()[3]);
	}
};

QTEST_GUILESS_MAIN(AudioPortsModelTest)
#include "AudioPortsModelTest.moc"
