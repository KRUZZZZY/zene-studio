/*
 * ControlCommandsMixerListen.cpp - mixer.listen / listen_mode / listen_get: AFL and PFL
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

/*! MX-037 (0.5.0 L09): solo to a listen bus. mixer.listen puts a channel on (or off) the listen bus;
 *  while any channel listens the master output carries the listened channels after their faders (afl)
 *  or before (pfl) instead of the mix (MixerListen.h). Interface state, like a monitor switch: not in
 *  the project, no undo step. mixer.send_pan (MX-048) pans one send, saved, one undo step. */

#include <QJsonArray>

#include "ControlCommandDef.h"
#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "ControlReversibility.h"
#include "Engine.h"
#include "Mixer.h"
#include "MixerListen.h"
#include "Song.h"

namespace lmms
{

using namespace control;

namespace
{

QJsonObject listenState()
{
	QJsonArray channels;
	for (const int index : mixerlisten::listeningChannels())
	{
		if (index < Engine::mixer()->numChannels()) { channels.append(channelIdOf(Engine::mixer()->mixerChannel(index))); }
	}
	return {{QStringLiteral("mode"), mixerlisten::mode() == mixerlisten::Mode::PreFader ? QStringLiteral("pfl") : QStringLiteral("afl")},
		{QStringLiteral("listening"), channels}, {QStringLiteral("active"), mixerlisten::anyListening()}};
}

ControlResult listen(const QJsonObject& args)
{
	ControlResult error;
	MixerChannel* channel = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), &error);
	if (channel == nullptr) { return error; }
	if (args.value(QStringLiteral("exclusive")).toBool()) { mixerlisten::clear(); }
	mixerlisten::setListening(channel->index(), args.value(QStringLiteral("on")).toBool(true));
	return ControlResult::success(listenState());
}

ControlResult listenMode(const QJsonObject& args)
{
	if (args.contains(QStringLiteral("mode")))
	{
		mixerlisten::setMode(args.value(QStringLiteral("mode")).toString() == QStringLiteral("pfl") ? mixerlisten::Mode::PreFader
			: mixerlisten::Mode::AfterFader);
	}
	if (args.value(QStringLiteral("clear")).toBool()) { mixerlisten::clear(); }
	return ControlResult::success(listenState());
}

//! MX-048: a send's own pan. Resolve persistent endpoint ids when the undo step runs;
//! a reorder or deletion can make the positions captured at edit time stale.
void setSendPan(const QString& fromId, const QString& toId, float pan)
{
	ControlResult ignored;
	MixerChannel* from = resolveMixerChannel(fromId, &ignored);
	MixerChannel* to = resolveMixerChannel(toId, &ignored);
	if (from == nullptr || to == nullptr) { return; }
	if (MixerRoute* route = findMixerRoute(static_cast<mix_ch_t>(from->index()), static_cast<mix_ch_t>(to->index())))
	{
		route->pan()->setValue(pan);
	}
}

ControlResult sendPan(const QJsonObject& args)
{
	ControlResult error;
	RoutingEnds ends;
	if (!resolveRoutingEnds(args, &ends, &error)) { return error; }
	const mix_ch_t from = static_cast<mix_ch_t>(ends.from->index());
	const mix_ch_t to = static_cast<mix_ch_t>(ends.to->index());
	MixerRoute* route = findMixerRoute(from, to);
	if (route == nullptr) { return ControlResult::failure(ControlErrorKind::NotFound, QStringLiteral("no send from %1 to %2").arg(
		channelIdOf(ends.from), channelIdOf(ends.to))); }
	const double pan = args.value(QStringLiteral("pan")).toDouble();
	if (pan < -1.0 || pan > 1.0) { return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'pan' is -1..1")); }
	const float before = route->pan()->value();
	const QString fromId = channelIdOf(ends.from);
	const QString toId = channelIdOf(ends.to);
	addUndoStep([fromId, toId, before]() { setSendPan(fromId, toId, before); },
		[fromId, toId, pan]() { setSendPan(fromId, toId, static_cast<float>(pan)); });
	route->pan()->setValue(static_cast<float>(pan));
	Engine::getSong()->setModified();
	QJsonObject result{{QStringLiteral("from"), channelIdOf(ends.from)}, {QStringLiteral("to"), channelIdOf(ends.to)}, {QStringLiteral("pan"), pan}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("pan"), static_cast<double>(before)}},
		QStringLiteral("mixer.send_pan"), {{QStringLiteral("channel"), channelIdOf(ends.from)}, {QStringLiteral("to"), channelIdOf(ends.to)},
			{QStringLiteral("pan"), static_cast<double>(before)}}, true, QStringLiteral("action checkpoint: the recorded undo step puts the send's previous pan back")));
	return ControlResult::success(result);
}

void add(ControlRegistry& registry, const char* verb, const QString& description, const QJsonObject& args,
	ControlResult (*handler)(const QJsonObject&), bool mutating = false)
{
	control::defineCommandWithSchema(registry, QStringLiteral("mixer.") + QLatin1String(verb), description, args, mutating, handler);
}

} // namespace

void registerMixerListenCommands(ControlRegistry& registry)
{
	add(registry, "listen", QStringLiteral("Solo a channel to the listen bus (AFL / PFL): 'on' (default true) puts it on, "
		"false takes it off, 'exclusive' first takes every other channel off. While any channel listens the master "
		"output carries the listened channels instead of the mix; the mix, mute, solo and sends are untouched. "
		"Interface state: not saved, no undo step."), objectSchema({{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("on"), booleanProperty()}, {QStringLiteral("exclusive"), booleanProperty()}}, {QStringLiteral("channel")}), listen);
	add(registry, "listen_mode", QStringLiteral("The listen bus: 'mode' afl (after the fader, the default) or pfl (before "
		"it); 'clear' takes every channel off. Answers the mode and the channels listening."),
		objectSchema({{QStringLiteral("mode"), enumProperty({QStringLiteral("afl"), QStringLiteral("pfl")})},
			{QStringLiteral("clear"), booleanProperty()}}), listenMode);
	add(registry, "send_pan", QStringLiteral("Pan one send ('channel' -> 'to', which must exist): -1..1, a balance on top of the "
		"channel's own pan, which the sent signal already carries (0, the default, follows it). Saved with the send; one "
		"undo step."), objectSchema({{QStringLiteral("channel"), stringProperty()}, {QStringLiteral("to"), stringProperty()},
		{QStringLiteral("pan"), numberProperty()}}, {QStringLiteral("channel"), QStringLiteral("to")}), sendPan, true);
}

} // namespace lmms
