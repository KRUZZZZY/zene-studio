/*
 * ControlCommandsMixerTools.cpp - pan law, sends switched off, returns, finding, ganging, ordering and cleaning
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
/*! 0.5.0 L08 / L10 (W3), the mixer's working tools:
 *  - mixer.options (MX-025): the project's pan law;
 *  - mixer.send_enable (MX-047): a send switched off and on again, keeping its level;
 *  - mixer.create_return (MX-051): a new return channel, fed by a send from the channel;
 *  - mixer.find (MX-009): the channels whose name holds a text;
 *  - mixer.adjust (MX-028): several channels' faders and pans moved together, by the same amount;
 *  - mixer.order_by_tracks (MX-018): the channels put in the order of the tracks that use them;
 *  - vca.color_members (MX-067): a group's member channels given the group's colour;
 *  - chain.bypass_all / chain.clean (MX-078): every device of a chain bypassed or back on, and the
 *    bypassed ones removed. */

#include <algorithm>
#include <cmath>
#include <optional>

#include <QColor>
#include <QJsonArray>

#include "ControlDeviceMoves.h"
#include "ControlDevicePlacement.h"
#include "ControlDeviceSupport.h"
#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerOptions.h"
#include "Song.h"
#include "Track.h"
#include "VcaGroup.h"

namespace lmms
{

using namespace control;

namespace
{

Mixer* mixer() { return Engine::mixer(); }

//! The channel whose persistent id is @a id, or nullptr.
MixerChannel* channelWithId(int id)
{
	for (int i = 0; i < mixer()->numChannels(); ++i)
	{
		if (mixer()->mixerChannel(i)->id() == id) { return mixer()->mixerChannel(i); }
	}
	return nullptr;
}

ControlResult options(const QJsonObject& args)
{
	MixerOptions& options = MixerOptions::instance();
	const PanLaw before = options.panLaw();
	if (args.contains(QStringLiteral("pan_law")))
	{
		const auto law = static_cast<PanLaw>(MixerOptions::panLawNames().indexOf(args.value(QStringLiteral("pan_law")).toString()));
		if (law != before)
		{
			options.setPanLaw(law);
			addUndoStep([before]() { MixerOptions::instance().setPanLaw(before); }, [law]() { MixerOptions::instance().setPanLaw(law); });
			Engine::getSong()->setModified();
		}
	}
	return ControlResult::success({{QStringLiteral("pan_law"), MixerOptions::panLawNames().value(static_cast<int>(options.panLaw()))}});
}

void setSendEnabled(mix_ch_t from, mix_ch_t to, bool on)
{
	if (MixerRoute* route = findMixerRoute(from, to)) { route->enabledModel()->setValue(on); }
}

ControlResult sendEnable(const QJsonObject& args)
{
	ControlResult error;
	RoutingEnds ends;
	if (!resolveRoutingEnds(args, &ends, &error)) { return error; }
	const auto from = static_cast<mix_ch_t>(ends.from->index());
	const auto to = static_cast<mix_ch_t>(ends.to->index());
	MixerRoute* route = findMixerRoute(from, to);
	if (route == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no send from %1 to %2").arg(channelIdOf(ends.from), channelIdOf(ends.to)));
	}
	const bool on = args.value(QStringLiteral("enabled")).toBool(true);
	const bool before = route->enabledModel()->value();
	if (on != before)
	{
		route->enabledModel()->setValue(on);
		addUndoStep([from, to, before]() { setSendEnabled(from, to, before); }, [from, to, on]() { setSendEnabled(from, to, on); });
		Engine::getSong()->setModified();
	}
	return ControlResult::success({{QStringLiteral("from"), channelIdOf(ends.from)}, {QStringLiteral("to"), channelIdOf(ends.to)},
		{QStringLiteral("enabled"), on}, {QStringLiteral("amount"), static_cast<double>(route->amount()->value())}});
}

//! A return channel named @a name, fed from channel @a fromId at @a amount; its index.
int buildReturn(int fromId, const QString& name, float amount, bool preFader)
{
	const int index = mixer()->createChannel();
	MixerChannel* made = mixer()->mixerChannel(index);
	made->m_name = name;
	made->setIsBus(true);
	if (MixerChannel* from = channelWithId(fromId))
	{
		MixerRoute* route = mixer()->createChannelSend(static_cast<mix_ch_t>(from->index()), static_cast<mix_ch_t>(index), amount);
		if (route != nullptr) { route->setPreFader(preFader); }
	}
	return index;
}

ControlResult createReturn(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* from = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (from == nullptr) { return error; }
	const int fromId = from->id();
	const QString name = args.value(QStringLiteral("name")).toString(QStringLiteral("Return"));
	const auto amount = static_cast<float>(args.value(QStringLiteral("amount")).toDouble(1.0));
	const bool preFader = args.value(QStringLiteral("pre_fader")).toBool(false);
	const int before = mixer()->numChannels();
	const int index = buildReturn(fromId, name, amount, preFader);
	addUndoStep([before]() { if (mixer()->numChannels() > before) { mixer()->deleteChannel(mixer()->numChannels() - 1); } },
		[fromId, name, amount, preFader]() { buildReturn(fromId, name, amount, preFader); });
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("channel"), channelIdOf(mixer()->mixerChannel(index))},
		{QStringLiteral("from"), channelIdOf(from)}, {QStringLiteral("name"), name}});
}

