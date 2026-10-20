/*
 * ControlCommandsDeviceTools.cpp - rack.split: a rack's chains fed by band, mid / side or left / right
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
/*! 0.5.0 L10 (W3):
 *  - rack.split (MX-108): what each chain of a parallel rack is fed - the whole signal, one band of
 *    an up-to-six-way Linkwitz-Riley crossover, the mid or the side, the left or the right; the
 *    rack's sum puts the parts back together (include/RackSplit.h). Saved with the rack, one undo
 *    step;
 *  - mixer.console_apply (MX-150): the Console device on every mixer channel, all set alike - or
 *    the settings of the ones there changed, or all of them taken out - as one undo step;
 *  - plugin.impulse_load (MX-152): an impulse response file into a Convolution Reverb, through the
 *    device's own settings document (its 'impulse' attribute), so an undo restores the previous one. */

#include <algorithm>
#include <functional>
#include <utility>
#include <vector>

#include <QDomDocument>
#include <QFileInfo>
#include <QJsonArray>

#include "AutomatableModel.h"
#include "ControlDeviceMoves.h"
#include "ControlDeviceSupport.h"
#include "ControlDeviceTools.h"
#include "ControlMixerSupport.h"
#include "ControlRackSupport.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "ParameterControls.h"
#include "ProjectJournal.h"
#include "Rack.h"
#include "RackSplit.h"
#include "SampleDecoder.h"
#include "Song.h"

namespace lmms
{

namespace control
{

void addDeviceCommand(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args,
	const QStringList& required, bool mutating, ControlResult (*handler)(const QJsonObject&))
{
	ControlCommand cmd;
	cmd.id = QLatin1String(id);
	const int dot = cmd.id.indexOf(QLatin1Char('.'));
	cmd.group = cmd.id.left(dot);
	cmd.verb = cmd.id.mid(dot + 1);
	cmd.description = description;
	cmd.argsSchema = objectSchema(args, QJsonArray::fromStringList(required));
	cmd.resultSchema = objectProperty();
	cmd.mutating = mutating;
	cmd.handler = handler;
	registry.registerCommand(cmd);
}

int writeModelsRecorded(const std::vector<std::pair<AutomatableModel*, float>>& writes)
{
	std::vector<std::pair<AutomatableModel*, float>> changes;
	for (const auto& [model, value] : writes)
	{
		const float clamped = std::clamp(value, model->minValue<float>(), model->maxValue<float>());
		if (clamped != model->value<float>()) { changes.emplace_back(model, clamped); }
	}
	QVector<JournallingObject*> touched;
	for (const auto& change : changes) { touched << change.first; }
	if (!touched.isEmpty()) { addUndoStep(touched); }
	for (const auto& [model, value] : changes) { model->setValue(value); }
	return static_cast<int>(changes.size());
}

} // namespace control

using namespace control;

namespace
{

//! The crossovers in @a args, checked and sorted into @a split.
bool readCrossovers(const QJsonObject& args, RackSplit* split, ControlResult* error)
{
	const QJsonArray values = args.value(QStringLiteral("crossovers")).toArray();
	if (values.size() > RackSplit::MaxBands - 1)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("at most %1 crossovers (%2 bands)").arg(RackSplit::MaxBands - 1).arg(RackSplit::MaxBands));
		return false;
	}
	for (const QJsonValue& value : values)
	{
		const double hz = value.toDouble(-1.0);
		if (!value.isDouble() || hz < RackSplit::MinCrossoverHz || hz > RackSplit::MaxCrossoverHz)
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("a crossover is a frequency in Hz between %1 and %2").arg(static_cast<double>(RackSplit::MinCrossoverHz)).arg(static_cast<double>(RackSplit::MaxCrossoverHz)));
			return false;
		}
		split->crossovers[static_cast<std::size_t>(split->crossoverCount++)] = static_cast<float>(hz);
	}
	std::sort(split->crossovers.begin(), split->crossovers.begin() + split->crossoverCount);
	return true;
}

