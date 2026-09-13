/*
 * ControlCommandsRack.cpp - the rack.* command group (SPEC A11-A16): the rack
 *                            object itself, its chain selector and its zones.
 *
 * The rack ENGINE landed with #599 (include/Rack.h, src/core/Rack.cpp, the
 * <rack> element inside a <mixerchannel>, RackTest). What was missing was any
 * way to reach it: the lane's own report (docs/RACKS.md section 5) says the only
 * way to have a rack was "a project file that already contains a <rack>
 * element". This group is that way - plus the two halves the engine slice
 * deliberately left out, macros (ControlCommandsRackMacros.cpp) and key and
 * velocity zones (ControlCommandsRackZones.cpp), each in its own translation
 * unit for the same reason the automation and warp groups are split.
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

#include <QDomDocument>
#include <QDomElement>
#include <QJsonArray>
#include <QJsonObject>

#include "ControlDeviceSupport.h" // ControlSnapshotLimit, the SPEC A16 snapshot bound
#include "ControlEdit.h"
#include "ControlRackSupport.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "EffectChain.h"
#include "Engine.h"
#include "Mixer.h"
#include "Rack.h"
#include "RackMacros.h"
#include "RackZones.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! The chain index in a handler's args, as an int (0 when absent - every
//! command that takes one declares it required, so absence is caught by the
//! registry's schema check, not here).
int chainArg(const QJsonObject& args)
{
	return static_cast<int>(args.value(QStringLiteral("chain")).toDouble());
}

/*! The chain's own state as XML, or empty when it is larger than the bounded
 * snapshot size (SPEC A16's fallback is a *bounded* record, so an oversized one
 * is reported by size rather than truncated into a corrupt snapshot).
 *
 * `EffectChain::saveState` is the same call the project file's `<fxchain>` is
 * written by, so this captures the effects, their order and their settings
 * without a second serialiser to drift from it.
 */
QString captureChainXml(EffectChain& chain)
{
	QDomDocument doc;
	QDomElement element = doc.createElement(QStringLiteral("chainstate"));
	doc.appendChild(element);
	chain.saveState(doc, element);
	const QString xml = doc.toString();
	if (xml.size() > ControlSnapshotLimit) { return QString(); }
	return xml;
}

//! The action step that puts the selector back, as ONE undoable step: undo
//! restores the previous selection, redo re-applies the new one.
void addSelectorUndoStep(const QString& channelIdText, int previous, int next)
{
	addUndoStep(
		[channelIdText, previous]() {
			ControlResult ignored;
			Rack* rack = resolveRack(channelIdText, &ignored);
			if (rack != nullptr) { rack->setSelectedChain(previous); }
		},
		[channelIdText, next]() {
			ControlResult ignored;
			Rack* rack = resolveRack(channelIdText, &ignored);
			if (rack != nullptr) { rack->setSelectedChain(next); }
		});
}

void registerRackGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.get_state");
	cmd.group = QStringLiteral("rack");
	cmd.verb = QStringLiteral("get_state");
	cmd.description = QStringLiteral("One mixer channel's rack: every chain with its device count, "
		"the chain selector, and every macro and key/velocity zone with its targets. 'channel' is "
		"a ch-<n> id.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
	}, {QStringLiteral("channel")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("chain_count"), integerProperty()},
		{QStringLiteral("selected"), integerProperty()},
		{QStringLiteral("routed"), arrayProperty()},
		{QStringLiteral("chains"), arrayProperty()},
		{QStringLiteral("macros"), arrayProperty()},
		{QStringLiteral("macro_count"), integerProperty()},
		{QStringLiteral("zones"), arrayProperty()},
		{QStringLiteral("zone_count"), integerProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(),
			&error);
		if (channel == nullptr) { return error; }
		return ControlResult::success(rackState(channel, channel->m_rack));
	};
	registry.registerCommand(cmd);
}

