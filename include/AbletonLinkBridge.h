/*
 * AbletonLinkBridge.h - the UI-thread half of Ableton Link session sync: the
 *                       "ableton-link" backend behind LinkSyncEngine and the
 *                       link.* commands (docs/LINK-SYNC.md sections 2 and 7).
 *
 * It owns this process's AbletonLinkSession and publishes it to the audio
 * thread's LinkAudioFollower. Its periodic service() (LinkSyncEngine's timer,
 * every 25 ms while enabled) does three things, each through the path the same
 * change takes when a user makes it:
 *  - a LOCAL tempo change (the tempo dial, transport.set_tempo, a script) is
 *    seen as "the Song's tempo is not what this bridge last wrote or accepted"
 *    and committed to the session - through the audio thread's commit while it
 *    runs (Link's rule), through Link's application-thread commit only when no
 *    audio thread is running;
 *  - a PEER's tempo is written to the Song with Song::setTempo - the call the
 *    tempo dial makes, so frames-per-tick, sounding notes, VST sync and the
 *    tempoChanged signal all follow - but with the tempo model's journalling
 *    suspended: like a MIDI clock it is an external source's trajectory, so it
 *    is NOT an undo step and does not mark the project modified by itself
 *    (docs/LINK-SYNC.md section 2). The session tempo is a double; the Song's
 *    is an integer, so the Song shows it rounded while the audio thread runs
 *    the play head at the session's exact rate;
 *  - with start/stop sync on, a CHANGE of the session's playing state starts or
 *    stops this transport with Song::playSong() / Song::stop() - what the play
 *    and stop buttons call. Only a change acts: the state a session already has
 *    when sync is switched on is left alone (Link.hpp: "the current local
 *    start/stop state persists when joining").
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

#ifndef LMMS_ABLETON_LINK_BRIDGE_H
#define LMMS_ABLETON_LINK_BRIDGE_H

#include <memory>

#include <QMetaObject>

#include "AbletonLinkSession.h"
#include "lmms_export.h"

namespace lmms
{

class Song;

namespace link
{
struct LinkSessionReport;
//! The model name the "ableton-link" backend reports in link.get_state.
constexpr const char* AbletonModelName = "ableton-link";
//! The UI-thread service interval while an Ableton Link session is enabled.
constexpr int AbletonServiceIntervalMs = 25;
} // namespace link

class LMMS_EXPORT AbletonLinkBridge
{
public:
	AbletonLinkBridge();
	~AbletonLinkBridge();
	AbletonLinkBridge(const AbletonLinkBridge&) = delete;
	AbletonLinkBridge& operator=(const AbletonLinkBridge&) = delete;

	void setEnabled(bool on);
	bool enabled() const { return m_enabled; }
	void setQuantum(int quantum);
	void setStartStopSync(bool on);
	//! link.set_session_tempo: declare an exact (fractional) session tempo.
	void declareTempo(double bpm);
	double sessionTempo() const;
	void service();
	//! Everything link.get_state reports that this backend knows.
	void fill(link::LinkSessionReport& report) const;

private:
	void pushLocalTempo(Song* song);
	void followSessionTempo(Song* song, const LinkTimelinePoint& point);
	void followStartStop(Song* song, const LinkTimelinePoint& point);
	void commitTempo(double bpm);
	double enginePhase(double quantum) const;
	void fillSession(link::LinkSessionReport& report) const;
	void fillFollower(link::LinkSessionReport& report) const;

	std::unique_ptr<AbletonLinkSession> m_session;
	bool m_enabled = false;
	bool m_startStopSync = false;
	int m_quantum = 4;
	//! The Song tempo this bridge last wrote or accepted: anything else is local.
	double m_appliedTempo = -1.0;
	//! The session tempo last seen, so only a CHANGE is written to the Song.
	double m_sessionTempo = 0.0;
	//! The tempo this bridge last committed, so its own echo is told from a peer's.
	double m_committedTempo = 0.0;
	bool m_lastSessionPlaying = false;
	bool m_tempoIsLocal = true;
	//! Song::playbackStateChanged -> LinkAudioFollower::noteTransportEdge.
	QMetaObject::Connection m_transportEdges;
};

} // namespace lmms

#endif // LMMS_ABLETON_LINK_BRIDGE_H
