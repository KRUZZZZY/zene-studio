/*
 * ControlEditSupport.cpp - shared helpers of the notes / clips / tracks command
 *                          groups (SPEC-zene-studio.md A11-A16).
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

#include "ControlEdit.h"

#include "ControlVocabulary.h"

#include <algorithm>

#include <QDomDocument>
#include <QDomElement>

#include "Clip.h"
#include "ControlRegistry.h"
#include "DataFile.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"
#include "SongEditor.h"
#include "TrackContainer.h"
#include "TrackView.h"

namespace lmms
{

using namespace control;  // the shared vocabulary lives in ControlVocabulary.h

namespace control
{

// ---------------------------------------------------------------------------
// schemas
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// track types (the id formatters moved to ControlVocabulary.cpp, 2026-09-12)
// ---------------------------------------------------------------------------

QString trackTypeNameOf(Track::Type type)
{
	switch (type)
	{
		case Track::Type::Instrument: return QStringLiteral("instrument");
		case Track::Type::Pattern: return QStringLiteral("pattern");
		case Track::Type::Sample: return QStringLiteral("sample");
		case Track::Type::Event: return QStringLiteral("event");
		case Track::Type::Video: return QStringLiteral("video");
		case Track::Type::Automation: return QStringLiteral("automation");
		case Track::Type::HiddenAutomation: return QStringLiteral("hidden_automation");
		case Track::Type::Count: break;
	}
	return QStringLiteral("unknown");
}

namespace
{

//! One entry of a track's clips, in the order clip-<n> enumerates them.
struct ClipOrder
{
	tick_t position = 0;
	int indexInTrack = 0;
};

QVector<ClipOrder> clipOrderOf(Track* track)
{
	QVector<ClipOrder> order;
	for (int c = 0; c < track->numOfClips(); ++c)
	{
		order.append(ClipOrder{track->getClip(c)->startPosition().getTicks(), c});
	}
	// Arrangement order: position first, the track's own order on a tie.
	std::stable_sort(order.begin(), order.end(),
		[](const ClipOrder& a, const ClipOrder& b) { return a.position < b.position; });
	return order;
}

} // namespace

QVector<ClipRef> enumerateClips()
{
	QVector<ClipRef> refs;
	const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
	for (int t = 0; t < static_cast<int>(tracks.size()); ++t)
	{
		Track* track = tracks[t];
		for (const ClipOrder& entry : clipOrderOf(track))
		{
			ClipRef ref;
			ref.clip = track->getClip(entry.indexInTrack);
			ref.track = track;
			ref.trackIndex = t;
			ref.indexInTrack = entry.indexInTrack;
			ref.ordinal = refs.size();
			refs.append(ref);
		}
	}
	return refs;
}

// ---------------------------------------------------------------------------
// resolution
// ---------------------------------------------------------------------------

Track* resolveTrack(const QString& id, ControlResult* error)
{
	const int wanted = idToIndex(id, QStringLiteral("trk-"));
	if (wanted < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a track id of the form trk-<n>").arg(id));
		return nullptr;
	}
	// Resolve the OBJECT the id names. Since SPEC-stable-ids.md the number is the
	// track's creation-assigned id (Track::id()), not its position in the
	// container, so adding or removing a sibling cannot move it (the defect of
	// section 1.2(a)). There is deliberately NO positional fallback: every Track
	// carries an id from construction, so a fallback could only ever resolve a
	// stale position - exactly the silent mis-resolution this change removes.
	// A well-formed id naming no live track is the typed not_found below.
	const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
	for (Track* track : tracks)
	{
		if (track->id() == wanted) { return track; }
	}
	*error = ControlResult::failure(ControlErrorKind::NotFound,
		QStringLiteral("no track %1 (the song has %2)").arg(id).arg(tracks.size()));
	return nullptr;
}

bool resolveClip(const QString& id, ClipRef* ref, ControlResult* error)
{
	const int ordinal = idToIndex(id, QStringLiteral("clip-"));
	if (ordinal < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a clip id of the form clip-<n>").arg(id));
		return false;
	}
	const QVector<ClipRef> refs = enumerateClips();
	if (ordinal >= refs.size())
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no clip %1 (the song has %2)").arg(id).arg(refs.size()));
		return false;
	}
	*ref = refs[ordinal];
	return true;
}

MidiClip* resolveMidiClip(const QString& id, ClipRef* ref, ControlResult* error)
{
	if (!resolveClip(id, ref, error)) { return nullptr; }
	auto* midiClip = dynamic_cast<MidiClip*>(ref->clip);
	if (midiClip == nullptr)
	{
		// Honest refusal, not an empty success: only a MidiClip owns a note list.
		// A PatternClip (pattern track) or a SampleClip is a real clip with a real
		// id, but there is nothing in it for the piano roll to edit.
		*error = ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("%1 has no note list: it sits on a %2 track, and the piano roll "
				"edits MIDI clips (an instrument track's clip)").arg(id,
				trackTypeNameOf(ref->track->type())));
		return nullptr;
	}
	return midiClip;
}

Note* resolveNote(MidiClip* clip, const QString& id, int* index, ControlResult* error)
{
	const int i = idToIndex(id, QStringLiteral("note-"));
	if (i < 0)
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'%1' is not a note id of the form note-<n>").arg(id));
		return nullptr;
	}
	if (i >= static_cast<int>(clip->notes().size()))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no note %1 (the clip has %2)").arg(id).arg(clip->notes().size()));
		return nullptr;
	}
	*index = i;
	return clip->notes()[i];
}

// ---------------------------------------------------------------------------
// selection
// ---------------------------------------------------------------------------

namespace
{

QString s_selectedClip;
QString s_noteSelectionClip;
QVector<int> s_selectedNotes;

} // namespace

void selectClip(const QString& id)
{
	s_selectedClip = id;
}

QString selectedClipId()
{
	return s_selectedClip;
}

void selectNotes(const QString& clip, int noteCount, const QVector<int>& indices)
{
	QVector<int> kept;
	for (int index : indices)
	{
		if (index >= 0 && index < noteCount) { kept.append(index); }
	}
	std::sort(kept.begin(), kept.end());
	kept.erase(std::unique(kept.begin(), kept.end()), kept.end());
	s_noteSelectionClip = clip;
	s_selectedNotes = kept;
}

bool noteSelected(const QString& clip, int index)
{
	return s_noteSelectionClip == clip && s_selectedNotes.contains(index);
}

QVector<int> selectedNoteIndices(const QString& clip)
{
	return s_noteSelectionClip == clip ? s_selectedNotes : QVector<int>();
}

// ---------------------------------------------------------------------------
// state
// ---------------------------------------------------------------------------

QJsonObject clipState(const ClipRef& ref)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("id"), clipId(ref.ordinal));
	entry.insert(QStringLiteral("track"), trackIdOf(ref.track));
	entry.insert(QStringLiteral("index_in_track"), ref.indexInTrack);
	entry.insert(QStringLiteral("name"), ref.clip->name());
	entry.insert(QStringLiteral("position"), ref.clip->startPosition().getTicks());
	entry.insert(QStringLiteral("length"), ref.clip->length().getTicks());
	entry.insert(QStringLiteral("muted"), ref.clip->isMuted());
	entry.insert(QStringLiteral("auto_resize"), ref.clip->getAutoResize());
	// null rather than 0 for a clip that carries no notes at all: "no note list"
	// and "an empty note list" are different facts about the model.
	auto* midiClip = dynamic_cast<MidiClip*>(ref.clip);
	entry.insert(QStringLiteral("note_count"),
		midiClip == nullptr ? QJsonValue(QJsonValue::Null)
			: QJsonValue(static_cast<int>(midiClip->notes().size())));
	entry.insert(QStringLiteral("selected"), selectedClipId() == clipId(ref.ordinal));
	return entry;
}

QJsonObject noteState(const Note* note, int index)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("id"), noteId(index));
	entry.insert(QStringLiteral("index"), index);
	entry.insert(QStringLiteral("key"), note->key());
	entry.insert(QStringLiteral("position"), note->pos().getTicks());
	entry.insert(QStringLiteral("length"), note->length().getTicks());
	entry.insert(QStringLiteral("velocity"), static_cast<double>(note->getVolume()));
	// The engine's note volume is volume_t (0..200, DefaultVolume 100 - see
	// volume.h); the conventional MIDI reading of it is reported alongside.
	entry.insert(QStringLiteral("midi_velocity"), note->midiVelocity(127));
	entry.insert(QStringLiteral("pan"), static_cast<double>(note->getPanning()));
	entry.insert(QStringLiteral("slide"), note->slide());
	return entry;
}

QJsonObject rollState(const ClipRef& ref)
{
	QJsonObject out = clipState(ref);
	out.insert(QStringLiteral("clip"), clipId(ref.ordinal));
	QJsonArray notes;
	auto* midiClip = dynamic_cast<MidiClip*>(ref.clip);
	if (midiClip != nullptr)
	{
		int index = 0;
		for (const Note* note : midiClip->notes())
		{
			QJsonObject entry = noteState(note, index);
			// The clip the note belongs to travels with the note, so a caller that
			// only kept a note id can still address it.
			entry.insert(QStringLiteral("clip"), clipId(ref.ordinal));
			entry.insert(QStringLiteral("selected"), noteSelected(clipId(ref.ordinal), index));
			notes.append(entry);
			++index;
		}
	}
	out.insert(QStringLiteral("notes"), notes);
	out.insert(QStringLiteral("note_count"), notes.size());
	QJsonArray selected;
	for (int index : selectedNoteIndices(clipId(ref.ordinal)))
	{
		selected.append(noteId(index));
	}
	out.insert(QStringLiteral("selected_notes"), selected);
	return out;
}

QJsonObject trackEditState(Track* track, int index)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("id"), control::trackIdOf(track));
	entry.insert(QStringLiteral("index"), index);
	entry.insert(QStringLiteral("name"), track->name());
	entry.insert(QStringLiteral("type"), control::trackTypeNameOf(track->type()));
	entry.insert(QStringLiteral("muted"), track->isMuted());
	entry.insert(QStringLiteral("soloed"), track->isSolo());
	entry.insert(QStringLiteral("clip_count"), track->numOfClips());
	return entry;
}

// ---------------------------------------------------------------------------
// SPEC A16 structural helpers
// ---------------------------------------------------------------------------

void removeTrack(Track* track)
{
	gui::SongEditor* editor = gui::getGUI() == nullptr || gui::getGUI()->songEditor() == nullptr
		? nullptr : gui::getGUI()->songEditor()->m_editor;
	if (editor == nullptr) { delete track; return; }
	for (gui::TrackView* view : editor->trackViews())
	{
		if (view->getTrack() == track) { editor->deleteTrackView(view); return; }
	}
	delete track;
}

namespace
{
//! The wrapper element of a captured track snapshot. Explicit, because
//! DataFile's own root is the project element: serializing into it and then
//! taking its first child yielded <head>, and the "restored" track was a
//! phantom built from the head element (measured).
const QString TrackSnapshotRoot = QStringLiteral("zene-track-snapshot");
} // namespace

bool captureTrackXml(const Track* track, QString* xml, int maxChars)
{
	QDomDocument document;
	QDomElement root = document.createElement(TrackSnapshotRoot);
	document.appendChild(root);

	// saveState is non-const on SerializingObject and the control surface only
	// ever calls this on a live track it is about to remove.
	Track* mutableTrack = const_cast<Track*>(track);
	mutableTrack->saveState(document, root);
	if (root.firstChildElement().isNull()) { return false; }

	QString captured;
	QTextStream stream(&captured);
	document.save(stream, 1);
	if (captured.isEmpty() || captured.size() > maxChars) { return false; }
	*xml = captured;
	return true;
}

Track* restoreTrackFromXml(const QString& xml, TrackContainer* container)
{
	if (container == nullptr || xml.isEmpty()) { return nullptr; }
	QDomDocument document;
	if (!document.setContent(xml)) { return nullptr; }
	const QDomElement root = document.documentElement();
	if (root.tagName() != TrackSnapshotRoot) { return nullptr; }
	const QDomElement element = root.firstChildElement();
	if (element.isNull()) { return nullptr; }
	return Track::create(element, container);
}

// ---------------------------------------------------------------------------
// SPEC A16 transactions
// ---------------------------------------------------------------------------

QJsonObject transactionPayload(const QJsonObject& before, const QString& inverseOp,
	const QJsonObject& inverseArgs, bool reversible, const QString& mechanism)
{
	QJsonObject inverse;
	inverse.insert(QStringLiteral("op"), inverseOp);
	inverse.insert(QStringLiteral("args"), inverseArgs);
	QJsonObject transaction;
	transaction.insert(QStringLiteral("before"), before);
	transaction.insert(QStringLiteral("inverse"), inverse);
	transaction.insert(QStringLiteral("reversible"), reversible);
	transaction.insert(QStringLiteral("mechanism"), mechanism);
	return transaction;
}

} // namespace control

} // namespace lmms
