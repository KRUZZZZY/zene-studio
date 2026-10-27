/*
 * ControlCommandsDeviceParams.cpp - find, rename, randomise and blend a device's parameters; fold and compatibility options
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
/*! 0.5.0 L10 (W3), the parameters and options of one device:
 *  - plugin.param_search (MX-089): the parameters whose name (or alias) holds a text, over one
 *    target or every track and channel;
 *  - plugin.param_rename (MX-097): a name of the user's for a parameter, kept with the device;
 *  - plugin.randomise / plugin.blend (MX-093): new settings - random within each parameter's own
 *    range, by an amount - or a mix of the two settings plugin.ab stored;
 *  - plugin.fold (MX-113): the device's card drawn as its header only;
 *  - plugin.options (MX-099 / MX-094): the generic editor instead of the plug-in's own, never
 *    sleeping on silence, a latency override for delay compensation, the editor's zoom;
 *  - plugin.delta (MX-080): an effect's output minus its input, to hear what it changes.
 *  Options are carried by the device (include/DeviceOptions.h); every write is one undo step. */

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iterator>
#include <limits>
#include <random>
#include <utility>

#include <QJsonArray>

#include "AudioEngine.h"
#include "AutomatableModel.h"
#include "ControlDeviceSupport.h"
#include "ControlDeviceTools.h"
#include "ControlRackSupport.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "DeviceOptions.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MeterModel.h"
#include "Mixer.h"
#include "MixerPostInserts.h"
#include "ParameterAliases.h"
#include "ParameterReadOnly.h"
#include "Rack.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;

namespace
{

constexpr int kMaxSearchHits = 200;

QString targetOf(const QJsonObject& args) { return args.value(QStringLiteral("target")).toString(); }
QString pluginOf(const QJsonObject& args) { return args.value(QStringLiteral("plugin")).toString(); }

//! The effect 'target' / 'plugin' (fx-<n>) name, or nullptr with *error set.
Effect* effectOf(const QString& targetId, const QString& pluginId, ControlResult* error)
{
	ControlTarget target;
	if (!resolveControlTarget(targetId, &target, error)) { return nullptr; }
	return resolveControlEffect(target, pluginId, error);
}

// ---------------------------------------------------------------------------
// options
// ---------------------------------------------------------------------------

//! Put @a options on @a effect and tell what shows them: the chain's delay compensation (a latency
//! override) and its views (the Device panel redraws a card on the chain's dataChanged). The audio
//! thread reads keepRunning and latencyOverride while this assigns them: each is a SharedOption, so
//! the assignment stores them atomically (catch-up W3).
void applyOptions(Effect* effect, const DeviceOptions& options)
{
	effect->deviceOptions() = options;
	if (EffectChain* chain = effect->effectChain())
	{
		chain->refreshLatency();
		emit chain->dataChanged();
	}
	Engine::getSong()->setModified();
}

QJsonObject optionsJson(Effect* effect, const QString& targetId, const QString& pluginId, const DeviceOptions& options)
{
	// W6: the aliases are kept by each parameter's stable key and reported by its display name (ParameterAliases.h).
	const QJsonObject aliases = parameteraliases::toJson(effect, options);
	return {{QStringLiteral("target"), targetId}, {QStringLiteral("plugin"), pluginId}, {QStringLiteral("aliases"), aliases},
		{QStringLiteral("folded"), options.folded}, {QStringLiteral("editor_scale"), static_cast<double>(options.editorScale)},
		{QStringLiteral("generic_editor"), options.genericEditor}, {QStringLiteral("keep_running"), options.keepRunning.load()},
		{QStringLiteral("latency_override"), options.latencyOverride.load()}};
}

using OptionsEdit = std::function<bool(Effect*, DeviceOptions*, ControlResult*)>;

//! The device's options changed by @a edit, applied, and recorded as one undo step that finds the
//! device again by its ids (an undo can run after the device was removed and put back).
ControlResult editOptions(const QJsonObject& args, const OptionsEdit& edit)
{
	ControlResult error;
	const QString targetId = targetOf(args);
	const QString pluginId = pluginOf(args);
	Effect* effect = effectOf(targetId, pluginId, &error);
	if (effect == nullptr) { return error; }
	const DeviceOptions before = effect->deviceOptions();
	DeviceOptions after = before;
	if (!edit(effect, &after, &error)) { return error; }
	if (after != before)
	{
		applyOptions(effect, after);
		const auto put = [targetId, pluginId](const DeviceOptions& options) {
			ControlResult ignored;
			if (Effect* found = effectOf(targetId, pluginId, &ignored)) { applyOptions(found, options); }
		};
		addUndoStep([put, before]() { put(before); }, [put, after]() { put(after); });
	}
	return ControlResult::success(optionsJson(effect, targetId, pluginId, after));
}

ControlResult paramRename(const QJsonObject& args)
{
	return editOptions(args, [&args](Effect* effect, DeviceOptions* options, ControlResult* error) {
		AutomatableModel* model = controlResolveParameter(effect, args.value(QStringLiteral("name")).toString(),
			args.value(QStringLiteral("index")).toInt(), args.contains(QStringLiteral("index")), error);
		if (model == nullptr) { return false; }
		parameteraliases::setAlias(effect, model, args.value(QStringLiteral("alias")).toString(), options);
		return true;
	});
}

ControlResult fold(const QJsonObject& args)
{
	return editOptions(args, [&args](Effect*, DeviceOptions* options, ControlResult*) {
		options->folded = args.value(QStringLiteral("folded")).toBool(!options->folded);
		return true;
	});
}

bool readScale(const QJsonObject& args, DeviceOptions* options, ControlResult* error)
{
	if (!args.contains(QStringLiteral("editor_scale"))) { return true; }
	const double scale = args.value(QStringLiteral("editor_scale")).toDouble();
	if (scale < DeviceOptions::MinEditorScale || scale > DeviceOptions::MaxEditorScale)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'editor_scale' is a zoom between %1 and %2")
			.arg(static_cast<double>(DeviceOptions::MinEditorScale)).arg(static_cast<double>(DeviceOptions::MaxEditorScale)));
		return false;
	}
	options->editorScale = static_cast<float>(scale);
	return true;
}

