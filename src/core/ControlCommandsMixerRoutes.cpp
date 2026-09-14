/*
 * ControlCommandsMixerRoutes.cpp - the mixer group's routing verbs (SPEC
 *                                  A11-A16): mixer.route_to, mixer.send_to,
 *                                  mixer.sidechain_to, mixer.route_remove.
 *
 * ableton-gap/AGENT-TOOLING.md:186 names `mixer.route_to` / `mixer.send_to` as
 * part of the mixer/routing surface this release must tool; at the 0.3.0 tip the
 * mixer group registered only five ids (add_channel, get_state, remove_channel,
 * set_pan, set_volume), so both were missing. The sidechain half of feature row
 * 27 ("PDC and sidechain", docs/FEATURE-LIST-0.3.0.md) is here for the same
 * reason: the engine's sidechain routing is real and proven
 * (tests/src/core/PhaseDSidechainTest.cpp; Mixer::createSidechainSend,
 * src/core/Mixer.cpp:1179) and had no command at all.
 *
 * WHY A SEPARATE TRANSLATION UNIT rather than more ids in
 * ControlCommandsMixer.cpp: that file is 296 lines and the four verbs below are
 * ~300 more; gate 7 measures a file, so this is the
 * ControlCommandsAutomation.cpp / ControlCommandsAutomationEdit.cpp split, in the
 * mixer group instead of the automation group. The ids keep the `mixer.` prefix,
 * so the group a client sees is still one group.
 *
 * ONE ENGINE MECHANISM, TWO NAMES, stated plainly because the alternative is a
 * client guessing: the mixer has ONE edge type - MixerRoute - and both a
 * "routing" and an "aux send" are one. They differ in what they mean and in what
 * this surface defaults, not in what the engine builds:
 *   * mixer.route_to   the channel's OUTPUT path. Amount 1.0 (a routing carries
 *                      the signal, it does not attenuate it).
 *   * mixer.send_to    an AUX send to the same destination, with an amount. A
 *                      send to a bus is pre-fader by default, which is the
 *                      engine's own rule (Mixer::createChannelSend,
 *                      src/core/Mixer.cpp:1089), because a bus must not be
 *                      attenuated by the sender's fader.
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

#include <QJsonArray>
#include <QJsonObject>

#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"

#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The regular send from \a fromIndex to \a toIndex, or nullptr. The mixer's
//! own list, walked the way channelSendModel does.
MixerRoute* findRoute(mix_ch_t fromIndex, mix_ch_t toIndex)
{
	MixerChannel* from = Engine::mixer()->mixerChannel(fromIndex);
	for (MixerRoute* route : from->m_sends)
	{
		if (route->receiverIndex() == toIndex) { return route; }
	}
	return nullptr;
}

//! Both endpoints of a routing verb, resolved and validated. Every refusal here
//! happens BEFORE any write, including the engine's own cycle rule.
struct RouteEnds
{
	MixerChannel* from = nullptr;
	MixerChannel* to = nullptr;
};

bool resolveRouteEnds(const QJsonObject& args, RouteEnds* ends, ControlResult* error)
{
	ends->from = resolveMixerChannel(args.value(QStringLiteral("channel")).toString(), error);
	if (ends->from == nullptr) { return false; }
	ends->to = resolveMixerChannel(args.value(QStringLiteral("to")).toString(), error);
	if (ends->to == nullptr) { return false; }
	if (ends->from->index() == ends->to->index())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("a channel cannot route to itself"));
		return false;
	}
	// The engine refuses a cycle at scheduling time (Mixer::checkInfiniteLoop);
	// asking it FIRST means the refusal is the engine's judgement and nothing
	// has been written when it comes back.
	Mixer* mixer = Engine::mixer();
	if (mixer->isInfiniteLoop(ends->from->index(), ends->to->index()))
	{
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("routing %1 to %2 would close a feedback path (the mixer refuses it)")
				.arg(channelId(ends->from->index()), channelId(ends->to->index())));
		return false;
	}
	return true;
}

//! The before-state of a regular send, so a recorded inverse can restore it.
QJsonObject routeBeforeState(MixerRoute* route)
{
	QJsonObject before;
	before.insert(QStringLiteral("existed"), route != nullptr);
	if (route != nullptr)
	{
		before.insert(QStringLiteral("amount"), static_cast<double>(route->amount()->value()));
		before.insert(QStringLiteral("pre_fader"), route->preFader());
	}
	return before;
}

//! The before-state of a sidechain send.
QJsonObject sidechainBeforeState(MixerSidechainRoute* route)
{
	QJsonObject before;
	before.insert(QStringLiteral("existed"), route != nullptr);
	if (route != nullptr)
	{
		before.insert(QStringLiteral("amount"), static_cast<double>(route->amount()->value()));
		before.insert(QStringLiteral("tap_point"), sidechainTapPointName(route->mode()));
	}
	return before;
}

/*! One undo step for a create-or-adjust of a regular send.
 *
 * A route that did not exist is undone by DELETING it; a route that did exist is
 * undone by putting its amount and pre-fader flag back. One step either way, so
 * one agent command stays one Ctrl+Z (SPEC A16 deliverable 3).
 */
