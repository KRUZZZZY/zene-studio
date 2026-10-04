/*
 * CollabPanel.h - the small collaboration panel: host, invite, join, and who
 *                 is connected (0.5.0 F6, WF-050/052)
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

#ifndef LMMS_GUI_COLLAB_PANEL_H
#define LMMS_GUI_COLLAB_PANEL_H

#include <QJsonObject>
#include <QString>
#include <QWidget>

#include "ControlRegistry.h"
#include "lmms_export.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QListWidget;
class QPushButton;

namespace lmms::gui
{

/*! Asks the person at the screen (Yes / No, No by default). The only consent the GUI takes: collab.host and
 *  collab.join call it whenever a person can answer (not in an unattended run). */
LMMS_EXPORT bool collabConsentPrompt(const QString& title, const QString& text);

//! collab.show_panel: shows (creating on first use) the panel beside the main window.
LMMS_EXPORT ControlResult showCollabPanel();

/*! Every button runs a registered collab.* command, so the panel, a menu entry, Lua, the socket and MCP are
 *  one implementation. The list shows the host and each peer with its role and whether it is connected. */
class CollabPanel : public QWidget
{
	Q_OBJECT
public:
	explicit CollabPanel(QWidget* parent = nullptr);

private:
	void refresh();
	void run(const QString& command, const QJsonObject& args);
	void copyInvite(const QString& scope);
	void fillPeers(const QJsonObject& state);

	QLabel* m_status;
	QListWidget* m_peers;
	QComboBox* m_role;
	QLineEdit* m_invite;
	QPushButton* m_host;
	QPushButton* m_stop;
	QPushButton* m_copy;
	QPushButton* m_copyLan;
	QPushButton* m_join;
	QPushButton* m_leave;
	QPushButton* m_resync;
};

} // namespace lmms::gui

#endif // LMMS_GUI_COLLAB_PANEL_H
