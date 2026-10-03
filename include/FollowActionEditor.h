/*
 * FollowActionEditor.h - a cell's or a scene's follow-action chain, edited as rows
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
#ifndef LMMS_GUI_FOLLOW_ACTION_EDITOR_H
#define LMMS_GUI_FOLLOW_ACTION_EDITOR_H

#include <QDialog>
#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

class QTableWidget;

namespace lmms::gui
{

/*! 0.5.0 L02 (WF-061): the follow-action chain of a cell or a scene as rows - the action, its
 *  chance, linked or unlinked timing and its bars, and the scene a jump goes to. OK writes the whole
 *  chain through the command that owns it (session.set_slot or session.set_scene, 'follow_actions'),
 *  so the editor and an agent make the same edit and one undo takes it back. */
class LMMS_EXPORT FollowActionEditor : public QDialog
{
public:
	FollowActionEditor(const QString& command, const QJsonObject& where, const QJsonArray& chain, const QString& title,
		QWidget* parent = nullptr);

	//! The chain the rows describe, in the command's JSON form.
	QJsonArray chain() const;
	void addRow(const QJsonObject& action);

private:
	void apply();
	QString m_command;
	QJsonObject m_where;
	QTableWidget* m_rows;
};

} // namespace lmms::gui

#endif // LMMS_GUI_FOLLOW_ACTION_EDITOR_H
