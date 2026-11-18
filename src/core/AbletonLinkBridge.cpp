/*
 * AbletonLinkBridge.cpp - include/AbletonLinkBridge.h: the UI-thread half of
 *                         Ableton Link session sync. The header says what each
 *                         step does and which path it shares with a user's
 *                         own change.
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

#include "AbletonLinkBridge.h"

#include <algorithm>
#include <cmath>

#include "Engine.h"
#include "LinkAudioFollower.h"
#include "LinkPhaseLock.h"
#include "LinkSync.h"
#include "Song.h"

namespace lmms
{

namespace
{

constexpr double TicksPerBeat = DefaultTicksPerBar / 4.0;

//! Link carries a tempo as whole microseconds per beat, so the value that comes
//! back from the session equals the one committed only to within this.
bool sameSessionTempo(double a, double b)
{
	return std::fabs(a - b) <= b * b * 0.5 / 60e6 * 1.000001;
}

/*! The engine's whole-BPM tempo for a session tempo. Snapped to 0.01 BPM first:
 *  a tempo that crossed the network is off by up to bpm^2 * 0.5 / 60e6 (97.5
 *  arrives as 97.49994), and without the snap every peer of a 97.5 session but
 *  the one that declared it would show 97 instead of 98. */
int engineTempoFor(double bpm)
{
	const double snapped = std::round(bpm * 100.0) / 100.0;
	return std::clamp(static_cast<int>(std::lround(snapped)), static_cast<int>(MinTempo), static_cast<int>(MaxTempo));
}

/*! A followed tempo: Song::setTempo, the tempo dial's own call, with the tempo
 *  model's journalling suspended - an external source's tempo is not an undo
 *  step (include/AbletonLinkBridge.h). */
void applyFollowedTempo(Song* song, int bpm)
{
	IntModel& model = song->tempoModel();
	model.saveJournallingState(false);
	song->setTempo(bpm);
	model.restoreJournallingState();
}

} // namespace

AbletonLinkBridge::AbletonLinkBridge() : m_session(std::make_unique<AbletonLinkSession>(120.0))
{
	LinkAudioFollower::instance().attach(m_session.get());
}

AbletonLinkBridge::~AbletonLinkBridge()
{
	QObject::disconnect(m_transportEdges);
	LinkAudioFollower& follower = LinkAudioFollower::instance();
	follower.setEngaged(false);
	// Waits for a period in flight to let go of the session before it is destroyed.
	follower.attach(nullptr);
	m_session->enable(false);
}

void AbletonLinkBridge::setEnabled(bool on)
{
	if (on == m_enabled) { return; }
	LinkAudioFollower& follower = LinkAudioFollower::instance();
	if (!on)
	{
		follower.setEngaged(false);
		m_session->enable(false);
		m_enabled = false;
		return;
	}
	// The audio thread is not engaged yet, so this application-thread commit
	// cannot race an audio-thread one: joining brings the tempo this engine plays,
	// and a session that is already running replaces it as Link decides.
	Song* song = Engine::getSong();
	const double tempo = song != nullptr ? static_cast<double>(song->getTempo()) : 120.0;
	if (song != nullptr && !m_transportEdges)
	{
		m_transportEdges = QObject::connect(song, &Song::playbackStateChanged,
			[] { LinkAudioFollower::instance().noteTransportEdge(); });
	}
	m_session->appSetTempo(tempo);
	m_appliedTempo = tempo;
	m_sessionTempo = tempo;
	m_tempoIsLocal = true;
	m_session->enableStartStopSync(m_startStopSync);
	m_session->enable(true);
	m_lastSessionPlaying = m_session->appCapture(m_quantum).playing;
	follower.setQuantum(m_quantum);
	follower.setStartStopSync(m_startStopSync);
	follower.setEngaged(true);
	m_enabled = true;
}

void AbletonLinkBridge::setQuantum(int quantum)
{
	m_quantum = quantum;
	LinkAudioFollower::instance().setQuantum(quantum);
}

void AbletonLinkBridge::setStartStopSync(bool on)
{
	m_startStopSync = on;
	m_session->enableStartStopSync(on);
	LinkAudioFollower::instance().setStartStopSync(on);
	// Only a later CHANGE acts: what the session is doing now is left alone.
	m_lastSessionPlaying = m_session->appCapture(m_quantum).playing;
}

void AbletonLinkBridge::declareTempo(double bpm)
{
	commitTempo(bpm);
	m_tempoIsLocal = true;
	Song* song = Engine::getSong();
	if (song == nullptr) { return; }
	const int rounded = engineTempoFor(bpm);
	if (song->getTempo() != rounded) { song->setTempo(rounded); }
	m_appliedTempo = song->getTempo();
}

double AbletonLinkBridge::sessionTempo() const
{
	return m_enabled ? m_session->appCapture(m_quantum).tempo : m_sessionTempo;
}

