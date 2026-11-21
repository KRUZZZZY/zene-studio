/*
 * ControlHostedBusLayoutTest.cpp - VST3/CLAP plug-ins whose bus layout is not one stereo in + one stereo
 *                                  out are hosted, or refused with their reason (cluster stress-3).
 *
 * The 2026-10-27 plug-in stress run had plugin.load refuse 13 third-party plug-ins with "the engine could
 * not instantiate 'vst3effect'" (or 'clapeffect') and nothing more: LSP's mono x4/x8/x16 mixers, Crossover
 * Mono x8 and Oscilloscope x1, ZAM's ZamCompX2/ZamGateX2 (a mono side-chain input) and Stochas (no audio
 * port at all). Every one but Stochas has an ODD channel count on one side, and the hosts handed that count
 * to AudioPortsModel as the track channel count, which refuses an odd one by throwing.
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

#include <QtTest>

#include <cmath>
#include <vector>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>

#include "AudioBus.h"
#include "AudioPortsModel.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DummyInstrument.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "HostedTrackChannels.h"
#include "InstrumentTrack.h"
#include "Song.h"

#ifndef CLAP_TEST_EFFECT_DIR
#define CLAP_TEST_EFFECT_DIR ""
#endif
#ifndef VST3_TEST_THREE_INSTRUMENT_BUNDLE
#define VST3_TEST_THREE_INSTRUMENT_BUNDLE ""
#endif

using namespace lmms;

namespace
{

//! The plugin.list entry of @a format named @a name (its "name" or "display_name"), or an empty object.
QJsonObject listedDevice(const QString& format, const QString& name)
{
	const ControlResult listed = ControlRegistry::instance()->invoke(QStringLiteral("plugin.list"),
		QJsonObject{{QStringLiteral("format"), format}});
	for (const QJsonValue& value : listed.result.value(QStringLiteral("devices")).toArray())
	{
		const QJsonObject device = value.toObject();
		if (device.value(QStringLiteral("name")).toString() == name
			|| device.value(QStringLiteral("display_name")).toString() == name)
		{
			return device;
		}
	}
	return QJsonObject();
}

//! plugin.load of the listed @a format device @a name onto a new instrument track.
ControlResult loadOnNewTrack(const QString& format, const QString& name, InstrumentTrack** track)
{
	const QJsonObject device = listedDevice(format, name);
	if (device.isEmpty())
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("'%1' is not listed").arg(name));
	}
	*track = new InstrumentTrack(Engine::getSong());
	return ControlRegistry::instance()->invoke(QStringLiteral("plugin.load"),
		QJsonObject{{QStringLiteral("target"), control::trackIdOf(*track)},
			{QStringLiteral("device"), device.value(QStringLiteral("id")).toString()}});
}

//! The three-channel VST3 fixture's key: its bundle and its class, the attributes the VST3 hosts read.
Plugin::Descriptor::SubPluginFeatures::Key threeChannelKey()
{
	Plugin::Descriptor::SubPluginFeatures::Key::AttributeMap attributes;
	attributes.insert(QStringLiteral("file"), QStringLiteral(VST3_TEST_THREE_INSTRUMENT_BUNDLE));
	attributes.insert(QStringLiteral("class"), QStringLiteral("Zene VST3 Three-Channel Test Instrument"));
	return Plugin::Descriptor::SubPluginFeatures::Key(nullptr, QStringLiteral("three-channel"), attributes);
}

} // namespace

class ControlHostedBusLayoutTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		// Tell PluginFactory where the build-tree plugins live *before* it is
		// first instantiated (it reads LMMS_PLUGIN_DIR in its constructor).
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		qunsetenv("CLAP_TEST_GAIN_LAYOUT");
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The rule itself: whole L/R pairs, never fewer than the stereo pair, never fewer than either side.
	void trackChannelsAreWholePairs()
	{
		QCOMPARE(hostedTrackChannels(1, 1), ch_cnt_t{2});
		QCOMPARE(hostedTrackChannels(2, 2), ch_cnt_t{2});
		QCOMPARE(hostedTrackChannels(3, 2), ch_cnt_t{4});   // stereo main + mono side-chain (ZamCompX2)
		QCOMPARE(hostedTrackChannels(5, 1), ch_cnt_t{6});   // Mixer x4 Mono
		QCOMPARE(hostedTrackChannels(1, 9), ch_cnt_t{10});  // Crossover Mono x8
		QCOMPARE(hostedTrackChannels(17, 1), ch_cnt_t{18}); // Mixer x16 Mono
		QCOMPARE(hostedTrackChannels(0, 3), ch_cnt_t{4});
		QCOMPARE(hostedTrackChannels(4, 8), ch_cnt_t{8});
	}

	//! The in-tree CLAP fixture with a stereo main input and a MONO side-chain (three input channels) loads
	//! through plugin.load and processes its main pair. Before the fix: "could not instantiate 'clapeffect'".
	void clapEffectWithAMonoSideChainLoadsAndProcesses()
	{
		if (!QFileInfo::exists(QStringLiteral(CLAP_TEST_EFFECT_DIR))) { QSKIP("this build has no CLAP effect fixture"); }
		ConfigManager::inst()->setVSTDir(QStringLiteral(CLAP_TEST_EFFECT_DIR));
		qputenv("CLAP_TEST_GAIN_LAYOUT", "mono-sidechain");
		InstrumentTrack* track = nullptr;
		const ControlResult loaded = loadOnNewTrack(QStringLiteral("clap"), QStringLiteral("LMMS CLAP Test Gain"), &track);
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
		QCOMPARE(loaded.result.value(QStringLiteral("plugin")).toString(), QStringLiteral("clapeffect"));
		const ControlResult gain = ControlRegistry::instance()->invoke(QStringLiteral("plugin.param_set"),
			QJsonObject{{QStringLiteral("target"), control::trackIdOf(track)},
				{QStringLiteral("plugin"), loaded.result.value(QStringLiteral("id")).toString()},
				{QStringLiteral("name"), QStringLiteral("Gain")}, {QStringLiteral("value"), 0.5}});
		QVERIFY2(gain.ok, qPrintable(gain.errorMessage));

		EffectChain* chain = track->audioBusHandle()->effects();
		QCOMPARE(chain->effects().size(), std::size_t{1});
		const auto frames = Engine::audioEngine()->framesPerPeriod();
		std::vector<SampleFrame> storage(frames, SampleFrame(0.5f, -0.25f));
		SampleFrame* busData[1] = {storage.data()};
		AudioBus bus{busData, 1, frames};
		QVERIFY(chain->processAudioBuffer(bus));
		QVERIFY2(std::abs(storage[0][0] - 0.25f) < 1e-6f, qPrintable(QStringLiteral("left %1").arg(storage[0][0])));
		QVERIFY2(std::abs(storage[0][1] + 0.125f) < 1e-6f, qPrintable(QStringLiteral("right %1").arg(storage[0][1])));
		qunsetenv("CLAP_TEST_GAIN_LAYOUT");
	}

	//! A CLAP plug-in with no audio port (a note-only one, Stochas) is refused - and the refusal says why,
	//! instead of the bare "the engine could not instantiate 'clapeffect'".
	void clapEffectWithNoAudioPortIsRefusedWithItsReason()
	{
		if (!QFileInfo::exists(QStringLiteral(CLAP_TEST_EFFECT_DIR))) { QSKIP("this build has no CLAP effect fixture"); }
		ConfigManager::inst()->setVSTDir(QStringLiteral(CLAP_TEST_EFFECT_DIR));
		qputenv("CLAP_TEST_GAIN_LAYOUT", "no-audio");
		InstrumentTrack* track = nullptr;
		const ControlResult loaded = loadOnNewTrack(QStringLiteral("clap"), QStringLiteral("LMMS CLAP Test Gain"), &track);
		qunsetenv("CLAP_TEST_GAIN_LAYOUT");
		QVERIFY(!loaded.ok);
		QVERIFY2(loaded.errorMessage.contains(QStringLiteral("has no usable audio ports (0 input, 0 output channels")),
			qPrintable(loaded.errorMessage));
		QVERIFY2(loaded.errorMessage.contains(QStringLiteral("note-only")), qPrintable(loaded.errorMessage));
	}

	//! The in-tree VST3 fixture with a three-channel (L R C) output: the effect host builds it. Before the fix
	//! it threw "There must be an even number of track channels".
	void vst3EffectHostsAThreeChannelOutput()
	{
		if (!QFileInfo::exists(QStringLiteral(VST3_TEST_THREE_INSTRUMENT_BUNDLE)))
		{
			QSKIP("this build has no three-channel VST3 fixture (WANT_VST3_TEST_INSTRUMENT is off)");
		}
		auto key = threeChannelKey();
		EffectChain chain{nullptr};
		Effect* effect = Effect::instantiate(QStringLiteral("vst3effect"), &chain, &key);
		QVERIFY2(effect != nullptr, "vst3effect refused a plug-in with a three-channel output");
		const AudioPortsModel* ports = effect->audioPortsModel();
		QVERIFY(ports != nullptr);
		QCOMPARE(ports->out().channelCount(), ch_cnt_t{3});
		QCOMPARE(ports->trackChannelCount(), ch_cnt_t{4});
		chain.appendEffect(effect);
	}

	//! The same fixture in the instrument host, which sized its track channels the same way.
	void vst3InstrumentHostsAThreeChannelOutput()
	{
		if (!QFileInfo::exists(QStringLiteral(VST3_TEST_THREE_INSTRUMENT_BUNDLE)))
		{
			QSKIP("this build has no three-channel VST3 fixture (WANT_VST3_TEST_INSTRUMENT is off)");
		}
		auto key = threeChannelKey();
		auto* track = new InstrumentTrack(Engine::getSong());
		Instrument* instrument = track->loadInstrument(QStringLiteral("vst3instrument"), &key);
		QVERIFY(instrument != nullptr);
		QVERIFY2(dynamic_cast<DummyInstrument*>(instrument) == nullptr,
			"vst3instrument fell back to a placeholder for a three-channel output");
	}

	//! The stress run's own subjects, where this machine has the plug-in farm (read-only): each loads.
	void thirdPartySubjectsLoad_data()
	{
		QTest::addColumn<QString>("dir");
		QTest::addColumn<QString>("format");
		QTest::addColumn<QString>("name");
		const QString root = QDir::homePath() + QStringLiteral("/.cache/zene-plugin-farm/root/");
		QTest::newRow("vst3 LSP Mixer x4 Mono") << root + QStringLiteral("lsp-plugins-vst3-1.2.35-1.1-x86_64_v3/usr/lib/vst3")
			<< QStringLiteral("vst3") << QStringLiteral("Mixer x4 Mono");
		QTest::newRow("clap ZamCompX2") << root + QStringLiteral("zam-plugins-clap-4.5-1.1-x86_64_v3/usr/lib/clap")
			<< QStringLiteral("clap") << QStringLiteral("com.zamaudio.ZamCompX2");
	}

	void thirdPartySubjectsLoad()
	{
		QFETCH(QString, dir);
		QFETCH(QString, format);
		QFETCH(QString, name);
		if (!QFileInfo::exists(dir)) { QSKIP(qPrintable(QStringLiteral("the plug-in farm has no %1").arg(dir))); }
		ConfigManager::inst()->setVSTDir(dir);
		InstrumentTrack* track = nullptr;
		const ControlResult loaded = loadOnNewTrack(format, name, &track);
		QVERIFY2(loaded.ok, qPrintable(loaded.errorMessage));
	}
};

QTEST_GUILESS_MAIN(ControlHostedBusLayoutTest)
#include "ControlHostedBusLayoutTest.moc"
