/*
 * ReferenceProjectPanel.h - browse the reference project and copy from it (WF-042)
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

#ifndef LMMS_GUI_REFERENCE_PROJECT_PANEL_H
#define LMMS_GUI_REFERENCE_PROJECT_PANEL_H

#include <QDialog>
#include <QJsonObject>

#include "lmms_export.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QTreeWidget;
class QTreeWidgetItem;

namespace lmms::gui
{

/*! 0.5.0 F3 (WF-042): the reference project (ReferenceProject.h) in a window beside the arranger - its
 *  tracks with their clips and devices, and its mixer channels with theirs - and the three copies into
 *  this project: the selected tracks, the selected clips onto a track here (where they were, or at the
 *  play position), the selected devices onto a track's or a channel's chain here. Every button runs a
 *  reference.* command through the registry (SPEC A11), so the panel, the socket and Lua do the same. */
class LMMS_EXPORT ReferenceProjectPanel : public QDialog
{
	Q_OBJECT
public:
	//! The one panel, made on first use and shown.
	static ReferenceProjectPanel* showPanel(QWidget* parent);
	//! Ask for a project file, open it as the reference and show the panel. False when nothing was opened.
	static bool openWithDialog(QWidget* parent);
	//! Refresh the panel if it exists (ReferenceProject's change hook).
	static void refreshIfOpen();

	//! Rebuild the tree from reference.get_state and the target list from this project.
	void refresh();

	//! The tree and the target list (a test's view of them).
	QTreeWidget* tree() const { return m_tree; }
	QComboBox* targets() const { return m_targets; }

	void copyTracks();
	void copyClips();
	void copyDevices();

private:
	explicit ReferenceProjectPanel(QWidget* parent);
	void buildTree(const QJsonObject& state);
	void fillTargets();
	//! The selected items of @a kind; false (with a message) when there are none, or - with @a oneOwner -
	//! when they do not share one owner.
	bool selectedOf(const QString& kind, QList<QTreeWidgetItem*>* items, bool oneOwner = true);
	void run(const QString& id, const QJsonObject& args);

	QLabel* m_path;
	QTreeWidget* m_tree;
	QComboBox* m_targets;
	QCheckBox* m_atPlayhead;
};

} // namespace lmms::gui

#endif // LMMS_GUI_REFERENCE_PROJECT_PANEL_H