void applySplit(const QString& channel, const RackSplit& split)
{
	ControlResult ignored;
	if (Rack* rack = resolveRack(channel, &ignored)) { rack->setSplit(split); }
}

QJsonObject splitResult(const QString& channel, const Rack& rack)
{
	QJsonObject result = rackSplitJson(rack);
	result.insert(QStringLiteral("channel"), channel);
	result.insert(QStringLiteral("chains"), rack.chainCount());
	return result;
}

ControlResult rackSplit(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	Rack& rack = channel->m_rack;
	const QString id = channelIdOf(channel);
	if (!args.contains(QStringLiteral("mode"))) { return ControlResult::success(splitResult(id, rack)); }
	RackSplit split;
	RackSplit::modeFromName(args.value(QStringLiteral("mode")).toString(), &split.mode);
	if (!readCrossovers(args, &split, &error)) { return error; }
	if (split.active() && rack.chainCount() < 2)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("a split divides the input between the rack's chains: %1 has one - add a parallel chain first").arg(id));
	}
	const RackSplit before = rack.split();
	if (split.mode == before.mode && split.crossovers == before.crossovers && split.crossoverCount == before.crossoverCount)
	{
		return ControlResult::success(splitResult(id, rack));
	}
	rack.setSplit(split);
	addUndoStep([id, before]() { applySplit(id, before); }, [id, split]() { applySplit(id, split); });
	Engine::getSong()->setModified();
	return ControlResult::success(splitResult(id, rack));
}

// ---------------------------------------------------------------------------
// the console across the mix
// ---------------------------------------------------------------------------

const QString kConsole = QStringLiteral("console");
const QStringList& characterNames()
{
	static const QStringList names{QStringLiteral("clean"), QStringLiteral("warm"), QStringLiteral("bright")};
	return names;
}

bool isConsole(const Effect* effect) { return QString::fromUtf8(effect->descriptor()->name) == kConsole; }

//! The model a ParameterControls device saves under @a key, or nullptr.
AutomatableModel* parameterByKey(Effect* effect, const QString& key)
{
	auto* controls = dynamic_cast<ParameterControls*>(effect->controls());
	if (controls == nullptr) { return nullptr; }
	for (const auto& [model, saved] : controls->parameters()) { if (saved == key) { return model; } }
	return nullptr;
}

//! The values @a args sets on a console: (model, value) for 'drive' and 'character'.
std::vector<std::pair<AutomatableModel*, float>> consoleWrites(Effect* effect, const QJsonObject& args)
{
	std::vector<std::pair<AutomatableModel*, float>> writes;
	AutomatableModel* drive = parameterByKey(effect, QStringLiteral("drive"));
	AutomatableModel* character = parameterByKey(effect, QStringLiteral("character"));
	if (drive != nullptr && args.contains(QStringLiteral("drive")))
	{
		writes.emplace_back(drive, static_cast<float>(args.value(QStringLiteral("drive")).toDouble()));
	}
	if (character != nullptr && args.contains(QStringLiteral("character")))
	{
		writes.emplace_back(character, static_cast<float>(characterNames().indexOf(args.value(QStringLiteral("character")).toString())));
	}
	return writes;
}

std::function<EffectChain*()> channelChain(const QString& id)
{
	return [id]() -> EffectChain* {
		ControlResult ignored;
		MixerChannel* found = resolveMixerChannel(id, &ignored);
		return found != nullptr ? &found->m_fxChain : nullptr;
	};
}

bool consoleEntry(ControlDeviceEntry* entry, ControlResult* error)
{
	for (const ControlDeviceEntry& candidate : controlDeviceCatalogue())
	{
		if (candidate.name == kConsole && candidate.format == QLatin1String("builtin")) { *entry = candidate; return true; }
	}
	*error = ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("this build has no console module"));
	return false;
}

//! Keeps a scratch device's construction and writes out of the user's undo history (the same guard
//! ControlChainPresetSupport.cpp keeps for its staged chains).
class ScopedJournalMute
{
public:
	ScopedJournalMute() :
		m_journal(Engine::projectJournal()),
		m_wasJournalling(m_journal != nullptr && m_journal->isJournalling())
	{
		if (m_journal != nullptr) { m_journal->setJournalling(false); }
	}

