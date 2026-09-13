/*
 * LinkSyncWire.cpp - the announcement codec and the two projections of the
 *                   session state: the packet this instance publishes, and the
 *                   report link.get_state answers with.
 *
 * Split out of src/core/LinkSync.cpp for this fork's file-length ratchet (Gate 7,
 * 500 lines per file), on the seam that already exists: LinkSync.cpp is the
 * state machine and the peer table, this file is how that state is expressed -
 * as bytes on the wire (encodePacket / decodePacket), as the announcement
 * buildPacket() would send, and as the LinkSessionReport the command group
 * serialises. Nothing here mutates the model; every function is a pure function
 * of it.
 *
 * READ LinkSync.h's `proto`/`model` checks before changing the codec. A packet
 * this build cannot read is IGNORED rather than misread: `model` names the
 * producer and `proto` its version, so a future real-Ableton-Link transport (or
 * a newer version of this one) can share the group without either being parsed
 * as the other. Interoperability is therefore a matter of adding a producer, not
 * of changing this file.
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

#include <algorithm>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "Engine.h"
#include "LinkPeerTransport.h"
#include "LinkSync.h"
#include "Song.h"

namespace lmms
{

using link::LinkPeerPacket;
using link::LinkSessionReport;

namespace
{

//! Is this a JSON object of a packet this build understands? `model` names the
//! producer and `proto` its version - the two fields that make an announcement
//! from ANOTHER producer (a real Ableton Link transport, or a newer version of
//! this one) ignore-able instead of misread.
bool isOurEnvelope(const QJsonObject& object)
{
	return object.value(QStringLiteral("proto")).toInt(0) == link::ProtoVersion
		&& object.value(QStringLiteral("model")).toString()
			== QString::fromLatin1(link::SyncModelName);
}

/*! The payload, or false when it could not have been produced by this model.
 *  A missing id, a non-positive tempo or send time, or a quantum outside the
 *  range this model accepts all make an announcement UNUSABLE rather than
 *  partially read: a peer that half-announces is not half in the session. */
bool readPacketFields(const QJsonObject& object, LinkPeerPacket* packet)
{
	LinkPeerPacket parsed;
	parsed.model = QString::fromLatin1(link::SyncModelName);
	parsed.id = object.value(QStringLiteral("id")).toString();
	parsed.name = object.value(QStringLiteral("name")).toString();
	parsed.revision = static_cast<quint32>(
		std::max(0, object.value(QStringLiteral("revision")).toInt(0)));
	parsed.revisionOwner = object.value(QStringLiteral("revision_owner")).toString();
	parsed.tempo = object.value(QStringLiteral("tempo")).toDouble(0.0);
	parsed.beat = object.value(QStringLiteral("beat")).toDouble(0.0);
	// A microsecond stamp is an exact integer in a double until a process has
	// been up for 285 years, so this round trip is lossless in practice.
	const double sentUs = object.value(QStringLiteral("sent_us")).toDouble(0.0);
	parsed.quantum = object.value(QStringLiteral("quantum")).toInt(link::DefaultQuantum);
	parsed.startStopSync = object.value(QStringLiteral("start_stop_sync")).toBool();
	parsed.playing = object.value(QStringLiteral("playing")).toBool();
	const bool usable = !parsed.id.isEmpty() && parsed.tempo > 0.0 && sentUs > 0.0
		&& parsed.quantum >= link::MinQuantum && parsed.quantum <= link::MaxQuantum;
	if (!usable)
	{
		return false;
	}
	parsed.sentUs = static_cast<quint64>(sentUs);
	*packet = parsed;
	return true;
}

} // namespace

