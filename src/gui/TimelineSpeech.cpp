/*
 * TimelineSpeech.cpp - spoken timeline position, selection, and markers (Q050-080)
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
 *
 */

#include "TimelineSpeech.h"

#include <QCoreApplication>

#include "AccessibleWidgets.h"
#include "Clip.h"
#include "Engine.h"
#include "MidiClip.h"
#include "MusicalTime.h"
#include "Note.h"
#include "NoteGrid.h"
#include "ProjectMarkers.h"
#include "Song.h"
#include "Track.h"

namespace lmms::speech
{

namespace
{

QString tr(const char* text) { return QCoreApplication::translate("TimelineSpeech", text); }

} // namespace

QString positionSpeech(tick_t tick)
{
	return musicaltime::positionText(tick);
}

QString clipSpeech(const Clip* clip)
{
	if (clip == nullptr) { return tr("No clip selected"); }
	const QString name = clip->name().isEmpty() ? tr("Unnamed clip") : clip->name();
	const QString track = clip->getTrack() != nullptr ? clip->getTrack()->name() : QString();
	const auto start = static_cast<tick_t>(clip->startPosition().getTicks());
	return tr("%1, on %2, %3, %4 long").arg(name, track, musicaltime::positionText(start),
		musicaltime::lengthText(static_cast<tick_t>(clip->length().getTicks()), start));
}

QString markerSpeech(const Marker& marker)
{
	const QString at = musicaltime::positionText(marker.tick);
	if (marker.name.isEmpty()) { return tr("Marker at %1").arg(at); }
	return tr("Marker %1 at %2").arg(marker.name, at);
}

QString markerCrossingSpeech(const Marker& marker)
{
	const QString at = musicaltime::positionText(marker.tick);
	if (marker.name.isEmpty()) { return tr("Marker, %1").arg(at); }
	return tr("Marker %1, %2").arg(marker.name, at);
}

QString noteSpeech(const Note& note)
{
	const auto start = static_cast<tick_t>(note.pos().getTicks());
	QString text = tr("%1, %2, %3 long, velocity %4").arg(gui::NoteGrid::keyName(note.key()),
		musicaltime::positionText(start),
		musicaltime::lengthText(static_cast<tick_t>(note.length().getTicks()), start)).arg(static_cast<int>(note.getVolume()));
	if (note.muted()) { text += tr(", muted"); }
	return text;
}

void say(const QString& message)
{
	QObject* source = QCoreApplication::instance();
	if (source == nullptr || message.isEmpty()) { return; }
	a11y::announceStatus(source, message, a11y::Politeness::Polite);
}

void announceNoteSelection(const MidiClip* clip, int selectedCount)
{
	if (clip == nullptr || selectedCount <= 0)
	{
		say(tr("No notes selected"));
		return;
	}
	const Note* first = nullptr;
	for (const Note* note : clip->notes())
	{
		if (note->selected() && (first == nullptr || note->pos() < first->pos())) { first = note; }
	}
	if (first != nullptr)
	{
		say(QCoreApplication::translate("NoteSpeech", "%1 notes selected, %2").arg(selectedCount).arg(noteSpeech(*first)));
	}
}

void announceSelection(const Clip* clip, const Track* track, const QString& channel)
{
	if (clip != nullptr) { say(clipSpeech(clip)); return; }
	Song* song = Engine::getSong();
	if (song == nullptr) { return; }
	const QString at = positionSpeech(song->getPlayPos().getTicks());
	if (track != nullptr)
	{
		say(QCoreApplication::translate("SelectionSpeech", "Track %1, edit cursor %2").arg(track->name(), at));
	}
	else if (!channel.isEmpty())
	{
		say(QCoreApplication::translate("SelectionSpeech", "Channel %1, edit cursor %2").arg(channel, at));
	}
}

void announceCrossedMarkers(tick_t before, tick_t now)
{
	for (const Marker& marker : ProjectMarkers::instance().markers())
	{
		if (!marker.name.isEmpty() && marker.tick > before && marker.tick <= now)
		{
			say(markerCrossingSpeech(marker));
		}
	}
}

} // namespace lmms::speech
