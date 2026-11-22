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
#include <limits>
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
#include "ParameterControls.h"
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

//! A device whose ParameterControls list registers two models under one save key. Saving writes
//! each model under that key, so the second write used to overwrite the first one's value.
class RepeatedSaveKeyControls : public ParameterControls
{
public:
	explicit RepeatedSaveKeyControls(Effect* effect) : ParameterControls(effect, QStringLiteral("RepeatedSaveKeyControls")) {}
	using ParameterControls::addParameter;
	gui::EffectControlDialog* createView() override { return nullptr; }
};

class RepeatedSaveKeyEffect : public Effect
{
public:
	explicit RepeatedSaveKeyEffect(Model* parent) : Effect{&s_descriptor, parent, nullptr}, m_controls{this}
	{
		m_controls.addParameter(&m_first, QStringLiteral("gain"));
		m_controls.addParameter(&m_second, QStringLiteral("gain"));
	}

	EffectControls* controls() override { return &m_controls; }

protected:
	ProcessStatus processImpl(SampleFrame*, const f_cnt_t) override { return ProcessStatus::Continue; }

public:
	FloatModel m_first{0.f, 0.f, 1.f, 0.01f, this, QStringLiteral("First")};
	FloatModel m_second{0.f, 0.f, 1.f, 0.01f, this, QStringLiteral("Second")};

private:
	RepeatedSaveKeyControls m_controls;
	static const Plugin::Descriptor s_descriptor;
};

const Plugin::Descriptor RepeatedSaveKeyEffect::s_descriptor
{
	"repeatedsavekeytest",
	"Repeated save key effect",
	"Two parameters registered under one save key",
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
	void initTestCase();

	void cleanupTestCase();

	//! MX-108: each band keeps its own frequencies, and two bands sum flat in level.
	void bandsSplitAndSumFlat();

	//! MX-108: mid / side and left / right parts add back to the input exactly.
	void midSideAndLeftRightAddBack();

	//! MX-108: rack.split needs two chains, renders through the rack, is saved and is undone.
	void aRackSplitIsSetSavedAndUndone();

	//! Repeating the current mode and crossover must not create a second journal step.
	void reapplyingAnUnchangedRackSplitDoesNotAddUndoStep();

	//! new-p12-195: a split whose count exceeds the five frequencies the array holds is clamped where
	//! it is stored, and saving such a split reads nothing past the end of crossovers.
	void anOverlongCrossoverCountIsClampedAndSavedSafely();

	//! A 64-character alias keeps a supplementary Unicode character intact at the length limit.
	void aliasTruncationPreservesUnicodeCodePoints();

	//! MX-089 / MX-097: a parameter is found by its name, renamed, found by its alias, keeps its own name
	//! as its address, saves the alias only when set, and the rename undoes.
	void aParameterIsFoundAndRenamed();

	//! W6: an alias is kept by the parameter's stable key, so it follows the parameter when its display name changes
	//! (the project opened in another language). A project saved before W6 keyed it by the display name: that entry
	//! is still read by the name and is moved to the key, while an entry that names no parameter here is kept.
	void anAliasFollowsTheParameterNotItsName();

	//! A legacy alias remains available when the stable key already has a newer alias.
	void aLegacyAliasSurvivesWhenItsStableKeyIsAlreadyOccupied();
	void aLegacyAliasSurvivesWhenItsValueCannotBeStoredUnderItsKey();

	//! MX-097: the "#2" suffix a duplicate key gets is checked against every other parameter's
	//! own key too, so a device that owns a port named "gain" twice AND a port named "gain#2"
	//! still gives each parameter a key of its own.
	void generatedKeysNeverCollideWithALiteralParameterKey();

	//! Two parameters can SHOW the same display name (two sub-sections each naming a knob
	//! "Gain"), and each keeps its own alias under its own stable key. The report is a JSON
	//! object, so a display name can only name one of them - the report must still show BOTH
	//! aliases rather than letting the later one overwrite the earlier.
	void twoParametersWithTheSameDisplayNameBothKeepTheirAlias();

	//! A display name can equal ANOTHER parameter's stable key. The duplicate that is reported under
	//! its stable key must not be overwritten by the parameter whose display name is that key.
	void aDisplayNameEqualToAnotherStableKeyDoesNotOverwriteIt();

	//! MX-093: randomise moves every setting inside its range; blend sets the A/B midpoint; both undo.
	void settingsAreRandomisedAndBlended();

	//! MX-093: a seed is an unsigned 32-bit integer: one above int's range repeats its own roll rather than
	//! reading as zero, and one above 32 bits is refused rather than truncated.
	void randomiseKeepsLargeSeeds();

	//! A non-finite seed never reaches seedOf's uint32 cast: NaN fails the schema's integer check (floor(NaN) != NaN)
	//! before the handler runs, and the infinities are refused by the handler's range test. Nothing is written.
	void aNonFiniteSeedIsRefusedBeforeItIsRead();

	//! MX-093: randomise that changes nothing reports no change and takes no undo step of its own.
	void aRandomiseThatChangesNothingIsAnEmptyUndoStep();

	//! A device saves its list through Effect::saveSettings and reads it back through loadSettings. With two
	//! models under one key the saved value must be the first model's: the second is refused at registration.
	void aRepeatedSaveKeyKeepsTheFirstModelsValueThroughTheDeviceSave();
};

#include "DeviceToolsTestParts1.h"

QTEST_MAIN(DeviceToolsTest)
#include "DeviceToolsTest.moc"
