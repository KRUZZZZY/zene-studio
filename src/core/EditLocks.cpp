/*
 * EditLocks.cpp - tracks and clips locked against editing
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

#include "EditLocks.h"

#include <algorithm>

#include <QDomElement>
#include <QJsonArray>

#include "Clip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Track.h"

namespace lmms
{

namespace
{

//! The commands that edit a track's clips or what is in them - every command of these groups and a few
//! more: what a lock refuses. Reading a clip, or copying it out (browser.save_clip), is not an edit.
bool editsContent(const QString& command)
{
	static const QStringList groups{QStringLiteral("clip"), QStringLiteral("note"), QStringLiteral("sample"), QStringLiteral("warp"),
		QStringLiteral("comp"), QStringLiteral("groove"), QStringLiteral("chord"), QStringLiteral("range")};
	static const QStringList commands{QStringLiteral("track.remove"), QStringLiteral("vca.edit_move"), QStringLiteral("scale.snap_notes"),
		QStringLiteral("bounce.in_place")};
	return groups.contains(command.section(QLatin1Char('.'), 0, 0)) || commands.contains(command);
}

//! The string ids under @a key: the value itself, or every string of an array.
QStringList idsUnder(const QJsonObject& args, const char* key)
{
	const QJsonValue value = args.value(QLatin1String(key));
	if (value.isString()) { return {value.toString()}; }
	QStringList out;
	for (const QJsonValue& each : value.toArray()) { out << each.toString(); }
	return out;
}

QString joined(const QSet<int>& ids)
{
	QList<int> sorted(ids.begin(), ids.end());
	std::sort(sorted.begin(), sorted.end());
	QStringList out;
	for (const int id : sorted) { out << QString::number(id); }
	return out.join(QLatin1Char(' '));
}

QSet<int> parsed(const QString& text)
{
	QSet<int> out;
	for (const QString& part : text.split(QLatin1Char(' '), Qt::SkipEmptyParts))
	{
		bool ok = false;
		const int id = part.toInt(&ok);
		if (ok && id >= 0) { out.insert(id); }
	}
	return out;
}

bool clipRefused(const QString& clip, QString* why)
{
	ControlResult ignored;
	control::ClipRef ref;
	if (!control::resolveClip(clip, &ref, &ignored)) { return false; }  // the handler reports a missing clip itself
	if (!EditLocks::instance().locks(ref.clip)) { return false; }
	*why = QStringLiteral("%1 is locked against editing (clip.set_locked / track.set_locked unlocks it)").arg(clip);
	return true;
}

bool trackRefused(const QString& track, QString* why)
{
	const int id = control::idToIndex(track, QStringLiteral("trk-"));
	if (id < 0 || !EditLocks::instance().trackLocked(id)) { return false; }
	*why = QStringLiteral("%1 is locked against editing (track.set_locked unlocks it)").arg(track);
	return true;
}

} // namespace

EditLocks& EditLocks::instance()
{
	static EditLocks* locks = [] {
		auto* made = new EditLocks();
		ProjectSection::registerSection(made);
		return made;
	}();
	return *locks;
}

bool EditLocks::locks(const Clip* clip) const
{
	if (clip == nullptr || isEmpty()) { return false; }
	return m_clips.contains(clip->id()) || (clip->getTrack() != nullptr && m_tracks.contains(clip->getTrack()->id()));
}

void EditLocks::setTrackLocked(int trackId, bool locked)
{
	if (locked) { m_tracks.insert(trackId); }
	else { m_tracks.remove(trackId); }
}

void EditLocks::setClipLocked(int clipId, bool locked)
{
	if (locked) { m_clips.insert(clipId); }
	else { m_clips.remove(clipId); }
}

bool EditLocks::refuses(const QString& command, const QJsonObject& args, QString* why)
{
	if (instance().isEmpty() || !editsContent(command) || command.endsWith(QLatin1String(".set_locked"))) { return false; }
	for (const QString& clip : idsUnder(args, "clip") + idsUnder(args, "clips"))
	{
		if (clipRefused(clip, why)) { return true; }
	}
	const QStringList tracks = idsUnder(args, "track") + idsUnder(args, "tracks");
	return std::any_of(tracks.begin(), tracks.end(), [why](const QString& track) { return trackRefused(track, why); });
}

void EditLocks::save(QDomDocument& doc, QDomElement& parent) const
{
	QDomElement element = doc.createElement(elementName());
	if (!m_tracks.isEmpty()) { element.setAttribute(QStringLiteral("tracks"), joined(m_tracks)); }
	if (!m_clips.isEmpty()) { element.setAttribute(QStringLiteral("clips"), joined(m_clips)); }
	parent.appendChild(element);
}

void EditLocks::load(const QDomElement& element)
{
	m_tracks = parsed(element.attribute(QStringLiteral("tracks")));
	m_clips = parsed(element.attribute(QStringLiteral("clips")));
}

void EditLocks::clear()
{
	m_tracks.clear();
	m_clips.clear();
}

} // namespace lmms