ControlResult find(const QJsonObject& args)
{
	const QString text = args.value(QStringLiteral("text")).toString().trimmed();
	QJsonArray found;
	for (int i = 0; i < mixer()->numChannels(); ++i)
	{
		const MixerChannel* channel = mixer()->mixerChannel(i);
		if (channel->m_name.contains(text, Qt::CaseInsensitive))
		{
			found.append(QJsonObject{{QStringLiteral("channel"), channelIdOf(channel)}, {QStringLiteral("name"), channel->m_name},
				{QStringLiteral("index"), i}});
		}
	}
	return ControlResult::success({{QStringLiteral("channels"), found}});
}

//! The channels a list of ch-<n> ids names, or a failure in @a error.
bool channelsOf(const QJsonArray& ids, std::vector<MixerChannel*>* out, ControlResult* error)
{
	for (const QJsonValue& id : ids)
	{
		MixerChannel* channel = resolveMixerChannel(id.toString(), error);
		if (channel == nullptr) { return false; }
		out->push_back(channel);
	}
	if (out->empty()) { *error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("name the 'channels'")); }
	return !out->empty();
}

ControlResult adjust(const QJsonObject& args)
{
	std::vector<MixerChannel*> channels;
	ControlResult error;
	if (!channelsOf(args.value(QStringLiteral("channels")).toArray(), &channels, &error)) { return error; }
	const double db = args.value(QStringLiteral("volume_db")).toDouble(0.0);
	const double pan = args.value(QStringLiteral("pan")).toDouble(0.0);
	QVector<JournallingObject*> models;
	for (MixerChannel* channel : channels) { models << &channel->m_volumeModel << &channel->m_panModel; }
	addUndoStep(models);
	const float factor = static_cast<float>(std::pow(10.0, db / 20.0));
	for (MixerChannel* channel : channels)
	{
		FloatModel& volume = channel->m_volumeModel;
		volume.setValue(std::clamp(volume.value() * factor, volume.minValue(), volume.maxValue()));
		channel->m_panModel.setValue(std::clamp(channel->m_panModel.value() + static_cast<float>(pan), -1.f, 1.f));
	}
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("adjusted"), static_cast<int>(channels.size())}});
}

//! The channels' persistent ids in mixer order (master first).
std::vector<int> channelOrder()
{
	std::vector<int> ids;
	for (int i = 0; i < mixer()->numChannels(); ++i) { ids.push_back(mixer()->mixerChannel(i)->id()); }
	return ids;
}

//! Put the channels in the order @a ids gives (by persistent id), through the mixer's own moves.
void arrange(const std::vector<int>& ids)
{
	for (std::size_t want = 1; want < ids.size(); ++want)
	{
		MixerChannel* channel = channelWithId(ids[want]);
		if (channel == nullptr) { continue; }
		for (int at = channel->index(); at > static_cast<int>(want); --at) { mixer()->moveChannelLeft(at); }
	}
}

