/*
 * DeviceToolsTest.cpp - rack splits, parameter search, aliases, randomise and blend (the chain-side tests: DeviceToolsChainTest.cpp)
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
 *  (MX-093); a device carries compatibility options (MX-094) - each saved only when set and undone in one
 *  step. The post-fader, delta, clip-print, replace and folding tests are in DeviceToolsChainTest.cpp. */

#include <QtTest>

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

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
#include "Plugin.h"
#include "ProjectJournal.h"
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

//! A device whose three parameters name themselves "gain", "gain" and "gain#2": the second
//! "gain" is the one that makes the key "id:gain#2" for the parameter that literally
//! names itself that. A LADSPA plug-in can own a port whose symbol ends in "#2".
class AliasCollisionEffect : public Effect
{
public:
	explicit AliasCollisionEffect(Model* parent) : Effect{&s_descriptor, parent, nullptr}
	{
		m_first.setObjectName(QStringLiteral("gain"));
		m_second.setObjectName(QStringLiteral("gain"));
		m_third.setObjectName(QStringLiteral("gain#2"));
	}

	EffectControls* controls() override { return nullptr; }

protected:
	ProcessStatus processImpl(SampleFrame*, const f_cnt_t) override { return ProcessStatus::Continue; }

public:
	FloatModel m_first{0.f, 0.f, 1.f, 0.01f, this, QStringLiteral("First")};
	FloatModel m_second{0.f, 0.f, 1.f, 0.01f, this, QStringLiteral("Second")};
	FloatModel m_third{0.f, 0.f, 1.f, 0.01f, this, QStringLiteral("Third")};

private:
	static const Plugin::Descriptor s_descriptor;
};