void registerRackAddChain(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.add_chain");
	cmd.group = QStringLiteral("rack");
	cmd.verb = QStringLiteral("add_chain");
	cmd.description = QStringLiteral("Append an empty parallel chain to a channel's rack and "
		"return its index (chain-<n>). Reversible through the ProjectJournal (the recorded undo "
		"step removes the chain this command created).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
	}, {QStringLiteral("channel")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("chain"), integerProperty()},
		{QStringLiteral("chain_count"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(),
			&error);
		if (channel == nullptr) { return error; }
		Rack& rack = channel->m_rack;
		const int before = rack.chainCount();
		const QString channelIdText = channelId(channel->index());

		// A created chain has no before-state to restore, so the inverse is the
		// OPERATION (SPEC A16 deliverable 5) - one action step, through the same
		// Rack::removeChain the remove command uses. A fresh chain carries no
		// effects, so removing it restores the rack exactly.
		const int mark = before;
		addUndoStep(
			[channelIdText, mark]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current == nullptr) { return; }
				if (current->chainCount() <= mark) { return; }
				current->removeChain(current->chainCount() - 1);
			},
			[channelIdText]() {
				ControlResult ignored;
				Rack* current = resolveRack(channelIdText, &ignored);
				if (current != nullptr) { current->addChain(); }
			});

		const int index = rack.addChain();

		QJsonObject result;
		result.insert(QStringLiteral("channel"), channelIdText);
		result.insert(QStringLiteral("chain"), index);
		result.insert(QStringLiteral("chain_count"), rack.chainCount());
		QJsonObject beforeState;
		beforeState.insert(QStringLiteral("chain_count"), before);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(beforeState, QStringLiteral("rack.remove_chain"),
				QJsonObject{{QStringLiteral("channel"), channelIdText},
					{QStringLiteral("chain"), index}},
				true,
				QStringLiteral("action checkpoint: the recorded undo step removes the chain "
					"this command created, through the same Rack::removeChain "
					"rack.remove_chain uses; a fresh chain carries no effects, so removing "
					"it restores the rack exactly")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerRackRemoveChain(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.remove_chain");
	cmd.group = QStringLiteral("rack");
	cmd.verb = QStringLiteral("remove_chain");
	cmd.description = QStringLiteral("Drop a parallel chain from a channel's rack. Chain 0 is the "
		"channel's own effect chain and is refused. The removed chain's effects and their state "
		"are captured in the transaction's before-state.");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("chain"), integerProperty()},
	}, {QStringLiteral("channel"), QStringLiteral("chain")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("removed"), integerProperty()},
		{QStringLiteral("chain_count"), integerProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(),
			&error);
		if (channel == nullptr) { return error; }
		Rack& rack = channel->m_rack;
		const int index = chainArg(args);
		if (index == 0)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("chain 0 is the channel's own effect chain: it is removed with "
					"the channel, not from the rack"));
		}
		EffectChain* chain = rack.chain(index);
		if (chain == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("the rack has no chain %1 (it has %2)")
					.arg(index)
					.arg(rack.chainCount()));
		}

		const int before = rack.chainCount();
		const QString stateXml = captureChainXml(*chain);
		const QString channelIdText = channelId(channel->index());
		rack.removeChain(index);

		QJsonObject beforeState;
		beforeState.insert(QStringLiteral("chain"), index);
		beforeState.insert(QStringLiteral("chain_count"), before);
		if (!stateXml.isEmpty()) { beforeState.insert(QStringLiteral("state_xml"), stateXml); }
		else { beforeState.insert(QStringLiteral("state_oversized"), true); }

		QJsonObject result;
		result.insert(QStringLiteral("channel"), channelIdText);
		result.insert(QStringLiteral("removed"), index);
		result.insert(QStringLiteral("chain_count"), rack.chainCount());
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(beforeState,
				QStringLiteral("UNIMPLEMENTED: re-create the chain with its effects"),
				QJsonObject{{QStringLiteral("channel"), channelIdText},
					{QStringLiteral("chain"), index}},
				false,
				stateXml.isEmpty()
					? QStringLiteral("snapshot only: the chain's state was larger than the "
						"bounded snapshot, so only its size is recorded and no automatic "
						"replay exists")
					: QStringLiteral("snapshot only: before-state holds the removed chain's "
						"state XML (bounded), and there is no command that recreates a chain "
						"WITH its effects")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerRackSetSelected(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("rack.set_selected");
	cmd.group = QStringLiteral("rack");
	cmd.verb = QStringLiteral("set_selected");
	cmd.description = QStringLiteral("Choose the chain a channel's rack routes to: a chain index, "
		"or -1 for parallel (every chain runs and the outputs sum). Reversible through the "
		"ProjectJournal (an action checkpoint restores the previous selection).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("chain"), integerProperty()},
	}, {QStringLiteral("channel"), QStringLiteral("chain")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("channel"), stringProperty()},
		{QStringLiteral("selected"), integerProperty()},
		{QStringLiteral("previous"), integerProperty()},
		{QStringLiteral("routed"), arrayProperty()},
	});
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		ControlResult error;
		MixerChannel* channel = resolveRackChannel(args.value(QStringLiteral("channel")).toString(),
			&error);
		if (channel == nullptr) { return error; }
		Rack& rack = channel->m_rack;
		const int index = chainArg(args);
		// The engine leaves a selection it cannot route UNWIRED rather than
		// guessing (Rack::setSelectedChain); the surface refuses it instead, so
		// a caller cannot ask for a chain nobody has and hear "ok".
		if (index != Rack::Parallel && rack.chain(index) == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'chain' %1 is neither -1 (parallel) nor a chain of this rack "
					"(0..%2)").arg(index).arg(rack.chainCount() - 1));
		}

		const int previous = rack.selectedChain();
		const QString channelIdText = channelId(channel->index());
		addSelectorUndoStep(channelIdText, previous, index);
		rack.setSelectedChain(index);

		QJsonArray routed;
		for (const int routedIndex : rack.routedChains()) { routed.append(routedIndex); }
		QJsonObject result;
		result.insert(QStringLiteral("channel"), channelIdText);
		result.insert(QStringLiteral("selected"), rack.selectedChain());
		result.insert(QStringLiteral("previous"), previous);
		result.insert(QStringLiteral("routed"), routed);
		QJsonObject beforeState;
		beforeState.insert(QStringLiteral("selected"), previous);
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(beforeState, QStringLiteral("rack.set_selected"),
				QJsonObject{{QStringLiteral("channel"), channelIdText},
					{QStringLiteral("chain"), previous}},
				true,
				QStringLiteral("action checkpoint: the recorded undo step restores the "
					"previous selection on the rack (the selection is a scalar the rack owns, "
					"not a JournallingObject, so the inverse is the operation)")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

// ---------------------------------------------------------------------------
// The group
// ---------------------------------------------------------------------------

void registerRackCommands(ControlRegistry& registry)
{
	registerRackGetState(registry);
	registerRackAddChain(registry);
	registerRackRemoveChain(registry);
	registerRackSetSelected(registry);
	// The two halves the engine slice left out, each in its own translation
	// unit (the automation and warp groups' split).
	registerRackMacroCommands(registry);
	registerRackZoneCommands(registry);
}

} // namespace lmms