ControlResult deviceOptions(const QJsonObject& args)
{
	return editOptions(args, [&args](Effect*, DeviceOptions* options, ControlResult* error) {
		if (!readScale(args, options, error)) { return false; }
		options->genericEditor = args.value(QStringLiteral("generic_editor")).toBool(options->genericEditor);
		options->keepRunning = args.value(QStringLiteral("keep_running")).toBool(options->keepRunning);
		options->latencyOverride = args.value(QStringLiteral("latency_override")).toInt(options->latencyOverride);
		return true;
	});
}

// ---------------------------------------------------------------------------
// search
// ---------------------------------------------------------------------------

struct SearchScope
{
	QString query;
	QJsonArray* hits = nullptr;
};

//! @a effect is the device whose aliases count, or nullptr (an instrument has none).
void searchModels(const SearchScope& scope, const QString& targetId, const QString& pluginId, const QString& device,
	const QList<AutomatableModel*>& models, Effect* effect)
{
	const QStringList aliases = effect != nullptr ? parameteraliases::aliasesOf(effect, models, effect->deviceOptions()) : QStringList();
	for (int i = 0; i < models.size() && scope.hits->size() <= kMaxSearchHits; ++i)  // one past the cap tells "there are more"
	{
		const QString name = models.at(i)->displayName();
		const QString alias = aliases.value(i);
		if (!name.contains(scope.query, Qt::CaseInsensitive) && !alias.contains(scope.query, Qt::CaseInsensitive)) { continue; }
		scope.hits->append(QJsonObject{{QStringLiteral("target"), targetId}, {QStringLiteral("plugin"), pluginId},
			{QStringLiteral("device"), device}, {QStringLiteral("index"), i}, {QStringLiteral("name"), name},
			{QStringLiteral("alias"), alias}});
	}
}

void searchChain(const SearchScope& scope, const QString& targetId, const EffectChain* chain)
{
	if (chain == nullptr) { return; }
	for (Effect* effect : chain->effects())
	{
		searchModels(scope, targetId, effectIdOf(effect), effect->displayName(), controlEffectParameters(effect), effect);
	}
}

