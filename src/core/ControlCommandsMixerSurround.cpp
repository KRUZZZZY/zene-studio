/*
 * ControlCommandsMixerSurround.cpp - mixer.channel_format, mixer.surround_pan, mixer.downmix_options, mixer.surround_get
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

/*! 0.5.0 F7 (MX-060): surround channels (MixerSurround.h).
 *  - mixer.channel_format: a channel's format - stereo, lcr, quad, 5.1 or 7.1. One undo step.
 *  - mixer.surround_pan: a channel's surround panner, for its sends into surround channels. One undo step.
 *  - mixer.downmix_options: how surround folds down to fewer speakers (the project's). One undo step.
 *  - mixer.surround_get: every channel's format, speakers, panner and hardware outputs, and the downmix
 *    options. Read only.
 *  Saved with the project; each change is made under the engine's change lock. */

#include <cmath>

#include <QJsonArray>

#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlHardwareIoSupport.h"
#include "ControlMixerSupport.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerSurround.h"
#include "Song.h"

namespace lmms
{

using namespace control;
using namespace control::hardware;

namespace
{

double toDb(float gain)
{
	return gain > 0.f ? 20.0 * std::log10(static_cast<double>(gain)) : -144.0;
}

float fromDb(double db)
{
	return static_cast<float>(std::pow(10.0, db / 20.0));
}

QJsonObject panJson(const surround::PanPosition& pan)
{
	return {{QStringLiteral("enabled"), pan.enabled}, {QStringLiteral("x"), static_cast<double>(pan.x)},
		{QStringLiteral("y"), static_cast<double>(pan.y)}, {QStringLiteral("lfe"), static_cast<double>(pan.lfe)},
		{QStringLiteral("spread"), static_cast<double>(pan.spread)}};
}

QJsonObject downmixJson(const surround::DownmixOptions& options)
{
	return {{QStringLiteral("center_db"), toDb(options.centre)}, {QStringLiteral("surround_db"), toDb(options.surround)},
		{QStringLiteral("lfe"), options.lfeOn}, {QStringLiteral("lfe_db"), toDb(options.lfe)},
		{QStringLiteral("center_gain"), static_cast<double>(options.centre)}, {QStringLiteral("surround_gain"), static_cast<double>(options.surround)}};
}

//! Where a surround channel plays on the hardware: its first output and how many it takes, or the downmix.
QJsonObject hardwareJson(MixerChannel& channel)
{
	const audioio::Destination out = channel.m_hardwareIo.effectiveOutput(channel.index() == 0);
	const int count = surround::channelCount(channel.m_surround.format());
	if (out.none()) { return {{QStringLiteral("status"), QStringLiteral("none")}}; }
	const bool wide = out.width == 2 && surround::deviceCarries(out.start, count);
	const QString last = QString::number(out.start + (wide ? count : out.width));
	return {{QStringLiteral("outputs"), QStringLiteral("%1-%2").arg(out.start + 1).arg(last)},
		{QStringLiteral("status"), wide ? QStringLiteral("ok") : QStringLiteral("downmix")}};
}

//! The surround channels @a channel sends into: where its panner acts.
QJsonArray surroundSends(MixerChannel& channel)
{
	QJsonArray sends;
	for (MixerRoute* route : channel.m_sends)
	{
		if (!route->receiver()->m_surround.isSurround()) { continue; }
		sends.append(QJsonObject{{QStringLiteral("to"), channelIdOf(route->receiver())},
			{QStringLiteral("format"), surround::formatName(route->receiver()->m_surround.format())}});
	}
	return sends;
}

QJsonObject channelJson(MixerChannel& channel)
{
	const surround::Format format = channel.m_surround.format();
	QJsonObject json{{QStringLiteral("channel"), channelIdOf(&channel)}, {QStringLiteral("name"), channel.m_name},
		{QStringLiteral("format"), surround::formatName(format)},
		{QStringLiteral("speakers"), QJsonArray::fromStringList(surround::speakerNames(format))},
		{QStringLiteral("pan"), panJson(channel.m_surround.pan())}, {QStringLiteral("surround_sends"), surroundSends(channel)}};
	if (channel.m_surround.isSurround()) { json.insert(QStringLiteral("hardware"), hardwareJson(channel)); }
	return json;
}

QJsonObject withTransaction(QJsonObject result, const QJsonObject& before, const QString& op, const QJsonObject& inverse,
	const QString& mechanism)
{
	result.insert(QStringLiteral("__transaction"), transactionPayload(before, op, inverse, true, mechanism));
	return result;
}

// ---- mixer.channel_format ----

void applyFormat(const QString& id, surround::Format format)
{
	MixerChannel* channel = channelById(id);
	if (channel == nullptr) { return; }
	underChangeLock([channel, format]() { channel->m_surround.setFormat(format); });
}

ControlResult channelFormat(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	surround::Format format = surround::Format::Stereo;
	if (!surround::formatFromName(args.value(QStringLiteral("format")).toString(), &format))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'format' is one of: %1")
			.arg(surround::formatNames().join(QStringLiteral(", "))));
	}
	const QString id = channelIdOf(channel);
	const surround::Format before = channel->m_surround.format();
	if (format == before) { return ControlResult::success(channelJson(*channel)); }
	applyFormat(id, format);
	addUndoStep([id, before]() { applyFormat(id, before); }, [id, format]() { applyFormat(id, format); });
	Engine::getSong()->setModified();
	const QString previous = surround::formatName(before);
	return ControlResult::success(withTransaction(channelJson(*channel), {{QStringLiteral("channel"), id}, {QStringLiteral("format"), previous}},
		QStringLiteral("mixer.channel_format"), {{QStringLiteral("channel"), id}, {QStringLiteral("format"), previous}},
		QStringLiteral("action checkpoint: the recorded undo step puts the channel's previous format back")));
}

