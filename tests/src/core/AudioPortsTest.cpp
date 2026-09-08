/*
 * AudioPortsTest.cpp
 *
 * Copyright (c) 2025 Dalton Messmer <messmer.dalton/at/gmail.com>
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

#include <type_traits>

#include "AudioPorts.h"
#include "AudioPortsModel.h"
#include "AudioPortsSettings.h"
#include "Engine.h"
#include "PluginAudioPorts.h"

namespace lmms
{

namespace
{

//! Minimal concrete AudioPortsModel for these tests. `bufferPropertiesChanging`
//! is a pure virtual that only the plugin-facing AudioPorts<>/PluginAudioPorts<>
//! layers implement (they need the processor buffers), so the tests provide a
//! no-op implementation here.
class TestAudioPortsModel : public AudioPortsModel
{
public:
	using AudioPortsModel::AudioPortsModel;

private:
	void bufferPropertiesChanging(ch_cnt_t, ch_cnt_t, f_cnt_t) override
	{
	}
};

} // namespace

} // namespace lmms


class AudioPortsTest : public QObject
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

	//! Set and reset every pin of a 4x4 pin matrix and verify the pin state
	//! and the cached "used track/processor channel" bookkeeping
	void pinMatrix4x4SetReset()
	{
		using namespace lmms;

		// 4 processor inputs/outputs and 4 track channels -> 4x4 pin matrices
		TestAudioPortsModel model{4, 4, false};
		model.setTrackChannelCount(4);

		QCOMPARE(static_cast<int>(model.trackChannelCount()), 4);
		QCOMPARE(static_cast<int>(model.in().trackChannelCount()), 4);
		QCOMPARE(static_cast<int>(model.in().channelCount()), 4);
		QCOMPARE(static_cast<int>(model.out().channelCount()), 4);
		QCOMPARE(static_cast<int>(model.in().pins().size()), 4);
		QCOMPARE(static_cast<int>(model.in().pins(3).size()), 4);
		QCOMPARE(static_cast<int>(model.out().pins().size()), 4);

		// Default connections: only the first two track channels are connected
		QVERIFY(model.in().enabled(0, 0));
		QVERIFY(model.in().enabled(1, 1));
		QVERIFY(!model.in().enabled(0, 1));
		QVERIFY(!model.in().enabled(1, 0));
		QVERIFY(!model.in().enabled(2, 2));
		QVERIFY(!model.in().enabled(3, 3));

		QSignalSpy dataChangedSpy{&model, &AudioPortsModel::dataChanged};
		QVERIFY(dataChangedSpy.isValid());

		// Reset the two default pins; each change emits dataChanged once
		model.in().setPin(0, 0, false);
		model.in().setPin(1, 1, false);
		QCOMPARE(dataChangedSpy.count(), 2);

		for (ch_cnt_t tc = 0; tc < 4; ++tc)
		{
			QVERIFY(!model.in().usedTrackChannels().test(tc));
			for (ch_cnt_t pc = 0; pc < 4; ++pc)
			{
				QVERIFY(!model.in().enabled(tc, pc));
			}
		}
		for (ch_cnt_t pc = 0; pc < 4; ++pc)
		{
			QVERIFY(!model.in().usedChannels()[pc]);
		}

		// Set all 16 pins; only changes emit dataChanged
		for (ch_cnt_t tc = 0; tc < 4; ++tc)
		{
			for (ch_cnt_t pc = 0; pc < 4; ++pc)
			{
				model.in().setPin(tc, pc, true);
			}
		}
		QCOMPARE(dataChangedSpy.count(), 2 + 16);

		for (ch_cnt_t tc = 0; tc < 4; ++tc)
		{
			QVERIFY(model.in().usedTrackChannels().test(tc));
			for (ch_cnt_t pc = 0; pc < 4; ++pc)
			{
				QVERIFY(model.in().enabled(tc, pc));
			}
		}
		for (ch_cnt_t pc = 0; pc < 4; ++pc)
		{
			QVERIFY(model.in().usedChannels()[pc]);
		}

		// Setting an already-enabled pin is a no-op and emits nothing
		model.in().setPin(2, 3, true);
		QCOMPARE(dataChangedSpy.count(), 2 + 16);

		// Reset all 16 pins
		for (ch_cnt_t tc = 0; tc < 4; ++tc)
		{
			for (ch_cnt_t pc = 0; pc < 4; ++pc)
			{
				model.in().setPin(tc, pc, false);
			}
		}
		QCOMPARE(dataChangedSpy.count(), 2 + 16 + 16);

		for (ch_cnt_t tc = 0; tc < 4; ++tc)
		{
			QVERIFY(!model.in().usedTrackChannels().test(tc));
			for (ch_cnt_t pc = 0; pc < 4; ++pc)
			{
				QVERIFY(!model.in().enabled(tc, pc));
			}
		}
		for (ch_cnt_t pc = 0; pc < 4; ++pc)
		{
			QVERIFY(!model.in().usedChannels()[pc]);
		}
	}

	//! Save -> load round trip: the runtime state configured by an
	//! AudioPortsSettings instance (channel counts + pin matrices, owned by
	//! AudioPortsModel) must survive saving and loading unchanged
	void audioPortsSettingsSerializationRoundTrip()
	{
		using namespace lmms;

		// AudioPortsSettings is a compile-time description of an audio processor.
		// The runtime state it parameterizes is the AudioPortsModel owned by the
		// processor, which is what gets saved to and loaded from the project file.
		constexpr auto settings = AudioPortsSettings{
			AudioDataKind::F32, false, 4, 4, false, true
		};
		static_assert(Validate<settings>{}());

		// Compile-time check of the Part A abstraction hierarchy
		static_assert(std::is_base_of_v<AudioPortsModel, AudioPorts<settings>>);

		TestAudioPortsModel source{settings.inputs, settings.outputs, false};
		source.setTrackChannelCount(4);

		// A non-default 4x4 pattern
		for (ch_cnt_t tc = 0; tc < 4; ++tc)
		{
			source.in().setPin(tc, tc, true);
			source.out().setPin(tc, 3 - tc, true);
		}
		source.in().setPin(1, 1, false);
		source.in().setPin(1, 2, true);

		// Save and serialize to text
		QDomDocument doc;
		auto elem = doc.createElement("test-element");
		doc.appendChild(elem);
		source.saveSettings(doc, elem);

		QDomDocument doc2;
		QVERIFY(doc2.setContent(doc.toString()));
		const auto elem2 = doc2.documentElement();

		// Load into a model that starts out with different channel counts
		TestAudioPortsModel loaded{2, 2, false};
		loaded.setTrackChannelCount(4);

		QSignalSpy dataChangedSpy{&loaded, &AudioPortsModel::dataChanged};
		QSignalSpy propertiesChangedSpy{&loaded, &AudioPortsModel::propertiesChanged};
		QVERIFY(dataChangedSpy.isValid());
		QVERIFY(propertiesChangedSpy.isValid());

		loaded.loadSettings(elem2);

		// The saved channel counts are restored ...
		QCOMPARE(static_cast<int>(loaded.in().channelCount()), static_cast<int>(source.in().channelCount()));
		QCOMPARE(static_cast<int>(loaded.out().channelCount()), static_cast<int>(source.out().channelCount()));
		QCOMPARE(static_cast<int>(loaded.trackChannelCount()), static_cast<int>(source.trackChannelCount()));

		// ... and so is every pin of both matrices
		QVERIFY(loaded.in().pins() == source.in().pins());
		QVERIFY(loaded.out().pins() == source.out().pins());
		for (ch_cnt_t tc = 0; tc < 4; ++tc)
		{
			for (ch_cnt_t pc = 0; pc < 4; ++pc)
			{
				QCOMPARE(loaded.in().enabled(tc, pc), source.in().enabled(tc, pc));
				QCOMPARE(loaded.out().enabled(tc, pc), source.out().enabled(tc, pc));
			}
		}

		// Changing the processor channel counts and the pins is reported once each
		QCOMPARE(propertiesChangedSpy.count(), 1);
		QCOMPARE(dataChangedSpy.count(), 1);

		// Saving the loaded model again produces the same XML
		QDomDocument doc3;
		auto elem3 = doc3.createElement("test-element");
		doc3.appendChild(elem3);
		loaded.saveSettings(doc3, elem3);
		QCOMPARE(doc3.toString(), doc.toString());
	}
};

QTEST_GUILESS_MAIN(AudioPortsTest)
#include "AudioPortsTest.moc"
