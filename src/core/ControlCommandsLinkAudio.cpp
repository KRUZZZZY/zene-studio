/*
 * ControlCommandsLinkAudio.cpp - link.audio_get_state / link.audio_set_enabled
 *                                (Q050-024, docs/LINK-AUDIO.md).
 *
 * The PCM stream is separate from Link transport sync: these commands never touch
 * LinkSyncEngine, and link.* never touches the stream.
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

#include <QJsonObject>
#include <QString>

#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlVocabulary.h"
#include "LinkAudioStream.h"

namespace lmms
{

using namespace control;

namespace
{

QJsonObject stateJson()
{
	link::LinkAudioStream* stream = link::LinkAudioStream::instance();
	const link::AudioStreamConfig cfg = stream->config();
	const link::AudioStreamReport r = stream->report();
	const auto num = [](std::uint64_t v) { return static_cast<double>(v); };
	QJsonObject o;
	o.insert(QStringLiteral("enabled"), cfg.enabled);
	o.insert(QStringLiteral("running"), r.running);
	o.insert(QStringLiteral("send"), cfg.send);
	o.insert(QStringLiteral("receive"), cfg.receive);
	o.insert(QStringLiteral("bind_address"), QString::fromStdString(cfg.bindAddress));
	o.insert(QStringLiteral("listen_port"), cfg.listenPort);
	o.insert(QStringLiteral("bound_port"), r.boundPort);
	o.insert(QStringLiteral("peer"), QString::fromStdString(cfg.peerAddress));
	o.insert(QStringLiteral("peer_port"), cfg.peerPort);
	o.insert(QStringLiteral("latency_ms"), cfg.latencyMs);
	o.insert(QStringLiteral("error"), QString::fromStdString(r.error));
	o.insert(QStringLiteral("format"), QStringLiteral("float32 stereo, UDP, <=128 frames per datagram"));
	o.insert(QStringLiteral("scope"), QStringLiteral("not Link transport sync and not Ableton LinkAudio; "
		"LAN-first, the peer is named by IPv4 address - docs/LINK-AUDIO.md"));
	QJsonObject stats;
	stats.insert(QStringLiteral("sample_rate"), static_cast<double>(r.sampleRate));
	stats.insert(QStringLiteral("periods"), num(r.periods));
	stats.insert(QStringLiteral("packets_sent"), num(r.packetsSent));
	stats.insert(QStringLiteral("packets_received"), num(r.packetsReceived));
	stats.insert(QStringLiteral("frames_sent"), num(r.framesSent));
	stats.insert(QStringLiteral("frames_received"), num(r.framesReceived));
	stats.insert(QStringLiteral("underruns"), num(r.underruns));
	stats.insert(QStringLiteral("trims"), num(r.trims));
	stats.insert(QStringLiteral("tx_overruns"), num(r.txOverruns));
	stats.insert(QStringLiteral("rx_overruns"), num(r.rxOverruns));
	stats.insert(QStringLiteral("lost_packets"), num(r.lostPackets));
	stats.insert(QStringLiteral("reordered_packets"), num(r.reorderedPackets));
	stats.insert(QStringLiteral("malformed_packets"), num(r.malformedPackets));
	stats.insert(QStringLiteral("rate_mismatches"), num(r.rateMismatches));
	stats.insert(QStringLiteral("send_errors"), num(r.sendErrors));
	stats.insert(QStringLiteral("buffered_ms"), r.latencyMs);
	stats.insert(QStringLiteral("buffered_max_ms"), r.latencyMaxMs);
	stats.insert(QStringLiteral("primed"), r.primed);
	o.insert(QStringLiteral("stats"), stats);
	return o;
}

QJsonObject configJson(const link::AudioStreamConfig& c)
{
	QJsonObject o;
	o.insert(QStringLiteral("enabled"), c.enabled);
	o.insert(QStringLiteral("send"), c.send);
	o.insert(QStringLiteral("receive"), c.receive);
	o.insert(QStringLiteral("bind_address"), QString::fromStdString(c.bindAddress));
	o.insert(QStringLiteral("listen_port"), c.listenPort);
	o.insert(QStringLiteral("peer"), QString::fromStdString(c.peerAddress));
	o.insert(QStringLiteral("peer_port"), c.peerPort);
	o.insert(QStringLiteral("latency_ms"), c.latencyMs);
	return o;
}

link::AudioStreamConfig mergeArgs(link::AudioStreamConfig c, const QJsonObject& a)
{
	const bool hadPeer = a.contains(QStringLiteral("peer"));
	c.enabled = a.value(QStringLiteral("enabled")).toBool();
	if (hadPeer) { c.peerAddress = a.value(QStringLiteral("peer")).toString().toStdString(); }
	if (a.contains(QStringLiteral("send"))) { c.send = a.value(QStringLiteral("send")).toBool(); }
	else { c.send = !c.peerAddress.empty(); }  // a peer to send to implies sending
	if (a.contains(QStringLiteral("receive"))) { c.receive = a.value(QStringLiteral("receive")).toBool(); }
	if (a.contains(QStringLiteral("bind_address"))) { c.bindAddress = a.value(QStringLiteral("bind_address")).toString().toStdString(); }
	if (a.contains(QStringLiteral("listen_port"))) { c.listenPort = a.value(QStringLiteral("listen_port")).toInt(); }
	if (a.contains(QStringLiteral("peer_port"))) { c.peerPort = a.value(QStringLiteral("peer_port")).toInt(); }
	if (a.contains(QStringLiteral("latency_ms"))) { c.latencyMs = a.value(QStringLiteral("latency_ms")).toInt(); }
	return c;
}

QJsonObject stateSchema()
{
	return objectSchema({
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("running"), booleanProperty()},
		{QStringLiteral("send"), booleanProperty()},
		{QStringLiteral("receive"), booleanProperty()},
		{QStringLiteral("bind_address"), stringProperty()},
		{QStringLiteral("listen_port"), integerProperty()},
		{QStringLiteral("bound_port"), integerProperty()},
		{QStringLiteral("peer"), stringProperty()},
		{QStringLiteral("peer_port"), integerProperty()},
		{QStringLiteral("latency_ms"), integerProperty()},
		{QStringLiteral("error"), stringProperty()},
		{QStringLiteral("format"), stringProperty()},
		{QStringLiteral("scope"), stringProperty()},
		{QStringLiteral("stats"), objectProperty()},
	});
}

void registerGetState(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("link.audio_get_state");
	cmd.group = QStringLiteral("link");
	cmd.verb = QStringLiteral("audio_get_state");
	cmd.description = QStringLiteral("The PCM audio stream between Zene instances: whether it runs, the "
		"address and port it listens on, the peer it sends to, and its counters (packets and frames each "
		"way, underruns, lost packets, buffer trims, buffered latency now and at most). This is an audio "
		"stream of our own, NOT Link transport sync (link.get_state) and NOT Ableton's LinkAudio. "
		"Read-only; no transaction (docs/LINK-AUDIO.md).");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = stateSchema();
	cmd.mutating = false;
	cmd.requiresEngine = false;
	cmd.handler = [](const QJsonObject&) { return ControlResult::success(stateJson()); };
	registry.registerCommand(cmd);
}

void registerSetEnabled(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("link.audio_set_enabled");
	cmd.group = QStringLiteral("link");
	cmd.verb = QStringLiteral("audio_set_enabled");
	cmd.description = QStringLiteral("Start or stop the PCM audio stream. 'peer' (an IPv4 address, no name "
		"lookup) is where this instance sends its master mix (float32 stereo UDP; a peer implies sending, "
		"'send' false turns it off). 'receive' (default true) adds the peer's stream to this "
		"instance's master mix through a jitter buffer of 'latency_ms' (5..200, default 20). It listens on "
		"'bind_address' (default 127.0.0.1: loopback only - give a LAN address to reach another machine) "
		"port 'listen_port' (default 20810, 0 = any, reported as bound_port). Independent of Link: tempo, "
		"beat phase and start/stop sync are untouched, with Link on or off. Arguments not given keep their "
		"current value. The audio thread only touches preallocated buffers. Local network only, no "
		"encryption or authentication (docs/LINK-AUDIO.md).");
	cmd.argsSchema = objectSchema({
		{QStringLiteral("enabled"), booleanProperty()},
		{QStringLiteral("peer"), stringProperty()},
		{QStringLiteral("peer_port"), integerProperty(1, 65535)},
		{QStringLiteral("send"), booleanProperty()},
		{QStringLiteral("receive"), booleanProperty()},
		{QStringLiteral("bind_address"), stringProperty()},
		{QStringLiteral("listen_port"), integerProperty(0, 65535)},
		{QStringLiteral("latency_ms"), integerProperty(link::MinAudioLatencyMs, link::MaxAudioLatencyMs)},
	}, {QStringLiteral("enabled")});
	cmd.resultSchema = stateSchema();
	cmd.mutating = true;
	cmd.requiresEngine = false;
	cmd.handler = [](const QJsonObject& args) {
		link::LinkAudioStream* stream = link::LinkAudioStream::instance();
		const link::AudioStreamConfig before = stream->config();
		const link::AudioStreamConfig requested = mergeArgs(before, args);
		std::string why;
		if (!stream->configure(requested, &why))
		{
			return ControlResult::failure(ControlErrorKind::InvalidArgs, QString::fromStdString(why));
		}
		// SPEC A16: process state the engine does not journal; the recorded step puts the
		// previous configuration back (a stopped stream stays stopped).
		control::addUndoStep(
			[before]() { std::string e; link::LinkAudioStream::instance()->configure(before, &e); },
			[requested]() { std::string e; link::LinkAudioStream::instance()->configure(requested, &e); });
		QJsonObject result = stateJson();
		QJsonObject transaction;
		transaction.insert(QStringLiteral("before"), configJson(before));
		transaction.insert(QStringLiteral("inverse"), QJsonObject{{QStringLiteral("op"),
			QStringLiteral("link.audio_set_enabled")}, {QStringLiteral("args"), configJson(before)}});
		transaction.insert(QStringLiteral("reversible"), true);
		transaction.insert(QStringLiteral("mechanism"), QStringLiteral("action checkpoint: the recorded undo "
			"step calls LinkAudioStream::configure with the previous configuration. Stream counters restart "
			"with each start; they are a report, not state"));
		result.insert(QStringLiteral("__transaction"), transaction);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace

void registerLinkAudioCommands(ControlRegistry& registry)
{
	registerGetState(registry);
	registerSetEnabled(registry);
}

} // namespace lmms