	~ScopedJournalMute()
	{
		if (m_journal != nullptr) { m_journal->setJournalling(m_wasJournalling); }
	}

	ScopedJournalMute(const ScopedJournalMute&) = delete;
	ScopedJournalMute& operator=(const ScopedJournalMute&) = delete;

private:
	ProjectJournal* m_journal;
	bool m_wasJournalling;
};

//! A console's settings document with @a args applied, from an instance on a scratch chain.
bool consoleTemplate(const QJsonObject& args, QString* state, ControlResult* error)
{
	ControlDeviceEntry entry;
	if (!consoleEntry(&entry, error)) { return false; }
	const ScopedJournalMute muteJournal;  // the scratch console is not in the project: building it is no undo step
	EffectChain scratch(nullptr);
	Effect* effect = controlInstantiateDevice(entry, &scratch, error);
	if (effect == nullptr) { return false; }
	for (const auto& [model, value] : consoleWrites(effect, args)) { model->setValue(value); }
	*state = controlEffectStateXml(effect);
	return true;
}

std::vector<Effect*> consolesOn(const EffectChain& chain)
{
	std::vector<Effect*> found;
	for (Effect* effect : chain.effects()) { if (isConsole(effect)) { found.push_back(effect); } }
	return found;
}

ControlResult removeConsoles()
{
	int removed = 0;
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		MixerChannel* channel = Engine::mixer()->mixerChannel(i);
		const std::vector<Effect*> consoles = consolesOn(channel->m_fxChain);
		if (consoles.empty() || !devicesRecordable(consoles)) { continue; }
		removeDevicesRecorded(channelChain(channelIdOf(channel)), consoles);
		removed += static_cast<int>(consoles.size());
	}
	if (removed > 0) { Engine::getSong()->setModified(); }
	return ControlResult::success({{QStringLiteral("removed"), removed}});
}

//! A console added to every channel without one (from @a state); the changes @a args makes to the
//! ones already there gathered in @a writes. The channels a console was added to.
int placeConsoles(const QJsonObject& args, const QString& state, std::vector<std::pair<AutomatableModel*, float>>* writes)
{
	int added = 0;
	ControlResult error;
	for (int i = 0; i < Engine::mixer()->numChannels(); ++i)
	{
		MixerChannel* channel = Engine::mixer()->mixerChannel(i);
		const std::vector<Effect*> consoles = consolesOn(channel->m_fxChain);
		for (Effect* console : consoles)
		{
			const auto more = consoleWrites(console, args);
			writes->insert(writes->end(), more.begin(), more.end());
		}
		if (consoles.empty() && !recreateDevices(channelChain(channelIdOf(channel)), {state}, -1, &error).isEmpty()) { ++added; }
	}
	return added;
}

ControlResult consoleApply(const QJsonObject& args)
{
	const double drive = args.value(QStringLiteral("drive")).toDouble(0.0);
	if (drive < 0.0 || drive > 100.0)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'drive' is 0..100 (%)"));
	}
	if (args.value(QStringLiteral("remove")).toBool()) { return removeConsoles(); }
	QString state;
	ControlResult error;
	if (!consoleTemplate(args, &state, &error)) { return error; }
	std::vector<std::pair<AutomatableModel*, float>> writes;
	const int added = placeConsoles(args, state, &writes);
	const int changed = writeModelsRecorded(writes);
	if (added > 0 || changed > 0) { Engine::getSong()->setModified(); }
	return ControlResult::success({{QStringLiteral("added"), added}, {QStringLiteral("changed"), changed},
		{QStringLiteral("channels"), Engine::mixer()->numChannels()}});
}

// ---------------------------------------------------------------------------
// the convolution reverb's impulse response
// ---------------------------------------------------------------------------

