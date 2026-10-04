/*
 * EditLocks.h - tracks and clips locked against editing
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

#ifndef LMMS_EDIT_LOCKS_H
#define LMMS_EDIT_LOCKS_H

#include <QJsonObject>
#include <QSet>
#include <QString>

#include "ProjectSection.h"
#include "lmms_export.h"

namespace lmms
{

class Clip;

/*! 0.5.0 L12 (WF-044): tracks and clips locked against editing, by their persistent ids (trk-<n>,
 *  clip-<n>), so a lock follows the object through moves, saves and loads. A locked clip - or any clip
 *  of a locked track - cannot be moved, resized, deleted or edited: the arranger's clip views ignore
 *  the mouse on it, and the control surface refuses every mutating edit command that names it (refuses()
 *  below, asked by the registry before a handler runs). Mute, solo, arm, the mixer and automation stay
 *  open on a locked track - a lock protects the edit, not the mix.
 *  Saved as <edit-locks> only when something is locked, so a project that never locked anything
 *  re-saves byte for byte; an absent element loads as nothing locked. UI-thread state. */
class LMMS_EXPORT EditLocks : public ProjectSection
{
public:
	static EditLocks& instance();

	bool trackLocked(int trackId) const { return m_tracks.contains(trackId); }
	bool clipLocked(int clipId) const { return m_clips.contains(clipId); }
	//! Whether @a clip is locked itself or through its track (what the clip views ask).
	bool locks(const Clip* clip) const;
	void setTrackLocked(int trackId, bool locked);
	void setClipLocked(int clipId, bool locked);
	bool isEmpty() const { return m_tracks.isEmpty() && m_clips.isEmpty(); }
	const QSet<int>& lockedTracks() const { return m_tracks; }
	const QSet<int>& lockedClips() const { return m_clips; }

	/*! Whether the mutating command @a command with @a args would edit a locked track or clip; the
	 *  reason in @a why. Always false when nothing is locked (the registry's fast path), and for the
	 *  lock commands themselves. 0.5.0 theme 2: a clip.move whose overlap mode would trim or fade a
	 *  locked clip it lands on is refused too (arrangement::lockedUnder). */
	static bool refuses(const QString& command, const QJsonObject& args, QString* why);

	/*! 0.5.0 theme 2: called after every change to what is locked (a lock, an unlock, an undo of one,
	 *  a load or a clear), so the interface can show it at once - the arranger's lock badges. One hook;
	 *  nullptr (the default, and in a core test) calls nothing. UI thread. */
	using ChangeHook = void (*)();
	static void setChangeHook(ChangeHook hook);

	QString elementName() const override { return QStringLiteral("edit-locks"); }
	bool shouldPersist() const override { return !isEmpty(); }
	void save(QDomDocument& doc, QDomElement& parent) const override;
	void load(const QDomElement& element) override;
	void clear() override;

private:
	EditLocks() = default;
	QSet<int> m_tracks;
	QSet<int> m_clips;
};

} // namespace lmms

#endif // LMMS_EDIT_LOCKS_H
