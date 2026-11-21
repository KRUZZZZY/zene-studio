/*
 * SessionAnnouncements.h - spoken launcher cell and scene state changes (0.5.0 Q050-320)
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef LMMS_GUI_SESSION_ANNOUNCEMENTS_H
#define LMMS_GUI_SESSION_ANNOUNCEMENTS_H

#include <QJsonObject>
#include <QObject>
#include <QString>
#include <QVector>

#include "lmms_export.h"

class QTimer;

namespace lmms::gui
{

/*! 0.5.0 Q050-320: speaks what the launcher's PUBLISHED state actually became.
 *
 *  It observes session.get_state on the GUI thread (never the audio thread), and only after a
 *  state transition has completed: a queued/launching cell is not spoken as playing, a refused
 *  launch changes nothing and therefore says nothing, and a stop is spoken when the cell has
 *  actually left the playing state. A scene launch that starts several cells at once is one
 *  scene announcement, not a burst of one per cell. All speech goes through announceStatus, so
 *  view.announcements gates ordinary launches exactly as it gates every other routine message.
 */
class LMMS_EXPORT SessionAnnouncer : public QObject
{
	Q_OBJECT
public:
	explicit SessionAnnouncer(QObject* parent = nullptr);

	//! Poll session.get_state every 150 ms. Starting twice is harmless.
	void start();
	//! Evaluate one session.get_state result (the timer's seam; tests drive this directly).
	void observe(const QJsonObject& state);
	//! Forget the last observed state, so the next observe seeds silently (no announcement for what is already playing).
	void reset();

	//! The last announcement this observer produced, whether or not the level let it through.
	QString lastMessage() const { return m_lastMessage; }

private:
	QTimer* m_timer = nullptr;
	QVector<int> m_playingScene;
	QVector<QString> m_names;
	QString m_lastMessage;
	int m_tracks = -1;
	int m_scenes = 0;
	bool m_seeded = false;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SESSION_ANNOUNCEMENTS_H