ControlResult orderByTracks(const QJsonObject&)
{
	const std::vector<int> before = channelOrder();
	std::vector<int> after{before.front()};
	for (const Track* track : Engine::getSong()->tracks())
	{
		auto* model = const_cast<Track*>(track)->mixerChannelModel();
		MixerChannel* channel = model != nullptr && model->value() > 0 ? mixer()->mixerChannel(model->value()) : nullptr;
		if (channel != nullptr && std::find(after.begin(), after.end(), channel->id()) == after.end()) { after.push_back(channel->id()); }
	}
	for (const int id : before) { if (std::find(after.begin(), after.end(), id) == after.end()) { after.push_back(id); } }
	if (after != before)
	{
		arrange(after);
		addUndoStep([before]() { arrange(before); }, [after]() { arrange(after); });
		Engine::getSong()->setModified();
	}
	return ControlResult::success({{QStringLiteral("moved"), after != before}});
}

ControlResult colorMembers(const QJsonObject& args)
{
	VcaGroup* group = mixer()->vcaGroup(args.value(QStringLiteral("group")).toInt(-1));
	if (group == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no such group")); }
	const QColor color(args.value(QStringLiteral("color")).toString());
	if (!color.isValid()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'color' is #rrggbb")); }
	std::vector<std::pair<int, std::optional<QColor>>> before;
	for (const mix_ch_t member : group->members())
	{
		if (MixerChannel* channel = mixer()->mixerChannel(member)) { before.emplace_back(channel->id(), channel->color()); }
	}
	const auto paint = [](const std::vector<std::pair<int, std::optional<QColor>>>& colors) {
		for (const auto& [id, value] : colors) { if (MixerChannel* channel = channelWithId(id)) { channel->setColor(value); } }
	};
	std::vector<std::pair<int, std::optional<QColor>>> after;
	for (const auto& entry : before) { after.emplace_back(entry.first, color); }
	paint(after);
	addUndoStep([paint, before]() { paint(before); }, [paint, after]() { paint(after); });
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("colored"), static_cast<int>(after.size())}, {QStringLiteral("color"), color.name()}});
}

//! The chain the arguments name, found again on every call (an undo may have rebuilt it).
std::function<EffectChain*()> chainFinder(const QJsonObject& args)
{
	return [args]() -> EffectChain* {
		ControlTarget target;
		ControlResult ignored;
		return resolveChainTarget(args, &target, &ignored) ? target.chain : nullptr;
	};
}

void setEnabledAll(const std::function<EffectChain*()>& chainOf, const std::vector<bool>& enabled)
{
	EffectChain* chain = chainOf();
	if (chain == nullptr) { return; }
	for (std::size_t i = 0; i < chain->effects().size() && i < enabled.size(); ++i) { chain->effects()[i]->setEnabled(enabled[i]); }
}

ControlResult bypassAll(const QJsonObject& args)
{
	const auto chainOf = chainFinder(args);
	ControlTarget target;
	ControlResult error;
	if (!resolveChainTarget(args, &target, &error)) { return error; }
	EffectChain* chain = target.chain;
	std::vector<bool> before;
	for (const Effect* effect : chain->effects()) { before.push_back(effect->isEnabled()); }
	const std::vector<bool> after(before.size(), !args.value(QStringLiteral("bypass")).toBool(true));
	setEnabledAll(chainOf, after);
	addUndoStep([chainOf, before]() { setEnabledAll(chainOf, before); }, [chainOf, after]() { setEnabledAll(chainOf, after); });
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("devices"), static_cast<int>(after.size())}, {QStringLiteral("bypassed"), !after.empty() && !after.front()}});
}

ControlResult clean(const QJsonObject& args)
{
	ControlTarget target;
	ControlResult error;
	if (!resolveChainTarget(args, &target, &error)) { return error; }
	EffectChain* chain = target.chain;
	std::vector<Effect*> bypassed;
	for (Effect* effect : chain->effects()) { if (!effect->isEnabled()) { bypassed.push_back(effect); } }
	if (!devicesRecordable(bypassed))
	{
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("a bypassed device's state is too large to keep for an undo"));
	}
	removeDevicesRecorded(chainFinder(args), bypassed);
	Engine::getSong()->setModified();
	return ControlResult::success({{QStringLiteral("removed"), static_cast<int>(bypassed.size())}});
}

