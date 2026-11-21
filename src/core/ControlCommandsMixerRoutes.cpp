/*
 * ControlCommandsMixerRoutes.cpp - the mixer group's routing verbs (SPEC
 *                                  A11-A16): mixer.route_to, mixer.send_to,
 *                                  mixer.sidechain_to, mixer.route_remove.
 *
 * docs/specs/AGENT-TOOLING.md:186 names `mixer.route_to` / `mixer.send_to` as
 * part of the mixer/routing surface this release must tool; at the 0.3.0 tip the
 * mixer group registered only five ids (add_channel, get_state, remove_channel,
 * set_pan, set_volume), so both were missing. The sidechain half of feature row
 * 27 ("PDC and sidechain", docs/FEATURE-LIST-0.3.0.md) is here for the same
 * reason: the engine's sidechain routing is real and proven
 * (tests/src/core/PhaseDSidechainTest.cpp; Mixer::createSidechainSend,
 * src/core/Mixer.cpp:1179) and had no command at all.
 *
 * WHY A SEPARATE TRANSLATION UNIT rather than more ids in
 * ControlCommandsMixer.cpp: that file is 296 lines and these four verbs plus
 * their engine-side helpers are more than gate 7's remaining headroom, so this
 * is the ControlCommandsAutomation.cpp / ControlCommandsAutomationEdit.cpp
 * split, in the mixer group instead of the automation group. The helper half -
 * resolving both endpoints, the engine's own cycle refusal, the before-states and
 * the recorded undo steps - lives in ControlMixerSupport.cpp beside the rest of
 * this surface's engine calls, so neither file approaches the cap. The ids keep
 * the `mixer.` prefix, so the group a client sees is still one group.
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
#include <QPointer>
#include <QVector>
#include <QJsonObject>

#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "Clip.h"
#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"

#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "PatternStore.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! An amount a send accepts: 0..2, the range mixer.set_volume publishes.
bool validSendAmount(double amount)
{
	return amount >= 0.0 && amount <= 2.0;
}

void collectAutomationClips(Track* track, AutomatableModel* model,
	QVector<QPointer<AutomationClip>>& clips)
{
	if (track == nullptr) { return; }
	for (Clip* clip : track->getClips())
	{
		auto* automation = dynamic_cast<AutomationClip*>(clip);
		if (automation == nullptr) { continue; }
		for (const QPointer<AutomatableModel>& object : automation->objects())
		{
			if (object == model)
			{
				clips.append(automation);
				break;
			}
		}
	}
}

void collectAutomationClips(TrackContainer* container, AutomatableModel* model,
	QVector<QPointer<AutomationClip>>& clips)
{
	if (container == nullptr) { return; }
	for (Track* track : container->tracks()) { collectAutomationClips(track, model, clips); }
}

QVector<QPointer<AutomationClip>> automationClipsFor(AutomatableModel* model)
{
	QVector<QPointer<AutomationClip>> clips;
	Song* song = Engine::getSong();
	collectAutomationClips(song, model, clips);
	collectAutomationClips(Engine::patternStore(), model, clips);
	if (song != nullptr) { collectAutomationClips(song->globalAutomationTrack(), model, clips); }
	return clips;
}

ControlResult handleRouteWrite(const QJsonObject& args, bool auxiliary)
{
	ControlResult error;
	RoutingEnds ends;
	if (!resolveRoutingEnds(args, &ends, &error)) { return error; }

	Mixer* mixer = Engine::mixer();
	const mix_ch_t fromIndex = ends.from->index();
	const mix_ch_t toIndex = ends.to->index();
	// An auxiliary send takes the amount it is given (1.0 when none is given); a
	// routing carries the signal at unity.
	const double wanted = auxiliary ? args.value(QStringLiteral("amount")).toDouble(1.0) : 1.0;
	if (!validSendAmount(wanted))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("amount %1 is out of range: a send is 0..2").arg(wanted));
	}
	const float amount = static_cast<float>(wanted);
	const bool preFader = args.value(QStringLiteral("pre_fader")).toBool(false);

	const QJsonObject before = mixerRouteBeforeState(findMixerRoute(fromIndex, toIndex));
	recordMixerRouteStep(channelIdOf(ends.from), channelIdOf(ends.to), before, amount, preFader);
	MixerRoute* route = mixer->createChannelSend(fromIndex, toIndex, amount, preFader);
	if (route == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the mixer refused to create %1 -> %2")
				.arg(channelIdOf(ends.from), channelIdOf(ends.to)));
	}
	Engine::getSong()->setModified();

	QJsonObject result = mixerRouteResult(ends, route, before);
	const QString inverseOp = auxiliary ? QStringLiteral("mixer.send_to")
									   : QStringLiteral("mixer.route_to");
	const bool existed = before.value(QStringLiteral("existed")).toBool();
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("channel"), channelIdOf(ends.from));
	inverseArgs.insert(QStringLiteral("to"), channelIdOf(ends.to));
	QJsonObject transaction = transactionPayload(before,
		existed ? inverseOp : QStringLiteral("mixer.route_remove"),
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

//! The tap point asked for, or a typed refusal naming the four the engine has.
bool resolveTapPoint(const QJsonObject& args, SidechainTapPoint* mode, ControlResult* error)
{
	const QString tapName = args.value(QStringLiteral("tap_point")).toString(
		QStringLiteral("post_fader"));
	if (sidechainTapPointFromName(tapName, mode)) { return true; }
	*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
		QStringLiteral("'%1' is not a tap point; use one of %2")
			.arg(tapName, sidechainTapPointNames().join(QStringLiteral(", "))));
	return false;
}

ControlResult handleSidechainWrite(const QJsonObject& args)
{
	ControlResult error;
	RoutingEnds ends;
	if (!resolveRoutingEnds(args, &ends, &error)) { return error; }
	const double wanted = args.value(QStringLiteral("amount")).toDouble(1.0);
	if (!validSendAmount(wanted))
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("amount %1 is out of range: a send is 0..2").arg(wanted));
	}
	SidechainTapPoint mode = SidechainTapPoint::PostFader;
	if (!resolveTapPoint(args, &mode, &error)) { return error; }

	Mixer* mixer = Engine::mixer();
	const mix_ch_t fromIndex = ends.from->index();
	const mix_ch_t toIndex = ends.to->index();
	const float amount = static_cast<float>(wanted);
	const QJsonObject before =
		mixerSidechainBeforeState(mixer->channelSidechainSend(fromIndex, toIndex));
	recordMixerSidechainStep(channelIdOf(ends.from), channelIdOf(ends.to), before, amount, mode);
	MixerSidechainRoute* route = mixer->createSidechainSend(fromIndex, toIndex, amount, mode);
	if (route == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the mixer refused a sidechain send %1 -> %2: the master cannot send, "
				"or the route would close a cycle made of sidechain sends alone (a sidechain edge "
				"never creates a circular wait, so the mixer refuses that one outright)")
				.arg(channelIdOf(ends.from), channelIdOf(ends.to)));
	}
	Engine::getSong()->setModified();

	QJsonObject result;
	result.insert(QStringLiteral("from"), channelIdOf(ends.from));
	result.insert(QStringLiteral("to"), channelIdOf(ends.to));
	result.insert(QStringLiteral("amount"), static_cast<double>(route->amount()->value()));
	result.insert(QStringLiteral("tap_point"), sidechainTapPointName(route->mode()));
	result.insert(QStringLiteral("deferred"), route->deferred());
	result.insert(QStringLiteral("created"), !before.value(QStringLiteral("existed")).toBool());
	result.insert(QStringLiteral("sidechain"), sidechainRouteJson(*route));

	const bool existed = before.value(QStringLiteral("existed")).toBool();
	QJsonObject inverseArgs;
	inverseArgs.insert(QStringLiteral("channel"), channelIdOf(ends.from));
	inverseArgs.insert(QStringLiteral("to"), channelIdOf(ends.to));
	QJsonObject transaction = transactionPayload(before,
		existed ? QStringLiteral("mixer.sidechain_to")
				: QStringLiteral("control.undo"),  // E19: the recorded step deletes the send
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

//! Remove a sidechain send: re-created by the recorded step with its amount and
//! tap point, so the inverse is exact.
ControlResult removeSidechainSend(Mixer* mixer, const RoutingEnds& ends)
{
	const mix_ch_t fromIndex = ends.from->index();
	const mix_ch_t toIndex = ends.to->index();
	MixerSidechainRoute* route = mixer->channelSidechainSend(fromIndex, toIndex);
	if (route == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no sidechain send from %1 to %2")
				.arg(channelIdOf(ends.from), channelIdOf(ends.to)));
	}
	const QJsonObject before = mixerSidechainBeforeState(route);
	const float amount = static_cast<float>(route->amount()->value());
	const SidechainTapPoint mode = route->mode();
	const bool deferred = route->deferred();
	control::addUndoStep(
		[mixer, fromIndex, toIndex, amount, mode]() {
			mixer->createSidechainSend(fromIndex, toIndex, amount, mode);
		},
		[mixer, fromIndex, toIndex]() { mixer->deleteSidechainSend(fromIndex, toIndex); });
	mixer->deleteSidechainSend(route);
	Engine::getSong()->setModified();

	QJsonObject result;
	result.insert(QStringLiteral("removed"), true);
	result.insert(QStringLiteral("sidechain"), true);
	result.insert(QStringLiteral("from"), channelIdOf(ends.from));
	result.insert(QStringLiteral("to"), channelIdOf(ends.to));
	QJsonObject transaction = transactionPayload(before,
		QStringLiteral("mixer.sidechain_to"),
		QJsonObject{{QStringLiteral("channel"), channelIdOf(ends.from)},
			{QStringLiteral("to"), channelIdOf(ends.to)},
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

//! Remove a regular send: re-created by the recorded step with its amount,
//! pre-fader flag, pan and enabled state.
ControlResult removeRegularSend(Mixer* mixer, const RoutingEnds& ends)
{
	const mix_ch_t fromIndex = ends.from->index();
	const mix_ch_t toIndex = ends.to->index();
	MixerRoute* route = findMixerRoute(fromIndex, toIndex);
	if (route == nullptr)
	{
		return ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no send from %1 to %2 (read %1's sends with pdc.report or "
				"mixer.get_state)").arg(channelIdOf(ends.from), channelIdOf(ends.to)));
	}
	QJsonObject before = mixerRouteBeforeState(route);
	const float amount = static_cast<float>(route->amount()->value());
	const bool preFader = route->preFader();
	const float pan = route->pan()->value();
	const bool enabled = route->enabled();
	before.insert(QStringLiteral("pan"), static_cast<double>(pan));
	before.insert(QStringLiteral("enabled"), enabled);
	const auto panAutomation = automationClipsFor(route->pan());
	const auto panAutomationMode = route->pan()->automationMode();
	control::addUndoStep(
		[mixer, fromIndex, toIndex, amount, preFader, pan, enabled, panAutomation, panAutomationMode]() {
			if (MixerRoute* restored = mixer->createChannelSend(fromIndex, toIndex, amount, preFader))
			{
				restored->pan()->setValue(pan);
				restored->pan()->setAutomationMode(panAutomationMode);
				restored->enabledModel()->setValue(enabled);
				for (const QPointer<AutomationClip>& automation : panAutomation)
				{
					if (automation != nullptr) { automation->addObject(restored->pan(), false); }
				}
			}
		},
		[mixer, fromIndex, toIndex]() { mixer->deleteChannelSend(fromIndex, toIndex); });
	mixer->deleteChannelSend(route);
	Engine::getSong()->setModified();

	QJsonObject result;
	result.insert(QStringLiteral("removed"), true);
	result.insert(QStringLiteral("sidechain"), false);
	result.insert(QStringLiteral("from"), channelIdOf(ends.from));
	result.insert(QStringLiteral("to"), channelIdOf(ends.to));
	QJsonObject transaction = transactionPayload(before,
		QStringLiteral("control.undo"), QJsonObject{},
		true,
		QStringLiteral("ProjectJournal action checkpoint: control.undo or Ctrl+Z restores the captured "
			"send amount, pre-fader flag, pan and enabled state, pan automation mode, and attached "
			"pan automation clips"));
	result.insert(QStringLiteral("__transaction"), transaction);
	return ControlResult::success(result);
}

ControlResult handleRouteRemove(const QJsonObject& args)
{
	ControlResult error;
	RoutingEnds ends;
	if (!resolveRoutingEnds(args, &ends, &error)) { return error; }
	Mixer* mixer = Engine::mixer();
	if (args.value(QStringLiteral("sidechain")).toBool(false))
	{
		return removeSidechainSend(mixer, ends);
	}
	return removeRegularSend(mixer, ends);
}

} // namespace

//! MX-050 (0.5.0 L09): the same send (or routing) from many channels to one, as ONE undo step - each
//! channel's write goes through handleRouteWrite, whose action steps the registry merges.
ControlResult handleSendMany(const QJsonObject& args)
{
	const QJsonArray channels = args.value(QStringLiteral("channels")).toArray();
	if (channels.isEmpty() || channels.size() > 128)
	{
		return ControlResult::failure(ControlErrorKind::InvalidArgs, QStringLiteral("'channels' names 1..128 channels"));
	}
	QVector<QJsonObject> calls;
	for (const QJsonValue& channel : channels)
	{
		QJsonObject one = args;
		one.remove(QStringLiteral("channels"));
		one.remove(QStringLiteral("route"));
		one.insert(QStringLiteral("channel"), channel);
		ControlResult error;
		RoutingEnds ends;
		if (!resolveRoutingEnds(one, &ends, &error)) { return error; }  // every end checked before anything is written
		calls.append(one);
	}
	const bool route = args.value(QStringLiteral("route")).toBool();
	QJsonArray results;
	int made = 0;
	for (const QJsonObject& call : calls)
	{
		const ControlResult done = handleRouteWrite(call, !route);
		QJsonObject row{{QStringLiteral("channel"), call.value(QStringLiteral("channel"))}, {QStringLiteral("ok"), done.ok}};
		if (done.ok) { ++made; } else { row.insert(QStringLiteral("error"), done.errorMessage); }
		results.append(row);
	}
	if (made == 0) { return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("no send could be made")); }
	QJsonObject result{{QStringLiteral("to"), args.value(QStringLiteral("to"))}, {QStringLiteral("made"), made},
		{QStringLiteral("channels"), results}};
	result.insert(QStringLiteral("__transaction"), transactionPayload({{QStringLiteral("channels"), channels}}, QStringLiteral("control.undo"),
		{}, true, QStringLiteral("each send's action step, merged by the registry into one")));
	return ControlResult::success(result);
}

void registerMixerRouteCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mixer.send_many");
		cmd.group = QStringLiteral("mixer");
		cmd.verb = QStringLiteral("send_many");
		cmd.description = QStringLiteral("Send several channels ('channels', ch-<n> ids) to one ('to') at once - an "
			"auxiliary send with 'amount' and 'pre_fader' as mixer.send_to takes them, or with 'route' their routing as "
			"mixer.route_to makes it. Every end is checked first; 'channels' in the reply says which were made. ONE undo step.");
		cmd.argsSchema = objectSchema({{QStringLiteral("channels"), arrayProperty()}, {QStringLiteral("to"), stringProperty()},
			{QStringLiteral("amount"), numberProperty()}, {QStringLiteral("pre_fader"), booleanProperty()},
			{QStringLiteral("route"), booleanProperty()}}, {QStringLiteral("channels"), QStringLiteral("to")});
		cmd.resultSchema = objectProperty();
		cmd.mutating = true;
		cmd.handler = handleSendMany;
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mixer.route_to");
		cmd.group = QStringLiteral("mixer");
		cmd.verb = QStringLiteral("route_to");
		cmd.description = QStringLiteral("Make a channel's output go to another channel's input "
			"(the channel's routing), at unity. Validated against the mixer's own feedback rule "
			"before anything is written; inside the cycle-permitted submode (feedback.enable) a "
			"loop-closing routing is instead accepted and the result says so: `feedback` is true, "
			"`pdc_suspended` is true (compensation is SUSPENDED for that loop) and `warning` "
			"carries REAPER's feedback warning verbatim - otherwise those three answer false, "
			"false and \"\". `from`, `to`, `amount`, `pre_fader` and `created` describe the "
			"routing written and `route` is pdc.report's own element for it. Reversible: one "
			"undo step.");
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
			{QStringLiteral("feedback"), booleanProperty()},
			{QStringLiteral("pdc_suspended"), booleanProperty()},
			{QStringLiteral("warning"), stringProperty()},
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
			"engine's own rule. A loop-closing send is refused unless the cycle-permitted "
			"submode (feedback.enable) is on; when it is accepted there, the result says so: "
			"`feedback` true, `pdc_suspended` true (compensation SUSPENDED for that loop) and "
			"`warning` carrying REAPER's feedback warning verbatim - an ordinary send answers "
			"false, false and \"\". `from`, `to`, `amount`, `pre_fader` and `created` describe "
			"the send written and `route` is pdc.report's element for it. Reversible: one undo "
			"step.");
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
			{QStringLiteral("feedback"), booleanProperty()},
			{QStringLiteral("pdc_suspended"), booleanProperty()},
			{QStringLiteral("warning"), stringProperty()},
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
