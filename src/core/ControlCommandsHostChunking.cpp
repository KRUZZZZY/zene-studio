/*
 * ControlCommandsHostChunking.cpp - the `plugin.host_chunking` command (CODE-4)
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
 * The counters behind this command live in src/core/PluginHostChunking.cpp -
 * a separate translation unit with no engine dependency, because the host tests
 * compile that file next to the host sources. THIS file is the surface half and
 * is part of the core's command registration.
 */

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "PluginHostChunking.h"

namespace lmms
{

using namespace control;

namespace control
{

namespace
{

QJsonObject statsJson(const PluginHostChunkingCounters& counters, const QString& host)
{
	const PluginHostChunkingStats stats = counters.read();
	QJsonObject out;
	out.insert(QStringLiteral("host"), host);
	out.insert(QStringLiteral("requests"), static_cast<double>(stats.requests));
	out.insert(QStringLiteral("frames"), static_cast<double>(stats.frames));
	out.insert(QStringLiteral("chunks"), static_cast<double>(stats.chunks));
	out.insert(QStringLiteral("multi_chunk_requests"),
		static_cast<double>(stats.multiChunkRequests));
	out.insert(QStringLiteral("frames_beyond_prepared_block"),
		static_cast<double>(stats.framesBeyondPreparedBlock));
	out.insert(QStringLiteral("max_requested_frames"), static_cast<double>(stats.maxRequestedFrames));
	out.insert(QStringLiteral("prepared_block"), static_cast<double>(stats.preparedBlock));
	return out;
}

ControlResult handleHostChunking(const QJsonObject&)
{
	QJsonArray hosts;
	hosts.append(statsJson(vst3HostChunkingCounters(), QStringLiteral("vst3")));
	hosts.append(statsJson(clapHostChunkingCounters(), QStringLiteral("clap")));

	// The rule itself, as data: a caller reading the counters below needs to
	// know what the host promises, not only what it has done.
	QJsonObject contract;
	contract.insert(QStringLiteral("chunked"), true);
	contract.insert(QStringLiteral("prepared_block_source"),
		QStringLiteral("the block size passed to prepare() - the engine's framesPerPeriod at the "
					   "moment the device was loaded"));
	contract.insert(QStringLiteral("over_run_rule"),
		QStringLiteral("a request larger than the prepared block is split into chunks of at most "
					   "that block; the plug-in is never asked for more frames than it declared "
					   "in setupProcessing()/activate(), and the host's own silence and scratch "
					   "blocks are never indexed past their end"));
	contract.insert(QStringLiteral("tail_rule"),
		QStringLiteral("the last chunk carries the remainder, so a request that is not a multiple "
					   "of the prepared block is processed whole; every frame of every channel "
					   "the caller supplies is written before process() returns"));

	QJsonObject result;
	result.insert(QStringLiteral("contract"), contract);
	result.insert(QStringLiteral("hosts"), hosts);
	result.insert(QStringLiteral("note"),
		QStringLiteral("Counters are process-wide, reset only when the process exits, incremented "
					   "on the audio thread and read here: the read is a snapshot, not a "
					   "transaction. 'chunks' larger than 'requests', together with a non-zero "
					   "'frames_beyond_prepared_block', is a request that was CHUNKED - which is "
					   "the property this command exists to make observable. Read-only; SPEC A16 "
					   "row not_mutating."));
	return ControlResult::success(result);
}

} // namespace

void registerPluginHostChunkingCommands(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("plugin.host_chunking");
	cmd.group = QStringLiteral("plugin");
	cmd.verb = QStringLiteral("host_chunking");
	cmd.description = QStringLiteral("The chunking contract both plugin host paths keep, and what "
		"this instance's audio path has actually been asked for: per host, the process() calls "
		"that reached a loaded plug-in, the frames they asked for, the plug-in calls they were "
		"turned into, the requests that needed more than one chunk and the frames beyond the "
		"prepared block they carried, the largest request and the block size the last one was "
		"prepared with. 'chunks' > 'requests' with a non-zero "
		"'frames_beyond_prepared_block' is a request that was chunked instead of truncated or "
		"over-run. Counters are process-wide and never reset; read-only.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({{QStringLiteral("contract"), objectProperty()},
		{QStringLiteral("hosts"), arrayProperty()},
		{QStringLiteral("note"), stringProperty()}});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) { return handleHostChunking(args); };
	registry.registerCommand(cmd);
}

} // namespace control

} // namespace lmms