void recordRouteStep(mix_ch_t fromIndex, mix_ch_t toIndex, const QJsonObject& before,
	float wantedAmount, bool wantedPreFader)
{
	const bool existed = before.value(QStringLiteral("existed")).toBool();
	const float amount = static_cast<float>(before.value(QStringLiteral("amount")).toDouble(1.0));
	const bool preFader = before.value(QStringLiteral("pre_fader")).toBool();
	Mixer* mixer = Engine::mixer();
	control::addUndoStep(
		[mixer, fromIndex, toIndex, existed, amount, preFader]() {
			if (!existed) { mixer->deleteChannelSend(fromIndex, toIndex); return; }
			MixerRoute* route = findRoute(fromIndex, toIndex);
			if (route != nullptr)
			{
				route->amount()->setValue(amount);
				route->setPreFader(preFader);
			}
		},
		// The redo re-applies exactly what this call applies, so a GUI redo is
		// faithful rather than a silently-dropped step.
		[mixer, fromIndex, toIndex, wantedAmount, wantedPreFader]() {
			mixer->createChannelSend(fromIndex, toIndex, wantedAmount, wantedPreFader);
		});
}

//! The same, for a sidechain send.
void recordSidechainStep(mix_ch_t fromIndex, mix_ch_t toIndex, const QJsonObject& before,
	float wantedAmount, SidechainTapPoint wantedMode)
{
	const bool existed = before.value(QStringLiteral("existed")).toBool();
	const float amount = static_cast<float>(before.value(QStringLiteral("amount")).toDouble(1.0));
	SidechainTapPoint mode = SidechainTapPoint::PostFader;
	sidechainTapPointFromName(before.value(QStringLiteral("tap_point")).toString(), &mode);
	Mixer* mixer = Engine::mixer();
	control::addUndoStep(
		[mixer, fromIndex, toIndex, existed, amount, mode]() {
			if (!existed) { mixer->deleteSidechainSend(fromIndex, toIndex); return; }
			MixerSidechainRoute* route = mixer->channelSidechainSend(fromIndex, toIndex);
			if (route != nullptr)
			{
				route->amount()->setValue(amount);
				route->setMode(mode);
			}
		},
		// The redo re-applies exactly what this call applies (the engine derives
		// the deferred flag itself, so it is not re-applied by hand).
		[mixer, fromIndex, toIndex, wantedAmount, wantedMode]() {
			mixer->createSidechainSend(fromIndex, toIndex, wantedAmount, wantedMode);
		});
}

QJsonObject routeResult(const RouteEnds& ends, MixerRoute* route, const QJsonObject& before)
{
	QJsonObject result;
	result.insert(QStringLiteral("from"), channelId(ends.from->index()));
	result.insert(QStringLiteral("to"), channelId(ends.to->index()));
	result.insert(QStringLiteral("amount"), static_cast<double>(route->amount()->value()));
	result.insert(QStringLiteral("pre_fader"), route->preFader());
	result.insert(QStringLiteral("created"), !before.value(QStringLiteral("existed")).toBool());
	result.insert(QStringLiteral("route"), routeJson(*route));
	return result;
}