//! @a state (a device's settings document) with the controls element's 'impulse' set to @a path.
bool withImpulse(const QString& state, const QString& path, QString* changed)
{
	QDomDocument doc;
	if (!doc.setContent(state)) { return false; }
	QDomElement controls = doc.elementsByTagName(QStringLiteral("ConvolutionReverbControls")).item(0).toElement();
	if (controls.isNull()) { return false; }
	controls.setAttribute(QStringLiteral("impulse"), path);
	*changed = doc.toString();
	return true;
}

ControlResult impulseLoad(const QJsonObject& args)
{
	const QString targetId = args.value(QStringLiteral("target")).toString();
	const QString pluginId = args.value(QStringLiteral("plugin")).toString();
	ControlTarget target;
	ControlResult error;
	if (!resolveControlTarget(targetId, &target, &error)) { return error; }
	Effect* effect = resolveControlEffect(target, pluginId, &error);
	if (effect == nullptr) { return error; }
	const QString path = QFileInfo(args.value(QStringLiteral("path")).toString()).absoluteFilePath();
	if (!QFileInfo::exists(path)) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no such file: %1").arg(path)); }
	if (!SampleDecoder::decode(path))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("%1 is not an audio file this build reads").arg(path));
	}
	const QString before = controlEffectStateXml(effect);
	QString after;
	if (!withImpulse(before, path, &after))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("%1 is not a Convolution Reverb").arg(pluginId));
	}
	const ControlResult restored = controlRestoreEffectState(effect, after.toUtf8());
	if (!restored.ok) { return restored; }
	addUndoStep([targetId, pluginId, before]() { controlRestoreCapturedState(targetId, pluginId, before.toUtf8()); },
		[targetId, pluginId, after]() { controlRestoreCapturedState(targetId, pluginId, after.toUtf8()); });
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("target"), targetId}, {QStringLiteral("plugin"), pluginId}, {QStringLiteral("impulse"), path}});
}

} // namespace

void registerDeviceToolCommands(ControlRegistry& registry)
{
	addDeviceCommand(registry, "plugin.impulse_load", QStringLiteral("Load an impulse response - an audio file (WAV, FLAC, OGG...), "
		"stereo or mono, up to 5 seconds - into a Convolution Reverb ('plugin' fx-<n> on 'target'); it is resampled to the "
		"engine's rate and saved with the device. One undo step puts the previous response back."),
		{{QStringLiteral("target"), stringProperty()}, {QStringLiteral("plugin"), stringProperty()}, {QStringLiteral("path"), stringProperty()}},
		{QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("path")}, true, impulseLoad);
	addDeviceCommand(registry, "mixer.console_apply", QStringLiteral("Put the Console device - a console channel's saturation and "
		"tone - at the end of every mixer channel's chain, master included, all set alike: 'drive' (0..100 %) and "
		"'character' (clean, warm, bright); channels that have one keep it and take the new settings. 'remove' true takes "
		"every Console out instead. One undo step."),
		{{QStringLiteral("drive"), numberProperty()}, {QStringLiteral("character"), enumProperty(characterNames())},
			{QStringLiteral("remove"), booleanProperty()}},
		{}, true, consoleApply);
	addDeviceCommand(registry, "rack.split", QStringLiteral("Feed each chain of a channel's parallel rack a part of the signal: "
		"'mode' bands (chain k gets band k, low to high, of a split at 'crossovers' - up to five frequencies in Hz, spread "
		"in octaves when fewer are given), mid_side (chain 0 the mid, chain 1 the side), left_right (chain 0 the left, chain 1 "
		"the right) or none (every chain the whole signal). The rack's sum puts the parts back together (the bands are "
		"Linkwitz-Riley crossovers that sum flat), so empty chains sound as the channel did. Applies while the rack routes in parallel. With no 'mode' it reports the split. Saved "
		"with the rack; one undo step."),
		{{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("mode"), enumProperty(RackSplit::modeNames())},
			{QStringLiteral("crossovers"), arrayProperty()}},
		{QStringLiteral("channel")}, true, rackSplit);
}

} // namespace lmms
