/*
 * SessionTransportBar.h - the clip launcher's Follow Actions / arrangement-record row
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

#ifndef LMMS_GUI_SESSION_TRANSPORT_BAR_H
#define LMMS_GUI_SESSION_TRANSPORT_BAR_H

#include <QTimer>
#include <QWidget>

#include "lmms_export.h"

class QLabel;
class QToolButton;

namespace lmms::gui
{

/*! KNOWN-LIMITATIONS "Follow Actions, Arrangement Record and the Back-to-Arrangement switch are
 *  drivable through the socket, not from the interface": a row for the clip launcher. Follow
 *  Actions (session.set_follow_actions, the whole session), Record to arrangement
 *  (session.arrangement_record_arm), Land (session.arrangement_record_land: the recorded
 *  performance becomes timeline clips) and Back to arrangement (session.back_to_arrangement). The
 *  toggles and the recorded count are re-read once a second from session.follow_get_state and
 *  session.arrangement_record_status, so a change made over the socket shows. */
class LMMS_EXPORT SessionTransportBar : public QWidget
{
public:
	explicit SessionTransportBar(QWidget* parent = nullptr);
	void refresh();

private:
	QToolButton* m_follow = nullptr;
	QToolButton* m_record = nullptr;
	QToolButton* m_land = nullptr;
	QToolButton* m_back = nullptr;
	QLabel* m_recorded = nullptr;
	QTimer m_timer;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SESSION_TRANSPORT_BAR_H
