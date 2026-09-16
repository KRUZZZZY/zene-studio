/*
 * ControlCommandsHostNotes.cpp - the `plugin.host_notes` command (feature
 *                                 row 79, board task #669)
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * The counters behind this command live in src/core/PluginHostNotes.cpp - a
 * separate translation unit with no engine dependency, because the host tests
 * compile that file next to the host sources. THIS file is the surface half and
 * is part of the core's command registration, exactly as
 * ControlCommandsHostChunking.cpp is for plugin.host_chunking (CODE-4).
 */

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "PluginHostNotes.h"

namespace lmms
{

using namespace control;

namespace control
{

namespace
{

//! The dialect bitfield as names, so a caller does not have to know
//! clap_note_dialect to read a report.
QJsonObject dialectJson(std::uint32_t dialects)
{
	QJsonObject out;
	out.insert(QStringLiteral("clap"), (dialects & 0x1u) != 0);
	out.insert(QStringLiteral("midi"), (dialects & 0x2u) != 0);
	out.insert(QStringLiteral("midi_mpe"), (dialects & 0x4u) != 0);
	out.insert(QStringLiteral("midi2"), (dialects & 0x8u) != 0);
	return out;
}

ControlResult handleHostNotes(const QJsonObject&)
{
	const PluginHostNoteStats stats = clapHostNoteCounters().read();

	// The rule itself, as data: a caller reading the counters below needs to
	// know what the host promises, not only what it has done.
	QJsonObject contract;
	contract.insert(QStringLiteral("note_ports"),
		QStringLiteral("the host reads clap.note-ports while the plug-in is deactivated and "
					   "delivers notes to the plug-in's OWN preferred input port index; a "
					   "plug-in that does not implement the extension is not required to"));
	contract.insert(QStringLiteral("event_form"),
		QStringLiteral("CLAP's own dialect: clap_event_note (note on / off / choke) in the "
					   "plug-in's input event list, with clap_event_note_t::header.time set to "
					   "the frame offset LMMS' MIDI route computed"));
	contract.insert(QStringLiteral("delivery_rule"),
		QStringLiteral("the notes of a request belong to the chunk that starts it, exactly like "
					   "the parameter changes, so an event at frame N of a request affects "
					   "frame N of that chunk and is delivered once, never once per chunk"));
	contract.insert(QStringLiteral("queue_rule"),
		QStringLiteral("the queue between the MIDI route and the audio thread is bounded and "
					   "lock free: a full queue, or a plug-in with no note input port, "
					   "refuses the event and counts it as dropped - it never blocks the "
					   "audio thread and never grows"));
	contract.insert(QStringLiteral("audio_output_rule"),
		QStringLiteral("a CLAP instrument is a generator: the host accepts a plug-in whose "
					   "audio layout has an output and no input port, and the instrument "
					   "module takes its channel counts from the plug-in's own "
					   "clap.audio-ports (a mono generator still gets a stereo bus)"));

	QJsonObject ports;
	ports.insert(QStringLiteral("count"), static_cast<double>(stats.ports));
	ports.insert(QStringLiteral("preferred"), static_cast<double>(stats.preferredPort));
	ports.insert(QStringLiteral("dialects"), dialectJson(stats.dialects));

	QJsonObject audio;
	audio.insert(QStringLiteral("inputs"), static_cast<double>(stats.audioInputs));
	audio.insert(QStringLiteral("outputs"), static_cast<double>(stats.audioOutputs));

	QJsonObject counters;
	counters.insert(QStringLiteral("loads"), static_cast<double>(stats.loads));
	counters.insert(QStringLiteral("pushed"), static_cast<double>(stats.pushed));
	counters.insert(QStringLiteral("dropped"), static_cast<double>(stats.dropped));
	counters.insert(QStringLiteral("delivered"), static_cast<double>(stats.delivered));
	counters.insert(QStringLiteral("played"), static_cast<double>(stats.played));

	QJsonObject result;
	result.insert(QStringLiteral("host"), QStringLiteral("clap"));
	result.insert(QStringLiteral("contract"), contract);
	result.insert(QStringLiteral("ports"), ports);
	result.insert(QStringLiteral("audio"), audio);
	result.insert(QStringLiteral("counters"), counters);
	result.insert(QStringLiteral("note"),
		QStringLiteral("'ports' and 'audio' describe the plug-in that was loaded LAST - note "
					   "ports and audio ports are per-instance facts, and this is the "
					   "process-wide view of them, as plugin.host_chunking's "
					   "'prepared_block' is. The counters are process-lifetime and "
					   "incremented on the audio thread, so this is a snapshot, not a "
					   "transaction: 'delivered' > 0 with 'played' > 0 is the note path "
					   "having carried notes into a plug-in - which is the property this "
					   "command exists to make observable. Read-only; SPEC A16 row "
					   "not_mutating."));
	return ControlResult::success(result);
}

} // namespace
} // namespace control

void registerPluginHostNotesCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.host_notes");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("host_notes");
	cmd.description = QStringLiteral("The note path the CLAP host keeps, and what this instance "
		"has actually carried: the note input ports (clap.note-ports) and the audio layout of "
		"the CLAP plug-in loaded last, plus the note events the MIDI route pushed, the ones the "
		"bounded queue (or a missing note port) refused, the ones put into a plug-in's input "
		"event list and the note-ONs among them. A plug-in with no note input port refuses "
		"every note and counts it here rather than hiding it. Read-only.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("host"), stringProperty()},
		{QStringLiteral("contract"), objectProperty()},
		{QStringLiteral("ports"), objectProperty()},
		{QStringLiteral("audio"), objectProperty()},
		{QStringLiteral("counters"), objectProperty()},
		{QStringLiteral("note"), stringProperty()}});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return handleHostNotes(args); };
	registry.registerCommand(cmd);
}

} // namespace lmms
