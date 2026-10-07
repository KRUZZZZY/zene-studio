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

#include "ArrangementEdits.h"
#include "Clip.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
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
	static const QStringList commands{QStringLiteral("track.remove"), QStringLiteral("vca.edit_move"), QStringLiteral("vca.edit_trim"),
		QStringLiteral("vca.edit_split"), QStringLiteral("vca.edit_slip"), QStringLiteral("vca.edit_fade"), QStringLiteral("freeze.track"),
		QStringLiteral("freeze.region"), QStringLiteral("scale.snap_notes"),
		QStringLiteral("bounce.in_place"), QStringLiteral("track.flatten"), QStringLiteral("project.subproject_refresh")};
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

//! A clip.move whose overlap mode would trim or fade a locked clip where it lands.
bool moveRefused(const QJsonObject& args, QString* why)
{
	ControlResult ignored;
	control::ClipRef ref;
	if (!control::resolveClip(args.value(QStringLiteral("clip")).toString(), &ref, &ignored)) { return false; }
	const auto start = static_cast<tick_t>(args.value(QStringLiteral("position")).toDouble());
	const Clip* locked = arrangement::lockedUnder(ref.clip, start, start + ref.clip->length().getTicks(), arrangement::modes().overlap);
	if (locked == nullptr) { return false; }
	*why = QStringLiteral("moving %1 there would trim %2, which is locked against editing (clip.set_locked / track.set_locked "
		"unlocks it; arrangement.modes overlap=free lets clips overlap)").arg(args.value(QStringLiteral("clip")).toString(), control::clipIdOf(locked));
	return true;
}

EditLocks::ChangeHook s_changeHook = nullptr;

void notifyChanged()
{
	if (s_changeHook != nullptr) { s_changeHook(); }
}

bool trackRefused(const QString& track, QString* why)
{
	const int id = control::idToIndex(track, QStringLiteral("trk-"));
	if (id < 0 || !EditLocks::instance().trackLocked(id)) { return false; }
	*why = QStringLiteral("%1 is locked against editing (track.set_locked unlocks it)").arg(track);
	return true;
}

bool trackRemovalRefused(const QJsonObject& args, QString* why)
{
	const QString trackId = args.value(QStringLiteral("track")).toString();
	ControlResult ignored;
	Track* track = control::resolveTrack(trackId, &ignored);
	if (track == nullptr) { return false; }
	for (const Clip* clip : track->getClips())
	{
		if (!EditLocks::instance().clipLocked(clip->id())) { continue; }
		*why = QStringLiteral("removing %1 would delete %2, which is locked against editing (clip.set_locked / "
			"track.set_locked unlocks it)").arg(trackId, control::clipIdOf(clip));
		return true;
	}
	return false;
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
	notifyChanged();
}

void EditLocks::setClipLocked(int clipId, bool locked)
{
	if (locked) { m_clips.insert(clipId); }
	else { m_clips.remove(clipId); }
	notifyChanged();
}

void EditLocks::setChangeHook(ChangeHook hook)
{
	s_changeHook = hook;
}

bool EditLocks::refuses(const QString& command, const QJsonObject& args, QString* why)
{
	if (instance().isEmpty() || !editsContent(command) || command.endsWith(QLatin1String(".set_locked"))) { return false; }
	for (const QString& clip : idsUnder(args, "clip") + idsUnder(args, "clips"))
	{
		if (clipRefused(clip, why)) { return true; }
	}
	if (command == QLatin1String("clip.move") && moveRefused(args, why)) { return true; }
	const QStringList tracks = idsUnder(args, "track") + idsUnder(args, "tracks");
	if (std::any_of(tracks.begin(), tracks.end(), [why](const QString& track) { return trackRefused(track, why); })) { return true; }
	return command == QLatin1String("track.remove") && trackRemovalRefused(args, why);
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
	notifyChanged();
}

void EditLocks::clear()
{
	m_tracks.clear();
	m_clips.clear();
	notifyChanged();
}

} // namespace lmms
