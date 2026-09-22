/*
 * ControlCommandsFeedback.cpp - the `feedback.*` command group (SPEC A11-A16):
 *                               the cycle-permitted signal-graph submode
 *                               (board card #709).
 *
 * THE ITEM THIS CLOSES. The mixer has refused loop-closing sends since the
 * routing work (Mixer::checkInfiniteLoop, asked FIRST by
 * resolveRoutingEnds so the refusal is the engine's own judgement) because a
 * loop deadlocks the dependency count - the same reason SPEC-dynamic-routing
 * line 120 excludes sidechain sends from dependency counting. Card #709 makes
 * the acyclicity rule OPTIONAL: an explicit submode where a defined subgraph
 * may be cyclic and PDC is suspended FOR THAT SUBGRAPH. The engine half is
 * Mixer::feedbackMode / MixerRoute::setFeedback (src/core/Mixer.cpp); this
 * file is the only way to reach it - MixerView still hides the loop-closing
 * arrows, so the interface cannot create a feedback send (the UI-absence line
 * lives in docs/KNOWN-LIMITATIONS.md).
 *
 * WHY THE WARNING IS IN THE RESULT, NOT ONLY IN THE DOCS. PDC is out of scope
 * for the routing spec (docs/specs/SPEC-dynamic-routing.md line 39), so this
 * submode must make compensation-off UNMISTAKABLE at the point of use:
 * feedback.enable says it, feedback.get_state says it, and the mixer.send_to
 * / mixer.route_to result that WRITES a loop-closing send says it - each
 * carrying REAPER's own warning, "feedback routing can in some instances be
 * useful, but can risk damaging audio equipment" (REAPER User Guide, main
 * changes 6.66-6.70).
 *
 * A16, honestly:
 *   feedback.get_state  not_mutating  - reads the flag and the flagged sends
 *   feedback.enable     true_inverse  - recorded action: undo turns the mode
 *                                       off, and enabling creates nothing, so
 *                                       at its own undo there is no flagged
 *                                       send to lose (sends made afterwards
 *                                       carry their OWN later steps, undone
 *                                       first - the stack is LIFO)
 *   feedback.disable    true_inverse  - recorded action: undo re-enables the
 *                                       mode and re-creates every send it
 *                                       removed with its amount and pre-fader
 *                                       flag, so nothing is silently lost
 *
 * The id is `feedback.*`: `routing.*`, `mixer.*` and `pdc.*` are taken and
 * none of them may grow a writer (routing.get_state's design decision, the
 * mixer verbs' contract).
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
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

#include "ControlEdit.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"

namespace lmms
{

using namespace control; // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! REAPER's warning, verbatim - carried by every point of use this group has.
const QString REAPER_WARNING = QStringLiteral(
	"feedback routing can in some instances be useful, but can risk damaging audio equipment "
	"(REAPER User Guide, main changes 6.66-6.70)");

//! What "suspended" means, in the one sentence every result repeats: the
//! loop's sends are uncompensated (0 frames, the alignment solve does not
//! traverse them) and the rest of the graph keeps its normal PDC.
const QString SUSPENSION = QStringLiteral(
	"compensation is SUSPENDED for cycle-permitted sends: each one is fixed at 0 frames and the "
	"alignment solve does not traverse it, because a loop's latency cannot be compensated - the "
	"rest of the graph keeps its normal PDC, unchanged");

const QString ENABLE_NOTE = QStringLiteral(
	"leaving the submode with feedback.disable removes every cycle-permitted send, restores "
	"normal PDC and makes mixer.send_to / mixer.route_to refuse the loop again");

const QString DISABLE_NOTE = QStringLiteral(
	"normal PDC restored: the cycle rule is back in force and a loop-closing send is refused "
	"again (the engine's own checkInfiniteLoop, unchanged)");

//! One loop-closing send, reduced to its whole state: the undo of
//! feedback.disable re-creates exactly these four values per send.
struct FeedbackEdge
{
	mix_ch_t from;
	mix_ch_t to;
	float amount;
	bool preFader;
};

//! Every send the submode is holding open, in the mixer's own route order.
QVector<FeedbackEdge> feedbackEdges(Mixer* mixer)
{
	QVector<FeedbackEdge> edges;
	for (MixerRoute* route : mixer->m_mixerRoutes)
	{
		if (route->feedback())
		{
			edges.append({route->senderIndex(), route->receiverIndex(),
				route->amount()->value(), route->preFader()});
		}
	}
	return edges;
}

//! The loop-closing sends as the JSON every result lists them in: endpoints,
//! amount, pre-fader flag, and the compensation they run at (0 frames - the
//! suspension is a measured fact here, not an adjective). Endpoints are the
//! STABLE channel ids (channelIdOf, the project-scoped counter), never the
//! index - a cached ch-7 still names its channel after a sibling moves.
QJsonArray edgeArray(Mixer* mixer, const QVector<FeedbackEdge>& edges)
{
	QJsonArray listed;
	for (const FeedbackEdge& edge : edges)
	{
		listed.append(QJsonObject{
			{QStringLiteral("from"), channelIdOf(mixer->mixerChannel(edge.from))},
			{QStringLiteral("to"), channelIdOf(mixer->mixerChannel(edge.to))},
			{QStringLiteral("amount"), static_cast<double>(edge.amount)},
			{QStringLiteral("pre_fader"), edge.preFader},
			{QStringLiteral("compensation_frames"), 0}});
	}
	return listed;
}

//! The state every result here answers with, so "what I asked" and "what the
//! engine is doing" cannot be two different stories (the clock group's rule).
QJsonObject feedbackState(Mixer* mixer, const QVector<FeedbackEdge>& edges)
{
	QJsonObject state;
	state.insert(QStringLiteral("enabled"), mixer->feedbackMode());
	state.insert(QStringLiteral("pdc_suspended"), mixer->feedbackMode());
	state.insert(QStringLiteral("feedback_routes"), edgeArray(mixer, edges));
	state.insert(QStringLiteral("suspension"), SUSPENSION);
	state.insert(QStringLiteral("warning"), REAPER_WARNING);
	state.insert(QStringLiteral("note"), mixer->feedbackMode() ? ENABLE_NOTE : DISABLE_NOTE);
	return state;
}

//! feedback.get_state: the flag, the sends it holds open, the suspension and
//! the warning. Writes nothing.
QJsonObject getState(Mixer* mixer)
{
	return feedbackState(mixer, feedbackEdges(mixer));
}

//! Re-create the sends feedback.disable removed - the recorded undo of that
//! command, run as ONE step: the mode first (so the recreated sends are
//! permitted and flagged), then each send with its captured amount and
//! pre-fader flag.
void restoreEdges(Mixer* mixer, const QVector<FeedbackEdge>& edges)
{
	mixer->setFeedbackMode(true);
	for (const FeedbackEdge& edge : edges)
	{
		mixer->createChannelSend(edge.from, edge.to, edge.amount, edge.preFader);
		if (MixerRoute* route = findMixerRoute(edge.from, edge.to))
		{
			route->setFeedback(true);
		}
	}
}

//! feedback.enable: enter the submode. Entering creates nothing and changes
//! no audio - it only permits the next loop-closing send - so the recorded
//! undo is exactly "turn it off again".
ControlResult handleEnable(Mixer* mixer)
{
	QJsonObject result;
	result.insert(QStringLiteral("enabled"), true);
	result.insert(QStringLiteral("already_enabled"), mixer->feedbackMode());
	result.insert(QStringLiteral("pdc_suspended"), true);
	result.insert(QStringLiteral("suspension"), SUSPENSION);
	result.insert(QStringLiteral("warning"), REAPER_WARNING);
	result.insert(QStringLiteral("note"), ENABLE_NOTE);
	if (mixer->feedbackMode())
	{
		// No-op: nothing mutated, so nothing to undo and no transaction to
		// record - the state is already the one asked for.
		return ControlResult::success(result);
	}
	control::addUndoStep(
		[mixer]() { mixer->setFeedbackMode(false); },
		[mixer]() { mixer->setFeedbackMode(true); });
	mixer->setFeedbackMode(true);
	result.insert(QStringLiteral("__transaction"), transactionPayload(
		QJsonObject{{QStringLiteral("enabled"), false}}, QStringLiteral("feedback.disable"), {},
		true,
		QStringLiteral("action checkpoint: the recorded undo step calls "
			"Mixer::setFeedbackMode(false) - enabling creates nothing, so at its own undo no "
			"cycle-permitted send exists yet; one created afterwards carries its own later step, "
			"which the LIFO stack undoes first")));
	return ControlResult::success(result);
}

//! feedback.disable: leave the submode. This is the command that RESTORES
//! normal PDC - it removes every loop-closing send, and the recorded undo
//! re-enables the mode and re-creates them, so nothing is silently lost.
ControlResult handleDisable(Mixer* mixer)
{
	const QVector<FeedbackEdge> edges = feedbackEdges(mixer);
	QJsonObject result;
	result.insert(QStringLiteral("enabled"), false);
	result.insert(QStringLiteral("removed"), edges.size());
	result.insert(QStringLiteral("feedback_routes"), edgeArray(mixer, edges));
	result.insert(QStringLiteral("pdc_suspended"), false);
	result.insert(QStringLiteral("note"), DISABLE_NOTE);
	if (!mixer->feedbackMode())
	{
		// No-op: the submode was already off and there is nothing to remove.
		return ControlResult::success(result);
	}
	control::addUndoStep(
		[mixer, edges]() { restoreEdges(mixer, edges); },
		[mixer]() { mixer->setFeedbackMode(false); });
	mixer->setFeedbackMode(false);
	QJsonObject before;
	before.insert(QStringLiteral("enabled"), true);
	before.insert(QStringLiteral("removed"), edges.size());
	result.insert(QStringLiteral("__transaction"), transactionPayload(
		before, QStringLiteral("feedback.enable"), {}, true,
		QStringLiteral("action checkpoint: the recorded undo step turns the submode back on and "
			"re-creates every send this command removed through the same "
			"Mixer::createChannelSend call with the captured amount and pre-fader flag (a send's "
			"whole state), so ONE control.undo restores both the mode and the loop")));
	return ControlResult::success(result);
}

} // namespace

void registerFeedbackCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("feedback.get_state");
		cmd.group = QStringLiteral("feedback");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("The cycle-permitted signal-graph submode's live state: "
			"`enabled` (whether a loop-closing send would be accepted), `pdc_suspended` (true "
			"exactly while enabled - compensation for the loop is off), `feedback_routes` (every "
			"loop-closing send with its endpoints, amount, pre-fader flag and its compensation, "
			"0 while suspended), `suspension` (what is suspended and what is not - the rest of "
			"the graph keeps its normal PDC), `warning` (REAPER's own feedback warning, "
			"verbatim) and `note` (how to leave, and what leaving restores). Read-only: this "
			"command writes nothing.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("pdc_suspended"), booleanProperty()},
			{QStringLiteral("feedback_routes"), arrayProperty()},
			{QStringLiteral("suspension"), stringProperty()},
			{QStringLiteral("warning"), stringProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.mutating = false;
		cmd.handler = [](const QJsonObject&) {
			Mixer* mixer = Engine::mixer();
			if (mixer == nullptr)
			{
				return ControlResult::failure(ControlErrorKind::NotFound,
					QStringLiteral("the engine has no mixer"));
			}
			return ControlResult::success(getState(mixer));
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("feedback.enable");
		cmd.group = QStringLiteral("feedback");
		cmd.verb = QStringLiteral("enable");
		cmd.description = QStringLiteral("Enter the cycle-permitted submode: the next "
			"mixer.send_to / mixer.route_to that would close a loop (bus -> effect -> same bus) "
			"is ACCEPTED instead of refused, and is flagged as a cycle-permitted send. `enabled` "
			"is the new flag, `already_enabled` says the call changed nothing (then no undo step "
			"is recorded), `pdc_suspended` is true because compensation is SUSPENDED for every "
			"cycle-permitted send (stated again in `suspension`: their compensation is fixed at "
			"0 frames and the alignment solve does not traverse them, the rest of the graph "
			"unchanged), `warning` is REAPER's feedback warning verbatim and `note` says how to "
			"leave and what leaving restores. Creates nothing by itself. Reversible: one "
			"control.undo turns the mode off again.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("already_enabled"), booleanProperty()},
			{QStringLiteral("pdc_suspended"), booleanProperty()},
			{QStringLiteral("suspension"), stringProperty()},
			{QStringLiteral("warning"), stringProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject&) {
			Mixer* mixer = Engine::mixer();
			if (mixer == nullptr)
			{
				return ControlResult::failure(ControlErrorKind::NotFound,
					QStringLiteral("the engine has no mixer"));
			}
			return handleEnable(mixer);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("feedback.disable");
		cmd.group = QStringLiteral("feedback");
		cmd.verb = QStringLiteral("disable");
		cmd.description = QStringLiteral("Leave the cycle-permitted submode: this RESTORES "
			"normal PDC - every cycle-permitted send is removed, the cycle rule is back in force "
			"and the next mixer.send_to / mixer.route_to that would close a loop is refused "
			"again. `enabled` is the new flag, `removed` counts the sends taken out, "
			"`feedback_routes` lists each one with its endpoints, amount and pre-fader flag "
			"(exactly what comes back), `pdc_suspended` is false because compensation is normal "
			"again, and `note` says so. A no-op (already off) records no undo step. Reversible: "
			"ONE control.undo re-enables the mode and re-creates every removed send with its "
			"captured amount and pre-fader flag.");
		cmd.argsSchema = objectSchema({});
		cmd.resultSchema = objectSchema({
			{QStringLiteral("enabled"), booleanProperty()},
			{QStringLiteral("removed"), integerProperty()},
			{QStringLiteral("feedback_routes"), arrayProperty()},
			{QStringLiteral("pdc_suspended"), booleanProperty()},
			{QStringLiteral("note"), stringProperty()},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject&) {
			Mixer* mixer = Engine::mixer();
			if (mixer == nullptr)
			{
				return ControlResult::failure(ControlErrorKind::NotFound,
					QStringLiteral("the engine has no mixer"));
			}
			return handleDisable(mixer);
		};
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
