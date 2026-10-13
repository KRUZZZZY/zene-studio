/*
 * DeviceToolsTest.cpp - rack splits, parameter search, aliases, randomise and blend, folding and device options
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
/*! 0.5.0 L10 (W3): a parallel rack splits its input by band, mid / side or left / right and the
 *  sum puts it back together (MX-108); parameters are found by name or alias (MX-089) and renamed
 *  without changing their address (MX-097); new settings are randomised or blended from A/B
 *  (MX-093); a device folds (MX-113) and carries compatibility options (MX-099 / MX-094) - each
 *  saved only when set and undone in one step; a device's delta is what it changes (MX-080); a clip is
 *  printed through a chain's devices (MX-083); a device is hot-swapped in its slot (WF-016). */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>

#include <cmath>
#include <vector>

#include "AudioBus.h"
#include "AudioDevice.h"
#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlDeviceSupport.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DeviceOptions.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "ParameterAliases.h"
#include "Rack.h"
#include "RackSplit.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SampleFrame.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

using namespace lmms;

namespace
{

constexpr float kRate = 44100.f;
constexpr double kTwoPi = 6.283185307179586;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

MixerChannel* channel(int index) { return Engine::mixer()->mixerChannel(index); }

QString ch(int index) { return control::channelIdOf(channel(index)); }

//! The built-in effect named @a name: its dev-<n> in plugin.list, or empty.
QString deviceNamed(const QString& name)
{
	for (const QJsonValue& device : run(QStringLiteral("plugin.list"), {{QStringLiteral("kind"), QStringLiteral("effect")}})
		.result.value(QStringLiteral("devices")).toArray())
	{
		if (device.toObject().value(QStringLiteral("name")).toString() == name) { return device.toObject().value(QStringLiteral("id")).toString(); }
	}
	return QString();
}

//! Load the built-in device named @a name onto @a target: its fx-<n>, or empty.
QString loadByName(const QString& target, const QString& name)
{
	const QString device = deviceNamed(name);
	if (device.isEmpty()) { return QString(); }
	return run(QStringLiteral("plugin.load"), {{QStringLiteral("target"), target}, {QStringLiteral("device"), device}})
		.result.value(QStringLiteral("id")).toString();
}

double parameterValue(const QString& target, const QString& plugin, const QString& name)
{
	// plugin.param_get reads ONE parameter, by 'name' (or 'index').
	const ControlResult read = run(QStringLiteral("plugin.param_get"), {{QStringLiteral("target"), target},
		{QStringLiteral("plugin"), plugin}, {QStringLiteral("name"), name}});
	return read.ok ? read.result.value(QStringLiteral("parameter")).toObject().value(QStringLiteral("value")).toDouble() : -1e9;
}

bool setParameter(const QString& target, const QString& plugin, const QString& name, double value)
{
	return run(QStringLiteral("plugin.param_set"), {{QStringLiteral("target"), target}, {QStringLiteral("plugin"), plugin},
		{QStringLiteral("name"), name}, {QStringLiteral("value"), value}}).ok;
}

//! The peak of the last half of @a frames of a @a hz sine through @a filter (one channel read).
float bandPeak(RackSplitFilter& filter, double hz, int frames)
{
	std::vector<float> block(static_cast<std::size_t>(frames) * 2);
	for (int f = 0; f < frames; ++f)
	{
		const auto x = static_cast<float>(std::sin(kTwoPi * hz * f / kRate));
		block[2 * f] = x;
		block[2 * f + 1] = x;
	}
	filter.process(block.data(), frames);
	float peak = 0.f;
	for (int f = frames / 2; f < frames; ++f) { peak = std::max(peak, std::abs(block[2 * f])); }
	return peak;
}

RackSplit bandsAt(float hz)
{
	RackSplit split;
	split.mode = RackSplitMode::Bands;
	split.crossovers[0] = hz;
	split.crossoverCount = 1;
	return split;
}

//! @a blocks periods of a steady (@a level, @a level) signal through channel @a index's chain; the
//! last block's left peak (signed: the value of its first frame).
float runSteady(int index, float level, int blocks)
{
	static std::vector<SampleFrame> block;
	block.assign(Engine::audioEngine()->framesPerPeriod(), SampleFrame());
	SampleFrame* data[1] = {block.data()};
	for (int b = 0; b < blocks; ++b)
	{
		for (SampleFrame& frame : block) { frame = SampleFrame(level, level); }
		AudioBus bus{data, 1, static_cast<f_cnt_t>(block.size())};
		channel(index)->m_fxChain.processAudioBuffer(bus);
	}
	return block.front()[0];
}

Effect* effectOn(int index)
{
	const std::vector<Effect*>& effects = channel(index)->m_fxChain.effects();
	return effects.empty() ? nullptr : effects.back();
}

//! The amplifier @a effect's Volume parameter, or nullptr.
AutomatableModel* volumeOf(Effect* effect)
{
	ControlResult error;
	return controlResolveParameter(effect, QStringLiteral("Volume"), 0, false, &error);
}

} // namespace

class DeviceToolsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		Engine::audioEngine()->audioDev()->stopProcessing();  // the test reads and writes channel buffers itself
		while (Engine::mixer()->numChannels() <= 3) { Engine::mixer()->createChannel(); }
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! MX-108: each band keeps its own frequencies, and two bands sum flat in level.
	void bandsSplitAndSumFlat()
	{
		RackSplitFilter low;
		RackSplitFilter high;
		low.configure(bandsAt(1000.f), 0, 2, kRate);
		high.configure(bandsAt(1000.f), 1, 2, kRate);
		QVERIFY(low.active() && high.active());
		QVERIFY(bandPeak(low, 100.0, 8820) > 0.95f);
		QVERIFY(bandPeak(low, 10000.0, 8820) < 0.02f);   // 24 dB / octave: about -80 dB a decade up
		QVERIFY(bandPeak(high, 10000.0, 8820) > 0.95f);
		QVERIFY(bandPeak(high, 100.0, 8820) < 0.02f);
		// Low plus high at the crossover itself: an all-pass, so the level is the input's.
		RackSplitFilter a;
		RackSplitFilter b;
		a.configure(bandsAt(1000.f), 0, 2, kRate);
		b.configure(bandsAt(1000.f), 1, 2, kRate);
		const int frames = 8820;
		std::vector<float> one(static_cast<std::size_t>(frames) * 2);
		for (int f = 0; f < frames; ++f) { one[2 * f] = one[2 * f + 1] = static_cast<float>(std::sin(kTwoPi * 1000.0 * f / kRate)); }
		std::vector<float> two = one;
		a.process(one.data(), frames);
		b.process(two.data(), frames);
		float peak = 0.f;
		for (int f = frames / 2; f < frames; ++f) { peak = std::max(peak, std::abs(one[2 * f] + two[2 * f])); }
		QVERIFY2(std::abs(peak - 1.f) < 0.02f, qPrintable(QString::number(peak)));
	}

	//! MX-108: mid / side and left / right parts add back to the input exactly.
	void midSideAndLeftRightAddBack()
	{
		RackSplit split;
		split.mode = RackSplitMode::MidSide;
		RackSplitFilter mid;
		RackSplitFilter side;
		RackSplitFilter third;
		mid.configure(split, 0, 3, kRate);
		side.configure(split, 1, 3, kRate);
		third.configure(split, 2, 3, kRate);
		float m[2] = {1.f, 0.5f};
		float s[2] = {1.f, 0.5f};
		float t[2] = {1.f, 0.5f};
		mid.process(m, 1);
		side.process(s, 1);
		third.process(t, 1);
		QCOMPARE(m[0], 0.75f);
		QCOMPARE(s[1], -0.25f);
		QCOMPARE(m[0] + s[0], 1.f);
		QCOMPARE(m[1] + s[1], 0.5f);
		QCOMPARE(t[0], 0.f);  // a third chain gets silence
		split.mode = RackSplitMode::LeftRight;
		RackSplitFilter right;
		right.configure(split, 1, 2, kRate);
		float r[2] = {1.f, 0.5f};
		right.process(r, 1);
		QCOMPARE(r[0], 0.f);
		QCOMPARE(r[1], 0.5f);
	}

	//! MX-108: rack.split needs two chains, renders through the rack, is saved and is undone.
	void aRackSplitIsSetSavedAndUndone()
	{
		const QJsonObject bands{{QStringLiteral("channel"), ch(2)}, {QStringLiteral("mode"), QStringLiteral("mid_side")}};
		QCOMPARE(run(QStringLiteral("rack.split"), bands).errorKind, ControlErrorKind::Refused);  // one chain
		QVERIFY(run(QStringLiteral("rack.add_chain"), {{QStringLiteral("channel"), ch(2)}}).ok);
		const ControlResult made = run(QStringLiteral("rack.split"), bands);
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		QVERIFY(made.result.value(QStringLiteral("applies")).toBool());
		Rack& rack = channel(2)->m_rack;
		QCOMPARE(rack.split().mode, RackSplitMode::MidSide);
		// Two empty chains fed the mid and the side sum to the input.
		const auto frames = static_cast<f_cnt_t>(Engine::audioEngine()->framesPerPeriod());
		std::vector<SampleFrame> block(frames, SampleFrame(0.8f, -0.2f));
		SampleFrame* data[1] = {block.data()};
		AudioBus bus{data, 1, frames};
		bus.quietChannels().set(0, false);
		bus.quietChannels().set(1, false);
		rack.processAudioBuffer(bus);
		QVERIFY(std::abs(block[frames / 2][0] - 0.8f) < 1e-5f);
		QVERIFY(std::abs(block[frames / 2][1] + 0.2f) < 1e-5f);
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("mixerchannel"));
		rack.saveSettings(doc, element);
		QCOMPARE(element.firstChildElement(QStringLiteral("rack")).attribute(QStringLiteral("split")), QStringLiteral("mid_side"));
		QVERIFY(run(QStringLiteral("rack.split"), {{QStringLiteral("channel"), ch(2)}, {QStringLiteral("mode"), QStringLiteral("bands")},
			{QStringLiteral("crossovers"), QJsonArray{25000}}}).errorKind == ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(rack.split().mode, RackSplitMode::None);
		QDomElement plain = doc.createElement(QStringLiteral("mixerchannel"));
		rack.saveSettings(doc, plain);
		QVERIFY(!plain.firstChildElement(QStringLiteral("rack")).hasAttribute(QStringLiteral("split")));
	}

	//! new-p12-195: a split whose count exceeds the five frequencies the array holds is clamped where
	//! it is stored, and saving such a split reads nothing past the end of crossovers.
	void anOverlongCrossoverCountIsClampedAndSavedSafely()
	{
		RackSplit split;
		split.mode = RackSplitMode::Bands;
		for (int k = 0; k < RackSplit::MaxBands - 1; ++k) { split.crossovers[static_cast<std::size_t>(k)] = 100.f * (k + 1); }
		split.crossoverCount = RackSplit::MaxBands;  // one more than the array holds: no sixth frequency
		const int index = Engine::mixer()->createChannel();
		Rack& rack = channel(index)->m_rack;
		QVERIFY(rack.addChain() >= 0);
		rack.setSplit(split);
		QVERIFY(rack.split().crossoverCount <= RackSplit::MaxBands - 1);
		// Saving a raw split of that shape writes no more than the array holds either.
		QDomDocument raw;
		QDomElement rawRack = raw.createElement(QStringLiteral("rack"));
		split.save(rawRack);
		QVERIFY(rawRack.attribute(QStringLiteral("crossovers")).split(QLatin1Char(',')).size() <= RackSplit::MaxBands - 1);
		// The rack's own save carries the stored count's frequencies, nothing past them.
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("mixerchannel"));
		rack.saveSettings(doc, element);
		QCOMPARE(element.firstChildElement(QStringLiteral("rack")).attribute(QStringLiteral("crossovers")).split(QLatin1Char(',')).size(),
			rack.split().crossoverCount);
	}

	//! MX-089 / MX-097: a parameter is found by its name, renamed, found by its alias, keeps its own name
	//! as its address, saves the alias only when set, and the rename undoes.
	void aParameterIsFoundAndRenamed()
	{
		const QString amp = loadByName(ch(1), QStringLiteral("amplifier"));
		if (amp.isEmpty()) { QSKIP("this build has no amplifier module"); }
		const QJsonObject device{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), amp}};
		QJsonObject rename = device;
		rename.insert(QStringLiteral("name"), QStringLiteral("Volume"));
		rename.insert(QStringLiteral("alias"), QStringLiteral("Drive"));
		QVERIFY(run(QStringLiteral("plugin.param_rename"), rename).ok);
		Effect* effect = effectOn(1);
		const QString key = parameteraliases::keysOf(effect, {volumeOf(effect)}).value(0);
		QVERIFY2(key.startsWith(QStringLiteral("id:")), qPrintable(key));
		QCOMPARE(effect->deviceOptions().alias(key), QStringLiteral("Drive"));  // W6: by the parameter's key...
		QVERIFY(effect->deviceOptions().alias(QStringLiteral("Volume")).isEmpty());  // ...not its translated name
		const ControlResult found = run(QStringLiteral("plugin.param_search"), {{QStringLiteral("query"), QStringLiteral("driv")},
			{QStringLiteral("target"), ch(1)}});
		QCOMPARE(found.result.value(QStringLiteral("count")).toInt(), 1);
		QCOMPARE(found.result.value(QStringLiteral("parameters")).toArray().first().toObject().value(QStringLiteral("name")).toString(),
			QStringLiteral("Volume"));
		QVERIFY(setParameter(ch(1), amp, QStringLiteral("Volume"), 120.0));  // still addressed by its own name
		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("effect"));
		effect->saveSettings(doc, element);
		QVERIFY(element.attribute(QStringLiteral("aliases")).contains(QStringLiteral("Drive")));
		QVERIFY(!element.hasAttribute(QStringLiteral("folded")));
		DeviceOptions back;
		back.load(element);
		QCOMPARE(back.alias(key), QStringLiteral("Drive"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the parameter write
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the rename
		QVERIFY(effect->deviceOptions().aliases().isEmpty());
		rename.insert(QStringLiteral("name"), QStringLiteral("No such knob"));
		QCOMPARE(run(QStringLiteral("plugin.param_rename"), rename).errorKind, ControlErrorKind::NotFound);
	}

	//! W6: an alias is kept by the parameter's stable key, so it follows the parameter when its display name changes
	//! (the project opened in another language). A project saved before W6 keyed it by the display name: that entry
	//! is still read by the name and is moved to the key, while an entry that names no parameter here is kept.
	void anAliasFollowsTheParameterNotItsName()
	{
		Effect* effect = effectOn(1);
		if (effect == nullptr) { QSKIP("this build has no amplifier module"); }
		AutomatableModel* volume = volumeOf(effect);
		QVERIFY(volume != nullptr);
		QVERIFY(run(QStringLiteral("plugin.param_rename"), {{QStringLiteral("target"), ch(1)},
			{QStringLiteral("plugin"), control::effectIdOf(effect)}, {QStringLiteral("name"), QStringLiteral("Volume")},
			{QStringLiteral("alias"), QStringLiteral("Drive")}}).ok);
		volume->setDisplayName(QStringLiteral("Lautstaerke"));  // the same knob, named in another language
		const ControlResult found = run(QStringLiteral("plugin.param_search"), {{QStringLiteral("query"), QStringLiteral("driv")},
			{QStringLiteral("target"), ch(1)}});
		QCOMPARE(found.result.value(QStringLiteral("count")).toInt(), 1);
		QCOMPARE(found.result.value(QStringLiteral("parameters")).toArray().first().toObject().value(QStringLiteral("name")).toString(),
			QStringLiteral("Lautstaerke"));
		QCOMPARE(parameteraliases::shownNamesOf(effect, {volume}).value(0), QStringLiteral("Drive"));
		volume->setDisplayName(QStringLiteral("Volume"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(effect->deviceOptions().isDefault());

		QDomDocument doc;
		QDomElement saved = doc.createElement(QStringLiteral("effect"));
		saved.setAttribute(QStringLiteral("aliases"), QStringLiteral("{\"Volume\":\"Level\",\"Pegel\":\"Gain trim\"}"));
		effect->deviceOptions().load(saved);  // as a project saved before W6 reads back
		QCOMPARE(parameteraliases::aliasesOf(effect, {volume}, effect->deviceOptions()).value(0), QStringLiteral("Level"));
		QVERIFY(parameteraliases::adoptLegacyAliases(effect, &effect->deviceOptions()));
		const QString key = parameteraliases::keysOf(effect, {volume}).value(0);
		QCOMPARE(effect->deviceOptions().alias(key), QStringLiteral("Level"));
		QVERIFY(effect->deviceOptions().alias(QStringLiteral("Volume")).isEmpty());
		QCOMPARE(effect->deviceOptions().alias(QStringLiteral("Pegel")), QStringLiteral("Gain trim"));  // no such parameter: kept
		QVERIFY(!parameteraliases::adoptLegacyAliases(effect, &effect->deviceOptions()));  // nothing left to move
		const QJsonObject reported = parameteraliases::toJson(effect, effect->deviceOptions());
		QCOMPARE(reported.value(QStringLiteral("Volume")).toString(), QStringLiteral("Level"));  // the wire still names it
		QCOMPARE(reported.value(QStringLiteral("Pegel")).toString(), QStringLiteral("Gain trim"));
		effect->deviceOptions() = DeviceOptions();
	}

	//! MX-093: randomise moves every setting inside its range; blend sets the A/B midpoint; both undo.
	void settingsAreRandomisedAndBlended()
	{
		Effect* effect = effectOn(1);
		if (effect == nullptr) { QSKIP("this build has no amplifier module"); }
		const QString amp = control::effectIdOf(effect);
		const QJsonObject device{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), amp}};
		QVERIFY(setParameter(ch(1), amp, QStringLiteral("Volume"), 100.0));
		QCOMPARE(run(QStringLiteral("plugin.blend"), device).errorKind, ControlErrorKind::Refused);  // nothing stored
		QJsonObject random = device;
		random.insert(QStringLiteral("amount"), 1.0);
		random.insert(QStringLiteral("seed"), 7);
		const ControlResult shaken = run(QStringLiteral("plugin.randomise"), random);
		QVERIFY2(shaken.ok, qPrintable(shaken.errorMessage));
		const double volume = parameterValue(ch(1), amp, QStringLiteral("Volume"));
		QVERIFY(volume >= 0.0 && volume <= 200.0 && std::abs(volume - 100.0) > 1e-3);
		QVERIFY(effect->isEnabled());  // the device's own switch stays
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(parameterValue(ch(1), amp, QStringLiteral("Volume")), 100.0);
		QJsonObject store = device;
		store.insert(QStringLiteral("action"), QStringLiteral("store"));
		store.insert(QStringLiteral("slot"), QStringLiteral("a"));
		QVERIFY(run(QStringLiteral("plugin.ab"), store).ok);
		QVERIFY(setParameter(ch(1), amp, QStringLiteral("Volume"), 50.0));
		store.insert(QStringLiteral("slot"), QStringLiteral("b"));
		QVERIFY(run(QStringLiteral("plugin.ab"), store).ok);
		QJsonObject half = device;
		half.insert(QStringLiteral("amount"), 0.5);
		QVERIFY(run(QStringLiteral("plugin.blend"), half).ok);
		QVERIFY(std::abs(parameterValue(ch(1), amp, QStringLiteral("Volume")) - 75.0) < 1e-3);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(std::abs(parameterValue(ch(1), amp, QStringLiteral("Volume")) - 50.0) < 1e-3);
	}

	//! MX-080: the delta of a device that changes nothing is silence; of one that doubles the level,
	//! the input itself; the switch toggles and is not journalled.
	void theDeltaIsWhatTheDeviceChanges()
	{
		const QString amp = loadByName(ch(3), QStringLiteral("amplifier"));
		if (amp.isEmpty()) { QSKIP("this build has no amplifier module"); }
		const QJsonObject device{{QStringLiteral("target"), ch(3)}, {QStringLiteral("plugin"), amp}};
		QVERIFY(setParameter(ch(3), amp, QStringLiteral("Volume"), 100.0));  // unity
		QVERIFY(std::abs(runSteady(3, 0.4f, 4) - 0.4f) < 1e-4f);
		const ControlResult on = run(QStringLiteral("plugin.delta"), device);  // toggles on
		QVERIFY2(on.ok, qPrintable(on.errorMessage));
		QVERIFY(on.result.value(QStringLiteral("on")).toBool());
		QVERIFY(std::abs(runSteady(3, 0.4f, 4)) < 1e-4f);         // it changes nothing: silence
		QVERIFY(setParameter(ch(3), amp, QStringLiteral("Volume"), 200.0));
		QVERIFY(std::abs(runSteady(3, 0.4f, 4) - 0.4f) < 1e-3f);  // twice the input, minus the input
		QJsonObject off = device;
		off.insert(QStringLiteral("on"), false);
		QVERIFY(run(QStringLiteral("plugin.delta"), off).ok);
		QVERIFY(std::abs(runSteady(3, 0.4f, 4) - 0.8f) < 1e-3f);
		QJsonObject missing = device;
		missing.insert(QStringLiteral("plugin"), QStringLiteral("fx-999999"));
		QCOMPARE(run(QStringLiteral("plugin.delta"), missing).errorKind, ControlErrorKind::NotFound);
		channel(3)->m_fxChain.clear();
	}

	//! MX-083: a clip printed through a chain's devices takes their effect, the chain stays as it was,
	//! an empty chain is refused, and undo puts the clip's source back.
	void aClipIsPrintedThroughAChain()
	{
		const QString amp = loadByName(ch(3), QStringLiteral("amplifier"));
		if (amp.isEmpty()) { QSKIP("this build has no amplifier module"); }
		QVERIFY(setParameter(ch(3), amp, QStringLiteral("Volume"), 200.0));
		Track* track = Track::create(Track::Type::Sample, Engine::getSong());
		auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		const auto rate = static_cast<int>(Engine::audioEngine()->outputSampleRate());
		clip->setSampleBuffer(std::make_shared<SampleBuffer>(std::vector<SampleFrame>(4000, SampleFrame(0.25f, 0.25f)), rate));
		const QString id = control::clipIdOf(clip);
		QCOMPARE(run(QStringLiteral("clip.process"), {{QStringLiteral("clip"), id}, {QStringLiteral("from"), ch(2)}}).errorKind,
			ControlErrorKind::Refused);  // ch-2's own chain holds no device
		const ControlResult printed = run(QStringLiteral("clip.process"), {{QStringLiteral("clip"), id}, {QStringLiteral("from"), ch(3)}});
		QVERIFY2(printed.ok, qPrintable(printed.errorMessage));
		QVERIFY(std::abs(clip->sample().data()[2000][0] - 0.5f) < 1e-3f);
		QCOMPARE(channel(3)->m_fxChain.effects().size(), std::size_t{1});  // the chain itself is untouched
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(std::abs(clip->sample().data()[2000][0] - 0.25f) < 1e-6f);
		channel(3)->m_fxChain.clear();
	}

	//! WF-016: plugin.replace puts a fresh device at the replaced one's slot of the same chain, and one
	//! control.undo brings the old one back with its settings.
	void aDeviceIsReplacedInItsSlot()
	{
		const QString amp = loadByName(ch(3), QStringLiteral("amplifier"));
		if (amp.isEmpty()) { QSKIP("this build has no amplifier module"); }
		QVERIFY(setParameter(ch(3), amp, QStringLiteral("Volume"), 150.0));
		const ControlResult swapped = run(QStringLiteral("plugin.replace"), {{QStringLiteral("target"), ch(3)},
			{QStringLiteral("plugin"), amp}, {QStringLiteral("device"), deviceNamed(QStringLiteral("amplifier"))}});
		QVERIFY2(swapped.ok, qPrintable(swapped.errorMessage));
		QCOMPARE(swapped.result.value(QStringLiteral("index")).toInt(), 0);
		const QString fresh = swapped.result.value(QStringLiteral("plugin")).toString();
		QVERIFY(fresh != amp);
		QCOMPARE(channel(3)->m_fxChain.effects().size(), std::size_t{1});  // in the old one's place, not beside it
		QCOMPARE(control::effectIdOf(effectOn(3)), fresh);
		QCOMPARE(parameterValue(ch(3), fresh, QStringLiteral("Volume")), 100.0);  // a fresh device: its own default
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(channel(3)->m_fxChain.effects().size(), std::size_t{1});
		QCOMPARE(parameterValue(ch(3), control::effectIdOf(effectOn(3)), QStringLiteral("Volume")), 150.0);  // the old settings
		QCOMPARE(run(QStringLiteral("plugin.replace"), {{QStringLiteral("target"), ch(3)}, {QStringLiteral("plugin"), QStringLiteral("fx-999999")},
			{QStringLiteral("device"), deviceNamed(QStringLiteral("amplifier"))}}).errorKind, ControlErrorKind::NotFound);
		channel(3)->m_fxChain.clear();
	}

	//! MX-113 / MX-099: folding and the options are carried by the device, a latency override reaches
	//! the chain's delay compensation, the generic editor refuses the plug-in's own window, and both undo.
	void foldingAndOptionsAreKeptAndUndone()
	{
		Effect* effect = effectOn(1);
		if (effect == nullptr) { QSKIP("this build has no amplifier module"); }
		const QJsonObject device{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), control::effectIdOf(effect)}};
		QVERIFY(run(QStringLiteral("plugin.fold"), device).ok);  // toggles
		QVERIFY(effect->deviceOptions().folded);
		QJsonObject options = device;
		options.insert(QStringLiteral("latency_override"), 512);
		options.insert(QStringLiteral("keep_running"), true);
		options.insert(QStringLiteral("generic_editor"), true);
		QVERIFY(run(QStringLiteral("plugin.options"), options).ok);
		QCOMPARE(channel(1)->m_fxChain.latencyFrames(), 512);
		QVERIFY(effect->deviceOptions().keepRunning);
		const ControlResult editor = run(QStringLiteral("plugin.editor_open"), device);
		QVERIFY(!editor.ok);  // a headless run refuses it before the device is asked: requires a display
		if (editor.errorKind != ControlErrorKind::Requires) { QVERIFY(editor.errorMessage.contains(QStringLiteral("generic editor"))); }
		QJsonObject badScale = device;
		badScale.insert(QStringLiteral("editor_scale"), 9.0);
		QCOMPARE(run(QStringLiteral("plugin.options"), badScale).errorKind, ControlErrorKind::InvalidArgs);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(channel(1)->m_fxChain.latencyFrames(), 0);
		QVERIFY(!effect->deviceOptions().keepRunning);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(!effect->deviceOptions().folded);
		QVERIFY(effect->deviceOptions().isDefault());
	}
};

QTEST_MAIN(DeviceToolsTest)
#include "DeviceToolsTest.moc"
