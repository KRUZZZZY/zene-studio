/*
 * ClipLinks.cpp - the link relation between clips (feature-list row 6,
 *                 board task #645). The decisions are recorded in
 *                 include/ClipLinks.h and docs/LINKED-CLIPS.md.
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include "ClipLinks.h"

#include <algorithm>

#include <QDomDocument>
#include <QDomElement>
#include <QStringList>
#include <QTextStream>

#include "Clip.h"
#include "Engine.h"
#include "MidiClip.h"
#include "Note.h"
#include "ProjectIds.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"

namespace lmms
{

namespace ClipLinks
{

namespace
{

/*! How many mirrors are in flight. Non-zero while a member is being written,
 *  which is what makes the nested call from the member's own note entry point
 *  (MidiClip::addNote -> this file -> MidiClip::addNote) a no-op instead of an
 *  infinite recursion. UI-thread only, like every other write path here. */
int g_mirrorDepth = 0;
/*! How many load passes have suspended mirroring (MirroringSuspension). */
int g_suspendDepth = 0;

bool mirroringAllowed() { return g_mirrorDepth == 0 && g_suspendDepth == 0; }

//! The clips of one track, in the order clip-<n> enumerates them (start
//! position, the track's own order on a tie) - ControlEditSupport's
//! clipOrderOf(), which is the arrangement order the whole surface reports.
QVector<Clip*> clipsOfTrack(Track* track)
{
	QVector<Clip*> clips;
	for (int c = 0; c < track->numOfClips(); ++c)
	{
		clips.append(track->getClip(c));
	}
	std::stable_sort(clips.begin(), clips.end(),
		[](const Clip* a, const Clip* b) { return a->startPosition() < b->startPosition(); });
	return clips;
}

} // namespace

QVector<Clip*> group(int linkId)
{
	QVector<Clip*> members;
	if (linkId <= 0) { return members; }
	Song* song = Engine::getSong();
	if (song == nullptr) { return members; }
	for (Track* track : song->tracks())
	{
		if (track == nullptr) { continue; }
		for (Clip* clip : clipsOfTrack(track))
		{
			if (clip != nullptr && clip->linkId() == linkId) { members.append(clip); }
		}
	}
	return members;
}

QVector<Clip*> groupOf(const Clip* clip)
{
	return clip == nullptr ? QVector<Clip*>() : group(clip->linkId());
}

int allocateGroupId()
{
	// The project-scoped counter, so a group number is never handed out twice in
	// one document and never reused after a reload (the counter rides the
	// project's own `next-id` attribute). Never 0: 0 is "unlinked".
	const int id = ProjectIds::allocate();
	return id > 0 ? id : 1;
}

QVector<MidiClip*> contentMembersOf(const Clip* clip)
{
	QVector<MidiClip*> members;
	for (Clip* member : groupOf(clip))
	{
		if (member == clip) { continue; }
		if (auto* midi = dynamic_cast<MidiClip*>(member)) { members.append(midi); }
	}
	return members;
}

QString contentFingerprint(Clip* clip)
{
	auto* midi = dynamic_cast<MidiClip*>(clip);
	if (midi == nullptr) { return QString(); }

	// The clip's own note serialisation, through the exporter the project file
	// uses: two members compare equal exactly when a save would write the same
	// <note> elements for them. The element holding them is thrown away, so the
	// clip's placement (pos/len/off/name/...) cannot make two in-sync members
	// look different.
	QDomDocument doc;
	QDomElement holder = doc.createElement(QStringLiteral("linkfingerprint"));
	doc.appendChild(holder);
	midi->exportToXML(doc, holder);

	QStringList notes;
	for (QDomElement note = holder.firstChildElement(); !note.isNull();
			note = note.nextSiblingElement())
	{
		QString text;
		QTextStream stream(&text);
		note.save(stream, 0);
		notes.append(text);
	}
	return notes.join(QLatin1Char('\n'));
}

MirrorReport mirrorContent(Clip* source)
{
	MirrorReport report;
	if (source == nullptr)
	{
		report.ok = false;
		report.reason = QStringLiteral("there is no source clip");
		return report;
	}
	if (!mirroringAllowed())
	{
		// Either a mirror is already in flight (a member's own note entry point
		// called back into here) or a load pass has suspended propagation.
		report.reason = QStringLiteral("mirroring is already in flight or suspended");
		return report;
	}
	if (source->linkId() <= 0)
	{
		report.reason = QStringLiteral("the clip is not linked");
		return report;
	}

	const QVector<Clip*> members = group(source->linkId());
	report.members = static_cast<int>(members.size());

	auto* sourceMidi = dynamic_cast<MidiClip*>(source);
	if (sourceMidi == nullptr)
	{
		report.ok = false;
		report.reason = QStringLiteral("a link group's content channel is a note list, and this "
			"clip has none");
		return report;
	}

	const QString wanted = contentFingerprint(source);

	QVector<MidiClip*> stale;
	QVector<JournallingObject*> toCheckpoint;
	for (Clip* member : members)
	{
		auto* midi = dynamic_cast<MidiClip*>(member);
		if (midi == nullptr)
		{
			++report.skipped;
			continue;
		}
		if (midi == sourceMidi) { continue; }
		if (contentFingerprint(midi) == wanted)
		{
			++report.alreadyInSync;
			continue;
		}
		stale.append(midi);
		toCheckpoint.append(midi);
	}
	if (stale.isEmpty())
	{
		report.reason = QStringLiteral("every member already carries the source's content");
		return report;
	}

	// ONE checkpoint covering every member that is about to be written, taken
	// BEFORE the first write: the engine's own undo (Ctrl+Z and control.undo)
	// restores the whole group from it, and the control surface's
	// mergeCheckpointsFrom() folds it into the one step the calling command
	// makes.
	if (ProjectJournal* journal = Engine::projectJournal(); journal != nullptr)
	{
		journal->addJournalCheckPoint(toCheckpoint);
	}

	++g_mirrorDepth;
	for (MidiClip* member : stale)
	{
		// The member's note list becomes the source's, through the member's own
		// public note API - the same calls the piano roll makes - so the member's
		// clip length (auto-resize) and its views follow exactly as they do for
		// an edit made in that clip.
		member->clearNotes();
		for (const Note* note : sourceMidi->notes())
		{
			if (note != nullptr) { member->addNote(*note, false); }
		}
		member->dataChanged();
		++report.written;
	}
	--g_mirrorDepth;

	report.reason = QStringLiteral("wrote %1 of %2 members").arg(report.written).arg(report.members);
	return report;
}

void suspendMirroring() { ++g_suspendDepth; }
void resumeMirroring()
{
	if (g_suspendDepth > 0) { --g_suspendDepth; }
}

} // namespace ClipLinks

} // namespace lmms
