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
#include <memory>
#include <optional>

#include <QColor>
#include <QJsonArray>

#include "ControlDeviceMoves.h"
#include "ControlDevicePlacement.h"
#include "ControlDeviceSupport.h"
#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlMeterSupport.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Effect.h"
#include "EffectChain.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "InputMeter.h"
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

//! Switch the send from the channel with persistent id @a fromId to the one with
//! @a toId on or off. The endpoints are resolved to their LIVE positions here, so
//! a reorder or removal between the recording and the undo/redo cannot make the
//! step act on the routes that now occupy the recorded positions (BUG 6).
void setSendEnabled(int fromId, int toId, bool on)
{
	MixerChannel* from = channelWithId(fromId);
	MixerChannel* to = channelWithId(toId);
	if (from == nullptr || to == nullptr) { return; }
	if (MixerRoute* route = findMixerRoute(static_cast<mix_ch_t>(from->index()), static_cast<mix_ch_t>(to->index())))
	{
		route->enabledModel()->setValue(on);
	}
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
		// The step keeps the endpoints' PERSISTENT ids, resolved to live positions when it
		// runs: a reorder between this call and its undo would otherwise make it toggle the
		// route that took the recorded positions' places (BUG 6).
		const int fromId = ends.from->id();
		const int toId = ends.to->id();
		addUndoStep([fromId, toId, before]() { setSendEnabled(fromId, toId, before); }, [fromId, toId, on]() { setSendEnabled(fromId, toId, on); });
		Engine::getSong()->setModified();
	}
	QJsonObject result{{QStringLiteral("from"), channelIdOf(ends.from)}, {QStringLiteral("to"), channelIdOf(ends.to)},
		{QStringLiteral("enabled"), on}, {QStringLiteral("amount"), static_cast<double>(route->amount()->value())}};
	if (on != before)
	{
		result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("from"), channelIdOf(ends.from)},
			{QStringLiteral("to"), channelIdOf(ends.to)}, {QStringLiteral("enabled"), before}}, QStringLiteral("control.undo"), {}, true,
			QStringLiteral("action checkpoint: the recorded undo step restores the send's previous enabled state")));
	}
	return ControlResult::success(result);
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
	// The master cannot send (the mixer's rule): a return fed from it would play back into it.
	if (from->index() == 0) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("the master cannot feed a return")); }
	const int fromId = from->id();
	const QString name = args.value(QStringLiteral("name")).toString(QStringLiteral("Return"));
	const double wanted = args.value(QStringLiteral("amount")).toDouble(1.0);
	if (wanted < 0.0 || wanted > 1.0) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'amount' is 0..1")); }
	const auto amount = static_cast<float>(wanted);
	const bool preFader = args.value(QStringLiteral("pre_fader")).toBool(false);
	const int index = buildReturn(fromId, name, amount, preFader);
	// The return by its persistent id, which the redo refreshes: undo removes that channel, not whichever is last
	// (review W3-batchA #9).
	auto made = std::make_shared<int>(mixer()->mixerChannel(index)->id());
	const auto removeMade = [made]() {
		for (int i = 1; i < mixer()->numChannels(); ++i)
		{
			if (mixer()->mixerChannel(i)->id() == *made) { mixer()->deleteChannel(i); return; }
		}
	};
	addUndoStep(removeMade, [made, fromId, name, amount, preFader]() { *made = mixer()->mixerChannel(buildReturn(fromId, name, amount, preFader))->id(); });
	Engine::getSong()->setModified();
	const QString madeId = channelIdOf(mixer()->mixerChannel(index));
	QJsonObject result{{QStringLiteral("channel"), madeId}, {QStringLiteral("from"), channelIdOf(from)}, {QStringLiteral("name"), name}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("channel"), madeId},
		{QStringLiteral("from"), channelIdOf(from)}, {QStringLiteral("existed"), false}}, QStringLiteral("control.undo"), {}, true,
		QStringLiteral("action checkpoint: the recorded undo step removes the created return and its send")));
	return ControlResult::success(result);
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
		if (std::find(out->begin(), out->end(), channel) == out->end()) { out->push_back(channel); }  // named twice moves once
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
	if (!std::isfinite(db) || !std::isfinite(pan))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'volume_db' and 'pan' must be finite"));
	}
	if (db == 0.0 && pan == 0.0) { return ControlResult::success({{QStringLiteral("adjusted"), 0}}); }  // nothing to move (review W3-batchA #11)
	const float factor = static_cast<float>(std::pow(10.0, db / 20.0));
	const float panDelta = static_cast<float>(pan);
	bool changesAnyChannel = false;
	for (MixerChannel* channel : channels)
	{
		const FloatModel& volume = channel->m_volumeModel;
		const float adjustedVolume = std::clamp(volume.value() * factor, volume.minValue(), volume.maxValue());
		const float adjustedPan = std::clamp(channel->m_panModel.value() + panDelta, -1.f, 1.f);
		if (adjustedVolume != volume.value() || adjustedPan != channel->m_panModel.value()) { changesAnyChannel = true; break; }
	}
	if (!changesAnyChannel) { return ControlResult::success({{QStringLiteral("adjusted"), 0}}); }
	QVector<JournallingObject*> models;
	for (MixerChannel* channel : channels) { models << &channel->m_volumeModel << &channel->m_panModel; }
	addUndoStep(models);
	for (MixerChannel* channel : channels)
	{
		FloatModel& volume = channel->m_volumeModel;
		volume.setValue(std::clamp(volume.value() * factor, volume.minValue(), volume.maxValue()));
		channel->m_panModel.setValue(std::clamp(channel->m_panModel.value() + panDelta, -1.f, 1.f));
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
		const bool onAChannel = model != nullptr && model->value() > 0 && model->value() < mixer()->numChannels();
		MixerChannel* channel = onAChannel ? mixer()->mixerChannel(model->value()) : nullptr;
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

//! MX-085: the strip EQ's four values, read from and written to a channel found by its persistent id.
struct EqValues
{
	float low = 0.f;
	float mid = 0.f;
	float midFreq = 1000.f;
	float high = 0.f;
};

EqValues eqOf(const MixerChannel* channel)
{
	return {channel->m_eqLowModel.value(), channel->m_eqMidModel.value(), channel->m_eqMidFreqModel.value(), channel->m_eqHighModel.value()};
}

bool sameEq(const EqValues& a, const EqValues& b)
{
	return a.low == b.low && a.mid == b.mid && a.midFreq == b.midFreq && a.high == b.high;
}

void setEq(int id, const EqValues& eq)
{
	MixerChannel* channel = channelWithId(id);
	if (channel == nullptr) { return; }
	channel->m_eqLowModel.setValue(eq.low);
	channel->m_eqMidModel.setValue(eq.mid);
	channel->m_eqMidFreqModel.setValue(eq.midFreq);
	channel->m_eqHighModel.setValue(eq.high);
}

ControlResult channelEq(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	const EqValues before = eqOf(channel);
	const EqValues after{static_cast<float>(std::clamp(args.value(QStringLiteral("low")).toDouble(before.low), -18.0, 18.0)),
		static_cast<float>(std::clamp(args.value(QStringLiteral("mid")).toDouble(before.mid), -18.0, 18.0)),
		static_cast<float>(std::clamp(args.value(QStringLiteral("mid_freq")).toDouble(before.midFreq), 100.0, 10000.0)),
		static_cast<float>(std::clamp(args.value(QStringLiteral("high")).toDouble(before.high), -18.0, 18.0))};
	const int id = channel->id();
	if (!sameEq(before, after))  // with only 'channel' it is a report: no write, no step, nothing modified
	{
		setEq(id, after);
		addUndoStep([id, before]() { setEq(id, before); }, [id, after]() { setEq(id, after); });
		Engine::getSong()->setModified();
	}
	return ControlResult::success({{QStringLiteral("channel"), channelIdOf(channel)}, {QStringLiteral("low"), static_cast<double>(after.low)},
		{QStringLiteral("mid"), static_cast<double>(after.mid)}, {QStringLiteral("mid_freq"), static_cast<double>(after.midFreq)},
		{QStringLiteral("high"), static_cast<double>(after.high)}});
}

//! MX-055: every channel with its sends - the routing matrix's rows.
ControlResult routingMatrix(const QJsonObject&)
{
	QJsonArray channels;
	for (int i = 0; i < mixer()->numChannels(); ++i)
	{
		MixerChannel* channel = mixer()->mixerChannel(i);
		QJsonArray sends;
		for (MixerRoute* route : channel->m_sends)
		{
			sends.append(QJsonObject{{QStringLiteral("to"), channelIdOf(route->receiver())}, {QStringLiteral("amount"), static_cast<double>(route->amount()->value())},
				{QStringLiteral("pre_fader"), route->preFader()}, {QStringLiteral("enabled"), route->enabled()}});
		}
		channels.append(QJsonObject{{QStringLiteral("channel"), channelIdOf(channel)}, {QStringLiteral("name"), i == 0 ? QStringLiteral("Master") : channel->m_name},
			{QStringLiteral("index"), i}, {QStringLiteral("sends"), sends}});
	}
	return ControlResult::success({{QStringLiteral("channels"), channels}});
}

ControlResult meterReference(const QJsonObject& args)
{
	const double before = meterReferenceDbfs();
	const double dbfs = args.value(QStringLiteral("dbfs")).toDouble(before);
	if (!std::isfinite(dbfs) || dbfs < -30.0 || dbfs > 0.0) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'dbfs' is -30..0")); }
	if (dbfs != before)
	{
		setMeterReferenceDbfs(dbfs);
		addUndoStep([before]() { setMeterReferenceDbfs(before); }, [dbfs]() { setMeterReferenceDbfs(dbfs); });
	}
	return ControlResult::success({{QStringLiteral("reference_dbfs"), dbfs}});
}

ControlResult inputLevel(const QJsonObject&)
{
	float left = 0.f;
	float right = 0.f;
	inputmeter::take(&left, &right);
	const auto db = [](float v) { return v > 0.f ? 20.0 * std::log10(v) : -120.0; };
	return ControlResult::success({{QStringLiteral("peak_left_db"), db(left)}, {QStringLiteral("peak_right_db"), db(right)},
		{QStringLiteral("clipped"), left >= 1.f || right >= 1.f}});
}

void setDelayOf(const QString& id, int ms)
{
	ControlResult ignored;
	if (auto* track = dynamic_cast<InstrumentTrack*>(resolveTrack(id, &ignored))) { track->setDelayMs(ms); }
}

ControlResult trackDelay(const QJsonObject& args)
{
	ControlResult error;
	auto* track = dynamic_cast<InstrumentTrack*>(resolveTrack(args.value(QStringLiteral("track")).toString(), &error));
	if (track == nullptr) { return error.errorMessage.isEmpty() ? ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'track' is an instrument track")) : error; }
	const QString id = trackIdOf(track);
	const int before = track->delayMs();
	const int ms = args.value(QStringLiteral("delay_ms")).toInt(before);
	if (ms != before)
	{
		track->setDelayMs(ms);
		addUndoStep([id, before]() { setDelayOf(id, before); }, [id, ms]() { setDelayOf(id, ms); });
		Engine::getSong()->setModified();
	}
	return ControlResult::success({{QStringLiteral("track"), id}, {QStringLiteral("delay_ms"), track->delayMs()}});
}

//! MX-017: what feeds a channel and where it goes.
ControlResult routingUsage(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	QJsonArray tracks;
	for (Track* track : Engine::getSong()->tracks())
	{
		IntModel* model = track->mixerChannelModel();
		if (model != nullptr && model->value() == channel->index()) { tracks.append(trackIdOf(track)); }
	}
	QJsonArray from;
	for (const MixerRoute* route : channel->m_receives) { from.append(channelIdOf(route->sender())); }
	QJsonArray to;
	for (const MixerRoute* route : channel->m_sends) { to.append(channelIdOf(route->receiver())); }
	return ControlResult::success({{QStringLiteral("channel"), channelIdOf(channel)}, {QStringLiteral("tracks"), tracks},
		{QStringLiteral("receives_from"), from}, {QStringLiteral("sends_to"), to}});
}

ControlResult colorMembers(const QJsonObject& args)
{
	VcaGroup* group = mixer()->vcaGroup(args.value(QStringLiteral("group")).toInt(-1));
	if (group == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no such group")); }
	QColor color(args.value(QStringLiteral("color")).toString());
	if (!color.isValid()) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'color' is #rrggbb")); }
	color.setAlpha(255);  // what the project saves (#rrggbb) and the reply says
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
	if (after != before) // an empty group, or one already in this colour, changes nothing to journal
	{
		paint(after);
		addUndoStep([paint, before]() { paint(before); }, [paint, after]() { paint(after); });
		Engine::getSong()->setModified();
	}
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
	control::defineCommandWithSchema(registry, QLatin1String(id), description, objectSchema(args, QJsonArray::fromStringList(required)), mutating, handler);
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
	add(registry, "mixer.channel_eq", QStringLiteral("A channel's own three-band EQ, after its inserts: 'low' (a shelf at 100 Hz), "
		"'mid' (a bell at 'mid_freq', 100..10000 Hz) and 'high' (a shelf at 8 kHz), each -18..18 dB; flat skips it. Saved with "
		"the project. With only 'channel' it reports it. One undo step."),
		{{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("low"), numberProperty()}, {QStringLiteral("mid"), numberProperty()},
			{QStringLiteral("mid_freq"), numberProperty()}, {QStringLiteral("high"), numberProperty()}}, {QStringLiteral("channel")}, true, channelEq);
	add(registry, "meter.set_reference", QStringLiteral("The studio's calibration reference: the dBFS that reads 0 on the reference "
		"scale (-18 by default, 0 VU = -18 dBFS; -30..0). A preference, kept in the config file; meter.get_state reports it "
		"in 'target'. With no arguments it reports it. One undo step."), {{QStringLiteral("dbfs"), numberProperty()}}, {}, true, meterReference);
	add(registry, "meter.input", QStringLiteral("How loud the audio input is: the peak of each side since the last call, in dB "
		"(-120 for silence), and whether it reached full scale - the level to set a microphone by. Read only."), {}, {}, false, inputLevel);
	add(registry, "track.set_delay", QStringLiteral("Start every note of instrument track 'track' 'delay_ms' (0..500) milliseconds later - "
		"a slow instrument lined up, or a part sat behind the beat. Saved with the track. With only 'track' it reports it. One undo step."),
		{{QStringLiteral("track"), stringProperty()}, {QStringLiteral("delay_ms"), integerProperty(0, 500)}}, {QStringLiteral("track")}, true, trackDelay);
	add(registry, "routing.usage", QStringLiteral("What uses a channel: the tracks playing into it, the channels sending into it and "
		"the channels it sends to. Read only."), {{QStringLiteral("channel"), stringProperty()}}, {QStringLiteral("channel")}, false, routingUsage);
	add(registry, "routing.matrix", QStringLiteral("The routing matrix: every channel (id, name, position) with its sends - to, amount, "
		"pre-fader, on or off. Read only."), {}, {}, false, routingMatrix);
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