void add(ControlRegistry& registry, const char* id, const QString& description, const QJsonObject& args, const QStringList& required,
	bool mutating, ControlResult (*handler)(const QJsonObject&))
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

} // namespace

void registerMixerToolCommands(ControlRegistry& registry)
{
	MixerOptions::instance();  // registers its project section before a project loads
	const QJsonObject chainArgs{{QStringLiteral("target"), stringProperty()}, {QStringLiteral("chain"), integerProperty(0, 64)}};
	add(registry, "mixer.options", QStringLiteral("The project's pan law: balance (the default - a centred signal is not cut), "
		"constant_power (-3 dB at the centre), compromise (-4.5 dB) or linear (-6 dB); every channel's balance pan uses it. "
		"Saved with the project. With no arguments it reports it. One undo step."),
		{{QStringLiteral("pan_law"), enumProperty(MixerOptions::panLawNames())}}, {}, true, options);
	add(registry, "mixer.send_enable", QStringLiteral("Switch the send from 'channel' to 'to' off ('enabled' false) or on again; it "
		"keeps its level while it is off. One undo step."),
		{{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("to"), stringProperty()}, {QStringLiteral("enabled"), booleanProperty()}},
		{QStringLiteral("channel"), QStringLiteral("to"), QStringLiteral("enabled")}, true, sendEnable);
	add(registry, "mixer.create_return", QStringLiteral("Make a return channel ('name', Return when left out) and send 'channel' to it "
		"at 'amount' (0..1, 1 when left out), pre-fader when 'pre_fader' - then add a reverb or a delay to the return. One undo "
		"step removes both."),
		{{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("name"), stringProperty()}, {QStringLiteral("amount"), numberProperty()},
			{QStringLiteral("pre_fader"), booleanProperty()}}, {QStringLiteral("channel")}, true, createReturn);
	add(registry, "mixer.find", QStringLiteral("The channels whose name holds 'text' (any case): id, name and position."),
		{{QStringLiteral("text"), stringProperty()}}, {QStringLiteral("text")}, false, find);
	add(registry, "mixer.adjust", QStringLiteral("Move several channels together: every one of 'channels' is turned up or down by "
		"'volume_db' and panned by 'pan' (-2..2, added to its own), keeping their balance - ganged faders. One undo step."),
		{{QStringLiteral("channels"), arrayProperty()}, {QStringLiteral("volume_db"), numberProperty()}, {QStringLiteral("pan"), numberProperty()}},
		{QStringLiteral("channels")}, true, adjust);
	add(registry, "mixer.order_by_tracks", QStringLiteral("Put the mixer channels in the order of the tracks that use them (channels "
		"no track uses keep their order, after them). One undo step."), {}, {}, true, orderByTracks);
	add(registry, "vca.color_members", QStringLiteral("Give every member channel of VCA group 'group' the colour 'color' (#rrggbb). "
		"One undo step."), {{QStringLiteral("group"), integerProperty()}, {QStringLiteral("color"), stringProperty()}},
		{QStringLiteral("group"), QStringLiteral("color")}, true, colorMembers);
	QJsonObject bypassArgs = chainArgs;
	bypassArgs.insert(QStringLiteral("bypass"), booleanProperty());
	add(registry, "chain.bypass_all", QStringLiteral("Bypass every device of the chain of 'target' (a track or channel; 'chain' n for "
		"a rack's parallel chain), or with 'bypass' false switch them all back on. One undo step."), bypassArgs,
		{QStringLiteral("target")}, true, bypassAll);
	add(registry, "chain.clean", QStringLiteral("Remove every bypassed device from the chain of 'target' ('chain' n for a rack's "
		"parallel chain). One undo step puts them back where they were."), chainArgs, {QStringLiteral("target")}, true, clean);
}

} // namespace lmms
