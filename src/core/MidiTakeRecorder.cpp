/*
 * MidiTakeRecorder.cpp - the main-thread half of arrangement MIDI take recording
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

#include "MidiTakeRecorder.h"

#include <algorithm>
#include <array>
#include <map>

#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "MidiEvent.h"
#include "MidiPort.h"
#include "Note.h"
#include "ProjectJournal.h"
#include "RecordOptions.h"
#include "Song.h"
#include "TakeLane.h"
#include "volume.h"

namespace lmms
{

namespace
{

//! The note's volume as the live input path computes it (InstrumentTrack::processInEvent ->
//! MidiEvent::volume), so a recorded note plays as loud as it sounded while it was played.
volume_t volumeOf(InstrumentTrack* track, int velocity)
{
	const MidiEvent on(MidiNoteOn, 0, 0, velocity);
	const int base = std::max(1, track->midiPort()->baseVelocity());
	return std::clamp<volume_t>(on.volume(base), MinVolume, MaxVolume);
}

QJsonObject takeReport(const InstrumentTrack* track, const MidiClip* clip, int pass, int notes)
{
	return QJsonObject{{QStringLiteral("track"), control::trackIdOf(track)},
		{QStringLiteral("clip"), control::clipIdOf(clip)},
		{QStringLiteral("position"), clip->startPosition().getTicks()},
		{QStringLiteral("length"), clip->length().getTicks()},
		{QStringLiteral("pass"), pass},
		{QStringLiteral("notes"), notes},
		{QStringLiteral("lane"), clip->hasTakeGroup() ? clip->laneIndex() : -1},
		{QStringLiteral("group"), clip->hasTakeGroup() ? clip->takeGroupId() : -1}};
}

//! Q050-094 Replace for a plain pass: the notes already on the track that a recorded note overlaps are
//! erased - notes only, never controller lanes or automation (recordoptions::replaceOverlapped).
void replaceUnder(const std::vector<MidiClip*>& earlier, const MidiClip* recorded)
{
	if (recordoptions::options().mode != recordoptions::Mode::Replace) { return; }
	const tick_t recordedStart = recorded->startPosition().getTicks();
	for (MidiClip* clip : earlier)
	{
		std::vector<int> before;
		for (const Note* note : clip->notes()) { before.push_back(note->id()); }
		// A clip's note plays at start + start offset + its own position (InstrumentTrack::play).
		const tick_t origin = clip->startPosition().getTicks() + clip->startTimeOffset().getTicks();
		for (const Note* note : recorded->notes())
		{
			Note local(note->length(), TimePos(recordedStart + note->pos().getTicks() - origin), note->key(), note->getVolume());
			recordoptions::replaceOverlapped(clip, local, before);
		}
	}
}

} // namespace


MidiTakeRecorder* MidiTakeRecorder::s_instance = nullptr;


MidiTakeRecorder::MidiTakeRecorder(Song* song) :
	QObject(song),
	m_song(song)
{
	s_instance = this;
	m_drainTimer.setInterval(DrainIntervalMs);
	connect(&m_drainTimer, &QTimer::timeout, this, &MidiTakeRecorder::drain);
	connect(song, &Song::playbackStateChanged, this, &MidiTakeRecorder::onPlaybackStateChanged);
	connect(song, &Song::stopped, this, &MidiTakeRecorder::onStopped);
}


MidiTakeRecorder::~MidiTakeRecorder()
{
	if (s_instance == this) { s_instance = nullptr; }
}


void MidiTakeRecorder::onPlaybackStateChanged()
{
	if (m_song->isPlaying()) { begin(); }
}


void MidiTakeRecorder::onStopped()
{
	finish();
}


void MidiTakeRecorder::begin()
{
	if (m_active || !m_song->isRecording() || m_song->playMode() != Song::PlayMode::Song) { return; }
	m_tracks.clear();
	for (Track* track : m_song->tracks())
	{
		auto* instrument = dynamic_cast<InstrumentTrack*>(track);
		if (instrument != nullptr && instrument->midiTakeCapture().isArmed()) { m_tracks.push_back({instrument, {}}); }
	}
	if (m_tracks.empty()) { return; }
	// The pending capture of an earlier pass that could not be committed is replaced by this one.
	m_pending = false;
	const Timeline& timeline = m_song->getTimeline(Song::PlayMode::Song);
	m_loopEnabled = timeline.loopEnabled();
	m_loopBegin = timeline.loopBegin().getTicks();
	m_loopEnd = timeline.loopEnd().getTicks();
	m_recordFrom = -1;
	m_wrapsAtStart = static_cast<std::uint32_t>(m_song->loopWraps());
	// Runs inside the transport start's model lock, before any period of this pass rendered: the first
	// events are stamped with the start, not with where the last pass stopped.
	MidiTakeCapture::publishPosition(m_song->getPlayPos(Song::PlayMode::Song).getTicks(), m_song->loopWraps());
	for (TrackSession& session : m_tracks) { session.track->midiTakeCapture().startCapture(); }
	m_active = true;
	m_drainTimer.start();
}


void MidiTakeRecorder::setRecordFrom(tick_t tick)
{
	if (m_active) { m_recordFrom = tick; }
}


void MidiTakeRecorder::drain()
{
	for (TrackSession& session : m_tracks)
	{
		if (session.track != nullptr) { session.track->midiTakeCapture().drain(&session.events); }
	}
}


void MidiTakeRecorder::finish()
{
	if (!m_active) { return; }
	m_wrapsAtStop = MidiTakeCapture::publishedWraps();
	m_stopTick = MidiTakeCapture::publishedTick();
	for (TrackSession& session : m_tracks)
	{
		if (session.track != nullptr) { session.track->midiTakeCapture().stopCapture(); }
	}
	drain();
	m_active = false;
	m_drainTimer.stop();
	m_pending = !pendingTracks().empty();
	if (!m_pending || m_song->isLoadingProject()) { return; }
	// The take is made by the registered command whoever stopped the transport - the header's Stop, the
	// space bar or an agent's transport.stop - so it is one recorded, undoable edit. Without a ready control
	// surface (a bare engine) the same body runs here under one checkpoint.
	ControlRegistry* registry = ControlRegistry::instance();
	const QString command = QLatin1String(CommitCommand);
	if (ControlRegistry::isEngineReady() && registry->hasCommand(command))
	{
		registry->invoke(command, {});  // a refusal leaves the capture pending (status 'pending'), not lost
		return;
	}
	QVector<JournallingObject*> objects;
	for (InstrumentTrack* track : pendingTracks()) { objects.append(track); }
	Engine::projectJournal()->addJournalCheckPoint(objects);
	commitPending();
}


bool MidiTakeRecorder::loopRecording() const
{
	const std::uint32_t wraps = m_active ? MidiTakeCapture::publishedWraps() : m_wrapsAtStop;
	return m_loopEnabled && loopLength() > 0 && wraps != m_wrapsAtStart;
}


std::vector<MidiTakeRecorder::CapturedNote> MidiTakeRecorder::notesOf(const std::vector<MidiTakeEvent>& events) const
{
	const bool loop = loopRecording();
	std::vector<CapturedNote> notes;
	std::array<int, 128> open;
	open.fill(-1);
	for (const MidiTakeEvent& e : events)
	{
		if (beforeRecordPoint(e, loop)) { continue; }
		const int pass = passOf(e, loop);
		const tick_t at = unfolded(e, pass, loop);
		int& slot = open[e.key];
		if (slot >= 0) { notes[slot].length = std::max<tick_t>(1, at - notes[slot].position); }
		slot = e.on != 0 ? static_cast<int>(notes.size()) : -1;
		if (e.on != 0) { notes.push_back(CapturedNote{pass, at, 1, e.key, e.velocity}); }
	}
	closeHeldNotes(&notes, open, loop);
	if (loop) { foldIntoLoop(&notes); }
	return notes;
}


bool MidiTakeRecorder::beforeRecordPoint(const MidiTakeEvent& event, bool loop) const
{
	// A count-in or pre-roll is heard, not recorded (the audio punch gate opens at the same tick). In a loop
	// recording only the first pass has one: a later pass at the same loop ticks is recorded.
	if (m_recordFrom < 0 || event.tick >= m_recordFrom) { return false; }
	return !loop || event.wraps == m_wrapsAtStart;
}


int MidiTakeRecorder::passOf(const MidiTakeEvent& event, bool loop) const
{
	return loop ? static_cast<int>(event.wraps - m_wrapsAtStart) : 0;
}


tick_t MidiTakeRecorder::unfolded(const MidiTakeEvent& event, int pass, bool loop) const
{
	// A loop recording is unfolded into one timeline (pass x loop length + offset in the loop), so a note
	// held across the wrap still pairs with its note-off; foldIntoLoop() then cuts it at its pass's loop end.
	return loop ? pass * loopLength() + (event.tick - m_loopBegin) : event.tick;
}


void MidiTakeRecorder::closeHeldNotes(std::vector<CapturedNote>* notes, const std::array<int, 128>& open, bool loop) const
{
	for (const int slot : open)
	{
		if (slot < 0) { continue; }
		// Still held at Stop: the note lasts until its pass ended.
		CapturedNote& note = (*notes)[slot];
		const tick_t end = loop ? (note.pass + 1) * loopLength() : m_stopTick;
		note.length = std::max<tick_t>(1, end - note.position);
	}
}


void MidiTakeRecorder::foldIntoLoop(std::vector<CapturedNote>* notes) const
{
	const tick_t length = loopLength();
	for (CapturedNote& note : *notes)
	{
		const tick_t passStart = note.pass * length;
		note.length = std::max<tick_t>(1, std::min(note.length, passStart + length - note.position));
		note.position = m_loopBegin + (note.position - passStart);
	}
}


std::vector<InstrumentTrack*> MidiTakeRecorder::pendingTracks() const
{
	std::vector<InstrumentTrack*> tracks;
	for (const TrackSession& session : m_tracks)
	{
		if (session.track != nullptr && !notesOf(session.events).empty()) { tracks.push_back(session.track); }
	}
	return tracks;
}


QJsonObject MidiTakeRecorder::commitPending()
{
	QJsonArray takes;
	int notes = 0;
	qint64 dropped = 0;
	const bool loop = loopRecording();
	for (TrackSession& session : m_tracks)
	{
		if (session.track == nullptr) { continue; }
		const std::vector<CapturedNote> captured = notesOf(session.events);
		notes += static_cast<int>(captured.size());
		dropped += static_cast<qint64>(session.track->midiTakeCapture().dropped());
		for (const QJsonValue& take : commitTrack(session.track, captured)) { takes.append(take); }
		session.events.clear();
	}
	m_pending = false;
	if (!takes.isEmpty()) { m_song->setModified(); }
	m_lastCommit = QJsonObject{{QStringLiteral("loop_recording"), loop}, {QStringLiteral("takes"), takes},
		{QStringLiteral("notes"), notes}, {QStringLiteral("dropped_events"), dropped}};
	return m_lastCommit;
}


QJsonArray MidiTakeRecorder::commitTrack(InstrumentTrack* track, const std::vector<CapturedNote>& notes)
{
	if (notes.empty()) { return {}; }
	const auto guard = Engine::audioEngine()->requestChangesGuard();
	return loopRecording() ? commitLoopTakes(track, notes) : commitPlain(track, notes);
}


QJsonArray MidiTakeRecorder::commitPlain(InstrumentTrack* track, const std::vector<CapturedNote>& notes)
{
	std::vector<MidiClip*> earlier;
	for (Clip* clip : track->getClips())
	{
		if (auto* midi = dynamic_cast<MidiClip*>(clip)) { earlier.push_back(midi); }
	}
	const tick_t bar = TimePos::ticksPerBar();
	tick_t first = notes.front().position;
	for (const CapturedNote& note : notes) { first = std::min(first, note.position); }
	const tick_t start = std::max<tick_t>(0, first / bar * bar);
	auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(start)));
	if (clip == nullptr) { return {}; }
	clip->movePosition(TimePos(start));
	for (const CapturedNote& note : notes)
	{
		clip->addNote(Note(TimePos(note.length), TimePos(note.position - start), note.key,
			volumeOf(track, note.velocity)), false);
	}
	clip->updateLength();
	replaceUnder(earlier, clip);
	return QJsonArray{takeReport(track, clip, 0, static_cast<int>(notes.size()))};
}


int MidiTakeRecorder::loopTakeGroup(InstrumentTrack* track) const
{
	// A loop recording over the same region joins that region's take group, so a take the musician chose
	// there (comp.take_select) stays active through the new passes - the active-take model.
	for (Clip* clip : track->getClips())
	{
		if (dynamic_cast<MidiClip*>(clip) != nullptr && clip->hasTakeGroup()
			&& clip->startPosition().getTicks() == m_loopBegin && clip->length().getTicks() == loopLength())
		{
			return clip->takeGroupId();
		}
	}
	return -1;
}


MidiClip* MidiTakeRecorder::loopTakeClip(InstrumentTrack* track, const std::vector<const CapturedNote*>& notes)
{
	auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(m_loopBegin)));
	if (clip == nullptr) { return nullptr; }
	clip->movePosition(TimePos(m_loopBegin));
	for (const CapturedNote* note : notes)
	{
		clip->addNote(Note(TimePos(note->length), TimePos(note->position - m_loopBegin), note->key,
			volumeOf(track, note->velocity)), false);
	}
	clip->setAutoResize(false);  // a take spans the loop region, whatever its notes span
	clip->changeLength(TimePos(loopLength()));
	return clip;
}


QJsonArray MidiTakeRecorder::commitLoopTakes(InstrumentTrack* track, const std::vector<CapturedNote>& notes)
{
	std::map<int, std::vector<const CapturedNote*>> passes;
	for (const CapturedNote& note : notes) { passes[note.pass].push_back(&note); }
	int group = loopTakeGroup(track);
	TakeLaneModel& model = track->takeLanes();
	std::vector<std::pair<MidiClip*, int>> made;
	for (const auto& [pass, passNotes] : passes)
	{
		MidiClip* clip = loopTakeClip(track, passNotes);
		if (clip == nullptr) { continue; }
		const int lane = model.addLane();
		if (lane >= 0)
		{
			control::assignTakeLane(clip, lane);
			if (group < 0) { group = clip->id(); }
			clip->setTakeGroupId(group);
			model.recordTake(group, lane, track->getClips(), false);
		}
		made.emplace_back(clip, pass);
	}
	QJsonArray takes;
	for (const auto& [clip, pass] : made)
	{
		QJsonObject take = takeReport(track, clip, pass, static_cast<int>(passes[pass].size()));
		take.insert(QStringLiteral("active"), !clip->hasTakeGroup() || model.activeTakeLane(group) == clip->laneIndex());
		takes.append(take);
	}
	return takes;
}


QJsonArray MidiTakeRecorder::sessionTracks() const
{
	QJsonArray ids;
	for (const TrackSession& session : m_tracks)
	{
		if (session.track != nullptr) { ids.append(control::trackIdOf(session.track)); }
	}
	return ids;
}


QJsonObject MidiTakeRecorder::status() const
{
	QJsonArray armed;
	for (Track* track : m_song->tracks())
	{
		auto* instrument = dynamic_cast<InstrumentTrack*>(track);
		if (instrument == nullptr || !instrument->midiTakeCapture().isArmed()) { continue; }
		std::size_t queued = 0;
		for (const TrackSession& session : m_tracks)
		{
			if (session.track == instrument) { queued = session.events.size(); }
		}
		const MidiTakeCapture& capture = instrument->midiTakeCapture();
		armed.append(QJsonObject{{QStringLiteral("track"), control::trackIdOf(instrument)},
			{QStringLiteral("name"), instrument->name()},
			{QStringLiteral("capturing"), capture.isCapturing()},
			{QStringLiteral("events"), static_cast<qint64>(queued)},
			{QStringLiteral("dropped_events"), static_cast<qint64>(capture.dropped())}});
	}
	return QJsonObject{{QStringLiteral("recording"), m_active}, {QStringLiteral("pending"), m_pending},
		{QStringLiteral("loop_recording"), (m_active || m_pending) && loopRecording()},
		{QStringLiteral("capacity_events"), static_cast<qint64>(MidiTakeCapture::CapacityEvents)},
		{QStringLiteral("armed_tracks"), armed}, {QStringLiteral("session_tracks"), sessionTracks()},
		{QStringLiteral("last_commit"), m_lastCommit}};
}

} // namespace lmms