ControlResult handleRouteWrite(const QJsonObject& args, bool auxiliary)
{
	ControlResult error;
	RouteEnds ends;
	if (!resolveRouteEnds(args, &ends, &error)) { return error; }

	Mixer* mixer = Engine::mixer();
	const mix_ch_t fromIndex = ends.from->index();
	const mix_ch_t toIndex = ends.to->index();
	// An auxiliary send takes the amount it is given (1.0 when none is given); a
	// routing carries the signal at unity.
	const float amount = auxiliary
		? static_cast<float>(args.value(QStringLiteral("amount")).toDouble(1.0))
		: 1.0f;
	if (amount < 0.0f || amount > 2.0f)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("amount %1 is out of range: a send is 0..2").arg(amount));
	}
	const bool preFader = args.value(QStringLiteral("pre_fader")).toBool(false);

	const QJsonObject before = routeBeforeState(findRoute(fromIndex, toIndex));
	recordRouteStep(fromIndex, toIndex, before, amount, preFader);
	MixerRoute* route = mixer->createChannelSend(fromIndex, toIndex, amount, preFader);
	if (route == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the mixer refused to create %1 -> %2")
				.arg(channelId(fromIndex), channelId(toIndex)));
	}

	QJsonObject result = routeResult(ends, route, before);
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("channel"), channelId(fromIndex));
	inverseArgs.insert(QStringLiteral("to"), channelId(toIndex));
	const bool existed = before.value(QStringLiteral("existed")).toBool();
	QJsonObject transaction = transactionPayload(before,
		existed ? (auxiliary ? QStringLiteral("mixer.send_to") : QStringLiteral("mixer.route_to"))
				: QStringLiteral("mixer.route_remove"),
		inverseArgs, true,
		existed
			? QStringLiteral("action checkpoint: the recorded undo step writes the captured amount "
				"and pre-fader flag back through the route's own models - the state one command "
				"changed, restored as ONE step")
			: QStringLiteral("action checkpoint: the recorded undo step deletes the send this "
				"command created, through the same Mixer::deleteChannelSend path "
				"mixer.route_remove uses; a new send carries only the amount this command set, so "
				"deleting it restores the routing exactly"));
	result.insert(QStringLiteral("__transaction"), transaction);
	return ControlResult::success(result);
}

ControlResult handleSidechainWrite(const QJsonObject& args)
{
	ControlResult error;
	RouteEnds ends;
	if (!resolveRouteEnds(args, &ends, &error)) { return error; }

	Mixer* mixer = Engine::mixer();
	const mix_ch_t fromIndex = ends.from->index();
	const mix_ch_t toIndex = ends.to->index();
	const float amount = static_cast<float>(args.value(QStringLiteral("amount")).toDouble(1.0));
	if (amount < 0.0f || amount > 2.0f)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("amount %1 is out of range: a send is 0..2").arg(amount));
	}
	SidechainTapPoint mode = SidechainTapPoint::PostFader;
	const QString tapName = args.value(QStringLiteral("tap_point")).toString(
		QStringLiteral("post_fader"));
	if (!sidechainTapPointFromName(tapName, &mode))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a tap point; use one of %2")
				.arg(tapName, sidechainTapPointNames().join(QStringLiteral(", "))));
	}

	const QJsonObject before = sidechainBeforeState(mixer->channelSidechainSend(fromIndex, toIndex));
	recordSidechainStep(fromIndex, toIndex, before, amount, mode);
	MixerSidechainRoute* route = mixer->createSidechainSend(fromIndex, toIndex, amount, mode);
	if (route == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the mixer refused a sidechain send %1 -> %2: the master cannot send, "
				"or the route would close a cycle made of sidechain sends alone (a sidechain edge "
				"never creates a circular wait, so the mixer refuses that one outright)")
				.arg(channelId(fromIndex), channelId(toIndex)));
	}

	QJsonObject result;
	result.insert(QStringLiteral("from"), channelId(fromIndex));
	result.insert(QStringLiteral("to"), channelId(toIndex));
	result.insert(QStringLiteral("amount"), static_cast<double>(route->amount()->value()));
	result.insert(QStringLiteral("tap_point"), sidechainTapPointName(route->mode()));
	result.insert(QStringLiteral("deferred"), route->deferred());
	result.insert(QStringLiteral("created"), !before.value(QStringLiteral("existed")).toBool());
	result.insert(QStringLiteral("sidechain"), sidechainRouteJson(*route));

	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("channel"), channelId(fromIndex));
	inverseArgs.insert(QStringLiteral("to"), channelId(toIndex));
	const bool existed = before.value(QStringLiteral("existed")).toBool();
	QJsonObject transaction = transactionPayload(before,
		existed ? QStringLiteral("mixer.sidechain_to") : QStringLiteral("UNIMPLEMENTED: remove this "
			"sidechain send"),
		inverseArgs, true,
		existed
			? QStringLiteral("action checkpoint: the recorded undo step writes the captured amount "
				"and tap point back through the route's own models")
			: QStringLiteral("action checkpoint: the recorded undo step deletes the sidechain send "
				"this command created (Mixer::deleteSidechainSend), which is exact because a new "
				"route carries only the amount, tap point and deferred flag this command set"));
	result.insert(QStringLiteral("__transaction"), transaction);
	return ControlResult::success(result);
}

