/*
 * AudioBusTest.cpp
 *
 * Copyright (c) 2026 LMMS contributors
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

#include "AudioBus.h"

#include <QtTest>

#include <array>
#include <bitset>
#include <cmath>
#include <limits>

#include "AudioPortsModel.h"
#include "Engine.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

/*
 * AudioBus is a non-owning view over SampleFrame* channel pairs. This helper
 * builds a bus with 4 track channels (2 pairs) and keeps the backing storage
 * alive. The storage is fixed-size so the AudioBus view, constructed in-class
 * after the pointer table, stays valid.
 */
class TestBus
{
public:
	explicit TestBus(f_cnt_t frames)
		: m_frames(frames)
	{
	}

	auto bus() -> AudioBus& { return m_bus; }

	auto setSample(ch_cnt_t trackChannel, f_cnt_t frame, float value)
	{
		m_storage[trackChannel / 2][frame][trackChannel % 2] = value;
	}

	auto sample(ch_cnt_t trackChannel, f_cnt_t frame) -> float
	{
		return m_storage[trackChannel / 2][frame][trackChannel % 2];
	}

private:
	static constexpr ch_cnt_t s_pairs = 2;
	static constexpr f_cnt_t s_maxFrames = 64;

	f_cnt_t m_frames = 0;
	std::array<std::array<SampleFrame, s_maxFrames>, s_pairs> m_storage{};
	std::array<SampleFrame*, s_pairs> m_pointers{m_storage[0].data(), m_storage[1].data()};
	// constructed last so the pointers are valid; AudioBus is non-assignable
	AudioBus m_bus{m_pointers.data(), s_pairs, m_frames};
};

//! Concrete model for the tests (bufferPropertiesChanging is only implemented
//! by the plugin-facing AudioPorts<>/PluginAudioPorts<> layers).
class TestAudioPortsModel : public AudioPortsModel
{
public:
	using AudioPortsModel::AudioPortsModel;

private:
	void bufferPropertiesChanging(ch_cnt_t, ch_cnt_t, f_cnt_t) override
	{
	}
};

//! AudioPortsModel where every output pin of every track channel is enabled
//! (the default 2 track channels x 2 processor outputs).
//!
//! NOTE: model-driven AudioBus paths (update()/sanitize()/silenceChannels())
//! only inspect track channels below `trackChannelsUpperBound()`, which
//! starts at DEFAULT_CHANNELS (2) and only ever shrinks (see the TODO in
//! AudioPortsModel.h). Tests for track channels above the bound therefore
//! use the updateAll()/sanitizeAll()/silenceAllChannels() variants instead.
auto makeAllPinsModel() -> std::unique_ptr<TestAudioPortsModel>
{
	auto model = std::make_unique<TestAudioPortsModel>(0, 2, false);
	for (ch_cnt_t tc = 0; tc < 2; ++tc)
	{
		for (ch_cnt_t pc = 0; pc < 2; ++pc)
		{
			model->out().setPin(tc, pc, true);
		}
	}
	return model;
}

} // namespace


class AudioBusTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		lmms::Engine::init(true);
	}

	void cleanupTestCase()
	{
		lmms::Engine::destroy();
	}

	//! A fresh bus starts with every channel assumed to carry signal
	void InitialState()
	{
		TestBus t{16};
		QCOMPARE(t.bus().frames(), f_cnt_t(16));
		QCOMPARE(t.bus().channelPairs(), ch_cnt_t(2));
		QCOMPARE(t.bus().channels(), ch_cnt_t(4));
		QVERIFY(t.bus().quietChannels().none());
		// the non-owning view points at the first pair's storage
		QVERIFY(t.bus().bus() != nullptr);
		QVERIFY(t.bus().bus()[0] != nullptr);
	}

	//! update() with every output pin enabled must report per-channel silence
	//! exactly for the channels that are below the silence threshold
	void UpdateAllPinsReportsSilencePerChannel()
	{
		TestBus t{32};
		auto model = makeAllPinsModel();

		// leave everything at zero: both examined track channels are silent;
		// channels above trackChannelsUpperBound() were never examined
		QVERIFY(t.bus().update(*model));
		QVERIFY(t.bus().quietChannels()[0]);
		QVERIFY(t.bus().quietChannels()[1]);
		QVERIFY(!t.bus().quietChannels()[2]);
		QVERIFY(!t.bus().quietChannels()[3]);

		// a loud sample on the left channel breaks channel 0 only
		t.setSample(0, 5, 0.5f);
		QVERIFY(!t.bus().update(*model));
		QVERIFY(!t.bus().quietChannels()[0]);
		QVERIFY(t.bus().quietChannels()[1]);

		// right channel
		t.setSample(0, 5, 0.f);
		t.setSample(1, 7, -0.25f);
		QVERIFY(!t.bus().update(*model));
		QVERIFY(t.bus().quietChannels()[0]);
		QVERIFY(!t.bus().quietChannels()[1]);

		// below the threshold counts as silent
		t.setSample(1, 7, 0.f);
		QVERIFY(t.bus().update(*model));
		// examined channels are quiet again; channels above the model's
		// trackChannelsUpperBound() were never examined and stay false
		QVERIFY(t.bus().quietChannels()[0]);
		QVERIFY(t.bus().quietChannels()[1]);
		QVERIFY(!t.bus().quietChannels()[2]);
		QVERIFY(!t.bus().quietChannels()[3]);
	}

	//! update() with only some pins enabled must respect the pin matrix and
	//! leave unconnected channels' quiet flags untouched
	void UpdateWithPinsRespectsMatrix()
	{
		TestBus t{32};
		auto model = std::make_unique<TestAudioPortsModel>(0, 2, false);
		// default matrix is the stereo diagonal (0->0, 1->1); drop 1->1 so
		// only track channel 0 is routed to the processor output
		model->out().setPin(1, 1, false);

		// signal in connected channel 0
		t.setSample(0, 2, 0.5f);
		QVERIFY(!t.bus().update(*model));
		QVERIFY(!t.bus().quietChannels()[0]);
		// disconnected channels keep their previous (initial) assumption
		QVERIFY(!t.bus().quietChannels()[1]);

		// silence connected channel 0 again
		t.setSample(0, 2, 0.f);
		QVERIFY(t.bus().update(*model));
		QVERIFY(t.bus().quietChannels()[0]);
		// disconnected channels still untouched
		QVERIFY(!t.bus().quietChannels()[1]);
	}

	//! updateAll() ignores the pin matrix and inspects every channel
	void UpdateAllInspectsEveryChannel()
	{
		TestBus t{32};
		// No model/pins at all: update() through the model would be a no-op
		// because no output pins are used, but updateAll() must still find
		// the signal in pair 1.
		t.setSample(3, 9, 0.3f);
		QVERIFY(!t.bus().updateAll());
		QVERIFY(t.bus().quietChannels()[0]);
		QVERIFY(t.bus().quietChannels()[1]);
		QVERIFY(t.bus().quietChannels()[2]);
		QVERIFY(!t.bus().quietChannels()[3]);
	}

	//! hasInputNoise(): true exactly when a non-quiet channel is routed
	//! through the model. NOTE: despite the header comment speaking of
	//! "processor inputs", the implementation consults the OUTPUT matrix —
	//! a sleeping effect must wake if the channels its output is routed to
	//! carry signal (that passthrough is what would become audible).
	void HasInputNoise()
	{
		TestBus t{16};
		// 0 in / 2 out channels; default out matrix is the stereo diagonal
		auto model = std::make_unique<TestAudioPortsModel>(0, 2, false);

		t.bus().quietChannels().set(0);
		t.bus().quietChannels().set(1);
		QVERIFY(!t.bus().hasInputNoise(*model));

		// wake the processor up: any non-quiet connected channel is noise
		t.bus().quietChannels().reset(0);
		QVERIFY(t.bus().hasInputNoise(*model));

		t.bus().quietChannels().set(0);
		t.bus().quietChannels().reset(1);
		QVERIFY(t.bus().hasInputNoise(*model));

		// a channel not routed to any output is not input noise
		model->out().setPin(1, 1, false);
		QVERIFY(!t.bus().hasInputNoise(*model));
	}

	//! sanitize() clears Inf/NaN in the used output channels and
	//! marks them quiet; clean over-range values are clamped, not cleared.
	//! Only track channels below trackChannelsUpperBound() (2) are examined,
	//! so the pair-1 clamp assertions live in the *All* tests.
	void SanitizeCoversUsedOutputChannels()
	{
		TestBus t{8};
		auto model = makeAllPinsModel();

		const float NaN = std::numeric_limits<float>::quiet_NaN();
		const float Inf = std::numeric_limits<float>::infinity();
		t.setSample(0, 0, NaN);
		t.setSample(1, 0, Inf);
		t.setSample(2, 1, 2000.f); // above the bound: untouched by sanitize()

		t.bus().sanitize(*model);

		// NaN/Inf channels were zeroed and marked quiet
		QVERIFY(std::isnan(t.sample(0, 0)) == false);
		QCOMPARE(t.sample(0, 0), 0.f);
		QCOMPARE(t.sample(1, 0), 0.f);
		QVERIFY(t.bus().quietChannels()[0]);
		QVERIFY(t.bus().quietChannels()[1]);

		// pair 1 is above trackChannelsUpperBound(): neither sanitized
		QCOMPARE(t.sample(2, 1), 2000.f);
		QVERIFY(!t.bus().quietChannels()[2]);
		QVERIFY(!t.bus().quietChannels()[3]);

		// after sanitize() the bus considers the cleared channels quiet, so a
		// later silenceChannels() call must skip them (sample stays 1.f)
		t.setSample(0, 0, 1.f);
		t.bus().silenceChannels(*model);
		QCOMPARE(t.sample(0, 0), 1.f);
	}

	//! sanitize() with only one pin enabled only touches that track channel;
	//! the default matrix is the stereo diagonal, so dropping 1->1 leaves
	//! track channel 1 completely unexamined
	void SanitizeSingleSided()
	{
		TestBus t{8};
		auto model = std::make_unique<TestAudioPortsModel>(0, 2, false);
		model->out().setPin(1, 1, false);

		const float NaN = std::numeric_limits<float>::quiet_NaN();
		t.setSample(0, 0, NaN);
		t.setSample(1, 0, NaN);

		t.bus().sanitize(*model);

		// the used track channel 0 was cleared and marked quiet
		QCOMPARE(t.sample(0, 0), 0.f);
		QVERIFY(t.bus().quietChannels()[0]);
		// the disconnected track channel 1 is untouched (still NaN, not quiet)
		QVERIFY(std::isnan(t.sample(1, 0)));
		QVERIFY(!t.bus().quietChannels()[1]);

		// and the cleared channel can be silenced by a later call without
		// affecting the disconnected channel
		t.bus().silenceChannels(*model);
		QCOMPARE(t.sample(0, 0), 0.f);
		QVERIFY(std::isnan(t.sample(1, 0)));
	}

	//! sanitizeAll() scans both pairs regardless of pins
	void SanitizeAll()
	{
		TestBus t{8};
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		t.setSample(2, 4, NaN);
		t.setSample(3, 4, 1.f);
		t.setSample(0, 0, 2000.f); // over the +-1000 clamp, still finite
		t.setSample(1, 0, -2000.f);

		QVERIFY(t.bus().sanitizeAll());
		QCOMPARE(t.sample(2, 4), 0.f);
		QCOMPARE(t.sample(3, 4), 0.f);
		QVERIFY(t.bus().quietChannels()[2]);
		QVERIFY(t.bus().quietChannels()[3]);
		// finite over-range values were clamped, not cleared, and the
		// channels are NOT marked quiet (they carry signal)
		QCOMPARE(t.sample(0, 0), 1000.f);
		QCOMPARE(t.sample(1, 0), -1000.f);
		QVERIFY(!t.bus().quietChannels()[0]);
		QVERIFY(!t.bus().quietChannels()[1]);

		// a clean bus reports false
		TestBus clean{8};
		QVERIFY(!clean.bus().sanitizeAll());
		QVERIFY(clean.bus().quietChannels().none());
	}

	//! silenceChannels() zeroes exactly the used channels below
	//! trackChannelsUpperBound() that are not quiet
	void SilenceChannels()
	{
		TestBus t{8};
		auto model = makeAllPinsModel();

		for (f_cnt_t f = 0; f < 8; ++f)
		{
			t.setSample(0, f, 1.f);
			t.setSample(1, f, 1.f);
			t.setSample(2, f, 1.f);
			t.setSample(3, f, 1.f);
		}

		// pre-mark track channel 0 as quiet: it must be skipped
		t.bus().quietChannels().set(0);

		t.bus().silenceChannels(*model);

		// quiet channel skipped
		QCOMPARE(t.sample(0, 0), 1.f);
		// non-quiet used channel (below the upper bound) zeroed
		QCOMPARE(t.sample(1, 3), 0.f);
		// pair 1 is above trackChannelsUpperBound(): untouched
		QCOMPARE(t.sample(2, 0), 1.f);
		QCOMPARE(t.sample(3, 5), 1.f);
		// used non-quiet channels were marked quiet; pair 1 flags untouched
		QVERIFY(t.bus().quietChannels()[1]);
		QVERIFY(!t.bus().quietChannels()[2]);
		QVERIFY(!t.bus().quietChannels()[3]);
	}

	//! silenceAllChannels() zeroes the whole bus and marks everything quiet
	void SilenceAllChannels()
	{
		TestBus t{8};
		for (f_cnt_t f = 0; f < 8; ++f)
		{
			t.setSample(0, f, 1.f);
			t.setSample(1, f, 1.f);
			t.setSample(2, f, 1.f);
			t.setSample(3, f, 1.f);
		}

		t.bus().silenceAllChannels();

		for (f_cnt_t f = 0; f < 8; ++f)
		{
			QCOMPARE(t.sample(0, f), 0.f);
			QCOMPARE(t.sample(1, f), 0.f);
			QCOMPARE(t.sample(2, f), 0.f);
			QCOMPARE(t.sample(3, f), 0.f);
		}
		QVERIFY(t.bus().quietChannels().all());
	}

	//! getChannelCountText() reflects the configured channel counts
	void ModelChannelCountsText()
	{
		TestAudioPortsModel model{2, 2, false};
		QCOMPARE(model.getChannelCountText(), QStringLiteral("2 in 2 out"));

		model.setChannelCounts(4, 0);
		QCOMPARE(model.getChannelCountText(), QStringLiteral("4 in 0 out"));
	}

	//! sanitize() on a model whose only used track channel is the right one
	//! (0b01) must sanitize channel 1, and the clamp branch of
	//! sanitizeChannel() must clamp rather than clear finite over-range data.
	void SanitizeRightChannelOnly()
	{
		auto model = std::make_unique<TestAudioPortsModel>(0, 2, false);
		model->out().setPin(0, 0, false);
		QVERIFY(!model->out().usedTrackChannels()[0]);
		QVERIFY(model->out().usedTrackChannels()[1]);

		const float Inf = std::numeric_limits<float>::infinity();

		// Inf in the used right channel: cleared and marked quiet.
		{
			TestBus t{8};
			t.setSample(1, 0, Inf);
			t.setSample(0, 0, 1234.f);
			t.bus().sanitize(*model);
			QCOMPARE(t.sample(1, 0), 0.f);
			QVERIFY(t.bus().quietChannels()[1]);
			// the unused left channel is neither clamped nor marked quiet
			QCOMPARE(t.sample(0, 0), 1234.f);
			QVERIFY(!t.bus().quietChannels()[0]);
		}

		// Finite over-range data in the used right channel: clamped, not
		// cleared, and not marked quiet (sanitizeChannel returns false).
		{
			TestBus t{8};
			t.setSample(1, 0, -5000.f);
			t.bus().sanitize(*model);
			QCOMPARE(t.sample(1, 0), -1000.f);
			QVERIFY(!t.bus().quietChannels()[1]);
		}
	}

	//! sanitize() on a model with no used track channels (0b00) leaves the
	//! buffer untouched.
	void SanitizeNoUsedChannels()
	{
		auto model = std::make_unique<TestAudioPortsModel>(0, 2, false);
		model->out().setPin(0, 0, false);
		model->out().setPin(1, 1, false);
		QVERIFY(!model->out().usedTrackChannels().any());

		TestBus t{8};
		const float NaN = std::numeric_limits<float>::quiet_NaN();
		t.setSample(0, 0, NaN);
		t.setSample(1, 0, NaN);

		t.bus().sanitize(*model);

		QVERIFY(std::isnan(t.sample(0, 0)));
		QVERIFY(std::isnan(t.sample(1, 0)));
		QVERIFY(!t.bus().quietChannels()[0]);
		QVERIFY(!t.bus().quietChannels()[1]);
	}

	//! update() on a model whose only used track channel is the right one
	//! (0b01) must judge quietness from channel 1 alone.
	void UpdateRightChannelOnly()
	{
		auto model = std::make_unique<TestAudioPortsModel>(0, 2, false);
		model->out().setPin(0, 0, false);
		QVERIFY(!model->out().usedTrackChannels()[0]);

		TestBus t{32};
		t.setSample(1, 3, 0.5f);
		QVERIFY(!t.bus().update(*model));
		QVERIFY(!t.bus().quietChannels()[1]);

		t.setSample(1, 3, 0.f);
		QVERIFY(t.bus().update(*model));
		QVERIFY(t.bus().quietChannels()[1]);
		// the unused left channel was never examined
		QVERIFY(!t.bus().quietChannels()[0]);
	}

	//! update() must find a loud left sample that appears after a loud right
	//! sample (the right-channel scan must not give up on the left one).
	void UpdateLeftLoudAfterRight()
	{
		TestBus t{32};
		auto model = makeAllPinsModel();

		t.setSample(1, 0, 0.5f);
		t.setSample(0, 2, 0.5f);

		QVERIFY(!t.bus().update(*model));
		QVERIFY(!t.bus().quietChannels()[0]);
		QVERIFY(!t.bus().quietChannels()[1]);
	}

	//! update() on a model with no used track channels (0b00) reports every
	//! examined channel as quiet and leaves the flags untouched.
	void UpdateNoUsedChannels()
	{
		auto model = std::make_unique<TestAudioPortsModel>(0, 2, false);
		model->out().setPin(0, 0, false);
		model->out().setPin(1, 1, false);

		TestBus t{16};
		t.setSample(0, 1, 1.f);

		QVERIFY(t.bus().update(*model));
		QVERIFY(!t.bus().quietChannels()[0]);
		QVERIFY(!t.bus().quietChannels()[1]);
	}

	//! silenceChannels() with both used channels needing silence (0b11) and
	//! with only the left used channel needing it (0b10).
	void SilenceChannelsBothAndLeftOnly()
	{
		// 0b11: both used channels are silenced.
		{
			TestBus t{8};
			auto model = makeAllPinsModel();
			for (f_cnt_t f = 0; f < 8; ++f)
			{
				t.setSample(0, f, 1.f);
				t.setSample(1, f, 1.f);
			}

			t.bus().silenceChannels(*model);

			QCOMPARE(t.sample(0, 0), 0.f);
			QCOMPARE(t.sample(1, 7), 0.f);
			QVERIFY(t.bus().quietChannels()[0]);
			QVERIFY(t.bus().quietChannels()[1]);
			// track channels above the upper bound are never touched
			QVERIFY(!t.bus().quietChannels()[2]);
			QVERIFY(!t.bus().quietChannels()[3]);
		}

		// 0b10: only the left used channel needs silencing.
		{
			TestBus t{8};
			auto model = makeAllPinsModel();
			for (f_cnt_t f = 0; f < 8; ++f)
			{
				t.setSample(0, f, 1.f);
				t.setSample(1, f, 1.f);
			}
			t.bus().quietChannels().set(1);

			t.bus().silenceChannels(*model);

			QCOMPARE(t.sample(0, 3), 0.f);
			QCOMPARE(t.sample(1, 3), 1.f);
			QVERIFY(t.bus().quietChannels()[0]);
		}
	}

	//! Multi-pair control kept from the product's own suite: the pair-iteration
	//! bug in silenceAllChannels() must not return. Distinct per-pair values
	//! name the pair that leaks in the failure message.
	void SilenceAllChannelsMultiPairDistinctValues()
	{
		constexpr ch_cnt_t pairs = 4;
		constexpr f_cnt_t frames = 8;

		SampleFrame data[pairs][frames];
		SampleFrame* bus[pairs];
		for (ch_cnt_t p = 0; p < pairs; ++p)
		{
			bus[p] = data[p];
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				data[p][f][0] = 1.0f + static_cast<float>(p);
				data[p][f][1] = -1.0f - static_cast<float>(p);
			}
		}

		AudioBus audioBus{bus, pairs, frames};
		QCOMPARE(int(audioBus.channelPairs()), int(pairs));
		QCOMPARE(int(audioBus.channels()), int(pairs) * 2);

		audioBus.silenceAllChannels();

		for (ch_cnt_t p = 0; p < pairs; ++p)
		{
			for (f_cnt_t f = 0; f < frames; ++f)
			{
				QVERIFY2(data[p][f][0] == 0.0f,
					qPrintable(QString("pair %1 frame %2 left channel not silenced: %3")
						.arg(int(p)).arg(f).arg(double(data[p][f][0]))));
				QVERIFY2(data[p][f][1] == 0.0f,
					qPrintable(QString("pair %1 frame %2 right channel not silenced: %3")
						.arg(int(p)).arg(f).arg(double(data[p][f][1]))));
			}
		}
	}

	//! Single-pair control kept from the product's own suite: the simple case
	//! has always worked and must keep working after the pair-iteration fix.
	void SilenceAllChannelsSinglePair()
	{
		constexpr f_cnt_t frames = 16;

		SampleFrame storage[frames];
		for (f_cnt_t f = 0; f < frames; ++f)
		{
			storage[f][0] = 0.5f;
			storage[f][1] = -0.25f;
		}
		SampleFrame* busData[1] = { storage };

		AudioBus bus{busData, 1, frames};
		bus.silenceAllChannels();

		for (f_cnt_t f = 0; f < frames; ++f)
		{
			QCOMPARE(storage[f][0], 0.0f);
			QCOMPARE(storage[f][1], 0.0f);
		}
		QVERIFY(bus.quietChannels()[0]);
		QVERIFY(bus.quietChannels()[1]);
	}
};

QTEST_GUILESS_MAIN(AudioBusTest)
#include "AudioBusTest.moc"
