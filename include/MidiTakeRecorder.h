/*
 * MidiTakeRecorder.h - the main-thread half of arrangement MIDI take recording:
 *                      a transport.record session and the takes it leaves
 *
 * Copyright (c) 2026 LMMS developers
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
 *
 */

#ifndef LMMS_MIDI_TAKE_RECORDER_H
#define LMMS_MIDI_TAKE_RECORDER_H

#include <array>
#include <cstdint>
#include <vector>

#include <QJsonArray>
#include <QJsonObject>
#include <QObject>
#include <QPointer>
#include <QTimer>

#include "LmmsTypes.h"
#include "MidiTakeCapture.h"
#include "lmms_export.h"

namespace lmms
{

class InstrumentTrack;
class MidiClip;
class Song;

/*! Q050-054.1: records the MIDI played into armed instrument tracks (track.set_midi_arm) while
 *  transport.record runs, alongside the audio the same pass records, and leaves it in the arrangement.
 *
 *  - Record (Song::playAndRecord, Song mode) opens a session on every armed instrument track; their
 *    MidiTakeCapture queues note edges from the MIDI input threads; a main-thread timer drains them.
 *  - Stop closes the session and runs record.midi_capture_commit through the control registry - the
 *    one command, whoever pressed Stop - which turns the session into clips in ONE undo step:
 *    a plain pass is one MIDI clip from the bar of its first note; a loop recording (the transport
 *    wrapped at least once) makes every pass that holds notes a take clip over the loop region, each
 *    on a new take lane of one take group, under the active-take model: the newest pass is active
 *    unless the musician chose a take of that group (comp.take_select) - a later loop recording over
 *    the same region joins the same group and keeps that choice.
 *  - Nothing here runs on the audio or MIDI threads; they only touch MidiTakeCapture. */
class LMMS_EXPORT MidiTakeRecorder : public QObject
{
	Q_OBJECT
public:
	static constexpr int DrainIntervalMs = 50;
	static constexpr const char* CommitCommand = "record.midi_capture_commit";

	//! Song's constructor makes the one recorder, as its child (so it goes with the song).
	explicit MidiTakeRecorder(Song* song);
	~MidiTakeRecorder() override;
	//! The song's recorder; nullptr before the song exists and after it is gone.
	static MidiTakeRecorder* instance() { return s_instance; }

	//! A recording session is open (transport.record running with at least one armed track).
	bool isRecording() const { return m_active; }
	//! A closed session holds captured MIDI that has not been committed yet.
	bool hasPending() const { return m_pending; }
	//! The instrument tracks the open (or pending) session captures, by control id.
	QJsonArray sessionTracks() const;

	//! Turns the pending session into arrangement clips (the body of record.midi_capture_commit; the
	//! caller has taken the undo checkpoint of trackObjects()). Answers the commit report.
	QJsonObject commitPending();
	//! The tracks the pending session would change - what the commit's checkpoint covers.
	std::vector<InstrumentTrack*> pendingTracks() const;

	//! record.midi_capture_status: the armed tracks, the session, and the last commit.
	QJsonObject status() const;

	//! Drain the capture queues now (the timer does it every DrainIntervalMs while recording).
	void drain();
	//! Song::playAndRecord: the tick Record was pressed at, when a count-in or pre-roll started the
	//! transport earlier - a plain pass keeps only what was played from there on.
	void setRecordFrom(tick_t tick);

private:
	struct TrackSession
	{
		QPointer<InstrumentTrack> track;
		std::vector<MidiTakeEvent> events;
	};

	//! A captured note in song ticks, with the loop pass it was played in.
	struct CapturedNote
	{
		int pass = 0;
		tick_t position = 0;
		tick_t length = 1;
		int key = 0;
		int velocity = 0;
	};

	void onPlaybackStateChanged();
	void onStopped();
	void begin();
	void finish();
	bool loopRecording() const;
	tick_t loopLength() const { return m_loopEnd - m_loopBegin; }
	std::vector<CapturedNote> notesOf(const std::vector<MidiTakeEvent>& events) const;
	bool beforeRecordPoint(const MidiTakeEvent& event, bool loop) const;
	int passOf(const MidiTakeEvent& event, bool loop) const;
	tick_t unfolded(const MidiTakeEvent& event, int pass, bool loop) const;
	void closeHeldNotes(std::vector<CapturedNote>* notes, const std::array<int, 128>& open, bool loop) const;
	void foldIntoLoop(std::vector<CapturedNote>* notes) const;
	int loopTakeGroup(InstrumentTrack* track) const;
	MidiClip* loopTakeClip(InstrumentTrack* track, const std::vector<const CapturedNote*>& notes);
	QJsonArray commitTrack(InstrumentTrack* track, const std::vector<CapturedNote>& notes);
	QJsonArray commitPlain(InstrumentTrack* track, const std::vector<CapturedNote>& notes);
	QJsonArray commitLoopTakes(InstrumentTrack* track, const std::vector<CapturedNote>& notes);

	static MidiTakeRecorder* s_instance;
	Song* m_song;
	QTimer m_drainTimer;
	bool m_active = false;
	bool m_pending = false;
	std::vector<TrackSession> m_tracks;
	std::uint32_t m_wrapsAtStart = 0;
	std::uint32_t m_wrapsAtStop = 0;
	tick_t m_stopTick = 0;
	tick_t m_recordFrom = -1;
	bool m_loopEnabled = false;
	tick_t m_loopBegin = 0;
	tick_t m_loopEnd = 0;
	QJsonObject m_lastCommit;
};

} // namespace lmms

#endif // LMMS_MIDI_TAKE_RECORDER_H
