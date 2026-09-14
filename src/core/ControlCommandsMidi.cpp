/*
 * ControlCommandsMidi.cpp - the retrospective MIDI capture commands
 *                           (midi.retro_capture_*, SPEC A11-A16, owner item 14)
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
 */

#include <algorithm>
#include <cstddef>
#include <vector>

#include <QElapsedTimer>
#include <QJsonObject>
#include <QString>

#include "AudioEngine.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClient.h"
#include "MidiClip.h"
#include "RetroMidiCapture.h"
#include "RetroMidiCaptureSettings.h"
#include "RetroMidiClipWriter.h"
#include "RetroMidiRing.h"
#include "Song.h"
#include "TimePos.h"
#include "Track.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace
{

//! How long the consumer waits for the MIDI thread to acknowledge a snapshot
//! request before it reads the window anyway. copyOut() is publication-sequence
//! guarded (include/RetroMidiRing.h), so an acknowledgement that arrives late -
//! or never, because an idle MIDI thread has nothing to push - cannot tear the
//! copy. The wait is the document's section 3.5 preference for a quiesced
//! writer, with an upper bound: the control handler runs on the UI thread.
constexpr int QuiesceTimeoutMs = 5;

//! The live capture, or nullptr when this instance has no MIDI client at all.
RetroMidiCapture* liveCapture()
{
	AudioEngine* engine = Engine::audioEngine();
	MidiClient* client = engine != nullptr ? engine->midiClient() : nullptr;
	return client != nullptr ? &client->retroCapture() : nullptr;
}

QString liveClientName()
{
	AudioEngine* engine = Engine::audioEngine();
	return engine != nullptr ? engine->midiClientName() : QString();
}

//! One consistent copy of the ring's retained window, oldest first.
struct WindowSnapshot
{
	std::vector<RetroMidiEvent> events;
	bool quiesced = false;    //!< the producer acknowledged before the copy
	std::size_t refused = 0;  //!< events retained but not readable at this instant
};

//! Take the snapshot without ever blocking the MIDI thread: the producer is
//! asked to stand still, waited for briefly, and released again even when the
//! wait timed out.
WindowSnapshot snapshotWindow(RetroMidiCapture& capture)
{
	RetroMidiRing& ring = capture.ring();
	WindowSnapshot snapshot;
	snapshot.events.resize(ring.bufferedCount());

	ring.beginSnapshot();
	QElapsedTimer timer;
	timer.start();
	while (!ring.writerIdle() && timer.elapsed() < QuiesceTimeoutMs)
	{
		// Bounded spin: the producer acknowledges inside its next push().
	}
	snapshot.quiesced = ring.writerIdle();
	const std::size_t copied = ring.copyOut(snapshot.events.data(), snapshot.events.size());
	ring.endSnapshot();

	snapshot.events.resize(copied);
	if (copied == 0 && ring.bufferedCount() > 0)
	{
		// Retained events that no consistent window could be read for. Reported
		// rather than silently turned into "nothing was captured".
		snapshot.refused = ring.bufferedCount();
	}
	return snapshot;
}

//! The earliest and latest tick in \a events; both \a start and \a end receive
//! the same tick for a one-event window.
void windowBounds(const std::vector<RetroMidiEvent>& events, tick_t* start, tick_t* end)
{
	*start = static_cast<tick_t>(events.front().tick);
	*end = *start;
	for (const RetroMidiEvent& event : events)
	{
		*start = std::min(*start, static_cast<tick_t>(event.tick));
		*end = std::max(*end, static_cast<tick_t>(event.tick));
	}
}

//! Ordinal of \p clip in the arrangement, or -1 when it is not in the song.
//! The same enumeration clip-<n> uses (and the same helper shape as the one in
//! src/core/ControlCommandsClip.cpp, which is file-local there).
int ordinalOf(const Clip* clip)
{
	for (const ClipRef& ref : enumerateClips())
	{
		if (ref.clip == clip) { return ref.ordinal; }
	}
	return -1;
}

//! The line every refusal about a missing capture shares.
ControlResult noCapture()
{
	return ControlResult::failure(ControlErrorKind::Refused,
		QStringLiteral("this instance has no MIDI client to capture from: the capture "
			"object is a member of the client (src/core/MidiClient.h)"));
}

QJsonObject retroCaptureSchema()
{
	return objectSchema({
		{QStringLiteral("armed"), booleanProperty()},
		{QStringLiteral("changed"), booleanProperty()},
		{QStringLiteral("client"), stringProperty()},
		{QStringLiteral("persisted"), booleanProperty()},
		{QStringLiteral("capacity_events"), integerProperty(0, MaxSongLength)},
	});
}

void registerMidiRetroCaptureArm(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("midi.retro_capture_arm");
	cmd.group = QStringLiteral("midi");
	cmd.verb = QStringLiteral("retro_capture_arm");
	cmd.description = QStringLiteral("Arm or disarm retrospective MIDI capture - the mode the "
		"Edit > Arm MIDI Capture menu item drives (SPEC A11: the menu action declares this "
		"command id and its slot invokes this command, so the user and an agent arm the mode "
		"through one implementation). With no 'armed' argument the mode is ARMED; 'armed': "
		"false is its disarm. While armed, the MIDI input threads keep the most recent events "
		"in one bounded ring per open client, and midi.retro_capture_to_clip writes that window "
		"into a clip; nothing is recorded while disarmed. Mode/engine state, not project state, "
		"so no transaction is recorded and control.undo has nothing to reverse. The state is "
		"persisted to the config file's midi/retrocapture key (not the project file) and "
		"re-applied by the GUI startup path, not by the capture object's constructor, which "
		"runs before main(); 'persisted' reports what was stored.");
	// A13: no display, device or human is required - an offscreen instance arms the mode
	// exactly like a visible one, which is why this command is swept headlessly instead of
	// being allowlisted (the midi.learn_toggle precedent, which declares no `requires`
	// either). The capture is a member of the MIDI client, which exists headless: the
	// client falls back to the dummy one when no backend opens (src/core/AudioEngine.cpp).
	cmd.argsSchema = objectSchema({{QStringLiteral("armed"), booleanProperty()}});
	cmd.resultSchema = retroCaptureSchema();
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject& args) {
		RetroMidiCapture* capture = liveCapture();
		if (capture == nullptr) { return noCapture(); }

		const bool wasArmed = capture->isArmed();
		// No argument means "arm": the deterministic reading of a command named
		// retro_capture_ARM. A caller that wants the other direction says so.
		const bool wanted = args.contains(QStringLiteral("armed"))
			? args.value(QStringLiteral("armed")).toBool() : true;
		capture->arm(wanted);
		// The config file is written only when the mode actually moved, so
		// re-arming an armed capture never rewrites the user's file.
		if (wanted != wasArmed) { setRetroCapturePersistedArmed(wanted); }

		QJsonObject result;
		result.insert(QStringLiteral("armed"), capture->isArmed());
		result.insert(QStringLiteral("changed"), capture->isArmed() != wasArmed);
		result.insert(QStringLiteral("client"), liveClientName());
		result.insert(QStringLiteral("persisted"), retroCapturePersistedArmed());
		result.insert(QStringLiteral("capacity_events"),
			static_cast<int>(capture->ring().capacity()));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

void registerMidiRetroCaptureStatus(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("midi.retro_capture_status");
	cmd.group = QStringLiteral("midi");
	cmd.verb = QStringLiteral("retro_capture_status");
	cmd.description = QStringLiteral("Read-only: whether retrospective MIDI capture is armed, "
		"which MIDI client the instance is running, how many events its ring retains out of "
		"its capacity, how many events fell out of the window (overwritten) and how long the "
		"window is in ticks and in seconds (derived from the tick span and the song tempo). "
		"'refused_snapshots' and 'paused_dropped' are the ring's own loss counters - both 0 in "
		"practice - and 'quiesced' says whether the MIDI thread acknowledged this read. This "
		"is how an agent tells whether a capture is armed without a GUI.");
	cmd.argsSchema = objectSchema({});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("armed"), booleanProperty()},
		{QStringLiteral("client"), stringProperty()},
		{QStringLiteral("persisted"), booleanProperty()},
		{QStringLiteral("quiesced"), booleanProperty()},
		{QStringLiteral("events_buffered"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("capacity_events"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("overwritten"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("paused_dropped"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("refused_snapshots"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("ticks_span"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("seconds_span"), numberProperty()},
	});
	cmd.mutating = false;
	cmd.handler = [](const QJsonObject&) {
		RetroMidiCapture* capture = liveCapture();
		if (capture == nullptr) { return noCapture(); }

		const WindowSnapshot snapshot = snapshotWindow(*capture);
		tick_t oldest = 0;
		tick_t newest = 0;
		if (!snapshot.events.empty())
		{
			windowBounds(snapshot.events, &oldest, &newest);
		}
		const tick_t span = snapshot.events.empty() ? 0 : newest - oldest;
		// A span read against the CURRENT tempo: the honest approximation, because
		// the window can straddle a tempo change and the ring stores ticks, not
		// seconds (docs/MIDI-RETRO-CAPTURE.md 3.5).
		Song* song = Engine::getSong();
		const bpm_t tempo = song != nullptr ? song->getTempo() : DefaultTempo;

		QJsonObject result;
		result.insert(QStringLiteral("armed"), capture->isArmed());
		result.insert(QStringLiteral("client"), liveClientName());
		result.insert(QStringLiteral("persisted"), retroCapturePersistedArmed());
		result.insert(QStringLiteral("quiesced"), snapshot.quiesced);
		result.insert(QStringLiteral("events_buffered"),
			static_cast<int>(snapshot.events.size()));
		result.insert(QStringLiteral("capacity_events"),
			static_cast<int>(capture->ring().capacity()));
		result.insert(QStringLiteral("overwritten"),
			static_cast<double>(capture->ring().overwrittenCount()));
		result.insert(QStringLiteral("paused_dropped"),
			static_cast<double>(capture->ring().pausedDropCount()));
		result.insert(QStringLiteral("refused_snapshots"),
			static_cast<double>(capture->ring().refusedSnapshots()));
		result.insert(QStringLiteral("ticks_span"), span);
		result.insert(QStringLiteral("seconds_span"),
			TimePos::ticksToMilliseconds(span, tempo) / 1000.0);
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

//! The track a capture is written to: the named one, else the track of the clip
//! selected in the control surface, else the song's first instrument track.
//! nullptr with \a error filled when none of the three answers.
Track* captureTargetTrack(const QString& trackId, ControlResult* error)
{
	if (!trackId.isEmpty()) { return resolveTrack(trackId, error); }

	const QString selected = selectedClipId();
	if (!selected.isEmpty())
	{
		ClipRef ref;
		ControlResult selectionError;
		if (resolveClip(selected, &ref, &selectionError)) { return ref.track; }
	}

	for (Track* songTrack : Engine::getSong()->tracks())
	{
		if (qobject_cast<InstrumentTrack*>(songTrack) != nullptr) { return songTrack; }
	}

	*error = ControlResult::failure(ControlErrorKind::NotFound,
		QStringLiteral("no 'track' given, no clip is selected and the song has no "
			"instrument track to capture into"));
	return nullptr;
}

//! The new clip's length: the caller's, or the window rounded up to whole bars
//! (a clip the length of a two-second window is a clip no one can grab and loop).
tick_t clipLengthFor(const QJsonObject& args, tick_t windowStart, tick_t windowEnd)
{
	if (args.contains(QStringLiteral("length")))
	{
		const tick_t requested =
			static_cast<tick_t>(args.value(QStringLiteral("length")).toDouble());
		if (requested > 0) { return requested; }
	}
	const tick_t bar = std::max<tick_t>(1, Engine::getSong() != nullptr
		? Engine::getSong()->ticksPerBar() : DefaultTicksPerBar);
	return ((windowEnd - windowStart) / bar + 1) * bar;
}

//! The refusal an empty (or unreadable) window earns. A distinct helper so the
//! handler does not carry the two cases.
ControlResult emptyWindowFailure(const WindowSnapshot& snapshot)
{
	if (snapshot.refused > 0)
	{
		return ControlResult::failure(ControlErrorKind::Busy,
			QStringLiteral("the capture window could not be read consistently: %1 events "
				"are retained but the MIDI thread never stopped writing long enough")
				.arg(static_cast<qulonglong>(snapshot.refused)));
	}
	return ControlResult::failure(ControlErrorKind::NotFound,
		QStringLiteral("the capture window is empty: nothing has been recorded. Arm the "
			"capture with midi.retro_capture_arm and play"));
}

//! The result object of midi.retro_capture_to_clip.
QJsonObject capturedClipState(MidiClip* clip, Track* track, const QString& clipText,
	const RetroMidiClipWrite& written, int windowEvents, std::uint64_t overwritten)
{
	QJsonObject result;
	result.insert(QStringLiteral("clip"), clipText);
	result.insert(QStringLiteral("track"), trackIdOf(track));
	result.insert(QStringLiteral("position"), clip->startPosition().getTicks());
	result.insert(QStringLiteral("length"), clip->length().getTicks());
	result.insert(QStringLiteral("events"), windowEvents);
	result.insert(QStringLiteral("events_written"), written.notes);
	result.insert(QStringLiteral("unmatched_ons"), written.unmatchedOns);
	result.insert(QStringLiteral("unmatched_offs"), written.unmatchedOffs);
	result.insert(QStringLiteral("skipped"), written.skipped);
	result.insert(QStringLiteral("events_overwritten"), static_cast<double>(overwritten));
	result.insert(QStringLiteral("window_start"), written.windowStart);
	result.insert(QStringLiteral("window_end"), written.windowEnd);
	return result;
}

void registerMidiRetroCaptureToClip(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("midi.retro_capture_to_clip");
	cmd.group = QStringLiteral("midi");
	cmd.verb = QStringLiteral("retro_capture_to_clip");
	cmd.description = QStringLiteral("Write the events retrospective MIDI capture has retained "
		"into a NEW MIDI clip on 'track', and return its clip-<n> id. Without 'track', the "
		"track of the clip selected in the control surface is used, and with nothing selected "
		"the song's first instrument track. The clip starts at the window's first tick and is "
		"'length' ticks long, or - without it - the window rounded up to whole bars. The note "
		"matcher closes an unmatched note-on at the window's end and reports it in "
		"'unmatched_ons': the capture never truncates silently. One journal checkpoint (the "
		"Track's, like clip.add) over the new clip, so ONE control.undo removes the whole "
		"capture; the inverse is clip.delete. Reversible through the ProjectJournal (Track "
		"checkpoint).");
	// A13: the write itself needs no display, device or human - it creates a clip
	// and fills it through MidiClip::addNote(fresh, /*quant_pos=*/false), which does
	// not consult the piano roll - so no `requires` is declared and the headless
	// sweep exercises it.
	cmd.argsSchema = objectSchema({
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("length"), integerProperty(1, MaxSongLength)},
	});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("clip"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("position"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("length"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("events"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("events_written"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("unmatched_ons"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("unmatched_offs"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("skipped"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("events_overwritten"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("window_start"), integerProperty(0, MaxSongLength)},
		{QStringLiteral("window_end"), integerProperty(0, MaxSongLength)},
	});
	// SPEC A16: the clip list is part of the Track's serialized state, which is
	// why the checkpoint is the Track's - the clip.add shape.
	cmd.mutating = true;
	cmd.handler = [](const QJsonObject& args) {
		RetroMidiCapture* capture = liveCapture();
		if (capture == nullptr) { return noCapture(); }

		ControlResult error;
		Track* track = captureTargetTrack(args.value(QStringLiteral("track")).toString(), &error);
		if (track == nullptr) { return error; }
		InstrumentTrack* instrumentTrack = qobject_cast<InstrumentTrack*>(track);
		if (instrumentTrack == nullptr)
		{
			return ControlResult::failure(ControlErrorKind::Refused,
				QStringLiteral("track '%1' cannot hold MIDI notes: a capture is written to an "
					"instrument track").arg(trackIdOf(track)));
		}

		const WindowSnapshot snapshot = snapshotWindow(*capture);
		if (snapshot.events.empty()) { return emptyWindowFailure(snapshot); }

		tick_t windowStart = 0;
		tick_t windowEnd = 0;
		windowBounds(snapshot.events, &windowStart, &windowEnd);
		windowStart = std::max<tick_t>(0, windowStart);
		windowEnd = std::max(windowStart, windowEnd);
		const tick_t length = clipLengthFor(args, windowStart, windowEnd);

		const int clipCount = track->numOfClips();
		// TrackContentWidget::mousePressEvent checkpoints the track and then calls
		// Track::createClip(); do the same, so the journal owns a real inverse.
		track->addJournalCheckPoint();
		MidiClip* clip = static_cast<MidiClip*>(instrumentTrack->createClip(TimePos(windowStart)));
		const RetroMidiClipWrite written = writeWindowToClip(clip, snapshot.events.data(),
			snapshot.events.size(), windowStart);
		// The length is set AFTER the notes: MidiClip::addNote() calls
		// updateLength(), which would shrink a fresh auto-resizing clip to its
		// notes. An explicit length is a manually resized clip, exactly as
		// clip.resize leaves it.
		clip->changeLength(TimePos(length));
		clip->setAutoResize(false);
		clip->dataChanged();

		const int ordinal = ordinalOf(clip);
		if (ordinal < 0)
		{
			return ControlResult::failure(ControlErrorKind::NotFound,
				QStringLiteral("the new clip is not in the song's arrangement"));
		}
		const QString clipText = clipId(ordinal);

		QJsonObject result = capturedClipState(clip, track, clipText, written,
			static_cast<int>(snapshot.events.size()), capture->ring().overwrittenCount());
		result.insert(QStringLiteral("__transaction"),
			transactionPayload(
				QJsonObject{{QStringLiteral("track"), trackIdOf(track)},
					{QStringLiteral("clip_count"), clipCount}},
				QStringLiteral("clip.delete"),
				QJsonObject{{QStringLiteral("clip"), clipText}},
				true,
				QStringLiteral("ProjectJournal (Track checkpoint: Track::restoreState re-loads "
					"the track's serialized clips, which is how the GUI's own clip add/delete "
					"paths reverse themselves)")));
		return ControlResult::success(result);
	};
	registry.registerCommand(cmd);
}

} // namespace


void registerMidiRetroCaptureCommands(ControlRegistry& registry)
{
	registerMidiRetroCaptureArm(registry);
	registerMidiRetroCaptureStatus(registry);
	registerMidiRetroCaptureToClip(registry);
}


} // namespace lmms
