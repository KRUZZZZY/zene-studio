/*
 * LinkSync.cpp - the Link-style session-sync model (D11 "Ableton Link sync").
 *
 * Read include/LinkSync.h first: it holds the shape, the bounds and the
 * statement of what this is (a model with Link's semantics) and is not (the
 * Ableton Link library, which this build does not vendor - the licence analysis
 * that says vendoring it is PERMITTED is in docs/LINK-SYNC.md section 1).
 *
 * The model's shape and the arithmetic it rests on are stated in ONE place a
 * reader can check them against the code: docs/LINK-SYNC.md section 2 (the
 * timeline, the revision order, the quantum) and the file header of
 * include/LinkSync.h (what this is and is not). What is left here is the
 * state machine and the peer table; the announcement codec and the two
 * projections of this state (the packet it publishes and the report
 * link.get_state answers with) are in src/core/LinkSyncWire.cpp.
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

#include "LinkSync.h"

#include <algorithm>
#include <chrono>
#include <cmath>

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRandomGenerator>
#include <QSysInfo>
#include <QTimer>

#include "Engine.h"
#include "LinkPeerTransport.h"
#include "Song.h"
#include "TimePos.h"

namespace lmms
{

using link::LinkPeerPacket;
using link::LinkSessionReport;

namespace
{

//! Fractional part in [0, period): the phase inside one quantum.
double wrapPhase(double beats, double period)
{
	if (!(period > 0.0))
	{
		return 0.0;
	}
	const double wrapped = beats - period * std::floor(beats / period);
	return wrapped < 0.0 ? wrapped + period : wrapped;
}

} // namespace

double LinkSyncEngine::phaseOf(double beats) const
{
	return wrapPhase(beats, static_cast<double>(m_quantum));
}

double LinkSyncEngine::signedPhaseDifference(double a, double b, double period)
{
	if (!(period > 0.0))
	{
		return a - b;
	}
	// difference - period * round(difference / period), which is the signed
	// distance in (-period/2, period/2] without a branch per sign.
	return a - b - period * std::floor((a - b) / period + 0.5);
}

LinkSyncEngine* LinkSyncEngine::s_instance = nullptr;

LinkSyncEngine* LinkSyncEngine::instance()
{
	if (s_instance == nullptr)
	{
		s_instance = new LinkSyncEngine();
	}
	return s_instance;
}

void LinkSyncEngine::destroy()
{
	delete s_instance;
	s_instance = nullptr;
}

quint64 LinkSyncEngine::monotonicMicros()
{
	// steady_clock is CLOCK_MONOTONIC on Linux and QueryPerformanceCounter on
	// Windows: system-wide on both, which is what the transit-time arithmetic
	// needs. See the file header, point 3.
	using Clock = std::chrono::steady_clock;
	return static_cast<quint64>(std::chrono::duration_cast<std::chrono::microseconds>(
		Clock::now().time_since_epoch()).count());
}

LinkSyncEngine::LinkSyncEngine(QObject* parent) : QObject(parent)
{
	// Reserved ONCE. The table is bounded by link::MaxPeers: the model's memory
	// does not grow with the number of instances on the network.
	m_peers.reserve(link::MaxPeers);
	m_peerId = QStringLiteral("zene-%1-%2")
		.arg(QCoreApplication::applicationPid())
		.arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
	const QString host = QSysInfo::machineHostName();
	m_name = host.isEmpty() ? m_peerId : QStringLiteral("%1#%2").arg(host)
		.arg(QCoreApplication::applicationPid());
	m_transport = createLinkUdpTransport(this);
	m_transport->setReceiver([this](const QByteArray& payload, quint64 receivedUs) {
		handleDatagram(payload, receivedUs);
	});
	m_timer = new QTimer(this);
	m_timer->setInterval(link::DefaultPublishIntervalMs);
	connect(m_timer, &QTimer::timeout, this, &LinkSyncEngine::onTick);
}

void LinkSyncEngine::setEnabled(bool enabled)
{
	if (m_enabled == enabled)
	{
		return;
	}
	m_enabled = enabled;
	if (!m_enabled)
	{
		m_timer->stop();
		m_transport->stop();
		dropPeers();
		return;
	}
	ensureTransport();
	// Joining is itself a declaration: this instance brings the tempo its own
	// engine is playing, and the session converges from there (file header,
	// point 4). A peer that declared first is adopted on the first announcement
	// this instance reads.
	Song* song = Engine::getSong();
	const double base = song != nullptr ? static_cast<double>(song->getTempo())
		: (m_tempo > 0.0 ? m_tempo : 120.0);
	m_anchorUs = monotonicMicros();
	m_anchorBeat = 0.0;
	m_appliedTempo = -1.0;   // force the declaration to write through
	declareTempo(base, m_anchorUs);
	m_timer->start();
}

bool LinkSyncEngine::setQuantum(int quantum)
{
	if (quantum < link::MinQuantum || quantum > link::MaxQuantum)
	{
		return false;
	}
	m_quantum = quantum;
	return true;
}

void LinkSyncEngine::setSessionTempo(double bpm)
{
	declareTempo(bpm, monotonicMicros());
}

void LinkSyncEngine::service()
{
	if (!m_enabled)
	{
		return;
	}
	const quint64 now = monotonicMicros();
	expirePeers(now);
	pollLocalTempo(now);
	followNewestRevision(now);
	publish(now);
}

void LinkSyncEngine::onTick()
{
	service();
}

void LinkSyncEngine::declareTempo(double bpm, quint64 nowUs)
{
	// Re-anchor FIRST: the beats played at the old tempo must still be beats
	// after the new one takes effect, or every peer's phase jumps by the
	// difference (file header, point 2).
	rebase(nowUs);
	m_tempo = std::clamp(bpm, link::MinSessionTempo, link::MaxSessionTempo);
	m_revision = highestSeenRevision() + 1;
	m_revisionOwner = m_peerId;
	m_tempoIsLocal = true;
	applyTempoToEngine(m_tempo);
}

void LinkSyncEngine::pollLocalTempo(quint64 nowUs)
{
	Song* song = Engine::getSong();
	if (song == nullptr)
	{
		return;
	}
	const double local = static_cast<double>(song->getTempo());
	// "Unchanged" means "the Song still plays what this class last wrote or
	// read". That is what keeps an adopted tempo from looking like a local
	// edit, and it is why an agent's transport.set_tempo needs no new call to
	// reach the session.
	if (local == m_appliedTempo)
	{
		return;
	}
	declareTempo(local, nowUs);
}

void LinkSyncEngine::applyTempoToEngine(double bpm)
{
	Song* song = Engine::getSong();
	const int rounded = std::clamp(static_cast<int>(std::lround(bpm)),
		static_cast<int>(MinTempo), static_cast<int>(MaxTempo));
	if (song == nullptr)
	{
		m_appliedTempo = static_cast<double>(rounded);
		return;
	}
	if (song->getTempo() != rounded)
	{
		song->setTempo(rounded);
	}
	m_appliedTempo = static_cast<double>(song->getTempo());
}

void LinkSyncEngine::observePacket(const LinkPeerPacket& packet, quint64 receivedUs)
{
	if (!m_enabled || packet.id == m_peerId)
	{
		// Disabled: nothing is joined, so nothing is heard. Own id: the
		// multicast group loops our own announcement back to us.
		return;
	}
	rememberPeer(packet, receivedUs);
	m_lastPacketUs = receivedUs;
	if (adoptionIsNewer(packet))
	{
		adopt(packet, receivedUs);
	}
}

bool LinkSyncEngine::adoptionIsNewer(const LinkPeerPacket& packet) const
{
	if (packet.revision != m_revision)
	{
		return packet.revision > m_revision;
	}
	// Same revision number: two declarations made without having seen each
	// other. The owner id is the tiebreak, and it is a TOTAL order, so both
	// instances pick the same winner (file header, point 4).
	return packet.revisionOwner > m_revisionOwner;
}

void LinkSyncEngine::adopt(const LinkPeerPacket& packet, quint64 receivedUs)
{
	const double transit = static_cast<double>(
		static_cast<qint64>(receivedUs) - static_cast<qint64>(packet.sentUs));
	const double transitBeats = std::max(0.0, transit)
		* packet.tempo / (60.0 * 1000000.0);
	m_anchorUs = receivedUs;
	m_anchorBeat = packet.beat + transitBeats;
	m_tempo = packet.tempo;
	m_revision = packet.revision;
	m_revisionOwner = packet.revisionOwner.isEmpty() ? packet.id : packet.revisionOwner;
	m_tempoIsLocal = false;
	// The QUANTUM is not adopted: it is this instance's own launch preference
	// (as it is in Link), not session state.
	applyTempoToEngine(m_tempo);
}

void LinkSyncEngine::followNewestRevision(quint64 nowUs)
{
	const Peer* newest = newestPeer();
	if (newest == nullptr || !adoptionIsNewer(newest->packet))
	{
		return;
	}
	// A retry, not a second rule: this is the same arithmetic the live path
	// used, replaying the peer's own announcement with the timestamp this
	// process read it at. It matters for an announcement that arrived before
	// the engine existed, and as a re-assertion after the Song was reloaded.
	adopt(newest->packet, std::max(newest->lastSeenUs, m_lastPacketUs));
	(void)nowUs;
}

void LinkSyncEngine::handleDatagram(const QByteArray& payload, quint64 receivedUs)
{
	LinkPeerPacket packet;
	if (!decodePacket(payload, &packet))
	{
		return;
	}
	observePacket(packet, receivedUs);
}

void LinkSyncEngine::rememberPeer(const LinkPeerPacket& packet, quint64 receivedUs)
{
	for (Peer& peer : m_peers)
	{
		if (peer.packet.id == packet.id)
		{
			peer.packet = packet;
			peer.lastSeenUs = receivedUs;
			return;
		}
	}
	if (m_peers.size() >= link::MaxPeers)
	{
		// Full table: the stalest peer makes room. Bounded on purpose - the
		// model never allocates because someone joined the network.
		auto stalest = std::min_element(m_peers.begin(), m_peers.end(),
			[](const Peer& a, const Peer& b) { return a.lastSeenUs < b.lastSeenUs; });
		*stalest = Peer{packet, receivedUs};
		return;
	}
	m_peers.append(Peer{packet, receivedUs});
}

void LinkSyncEngine::expirePeers(quint64 nowUs)
{
	const quint64 timeout = static_cast<quint64>(link::PeerTimeoutMs) * 1000;
	m_peers.erase(std::remove_if(m_peers.begin(), m_peers.end(),
		[nowUs, timeout](const Peer& peer) {
			return nowUs > peer.lastSeenUs && nowUs - peer.lastSeenUs > timeout;
		}), m_peers.end());
}

void LinkSyncEngine::dropPeers()
{
	m_peers.clear();
	m_lastPacketUs = 0;
}

const LinkSyncEngine::Peer* LinkSyncEngine::newestPeer() const
{
	const Peer* best = nullptr;
	for (const Peer& peer : m_peers)
	{
		if (best == nullptr || adoptionIsNewer(peer.packet))
		{
			best = &peer;
		}
	}
	return best;
}

quint32 LinkSyncEngine::highestSeenRevision() const
{
	quint32 highest = m_revision;
	for (const Peer& peer : m_peers)
	{
		highest = std::max(highest, peer.packet.revision);
	}
	return highest;
}

void LinkSyncEngine::rebase(quint64 nowUs)
{
	m_anchorBeat = beatsAt(nowUs);
	m_anchorUs = nowUs;
}

double LinkSyncEngine::beatsAt(quint64 us) const
{
	const double elapsed = static_cast<double>(
		static_cast<qint64>(us) - static_cast<qint64>(m_anchorUs));
	return m_anchorBeat + elapsed * m_tempo / (60.0 * 1000000.0);
}

double LinkSyncEngine::enginePhaseBeats() const
{
	Song* song = Engine::getSong();
	if (song == nullptr)
	{
		return 0.0;
	}
	const int ticksPerBar = song->ticksPerBar() > 0 ? song->ticksPerBar() : DefaultTicksPerBar;
	const qint64 ticks = static_cast<qint64>(song->getPlayPos().getTicks());
	const double bars = static_cast<double>(ticks % ticksPerBar) / ticksPerBar;
	// Bars to beats at this model's rate (include/LinkSync.h, BeatsPerBar),
	// then wrapped into the quantum.
	return wrapPhase(bars * link::BeatsPerBar, static_cast<double>(m_quantum));
}

void LinkSyncEngine::ensureTransport()
{
	if (m_transport->available())
	{
		return;
	}
	m_transport->start();
}

void LinkSyncEngine::publish(quint64 nowUs)
{
	if (!m_transport->available())
	{
		return;
	}
	m_transport->send(encodePacket(buildPacket(nowUs)));
	++m_publishedCount;
}

bool LinkSyncEngine::transportAvailable() const
{
	return m_transport != nullptr && m_transport->available();
}

QString LinkSyncEngine::transportReason() const
{
	return m_transport != nullptr ? m_transport->reason() : QStringLiteral("no transport");
}

QString LinkSyncEngine::transportEndpoint() const
{
	return m_transport != nullptr ? m_transport->endpoint() : QString();
}

} // namespace lmms
