/*
 * ControlCommandsMixer.cpp - the mixer.* command group (SPEC A11-A16).
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

#include "ControlRegistry.h"
#include "Engine.h"
#include "Mixer.h"

namespace lmms
{

namespace
{

QJsonObject schemaObject(QJsonObject properties, QJsonArray required = {})
{
	QJsonObject schema;
	schema.insert(QStringLiteral("type"), QStringLiteral("object"));
	schema.insert(QStringLiteral("properties"), std::move(properties));
	schema.insert(QStringLiteral("required"), std::move(required));
	schema.insert(QStringLiteral("additionalProperties"), false);
	return schema;
}

QJsonObject channelSchema()
{
	return QJsonObject{{QStringLiteral("type"), QStringLiteral("string")}};
}

//! Resolve a "ch-<n>" id against the live mixer.
MixerChannel* resolveChannel(const QString& id, ControlResult* error)
{
	const int index = control::idToIndex(id, QStringLiteral("ch-"));
	if (index < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a channel id of the form ch-<n>").arg(id));
		return nullptr;
	}
	Mixer* mixer = Engine::mixer();
	if (index >= static_cast<int>(mixer->numChannels()))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no mixer channel %1 (the mixer has %2)").arg(id).arg(static_cast<int>(mixer->numChannels())));
		return nullptr;
	}
	return mixer->mixerChannel(index);
}

QJsonObject channelState(MixerChannel* channel)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("id"), control::channelId(channel->index()));
	entry.insert(QStringLiteral("index"), channel->index());
	entry.insert(QStringLiteral("name"), channel->m_name);
	entry.insert(QStringLiteral("volume"), static_cast<double>(channel->m_volumeModel.value()));
	entry.insert(QStringLiteral("muted"), channel->m_muteModel.value());
	entry.insert(QStringLiteral("soloed"), channel->m_soloModel.value());
	entry.insert(QStringLiteral("is_master"), channel->isMaster());
	entry.insert(QStringLiteral("is_bus"), channel->isBus());
	// This tree has no pan property on a mixer channel; the field is emitted as
	// null so a caller can see that rather than guess (mixer.set_pan refuses).
	entry.insert(QStringLiteral("pan"), QJsonValue::Null);

	QJsonArray sends;
	for (MixerRoute* route : channel->m_sends)
	{
		QJsonObject send;
		send.insert(QStringLiteral("to"), control::channelId(route->receiverIndex()));
		send.insert(QStringLiteral("amount"), static_cast<double>(route->amount()->value()));
		send.insert(QStringLiteral("pre_fader"), route->preFader());
		sends.append(send);
	}
	entry.insert(QStringLiteral("sends"), sends);
	return entry;
}

} // namespace

void registerMixerCommands(ControlRegistry& registry)
{
	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mixer.get_state");
		cmd.group = QStringLiteral("mixer");
		cmd.verb = QStringLiteral("get_state");
		cmd.description = QStringLiteral("Every mixer channel with its stable ch-<n> id, gain and routing.");
		cmd.argsSchema = schemaObject({});
		cmd.resultSchema = schemaObject({
			{QStringLiteral("channels"), QJsonObject{{QStringLiteral("type"), QStringLiteral("array")}}},
			{QStringLiteral("count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.handler = [](const QJsonObject&) {
			Mixer* mixer = Engine::mixer();
			QJsonArray channels;
			for (int i = 0; i < static_cast<int>(mixer->numChannels()); ++i)
			{
				channels.append(channelState(mixer->mixerChannel(i)));
			}
			QJsonObject result;
			result.insert(QStringLiteral("channels"), channels);
			result.insert(QStringLiteral("count"), channels.size());
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mixer.set_volume");
		cmd.group = QStringLiteral("mixer");
		cmd.verb = QStringLiteral("set_volume");
		cmd.description = QStringLiteral("Set a channel fader (0..2). Reversible through the ProjectJournal.");
		cmd.argsSchema = schemaObject(
			{{QStringLiteral("channel"), channelSchema()},
				{QStringLiteral("volume"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
					{QStringLiteral("minimum"), 0.0}, {QStringLiteral("maximum"), 2.0}}}},
			{QStringLiteral("channel"), QStringLiteral("volume")});
		cmd.resultSchema = schemaObject({
			{QStringLiteral("channel"), channelSchema()},
			{QStringLiteral("volume"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")}}},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) {
			ControlResult error;
			MixerChannel* channel = resolveChannel(args.value(QStringLiteral("channel")).toString(), &error);
			if (channel == nullptr) { return error; }

			const float previous = channel->m_volumeModel.value();
			const float volume = static_cast<float>(args.value(QStringLiteral("volume")).toDouble());
			// The channel's own FloatModel is a JournallingObject, so a checkpoint
			// here is a real inverse for the engine's undo stack (SPEC A16).
			channel->m_volumeModel.addJournalCheckPoint();
			channel->m_volumeModel.setValue(volume);

			QJsonObject result;
			result.insert(QStringLiteral("channel"), control::channelId(channel->index()));
			result.insert(QStringLiteral("volume"), static_cast<double>(channel->m_volumeModel.value()));
			QJsonObject transaction;
			transaction.insert(QStringLiteral("before"),
				QJsonObject{{QStringLiteral("channel"), control::channelId(channel->index())},
					{QStringLiteral("volume"), static_cast<double>(previous)}});
			transaction.insert(QStringLiteral("inverse"),
				QJsonObject{{QStringLiteral("op"), QStringLiteral("mixer.set_volume")},
					{QStringLiteral("args"),
						QJsonObject{{QStringLiteral("channel"), control::channelId(channel->index())},
							{QStringLiteral("volume"), static_cast<double>(previous)}}}});
			transaction.insert(QStringLiteral("reversible"), true);
			transaction.insert(QStringLiteral("mechanism"),
				QStringLiteral("ProjectJournal (MixerChannel volume model checkpoint)"));
			result.insert(QStringLiteral("__transaction"), transaction);
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mixer.set_pan");
		cmd.group = QStringLiteral("mixer");
		cmd.verb = QStringLiteral("set_pan");
		cmd.description = QStringLiteral("Set a channel pan. Refused: this tree has no pan on a mixer channel.");
		cmd.argsSchema = schemaObject(
			{{QStringLiteral("channel"), channelSchema()},
				{QStringLiteral("pan"), QJsonObject{{QStringLiteral("type"), QStringLiteral("number")},
					{QStringLiteral("minimum"), -1.0}, {QStringLiteral("maximum"), 1.0}}}},
			{QStringLiteral("channel"), QStringLiteral("pan")});
		cmd.resultSchema = schemaObject({});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) {
			// Honest refusal, not a fake success: lmms::MixerChannel carries no pan
			// control in this tree (only InstrumentTrack/SampleTrack panningModel
			// and per-note panning exist). Inventing one would change the mixer's
			// serialization format.
			ControlResult error;
			MixerChannel* channel = resolveChannel(args.value(QStringLiteral("channel")).toString(), &error);
			if (channel == nullptr) { return error; }
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("mixer channels have no pan property in this build: ") +
				QStringLiteral("pan lives on InstrumentTrack/SampleTrack (panningModel) and on notes"));
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mixer.add_channel");
		cmd.group = QStringLiteral("mixer");
		cmd.verb = QStringLiteral("add_channel");
		cmd.description = QStringLiteral("Append a mixer channel and return its new ch-<n> id.");
		cmd.argsSchema = schemaObject({});
		cmd.resultSchema = schemaObject({
			{QStringLiteral("channel"), channelSchema()},
			{QStringLiteral("index"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject&) {
			Mixer* mixer = Engine::mixer();
			const int before = static_cast<int>(mixer->numChannels());
			const int index = mixer->createChannel();

			QJsonObject result;
			result.insert(QStringLiteral("channel"), control::channelId(index));
			result.insert(QStringLiteral("index"), index);
			QJsonObject transaction;
			transaction.insert(QStringLiteral("before"), QJsonObject{{QStringLiteral("count"), before}});
			transaction.insert(QStringLiteral("inverse"),
				QJsonObject{{QStringLiteral("op"), QStringLiteral("mixer.remove_channel")},
					{QStringLiteral("args"), QJsonObject{{QStringLiteral("channel"), control::channelId(index)}}}});
			// No journal checkpoint exists for a structural mixer change; the
			// inverse is described but cannot be replayed through ProjectJournal.
			transaction.insert(QStringLiteral("reversible"), false);
			transaction.insert(QStringLiteral("mechanism"),
				QStringLiteral("snapshot only: ProjectJournal has no checkpoint for mixer channel creation"));
			result.insert(QStringLiteral("__transaction"), transaction);
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}

	{
		ControlCommand cmd;
		cmd.id = QStringLiteral("mixer.remove_channel");
		cmd.group = QStringLiteral("mixer");
		cmd.verb = QStringLiteral("remove_channel");
		cmd.description = QStringLiteral("Delete a channel (master ch-0 is refused).");
		cmd.argsSchema = schemaObject(
			{{QStringLiteral("channel"), channelSchema()}}, {QStringLiteral("channel")});
		cmd.resultSchema = schemaObject({
			{QStringLiteral("removed"), channelSchema()},
			{QStringLiteral("count"), QJsonObject{{QStringLiteral("type"), QStringLiteral("integer")}}},
		});
		cmd.mutating = true;
		cmd.handler = [](const QJsonObject& args) {
			ControlResult error;
			MixerChannel* channel = resolveChannel(args.value(QStringLiteral("channel")).toString(), &error);
			if (channel == nullptr) { return error; }
			const int index = channel->index();
			if (index == 0)
			{
				return ControlResult::failure(ControlErrorKind::Refused,
					QStringLiteral("the master channel ch-0 cannot be removed"));
			}

			Mixer* mixer = Engine::mixer();
			const int before = static_cast<int>(mixer->numChannels());
			QJsonObject beforeState = channelState(channel);
			mixer->deleteChannel(index);

			QJsonObject result;
			result.insert(QStringLiteral("removed"), control::channelId(index));
			result.insert(QStringLiteral("count"), static_cast<int>(mixer->numChannels()));
			QJsonObject transaction;
			transaction.insert(QStringLiteral("before"), beforeState);
			transaction.insert(QStringLiteral("inverse"),
				QJsonObject{{QStringLiteral("op"), QStringLiteral("UNIMPLEMENTED: restore a deleted mixer channel")},
					{QStringLiteral("count_before"), before}});
			transaction.insert(QStringLiteral("reversible"), false);
			transaction.insert(QStringLiteral("mechanism"),
				QStringLiteral("snapshot only: a deleted MixerChannel cannot be recreated by ProjectJournal"));
			result.insert(QStringLiteral("__transaction"), transaction);
			return ControlResult::success(result);
		};
		registry.registerCommand(cmd);
	}
}

} // namespace lmms