// ---- mixer.surround_pan ----

void applyPan(const QString& id, const surround::PanPosition& pan)
{
	MixerChannel* channel = channelById(id);
	if (channel == nullptr) { return; }
	underChangeLock([channel, pan]() { channel->m_surround.setPan(pan); });
}

bool inRange(double value, double low, double high)
{
	return value >= low && value <= high;
}

//! 'x', 'y', 'lfe', 'spread' and 'enabled' on top of @a pan. A position given without 'enabled' turns it on.
bool readPan(const QJsonObject& args, surround::PanPosition* pan, ControlResult* error)
{
	const double x = args.value(QStringLiteral("x")).toDouble(pan->x);
	const double y = args.value(QStringLiteral("y")).toDouble(pan->y);
	const double lfe = args.value(QStringLiteral("lfe")).toDouble(pan->lfe);
	const double spread = args.value(QStringLiteral("spread")).toDouble(pan->spread);
	if (!inRange(x, -1.0, 1.0) || !inRange(y, -1.0, 1.0) || !inRange(lfe, 0.0, 1.0) || !inRange(spread, 0.0, 1.0))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'x' and 'y' are -1..1 (front is y 1), "
			"'lfe' and 'spread' 0..1"));
		return false;
	}
	const bool moved = args.contains(QStringLiteral("x")) || args.contains(QStringLiteral("y"))
		|| args.contains(QStringLiteral("lfe")) || args.contains(QStringLiteral("spread"));
	*pan = {args.value(QStringLiteral("enabled")).toBool(pan->enabled || moved), static_cast<float>(x), static_cast<float>(y),
		static_cast<float>(lfe), static_cast<float>(spread)};
	return true;
}

QJsonObject panArgs(const QString& id, const surround::PanPosition& pan)
{
	QJsonObject args = panJson(pan);
	args.insert(QStringLiteral("channel"), id);
	return args;
}