void searchTarget(const SearchScope& scope, const QString& targetId)
{
	ControlTarget target;
	ControlResult ignored;
	if (!resolveControlTarget(targetId, &target, &ignored)) { return; }
	if (target.instrumentTrack != nullptr && target.instrumentTrack->instrument() != nullptr)
	{
		Instrument* instrument = target.instrumentTrack->instrument();
		searchModels(scope, targetId, QStringLiteral("inst"), instrument->displayName(), controlInstrumentParameters(instrument), nullptr);
	}
	searchChain(scope, targetId, target.chain);
	if (!targetId.startsWith(QLatin1String("ch-"))) { return; }
	if (postinserts::isTarget(targetId)) { return; }  // the post-fader inserts are a target of their own, searched above
	const Rack* rack = resolveRack(targetId, &ignored);
	for (int c = 1; rack != nullptr && c < rack->chainCount(); ++c) { searchChain(scope, targetId, rack->chain(c)); }
	// MX-082: a channel's post-fader inserts hang off that channel, so its search reaches them and
	// names them by the target that addresses them.
	const QString postId = postinserts::targetOf(targetId);
	ControlTarget post;
	if (resolveControlTarget(postId, &post, &ignored)) { searchChain(scope, postId, post.chain); }
}

ControlResult paramSearch(const QJsonObject& args)
{
	const QString query = args.value(QStringLiteral("query")).toString().trimmed();
	if (query.isEmpty()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'query' is the text to look for")); }
	QJsonArray hits;
	const SearchScope scope{query, &hits};
	if (args.contains(QStringLiteral("target")))
	{
		ControlTarget target;
		ControlResult error;
		if (!resolveControlTarget(targetOf(args), &target, &error)) { return error; }
		searchTarget(scope, targetOf(args));
	}
	else
	{
		for (const Track* track : Engine::getSong()->tracks()) { searchTarget(scope, trackIdOf(track)); }
		for (int i = 0; i < Engine::mixer()->numChannels(); ++i) { searchTarget(scope, channelIdOf(Engine::mixer()->mixerChannel(i))); }
	}
	// The scan collects one past the cap, so 'truncated' means more matches exist (review W3-agent-devices #3).
	const bool truncated = hits.size() > kMaxSearchHits;
	if (truncated) { hits.removeLast(); }
	return ControlResult::success({{QStringLiteral("query"), query}, {QStringLiteral("parameters"), hits},
		{QStringLiteral("count"), hits.size()}, {QStringLiteral("truncated"), truncated}});
}

// ---------------------------------------------------------------------------
// randomise and blend
// ---------------------------------------------------------------------------

//! A parameter a new setting may change: not the device's own switch, mix and decay (models owned
//! by the Effect itself), nor a tempo-synced knob's meter, nor a parameter the plug-in declares read-only
//! (stress-4: plugin.param_set refuses it, and the plug-in overwrites whatever is written to it).
bool isSetting(const AutomatableModel* model)
{
	return qobject_cast<const Effect*>(model->parent()) == nullptr && qobject_cast<const MeterModel*>(model->parent()) == nullptr
		&& !isParameterReadOnly(model);
}

//! Write @a values (one per model) as one undo step, and answer @a reply with the count.
ControlResult writeValues(const QList<AutomatableModel*>& models, const std::vector<float>& values, const QJsonObject& reply)
{
	std::vector<std::pair<AutomatableModel*, float>> writes;
	for (int i = 0; i < models.size(); ++i)
	{
		AutomatableModel* model = models.at(i);
		const float value = values[static_cast<std::size_t>(i)];
		// A value that stays the value it already had (plugin.randomise with 'amount: 0') is not a change:
		// it must not be written, counted or marked as one, and must not take an undo step.
		if (model->value<float>() == value) { continue; }
		writes.emplace_back(model, value);
	}
	const int changed = writeModelsRecorded(writes);
	if (changed > 0) { Engine::getSong()->setModified(); }
	QJsonObject result = reply;
	result.insert(QStringLiteral("changed"), changed);
	return ControlResult::success(result);
}

double amountOf(const QJsonObject& args, double fallback)
{
	return std::clamp(args.value(QStringLiteral("amount")).toDouble(fallback), 0.0, 1.0);
}

//! The 'seed' of a randomise is an unsigned 32-bit integer. One outside that range is refused, not truncated:
//! a truncated seed would be a different roll reported as the one that was asked for.
bool seedOf(const QJsonObject& args, std::uint32_t* seed, ControlResult* error)
{
	const double number = args.value(QStringLiteral("seed")).toDouble(-1.0);
	if (number < 0.0 || number > static_cast<double>(std::numeric_limits<std::uint32_t>::max()))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'seed' must be an unsigned 32-bit integer"));
		return false;
	}
	*seed = static_cast<std::uint32_t>(number);
	return true;
}