QByteArray LinkSyncEngine::encodePacket(const LinkPeerPacket& packet)
{
	QJsonObject object;
	object.insert(QStringLiteral("proto"), packet.proto);
	object.insert(QStringLiteral("model"), packet.model);
	object.insert(QStringLiteral("id"), packet.id);
	object.insert(QStringLiteral("name"), packet.name);
	object.insert(QStringLiteral("revision"), static_cast<int>(packet.revision));
	object.insert(QStringLiteral("revision_owner"), packet.revisionOwner);
	object.insert(QStringLiteral("tempo"), packet.tempo);
	object.insert(QStringLiteral("beat"), packet.beat);
	object.insert(QStringLiteral("sent_us"), static_cast<double>(packet.sentUs));
	object.insert(QStringLiteral("quantum"), packet.quantum);
	object.insert(QStringLiteral("start_stop_sync"), packet.startStopSync);
	object.insert(QStringLiteral("playing"), packet.playing);
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

bool LinkSyncEngine::decodePacket(const QByteArray& payload, LinkPeerPacket* packet)
{
	if (packet == nullptr)
	{
		return false;
	}
	QJsonParseError error{};
	const QJsonDocument document = QJsonDocument::fromJson(payload, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject())
	{
		return false;
	}
	const QJsonObject object = document.object();
	if (!isOurEnvelope(object))
	{
		return false;
	}
	return readPacketFields(object, packet);
}

LinkPeerPacket LinkSyncEngine::buildPacket(quint64 sentUs) const
{
	Song* song = Engine::getSong();
	LinkPeerPacket packet;
	packet.model = QString::fromLatin1(link::SyncModelName);
	packet.id = m_peerId;
	packet.name = m_name;
	packet.revision = m_revision;
	packet.revisionOwner = m_revisionOwner;
	packet.tempo = m_tempo;
	packet.beat = beatsAt(sentUs);
	packet.sentUs = sentUs;
	packet.quantum = m_quantum;
	packet.startStopSync = m_startStopSync;
	packet.playing = song != nullptr && song->isPlaying();
	return packet;
}

LinkSessionReport LinkSyncEngine::report() const
{
	const quint64 now = monotonicMicros();
	LinkSessionReport report;
	report.enabled = m_enabled;
	report.model = QString::fromLatin1(link::SyncModelName);
	report.peerId = m_peerId;
	report.instanceName = m_name;
	report.peerCount = m_peers.size();
	for (const Peer& peer : m_peers)
	{
		link::LinkPeerView view;
		view.id = peer.packet.id;
		view.name = peer.packet.name;
		view.tempo = peer.packet.tempo;
		view.revision = peer.packet.revision;
		view.quantum = peer.packet.quantum;
		view.playing = peer.packet.playing;
		view.ageMs = now > peer.lastSeenUs
			? static_cast<qint64>((now - peer.lastSeenUs) / 1000) : 0;
		report.peers.append(view);
	}
	report.quantum = m_quantum;
	report.startStopSync = m_startStopSync;
	report.sessionTempo = m_tempo;
	report.revision = m_revision;
	report.revisionOwner = m_revisionOwner;
	report.tempoIsLocal = m_tempoIsLocal;
	report.beat = beatsAt(now);
	report.phaseBeats = phaseOf(report.beat);
	report.enginePhaseBeats = enginePhaseBeats();
	report.phaseErrorBeats = signedPhaseDifference(report.phaseBeats,
		report.enginePhaseBeats, static_cast<double>(m_quantum));
	Song* song = Engine::getSong();
	report.engineTempo = song != nullptr ? song->getTempo() : 0;
	report.transportAvailable = m_transport != nullptr && m_transport->available();
	report.transportReason = m_transport != nullptr ? m_transport->reason()
		: QStringLiteral("no transport");
	report.transportEndpoint = m_transport != nullptr ? m_transport->endpoint() : QString();
	report.publishedCount = m_publishedCount;
	report.lastPacketAgeMs = (m_lastPacketUs > 0 && now > m_lastPacketUs)
		? static_cast<qint64>((now - m_lastPacketUs) / 1000) : -1;
	return report;
}

} // namespace lmms