ControlResult surroundPan(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	const surround::PanPosition before = channel->m_surround.pan();
	surround::PanPosition after = before;
	if (!readPan(args, &after, &error)) { return error; }
	const QString id = channelIdOf(channel);
	if (after == before) { return ControlResult::success(channelJson(*channel)); }
	applyPan(id, after);
	addUndoStep([id, before]() { applyPan(id, before); }, [id, after]() { applyPan(id, after); });
	Engine::getSong()->setModified();
	QJsonObject result = channelJson(*channel);
	if (!channel->m_surround.isSurround() && surroundSends(*channel).isEmpty())
	{
		result.insert(QStringLiteral("note"), QStringLiteral("the panner acts on a stereo channel's sends into surround channels; "
			"this channel has none (route it into one set with mixer.channel_format)"));
	}
	return ControlResult::success(withTransaction(result, panArgs(id, before), QStringLiteral("mixer.surround_pan"), panArgs(id, before),
		QStringLiteral("action checkpoint: the recorded undo step puts the channel's previous surround panner back")));
}

// ---- mixer.downmix_options ----

void applyDownmix(const surround::DownmixOptions& options)
{
	underChangeLock([options]() { surround::setDownmixOptions(options); });
}

bool readDb(const QJsonObject& args, const QString& key, double low, double high, float* gain, ControlResult* error)
{
	if (!args.contains(key)) { return true; }
	const double db = args.value(key).toDouble(1000.0);
	if (!inRange(db, low, high))
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'%1' is %2..%3 dB").arg(key).arg(low).arg(high));
		return false;
	}
	*gain = fromDb(db);
	return true;
}

bool readDownmix(const QJsonObject& args, surround::DownmixOptions* options, ControlResult* error)
{
	if (args.value(QStringLiteral("reset")).toBool(false)) { *options = surround::DownmixOptions{}; }
	options->lfeOn = args.value(QStringLiteral("lfe")).toBool(options->lfeOn);
	return readDb(args, QStringLiteral("center_db"), -60.0, 6.0, &options->centre, error)
		&& readDb(args, QStringLiteral("surround_db"), -60.0, 6.0, &options->surround, error)
		&& readDb(args, QStringLiteral("lfe_db"), -60.0, 12.0, &options->lfe, error);
}

QJsonObject downmixArgs(const surround::DownmixOptions& options)
{
	return {{QStringLiteral("center_db"), toDb(options.centre)}, {QStringLiteral("surround_db"), toDb(options.surround)},
		{QStringLiteral("lfe"), options.lfeOn}, {QStringLiteral("lfe_db"), toDb(options.lfe)}};
}

ControlResult downmixOptions(const QJsonObject& args)
{
	ControlResult error;
	const surround::DownmixOptions before = surround::downmixOptions();
	surround::DownmixOptions after = before;
	if (!readDownmix(args, &after, &error)) { return error; }
	if (after == before) { return ControlResult::success(downmixJson(after)); }
	applyDownmix(after);
	addUndoStep([before]() { applyDownmix(before); }, [after]() { applyDownmix(after); });
	Engine::getSong()->setModified();
	return ControlResult::success(withTransaction(downmixJson(after), downmixArgs(before), QStringLiteral("mixer.downmix_options"),
		downmixArgs(before), QStringLiteral("action checkpoint: the recorded undo step puts the previous downmix options back")));
}

// ---- mixer.surround_get ----

ControlResult surroundGet(const QJsonObject& args)
{
	QJsonArray channels;
	if (args.contains(QStringLiteral("channel")))
	{
		ControlResult error;
		MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
		if (channel == nullptr) { return error; }
		channels.append(channelJson(*channel));
	}
	else
	{
		for (int i = 0; i < Engine::mixer()->numChannels(); ++i) { channels.append(channelJson(*Engine::mixer()->mixerChannel(i))); }
	}
	return ControlResult::success({{QStringLiteral("channels"), channels}, {QStringLiteral("downmix"), downmixJson(surround::downmixOptions())},
		{QStringLiteral("formats"), QJsonArray::fromStringList(surround::formatNames())}});
}