ControlResult randomise(const QJsonObject& args)
{
	ControlTarget target;
	ControlResult error;
	if (!resolveControlTarget(targetOf(args), &target, &error)) { return error; }
	QList<AutomatableModel*> all;
	QString pluginName;
	if (!controlDeviceParameters(target, pluginOf(args), &all, &pluginName, &error)) { return error; }
	QList<AutomatableModel*> models;
	std::copy_if(all.begin(), all.end(), std::back_inserter(models), isSetting);
	if (models.isEmpty()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the device has no settings to randomise")); }
	const double amount = amountOf(args, 0.5);
	std::mt19937 random;
	if (args.contains(QStringLiteral("seed")))
	{
		std::uint32_t seed = 0;
		if (!seedOf(args, &seed, &error)) { return error; }
		random.seed(seed);
	}
	else { random.seed(std::random_device{}()); }
	std::vector<float> values;
	for (const AutomatableModel* model : models)
	{
		std::uniform_real_distribution<double> range(model->minValue<float>(), model->maxValue<float>());
		const double now = model->value<float>();
		values.push_back(static_cast<float>(now + amount * (range(random) - now)));
	}
	return writeValues(models, values, {{QStringLiteral("target"), target.id}, {QStringLiteral("plugin"), pluginOf(args)},
		{QStringLiteral("amount"), amount}});
}

ControlResult blend(const QJsonObject& args)
{
	ControlResult error;
	Effect* effect = effectOf(targetOf(args), pluginOf(args), &error);
	if (effect == nullptr) { return error; }
	std::vector<float> a;
	std::vector<float> b;
	const QList<AutomatableModel*> all = controlEffectParameters(effect);
	if (!abSlotValues(effect, &a, &b) || a.size() != static_cast<std::size_t>(all.size()) || b.size() != a.size())
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("store the two settings to blend first: plugin.ab 'store' in slot a and in slot b"));
	}
	const double amount = amountOf(args, 0.5);
	QList<AutomatableModel*> models;
	std::vector<float> values;
	for (int i = 0; i < all.size(); ++i)
	{
		if (!isSetting(all.at(i))) { continue; }
		const auto k = static_cast<std::size_t>(i);
		models << all.at(i);
		values.push_back(static_cast<float>(a[k] + amount * (b[k] - a[k])));
	}
	// As randomise: nothing to change is refused, not answered with an empty "success" and no undo step.
	if (models.isEmpty()) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("the device has no settings to blend")); }
	return writeValues(models, values, {{QStringLiteral("target"), targetOf(args)}, {QStringLiteral("plugin"), pluginOf(args)},
		{QStringLiteral("amount"), amount}});
}

// ---------------------------------------------------------------------------
// the delta
// ---------------------------------------------------------------------------

ControlResult delta(const QJsonObject& args)
{
	ControlResult error;
	Effect* effect = effectOf(targetOf(args), pluginOf(args), &error);
	if (effect == nullptr) { return error; }
	const bool on = args.value(QStringLiteral("on")).toBool(!effect->deltaTap().enabled());
	effect->deltaTap().setEnabled(on, Engine::audioEngine()->framesPerPeriod());
	return ControlResult::success({{QStringLiteral("target"), targetOf(args)}, {QStringLiteral("plugin"), pluginOf(args)},
		{QStringLiteral("on"), effect->deltaTap().enabled()}});
}

} // namespace

