/*
 * ShellSelection.h - the track and clip the shell's Inspector and Device panel follow
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

#ifndef LMMS_GUI_SHELL_SELECTION_H
#define LMMS_GUI_SHELL_SELECTION_H

#include <QMetaObject>
#include <QObject>
#include <QPointer>

#include "Clip.h"
#include "Track.h"
#include "lmms_export.h"

namespace lmms::gui
{

//! What the user is working on: one track and, optionally, one clip on it - or one mixer channel. The Inspector and
//! the Device panel show whatever this says; a click on a track header, opening a clip in the
//! note editor and window.select (the agent's way) all set it. Interface state, not project
//! state. A destroyed track clears the selection; a destroyed clip clears the clip and emits
//! changed(), leaving its still-live track selected.
class LMMS_EXPORT ShellSelection : public QObject
{
	Q_OBJECT

public:
	static ShellSelection* instance();

	Track* track() const { return m_track.data(); }
	Clip* clip() const { return m_clip.data(); }
	//! The mixer channel (ch-<n>) selected on its own - a click on a mixer strip; empty while a
	//! track is selected (a track's channel is the track's business).
	QString channel() const { return m_channel; }

	//! Select @a track (and drop a clip that is not on it).
	void setTrack(Track* track);
	//! Select @a clip and its track.
	void setClip(Clip* clip);
	//! Select mixer channel @a id (ch-<n>) and no track.
	void setChannel(const QString& id);

	//! Follow the pointer: a press on a track header selects that track, a press on a clip
	//! selects the clip. One application-wide filter that only reads presses; it never
	//! consumes an event, so every widget still gets its click as before.
	void trackClicks();

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

signals:
	void changed();

private:
	ShellSelection() = default;
	void watchSelectedObjects();
	void clearDestroyedSelection();

	QPointer<Track> m_track;
	QPointer<Clip> m_clip;
	QMetaObject::Connection m_trackDestroyedConnection;
	QMetaObject::Connection m_clipDestroyedConnection;
	QString m_channel;
	//! MX-021: the track playing into channel @a channelId when following is on, else nullptr.
	static Track* followedTrack(const QString& channelId);
};

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_SELECTION_H