QJsonObject rangeProperty(double minimum, double maximum)
{
	QJsonObject property = numberProperty();
	property.insert(QStringLiteral("minimum"), minimum);
	property.insert(QStringLiteral("maximum"), maximum);
	return property;
}

} // namespace

void registerSurroundCommands(ControlRegistry& registry)
{
	defineCommand(registry, QStringLiteral("mixer.channel_format"), QStringLiteral("Set a channel's speaker format: 'stereo' (the "
		"default), 'lcr', 'quad', '5.1' or '7.1' (WAVE channel order: L R C LFE Ls Rs; 7.1 L R C LFE Lrs Rrs Ls Rs). A surround channel "
		"keeps its speakers beyond L/R next to its stereo signal: its devices process L/R only, the other speakers pass with its trim, "
		"VCA gain, pan (a balance on each left/right pair), fader and mute. Into a stereo channel - the master, usually - it is "
		"downmixed (mixer.downmix_options); its hardware output names its first output and it takes as many as it has channels, "
		"or the stereo downmix where the device lacks them. Saved with the project; one undo step."),
		{{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("format"), enumProperty(surround::formatNames())}},
		{QStringLiteral("channel"), QStringLiteral("format")}, true, channelFormat);
	defineCommand(registry, QStringLiteral("mixer.surround_pan"), QStringLiteral("A channel's surround panner, for its sends into "
		"surround channels, in place of its stereo pan: 'x' -1 (left) .. 1 (right) and 'y' -1 (back) .. 1 (front) place it in the "
		"room (constant-power between the two speakers either side; nearer the middle, more of the room), 'lfe' 0..1 sends to the "
		"LFE, 'spread' 0..1 widens it toward every speaker. A position turns the panner on; 'enabled' false turns it off (L/R then "
		"go to the receiver's L/R). The channel's L/R are summed to mono. Saved with the project; one undo step."),
		{{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("x"), rangeProperty(-1.0, 1.0)}, {QStringLiteral("y"), rangeProperty(-1.0, 1.0)},
			{QStringLiteral("lfe"), rangeProperty(0.0, 1.0)}, {QStringLiteral("spread"), rangeProperty(0.0, 1.0)},
			{QStringLiteral("enabled"), booleanProperty()}},
		{QStringLiteral("channel")}, true, surroundPan);
	defineCommand(registry, QStringLiteral("mixer.downmix_options"), QStringLiteral("How surround folds into fewer speakers (into "
		"stereo, a surround master on a stereo device, and every export): 'center_db' and 'surround_db' (-3 dB each by default, "
		"ITU-R BS.775), 'lfe' true to keep the LFE (dropped by default) at 'lfe_db' (0 by default); 'reset' true for the defaults "
		"first. The project's; saved with it; one undo step."),
		{{QStringLiteral("center_db"), rangeProperty(-60.0, 6.0)}, {QStringLiteral("surround_db"), rangeProperty(-60.0, 6.0)},
			{QStringLiteral("lfe"), booleanProperty()}, {QStringLiteral("lfe_db"), rangeProperty(-60.0, 12.0)}, {QStringLiteral("reset"), booleanProperty()}},
		{}, true, downmixOptions);
	defineCommand(registry, QStringLiteral("mixer.surround_get"), QStringLiteral("Every channel's speaker format, speakers, surround "
		"panner, the surround channels it sends into and, for a surround channel, the outputs it plays on ('ok') or that it is "
		"downmixed ('downmix'), with the downmix options ('channel' for one). Read only."),
		{{QStringLiteral("channel"), stringProperty()}}, {}, false, surroundGet);
}

} // namespace lmms