void AbletonLinkBridge::service()
{
	Song* song = Engine::getSong();
	if (!m_enabled || song == nullptr) { return; }
	pushLocalTempo(song);
	const LinkTimelinePoint point = m_session->appCapture(m_quantum);
	followSessionTempo(song, point);
	followStartStop(song, point);
}

void AbletonLinkBridge::pushLocalTempo(Song* song)
{
	const double local = static_cast<double>(song->getTempo());
	if (local == m_appliedTempo) { return; }
	m_appliedTempo = local;
	m_tempoIsLocal = true;
	commitTempo(local);
}

void AbletonLinkBridge::followSessionTempo(Song* song, const LinkTimelinePoint& point)
{
	if (point.tempo == m_sessionTempo) { return; }
	m_sessionTempo = point.tempo;
	m_tempoIsLocal = sameSessionTempo(point.tempo, m_committedTempo);
	const int rounded = engineTempoFor(point.tempo);
	if (song->getTempo() != rounded) { applyFollowedTempo(song, rounded); }
	m_appliedTempo = song->getTempo();
}

void AbletonLinkBridge::followStartStop(Song* song, const LinkTimelinePoint& point)
{
	if (point.playing == m_lastSessionPlaying) { return; }
	m_lastSessionPlaying = point.playing;
	if (!m_startStopSync || song->isPlaying() == point.playing) { return; }
	if (point.playing) { song->playSong(); }
	else { song->stop(); }
}

void AbletonLinkBridge::commitTempo(double bpm)
{
	m_committedTempo = bpm;
	LinkAudioFollower& follower = LinkAudioFollower::instance();
	// Link's rule: while the audio thread runs, it is the one that modifies the
	// session; the application-thread commit is for when it does not.
	if (m_enabled && follower.audioRunning(m_session->clockMicros())) { follower.requestTempo(bpm); }
	else { m_session->appSetTempo(bpm); }
}

double AbletonLinkBridge::enginePhase(double quantum) const
{
	Song* song = Engine::getSong();
	if (song == nullptr || !(quantum > 0.0)) { return 0.0; }
	const Timeline& timeline = song->getTimeline();
	const double beats = (static_cast<double>(timeline.ticks())
		+ static_cast<double>(timeline.frameOffset()) / Engine::framesPerTick()) / TicksPerBeat;
	return beats - quantum * std::floor(beats / quantum);
}

void AbletonLinkBridge::fill(link::LinkSessionReport& report) const
{
	// The zene-link-style notions - a peer table, a revision, a loopback probe,
	// announcement counters - have no Link counterpart: their empty values.
	report.peers.clear();
	report.revision = 0;
	report.revisionOwner.clear();
	report.transportProbeAttempted = false;
	report.transportProbeDelivered = false;
	report.transportProbeElapsedMs = -1;
	report.transportProbeBoundMs = 0;
	report.publishedCount = 0;
	report.lastPacketAgeMs = -1;
	fillSession(report);
	fillFollower(report);
}

void AbletonLinkBridge::fillSession(link::LinkSessionReport& report) const
{
	const LinkTimelinePoint point = m_session->appCapture(m_quantum);
	Song* song = Engine::getSong();
	report.model = QString::fromLatin1(link::AbletonModelName);
	report.peerCount = m_enabled ? static_cast<int>(m_session->numPeers()) : 0;
	// A tempo this instance declared is the session's from the next audio period
	// on; until that period commits it, the declaration is what is reported, so
	// a link.set_session_tempo reply shows the tempo it set.
	const double pending = LinkAudioFollower::instance().pendingTempo();
	report.sessionTempo = pending > 0.0 ? pending : point.tempo;
	report.tempoIsLocal = m_tempoIsLocal;
	report.beat = point.beat;
	report.phaseBeats = point.phase;
	report.enginePhaseBeats = enginePhase(m_quantum);
	report.phaseErrorBeats = link::PhaseLock::signedDistance(report.phaseBeats, report.enginePhaseBeats, m_quantum);
	report.engineTempo = song != nullptr ? song->getTempo() : 0;
	report.transportAvailable = true;
	report.transportEndpoint = QStringLiteral("%1:%2").arg(QString::fromLatin1(link::DefaultGroup)).arg(link::DefaultPort);
	report.publishIntervalMs = link::AbletonServiceIntervalMs;
	report.peerTimeoutMs = 0;
	report.abletonLink = true;
	report.sessionPlaying = point.playing;
	report.libraryVersion = QString::fromLatin1(AbletonLinkSession::libraryVersion());
	report.libraryCommit = QString::fromLatin1(AbletonLinkSession::libraryCommit());
}

void AbletonLinkBridge::fillFollower(link::LinkSessionReport& report) const
{
	const LinkFollowerReading reading = LinkAudioFollower::instance().reading();
	report.followerLocked = reading.locked;
	report.followerPeriods = reading.periods;
	report.followerHolds = reading.holds;
	report.followerJumps = reading.jumps;
	report.followerErrorBeats = reading.lastErrorBeats;
}

} // namespace lmms
