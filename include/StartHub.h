/*
 * StartHub.h - M3 item 10: where a session starts, and where to learn the rest
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

#ifndef LMMS_GUI_START_HUB_H
#define LMMS_GUI_START_HUB_H

#include <QDialog>
#include <QJsonObject>
#include <QStringList>

#include "lmms_export.h"

class QLabel;
class QListWidget;
class QToolButton;

namespace lmms::gui
{

/*! File > Start Hub (M3 item 10): start a new project (empty, or from a template), reopen a
 *  recent one, and a short Learn section - the three ways into everything else (the command
 *  palette, the keyboard shortcuts page, the online manual). REGISTRY-FIRST: New and Open run
 *  project.new / project.open, the Learn buttons run window.command_palette / window.shortcuts /
 *  app.online_help, all through the registry. Not opened on startup: a window at launch changes
 *  startup for every run, and a modal there is what parked unattended runs before (#625). */
class LMMS_EXPORT StartHub : public QDialog
{
public:
	StartHub(const QStringList& recent, const QStringList& templates, QWidget* parent = nullptr);

	//! The controls, for tests and the agent-surface sweep.
	QToolButton* newButton() const { return m_new; }
	QListWidget* recentList() const { return m_recent; }
	QListWidget* templateList() const { return m_templates; }
	QString statusText() const;

	//! Opens the highlighted recent project / starts from the highlighted template.
	void openSelectedRecent();
	void startFromSelectedTemplate();

private:
	QToolButton* makeButton(const QString& command, const QString& text);
	void run(const QString& command, const QJsonObject& args);

	QToolButton* m_new = nullptr;
	QListWidget* m_recent = nullptr;
	QListWidget* m_templates = nullptr;
	QLabel* m_status = nullptr;
};

//! The template files the hub offers: the user's, then the factory's (*.mpt), by name.
LMMS_EXPORT QStringList startHubTemplates();

} // namespace lmms::gui

#endif // LMMS_GUI_START_HUB_H
