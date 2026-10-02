/*
 * Shell.h - the 0.5.0 single-window shell: header, four-region body, footer
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

#ifndef LMMS_GUI_SHELL_H
#define LMMS_GUI_SHELL_H

#include <QHash>
#include <QJsonObject>
#include <QPointer>
#include <QWidget>

#include "ShellLayout.h"
#include "lmms_export.h"

class QAction;
class QActionGroup;
class QHBoxLayout;
class QSplitter;

namespace lmms::gui
{

class ShellFooter;
class ShellHeader;
class ShellPanel;

//! The main window's one page (docs/UI-SHELL.md). It renders a ShellLayout - which panel is
//! open in which region, in which view - into splitters, and keeps the view and panel
//! actions every surface shares: the View menu, the footer buttons and the Alt+key shortcuts
//! all trigger the same QAction, and every QAction dispatches window.set_view or
//! window.toggle through the registry, exactly as an agent on the socket does (SPEC A11).
//!
//! The shell owns the panel frames, never the editors' models; an editor is mounted into its
//! frame once and moves with the frame when a view switch moves the frame to another region.
class LMMS_EXPORT Shell : public QWidget
{
	Q_OBJECT

public:
	explicit Shell(QWidget* parent = nullptr);
	~Shell() override;

	const ShellLayout& model() const { return m_layout; }

	//! Put @a content into the panel @a id (one of ShellLayout::panels()).
	void mount(const QString& id, QWidget* content);
	ShellPanel* panel(const QString& id) const { return m_panels.value(id); }

	//! The header strip the song editor adds its tempo, time and meter widgets to
	//! (MainWindow::addWidgetToToolBar); objectName "mainToolbar", which the agent-surface
	//! gate walks.
	QWidget* toolStrip() const;
	ShellHeader* header() const { return m_header; }

	void setView(ShellView view);
	//! Toggle a panel, the result being its visibility afterwards. A panel the current view
	//! does not offer is REVEALED instead: the shell switches to a view that offers it (the
	//! arranger brings back Arrange, the launcher Arrange too), so the old window.toggle
	//! contract - "a hidden editor is brought forward" - still holds for every name.
	bool togglePanel(const QString& id, QString* reason = nullptr);
	//! Make @a id visible, switching view when the current one does not offer it.
	bool revealPanel(const QString& id);

	//! window.get_state's view of the layout: `view`, `panels`, `focused`, `launcher`.
	QJsonObject state() const;

	//! The actions the View menu lists: the three views, then every panel.
	QList<QAction*> viewActions() const;
	QList<QAction*> panelActions() const;
	QAction* panelAction(const QString& id) const { return m_panelActions.value(id); }

	//! The layout as it is persisted - the model's state and the splitter sizes: what a saved
	//! layout (window.layout_save) holds.
	QJsonObject snapshot() const;
	//! Put @a doc back; what it does not recognise is ignored (ShellLayout::restore's rule).
	void restoreSnapshot(const QJsonObject& doc);

	//! WF-092: panel @a id in a window of its own - movable to a second monitor - or back in
	//! the shell. A detached panel stays in its window whatever view or layout is applied.
	bool detachPanel(const QString& id, QString* reason = nullptr);
	bool attachPanel(const QString& id);
	bool isDetached(const QString& id) const { return m_floating.contains(id); }

	//! Restore the persisted layout (config ui/shell.layout); safe to call once at start-up.
	//! Until it has been called the shell writes nothing to the config.
	void restoreConfiguredLayout();

signals:
	void layoutChanged();

protected:
	void showEvent(QShowEvent* event) override;

private:
	void buildBody();
	void buildActions();
	void apply();
	//! The host a panel belongs in under the current layout; nullptr = parked.
	QWidget* hostFor(const ShellPanelSpec& spec) const;
	void placeInto(QWidget* host, ShellPanel* frame);
	void persist() const;
	void syncActions();
	void onFocusChanged(QWidget* now);
	static ShellView viewOffering(const QString& id);
	//! The panel header's pop-out button (window.detach / window.attach, ShellFloating.cpp).
	void addFloatButton(ShellPanel* frame);

	ShellLayout m_layout;
	QHash<QString, ShellPanel*> m_panels;
	QHash<int, QWidget*> m_hosts;  // ShellRegion -> host; Center = the view's centre
	QWidget* m_launcherHost = nullptr;
	QWidget* m_parking = nullptr;
	QSplitter* m_columns = nullptr;  // left | centre column | right
	QSplitter* m_rows = nullptr;     // centre area / bottom
	QSplitter* m_centre = nullptr;   // centre panel | launcher
	ShellHeader* m_header = nullptr;
	ShellFooter* m_footer = nullptr;
	QActionGroup* m_viewGroup = nullptr;
	QHash<int, QAction*> m_viewActions;
	QHash<QString, QAction*> m_panelActions;
	bool m_restoring = false;
	bool m_restoreAttempted = false;
	bool m_shownOnce = false;
	QHash<QString, QPointer<QWidget>> m_floating;  // panel id -> its own window
};

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_H