const Plugin::Descriptor AliasCollisionEffect::s_descriptor
{
	"aliascollisiontest",
	"Alias key collision effect",
	"Three same-named parameters, one naming itself the generated key",
	"LMMS",
	0x0100,
	Plugin::Type::Effect,
	nullptr,
	nullptr,
	nullptr
};

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

	//! Repeating the current mode and crossover must not create a second journal step.
	void reapplyingAnUnchangedRackSplitDoesNotAddUndoStep()
	{
		Rack& rack = channel(2)->m_rack;
		if (rack.chainCount() < 2)
		{
			QVERIFY(run(QStringLiteral("rack.add_chain"), {{QStringLiteral("channel"), ch(2)}}).ok);
		}
		ProjectJournal* journal = Engine::projectJournal();
		QVERIFY(journal != nullptr);
		const QJsonObject split{{QStringLiteral("channel"), ch(2)}, {QStringLiteral("mode"), QStringLiteral("bands")},
			{QStringLiteral("crossovers"), QJsonArray{1000}}};
		const int depthBefore = journal->undoDepth();
		const ControlResult first = run(QStringLiteral("rack.split"), split);
		QVERIFY2(first.ok, qPrintable(first.errorMessage));
		QCOMPARE(journal->undoDepth(), depthBefore + 1);
		const ControlResult repeated = run(QStringLiteral("rack.split"), split);
		QVERIFY2(repeated.ok, qPrintable(repeated.errorMessage));
		const bool noExtraStep = journal->undoDepth() == depthBefore + 1;
		if (!noExtraStep) { QVERIFY(run(QStringLiteral("control.undo")).ok); }
		const ControlResult reverted = run(QStringLiteral("control.undo"));
		QVERIFY2(reverted.ok, qPrintable(reverted.errorMessage));
		QCOMPARE(journal->undoDepth(), depthBefore);
		QCOMPARE(rack.split().mode, RackSplitMode::None);
		QVERIFY(noExtraStep);
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

	//! A 64-character alias keeps a supplementary Unicode character intact at the length limit.
	void aliasTruncationPreservesUnicodeCodePoints()
	{
		const QString key = QStringLiteral("parameter");
		const QString alias = QString(63, QLatin1Char('a')) + QStringLiteral("😀");
		DeviceOptions options;
		options.setAlias(key, alias);
		QCOMPARE(options.alias(key), alias);

		QDomDocument doc;
		QDomElement element = doc.createElement(QStringLiteral("effect"));
		options.save(element);
		DeviceOptions loaded;
		loaded.load(element);
		QCOMPARE(loaded.alias(key), alias);
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

	//! A legacy alias remains available when the stable key already has a newer alias.
	void aLegacyAliasSurvivesWhenItsStableKeyIsAlreadyOccupied()
	{
		AliasCollisionEffect effect(nullptr);
		DeviceOptions options;
		const QString name = effect.m_second.displayName();
		const QString key = parameteraliases::keysOf(&effect, {&effect.m_second}).value(0);
		options.setAlias(name, QStringLiteral("Legacy"));
		options.setAlias(key, QStringLiteral("Current"));

		parameteraliases::adoptLegacyAliases(&effect, &options);
		QCOMPARE(options.alias(key), QStringLiteral("Current"));
		QCOMPARE(options.alias(name), QStringLiteral("Legacy"));
	}
	void aLegacyAliasSurvivesWhenItsValueCannotBeStoredUnderItsKey()
	{
		AliasCollisionEffect effect(nullptr);
		DeviceOptions options;
		const QString key = parameteraliases::keysOf(&effect, {&effect.m_second}).value(0);
		options.setAlias(effect.m_second.displayName(), key);

		parameteraliases::adoptLegacyAliases(&effect, &options);
		QVERIFY(options.alias(key).isEmpty());
		QCOMPARE(options.alias(effect.m_second.displayName()), key);
	}

	//! MX-097: the "#2" suffix a duplicate key gets is checked against every other parameter's
	//! own key too, so a device that owns a port named "gain" twice AND a port named "gain#2"
	//! still gives each parameter a key of its own.
	void generatedKeysNeverCollideWithALiteralParameterKey()
	{
		AliasCollisionEffect effect(nullptr);
		const QList<AutomatableModel*> all{&effect.m_first, &effect.m_second, &effect.m_third};
		const QStringList keys = parameteraliases::keysOf(&effect, all);
		QCOMPARE(keys.size(), 3);
		QCOMPARE(keys.at(0), QStringLiteral("id:gain"));
		QCOMPARE(keys.at(1), QStringLiteral("id:gain#2"));
		QVERIFY2(keys.at(2) != keys.at(1), qPrintable(keys.at(2)));  // the port literally named "gain#2"
		QCOMPARE(QSet<QString>(keys.cbegin(), keys.cend()).size(), 3);  // all three distinct

		DeviceOptions options;  // renaming one must not show on another
		parameteraliases::setAlias(&effect, &effect.m_second, QStringLiteral("Level"), &options);
		const QStringList aliases = parameteraliases::aliasesOf(&effect, all, options);
		QCOMPARE(aliases.at(1), QStringLiteral("Level"));
		QVERIFY(aliases.at(0).isEmpty());
		QVERIFY2(aliases.at(2).isEmpty(), qPrintable(aliases.at(2)));
	}

	//! Two parameters can SHOW the same display name (two sub-sections each naming a knob
	//! "Gain"), and each keeps its own alias under its own stable key. The report is a JSON
	//! object, so a display name can only name one of them - the report must still show BOTH
	//! aliases rather than letting the later one overwrite the earlier.
	void twoParametersWithTheSameDisplayNameBothKeepTheirAlias()
	{
		AliasCollisionEffect effect(nullptr);
		effect.m_first.setDisplayName(QStringLiteral("Gain"));
		effect.m_second.setDisplayName(QStringLiteral("Gain"));
		const QList<AutomatableModel*> all{&effect.m_first, &effect.m_second};
		DeviceOptions options;
		parameteraliases::setAlias(&effect, &effect.m_first, QStringLiteral("Level"), &options);
		parameteraliases::setAlias(&effect, &effect.m_second, QStringLiteral("Drive"), &options);

		const QStringList aliases = parameteraliases::aliasesOf(&effect, all, options);
		QCOMPARE(aliases.at(0), QStringLiteral("Level"));
		QCOMPARE(aliases.at(1), QStringLiteral("Drive"));

		// Both aliases are in the report. The first one keeps the display name; the second is
		// reported under its own stable key, because "Gain" already names the first.
		const QJsonObject reported = parameteraliases::toJson(&effect, options);
		const QStringList keys = parameteraliases::keysOf(&effect, all);
		QSet<QString> values;
		for (auto it = reported.begin(); it != reported.end(); ++it) { values.insert(it.value().toString()); }
		QCOMPARE(values.size(), 2);
		QVERIFY2(values.contains(QStringLiteral("Level")), qPrintable(QString::fromLatin1(QJsonDocument(reported).toJson(QJsonDocument::Compact))));
		QVERIFY(values.contains(QStringLiteral("Drive")));
		QVERIFY2(reported.contains(keys.at(0)) || reported.contains(QStringLiteral("Gain")),
			"the first parameter's alias is reported under neither its display name nor its key");
		QVERIFY2(reported.contains(keys.at(1)),
			"the second parameter's alias is lost: neither its display name nor its key is in the report");
	}

	//! A display name can equal ANOTHER parameter's stable key. The duplicate that is reported under
	//! its stable key must not be overwritten by the parameter whose display name is that key.
	void aDisplayNameEqualToAnotherStableKeyDoesNotOverwriteIt()
	{
		AliasCollisionEffect effect(nullptr);
		effect.m_first.setDisplayName(QStringLiteral("Gain"));
		effect.m_second.setDisplayName(QStringLiteral("Gain"));
		const QList<AutomatableModel*> all{&effect.m_first, &effect.m_second, &effect.m_third};
		effect.m_third.setDisplayName(parameteraliases::keysOf(&effect, all).at(1));
		DeviceOptions options;
		parameteraliases::setAlias(&effect, &effect.m_first, QStringLiteral("Level"), &options);
		parameteraliases::setAlias(&effect, &effect.m_second, QStringLiteral("Drive"), &options);
		parameteraliases::setAlias(&effect, &effect.m_third, QStringLiteral("Third"), &options);

		// All three aliases are in the report, each exactly once.
		const QJsonObject reported = parameteraliases::toJson(&effect, options);
		QStringList values; for (auto it = reported.begin(); it != reported.end(); ++it) { values << it.value().toString(); } values.sort();
		QCOMPARE(values, QStringList({QStringLiteral("Drive"), QStringLiteral("Level"), QStringLiteral("Third")}));
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

	//! MX-093: a seed is an unsigned 32-bit integer: one above int's range repeats its own roll rather than
	//! reading as zero, and one above 32 bits is refused rather than truncated.
	void randomiseKeepsLargeSeeds()
	{
		Effect* effect = effectOn(1);
		if (effect == nullptr) { QSKIP("this build has no amplifier module"); }
		const QString amp = control::effectIdOf(effect);
		QJsonObject random{{QStringLiteral("target"), ch(1)}, {QStringLiteral("plugin"), amp},
			{QStringLiteral("amount"), 1.0}, {QStringLiteral("seed"), static_cast<qint64>(2147483648LL)}};
		const ControlResult large = run(QStringLiteral("plugin.randomise"), random);
		QVERIFY2(large.ok, qPrintable(large.errorMessage));
		const double largeValue = parameterValue(ch(1), amp, QStringLiteral("Volume"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);

		random.insert(QStringLiteral("seed"), 0);
		const ControlResult zero = run(QStringLiteral("plugin.randomise"), random);
		QVERIFY2(zero.ok, qPrintable(zero.errorMessage));
		const double zeroValue = parameterValue(ch(1), amp, QStringLiteral("Volume"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(std::abs(largeValue - zeroValue) > 1e-6);

		random.insert(QStringLiteral("seed"), static_cast<qint64>(4294967296LL));
		QCOMPARE(run(QStringLiteral("plugin.randomise"), random).errorKind, ControlErrorKind::InvalidArgs);
	}

	//! MX-093: randomise that changes nothing reports no change and takes no undo step of its own.
	void aRandomiseThatChangesNothingIsAnEmptyUndoStep()
	{
		const QString amp = loadByName(ch(3), QStringLiteral("amplifier"));
		if (amp.isEmpty()) { QSKIP("this build has no amplifier module"); }
		const QJsonObject device{{QStringLiteral("target"), ch(3)}, {QStringLiteral("plugin"), amp}};
		QVERIFY(setParameter(ch(3), amp, QStringLiteral("Volume"), 120.0));
		QJsonObject still = device;
		still.insert(QStringLiteral("amount"), 0.0);
		const ControlResult none = run(QStringLiteral("plugin.randomise"), still);
		QVERIFY2(none.ok, qPrintable(none.errorMessage));
		QCOMPARE(none.result.value(QStringLiteral("changed")).toInt(), 0);
		QCOMPARE(parameterValue(ch(3), amp, QStringLiteral("Volume")), 120.0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // undoes the Volume write: the no-op took no step of its own
		QCOMPARE(parameterValue(ch(3), amp, QStringLiteral("Volume")), 100.0);
		channel(3)->m_fxChain.clear();
	}
};

QTEST_MAIN(DeviceToolsTest)
#include "DeviceToolsTest.moc"