void registerDeviceParamCommands(ControlRegistry& registry)
{
	const QJsonObject device{{QStringLiteral("target"), stringProperty()}, {QStringLiteral("plugin"), stringProperty()}};
	const QStringList both{QStringLiteral("target"), QStringLiteral("plugin")};
	addDeviceCommand(registry, "plugin.param_search", QStringLiteral("The parameters whose name - or the alias a user gave it - holds "
		"'query' (any case), on 'target' (trk-<n> / ch-<n>: its instrument, its devices and, for a channel, its rack's chains) or, "
		"without one, on every track and channel: target, plugin (fx-<n> or inst), device, index, name, alias. At most 200."),
		{{QStringLiteral("query"), stringProperty()}, {QStringLiteral("target"), stringProperty()}}, {QStringLiteral("query")}, false,
		paramSearch);
	QJsonObject rename = device;
	rename.insert(QStringLiteral("name"), stringProperty());
	rename.insert(QStringLiteral("index"), integerProperty());
	rename.insert(QStringLiteral("alias"), stringProperty());
	addDeviceCommand(registry, "plugin.param_rename", QStringLiteral("Give a parameter of an effect ('plugin' fx-<n> on 'target'; the "
		"parameter by 'name' or 'index') a name of your own, 'alias' - shown on the device's card; an empty alias puts the "
		"parameter's own name back. The parameter keeps its own name as its address (macros, modulators, plugin.param_*). Saved "
		"with the device; one undo step."), rename, {QStringLiteral("target"), QStringLiteral("plugin"), QStringLiteral("alias")}, true,
		paramRename);
	QJsonObject random = device;
	random.insert(QStringLiteral("amount"), numberProperty());
	random.insert(QStringLiteral("seed"), integerProperty());
	addDeviceCommand(registry, "plugin.randomise", QStringLiteral("New settings for a device ('plugin' fx-<n> or inst on 'target'): every "
		"parameter moves towards a random value inside its own range by 'amount' (0..1, 0.5 when left out; 1 is fully random). "
		"The device's on/off switch, mix and decay stay. 'seed' repeats a result. One undo step."), random, both, true, randomise);
	QJsonObject mix = device;
	mix.insert(QStringLiteral("amount"), numberProperty());
	addDeviceCommand(registry, "plugin.blend", QStringLiteral("Mix the two settings plugin.ab stored for an effect: every parameter "
		"set 'amount' of the way from slot A's value to slot B's (0..1, 0.5 when left out). One undo step."), mix, both, true, blend);
	QJsonObject folded = device;
	folded.insert(QStringLiteral("folded"), booleanProperty());
	addDeviceCommand(registry, "plugin.fold", QStringLiteral("Fold an effect's card on the Device panel to its header ('folded' true), "
		"or open it again; toggles when 'folded' is left out. View only - the sound does not change. Saved with the device; one "
		"undo step."), folded, both, true, fold);
	QJsonObject options = device;
	options.insert(QStringLiteral("generic_editor"), booleanProperty());
	options.insert(QStringLiteral("keep_running"), booleanProperty());
	options.insert(QStringLiteral("latency_override"), integerProperty(-1, 1 << 20));
	options.insert(QStringLiteral("editor_scale"), numberProperty());
	addDeviceCommand(registry, "plugin.options", QStringLiteral("An effect's compatibility and editor options: 'generic_editor' opens the "
		"host's generic controls instead of the plug-in's own window; 'keep_running' never puts it to sleep on silence; "
		"'latency_override' is the latency in frames delay compensation uses instead of what it reports (-1: its own); "
		"'editor_scale' is the zoom its own window opens at (0.5..3, for plug-ins that can scale). With none given it reports "
		"them (and its aliases and fold). Saved with the device; one undo step."), options, both, true, deviceOptions);
	QJsonObject listen = device;
	listen.insert(QStringLiteral("on"), booleanProperty());
	addDeviceCommand(registry, "plugin.delta", QStringLiteral("Hear only what an effect changes ('plugin' fx-<n> on 'target'): its "
		"output minus its input - silence while it changes nothing, the part a compressor takes away (inverted), the harmonics "
		"a saturator adds. 'on' switches it, toggling when left out. A monitor switch, like the listen bus: not saved, no undo "
		"step."), listen, both, false, delta);
}

} // namespace lmms