ControlResult handleRouteRemove(const QJsonObject& args)
{
	ControlResult error;
	RouteEnds ends;
	if (!resolveRouteEnds(args, &ends, &error)) { return error; }

	Mixer* mixer = Engine::mixer();
	const mix_ch_t fromIndex = ends.from->index();
	const mix_ch_t toIndex = ends.to->index();

	if (args.value(QStringLiteral("sidechain")).toBool(false))
	{
		MixerSidechainRoute* route = mixer->channelSidechainSend(fromIndex, toIndex);
		if (route == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("no sidechain send from %1 to %2")
					.arg(channelId(fromIndex), channelId(toIndex)));
		}
		const QJsonObject before = sidechainBeforeState(route);
		const float amount = static_cast<float>(route->amount()->value());
		const SidechainTapPoint mode = route->mode();
		const bool deferred = route->deferred();
		control::addUndoStep(
			[mixer, fromIndex, toIndex, amount, mode]() {
				mixer->createSidechainSend(fromIndex, toIndex, amount, mode);
			},
			[mixer, fromIndex, toIndex]() { mixer->deleteSidechainSend(fromIndex, toIndex); });
		mixer->deleteSidechainSend(route);

		QJsonObject result;
		result.insert(QStringLiteral("removed"), true);
		result.insert(QStringLiteral("sidechain"), true);
		result.insert(QStringLiteral("from"), channelId(fromIndex));
		result.insert(QStringLiteral("to"), channelId(toIndex));
		QJsonObject transaction = transactionPayload(before,
			QStringLiteral("mixer.sidechain_to"),
			QJsonObject{{QStringLiteral("channel"), channelId(fromIndex)},
				{QStringLiteral("to"), channelId(toIndex)},
				{QStringLiteral("amount"), static_cast<double>(amount)},
				{QStringLiteral("tap_point"), sidechainTapPointName(mode)}},
			true,
			deferred
				? QStringLiteral("action checkpoint: the recorded undo step re-creates the route "
					"through the same Mixer::createSidechainSend call, with the captured amount and "
					"tap point. LIMIT: the route was DEFERRED (it closed a cycle through a regular "
					"send), and the re-created route is deferred again only while that cycle is "
					"still there")
				: QStringLiteral("action checkpoint: the recorded undo step re-creates the route "
					"through the same Mixer::createSidechainSend call, with the captured amount and "
					"tap point"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	}

	MixerRoute* route = findRoute(fromIndex, toIndex);
	if (route == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no send from %1 to %2 (read %1's sends with pdc.report or "
				"mixer.get_state)").arg(channelId(fromIndex), channelId(toIndex)));
	}
	const QJsonObject before = routeBeforeState(route);
	const float amount = static_cast<float>(route->amount()->value());
	const bool preFader = route->preFader();
	control::addUndoStep(
		[mixer, fromIndex, toIndex, amount, preFader]() {
			mixer->createChannelSend(fromIndex, toIndex, amount, preFader);
		},
		[mixer, fromIndex, toIndex]() { mixer->deleteChannelSend(fromIndex, toIndex); });
	mixer->deleteChannelSend(route);

	QJsonObject result;
	result.insert(QStringLiteral("removed"), true);
	result.insert(QStringLiteral("sidechain"), false);
	result.insert(QStringLiteral("from"), channelId(fromIndex));
	result.insert(QStringLiteral("to"), channelId(toIndex));
	QJsonObject transaction = transactionPayload(before,
		QStringLiteral("mixer.route_to"),
		QJsonObject{{QStringLiteral("channel"), channelId(fromIndex)},
			{QStringLiteral("to"), channelId(toIndex)},
			{QStringLiteral("amount"), static_cast<double>(amount)},
			{QStringLiteral("pre_fader"), preFader}},
		true,
		QStringLiteral("action checkpoint: the recorded undo step re-creates the send through the "
			"same Mixer::createChannelSend call, with the captured amount and pre-fader flag (a "
			"send's whole state is those two numbers plus its endpoints)"));
	result.insert(QStringLiteral("__transaction"), transaction);
	return ControlResult::success(result);
}

} // namespace

void registerMixerRouteCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mixer.route_to");
		cmd.group = QStringLiteral("mixer");
		cmd.verb = QStringLiteral("route_to");
		cmd.description = QStringLiteral("Make a channel's output go to another channel's input "
			"(the channel's routing), at unity. Validated against the mixer's own feedback rule "
			"before anything is written. Reversible: one undo step.");
		cmd.argsSchema = objectSchema(
			{{QStringLiteral("channel"), stringProperty()},
				{QStringLiteral("to"), stringProperty()},
				{QStringLiteral("pre_fader"), booleanProperty()}},
			{QStringLiteral("channel"), QStringLiteral("to")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("from"), stringProperty()},
			{QStringLiteral("to"), stringProperty()},
			{QStringLiteral("amount"), numberProperty()},
			{QStringLiteral("pre_fader"), booleanProperty()},
			{QStringLiteral("created"), booleanProperty()},
			{QStringLiteral("route"), objectProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return handleRouteWrite(args, false); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mixer.send_to");
		cmd.group = QStringLiteral("mixer");
		cmd.verb = QStringLiteral("send_to");
		cmd.description = QStringLiteral("Create or adjust an auxiliary send from one channel to "
			"another, with an amount. A send INTO a bus is pre-fader by default, which is the "
			"engine's own rule. Reversible: one undo step.");
		cmd.argsSchema = objectSchema(
			{{QStringLiteral("channel"), stringProperty()},
				{QStringLiteral("to"), stringProperty()},
				{QStringLiteral("amount"), numberProperty()},
				{QStringLiteral("pre_fader"), booleanProperty()}},
			{QStringLiteral("channel"), QStringLiteral("to")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("from"), stringProperty()},
			{QStringLiteral("to"), stringProperty()},
			{QStringLiteral("amount"), numberProperty()},
			{QStringLiteral("pre_fader"), booleanProperty()},
			{QStringLiteral("created"), booleanProperty()},
			{QStringLiteral("route"), objectProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return handleRouteWrite(args, true); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mixer.sidechain_to");
		cmd.group = QStringLiteral("mixer");
		cmd.verb = QStringLiteral("sidechain_to");
		cmd.description = QStringLiteral("Create or adjust a SIDECHAIN send: the sender's signal is "
			"tapped at the given point and delivered to the receiver's sidechain input instead of "
			"being mixed into its output. tap_point is one of post_fader, pre_fx, pre_fader, "
			"post_fader_no_gain. Reversible: one undo step.");
		cmd.argsSchema = objectSchema(
			{{QStringLiteral("channel"), stringProperty()},
				{QStringLiteral("to"), stringProperty()},
				{QStringLiteral("amount"), numberProperty()},
				{QStringLiteral("tap_point"), enumProperty(sidechainTapPointNames())}},
			{QStringLiteral("channel"), QStringLiteral("to")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("from"), stringProperty()},
			{QStringLiteral("to"), stringProperty()},
			{QStringLiteral("amount"), numberProperty()},
			{QStringLiteral("tap_point"), stringProperty()},
			{QStringLiteral("deferred"), booleanProperty()},
			{QStringLiteral("created"), booleanProperty()},
			{QStringLiteral("sidechain"), objectProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return handleSidechainWrite(args); };
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mixer.route_remove");
		cmd.group = QStringLiteral("mixer");
		cmd.verb = QStringLiteral("route_remove");
		cmd.description = QStringLiteral("Remove the send from one channel to another - a regular "
			"routing with sidechain false (the default), or a sidechain send with sidechain true. "
			"Reversible: one undo step re-creates it with the amount and tap it had.");
		cmd.argsSchema = objectSchema(
			{{QStringLiteral("channel"), stringProperty()},
				{QStringLiteral("to"), stringProperty()},
				{QStringLiteral("sidechain"), booleanProperty()}},
			{QStringLiteral("channel"), QStringLiteral("to")});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("removed"), booleanProperty()},
			{QStringLiteral("sidechain"), booleanProperty()},
			{QStringLiteral("from"), stringProperty()},
			{QStringLiteral("to"), stringProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) { return handleRouteRemove(args); };
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
