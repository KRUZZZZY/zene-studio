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
#include <QJsonArray>
#include <QJsonObject>
#include <QPointer>
#include <QWidget>

#include "ShellLayout.h"
#include "lmms_export.h"

class QAction;
class QActionGroup;
class QHBoxLayout;
class QSplitter;
class QTimer;

namespace lmms::gui
{

class ShellDockHint;
class ShellFooter;
class ShellHeader;
class ShellPanel;
class ShellRegionTabs;

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
	//! 0.5.0 L08 (MX-007): pin or unpin a side panel (ShellLayout::setPinned); false for anything else.
	bool setPanelPinned(const QString& id, bool pinned);
	//! Make @a id visible, switching view when the current one does not offer it.
	bool revealPanel(const QString& id);

	//! window.get_state's view of the layout: `view`, `panels`, `focused`, `launcher`, and the
	//! chrome's (chromeState): `regions` and `footer`.
	QJsonObject state() const;
	//! Theme 8 (ShellRegionTabs.cpp): `regions` - per side region the panel open there and the
	//! tabs it offers in this view - and `footer`, how its switches fit (labelled, compact or
	//! overflowing, ShellOverflow.h).
	QJsonObject chromeState() const;
	//! The tab strip over side region @a region (none for the centre).
	ShellRegionTabs* regionTabs(ShellRegion region) const { return m_tabs.value(static_cast<int>(region)); }

	//! Theme 22 (ShellLayoutScope.cpp): project and personal layouts kept apart. While a project's
	//! own layout is on screen (window.layout_project_apply) the personal layout it replaced is
	//! put aside in the config (ui/shell.personal) - only the first: a second project's layout
	//! does not replace it - and it comes back when a project without a layout of its own opens,
	//! a new project starts, or the next start (restoreConfiguredLayout prefers it).
	void setPersonalAside();
	//! Put the personal layout set aside back on screen; false when none is aside.
	bool restorePersonal();
	//! The user chose a personal layout (window.layout_apply): nothing is aside any more.
	static void dropPersonalAside();
	static bool personalAside();

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
	//! the shell. A detached panel stays in its window whatever view is chosen; a saved layout
	//! that lists the floating panels (snapshot()'s `floating`) puts them back as it lists them.
	//! Docking opens the panel in its dock zone, so "Dock" always shows it in the main window.
	bool detachPanel(const QString& id, QString* reason = nullptr);
	bool attachPanel(const QString& id);
	bool isDetached(const QString& id) const { return m_floating.contains(id); }

	//! 0.5.0 G (theme 8), the tear-off (ShellFloating.cpp, ShellTearOff.cpp). window.detach or
	//! window.attach for @a id through the registry: the header's Dock button, the drag, the
	//! header's menu and the key all run this, as an agent on the socket does. A shell the
	//! commands cannot reach (not the main window's: a test's) acts directly.
	void dispatchFloat(const QString& id, bool floating);
	//! Where the next detachPanel(@a id) puts its window: global, the client area.
	void setNextFloatGeometry(const QString& id, const QRect& geometry);
	//! Where panel @a id docks, in this widget's coordinates: the view's centre when it is the
	//! view's main panel, else its home region (or that edge's band while the region is
	//! closed). Empty when the current view has no place for it (the arranger outside Arrange).
	QRect dockZone(const QString& id) const;
	//! The panel a dock of @a id would take the place of (its region shows one), or empty.
	QString displacedBy(const QString& id) const;
	//! The drop highlight over @a id's dock zone; an empty @a id hides it (ShellDockHint::Mode).
	void showDockHint(const QString& id, int mode);
	QWidget* floatingWindow(const QString& id) const { return m_floating.value(id).data(); }
	//! Bring floating @a id's window to the front; false when it does not float.
	bool raiseFloating(const QString& id);
	//! Move floating @a id's window onto screen @a index of QGuiApplication::screens(), keeping
	//! its size and its place relative to the screen; false when it does not float or there is
	//! no such screen.
	bool moveFloatingToScreen(const QString& id, int index);
	//! The keyboard route: float the focused panel, or dock it when it floats (Ctrl+Shift+F).
	QAction* floatAction() const { return m_floatAction; }
	//! A floating window moved or resized: persist the layout shortly (coalesced).
	void schedulePersist();
	//! A floating window's size: the panel's, at least 480 x 320 and at most 1200 x 800.
	static QSize floatSize(const QWidget* frame);

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
	//! Park every panel @a target (panel id -> host) gives no host, unless it floats.
	void parkUnused(const QHash<QString, QWidget*>& target);
	//! Give the keyboard focus to panel @a id's content, when it has some.
	void focusPanel(const QString& id);
	//! The host a panel belongs in under the current layout; nullptr = parked.
	QWidget* hostFor(const ShellPanelSpec& spec) const;
	void placeInto(QWidget* host, ShellPanel* frame);
	void persist() const;
	void syncActions();
	void onFocusChanged(QWidget* now);
	static ShellView viewOffering(const QString& id);
	//! The panel header's pop-out button (window.detach / window.attach, ShellFloating.cpp) and
	//! the header's drag handle (ShellTearOff).
	void addFloatButton(ShellPanel* frame);
	//! The Dock button, the drop highlight, the centre's placeholder and the persist timer.
	void buildFloating();
	//! The button says "Dock" while its panel floats, and shows the pop-out glyph while it is docked.
	void syncFloatButton(const QString& id);
	//! While the view's main panel floats, the centre says where it went and offers Dock.
	void syncCentrePlaceholder();
	//! Put floating @a id back in the shell without touching the layout (a layout restore).
	bool dockQuietly(const QString& id);
	//! The floating panels as a layout saves them - [{panel, screen, x, y, width, height}] - and
	//! putting them back: listed panels float there, others dock. Before the window is first
	//! shown the list waits (m_pendingFloats), so no panel window opens before the main one.
	QJsonArray floatingSnapshot() const;
	void restoreFloating(const QJsonArray& floating);
	void floatOne(const QJsonObject& entry);

	ShellLayout m_layout;
	QHash<QString, ShellPanel*> m_panels;
	QHash<int, QWidget*> m_hosts;  // ShellRegion -> host; Center = the view's centre
	QHash<int, ShellRegionTabs*> m_tabs;  // side ShellRegion -> its tab strip (theme 8)
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
	QHash<QString, QRect> m_nextFloatGeometry;     // panel id -> where its next window opens
	QJsonArray m_pendingFloats;                    // a restored layout's floats, until the first show
	QAction* m_floatAction = nullptr;
	ShellDockHint* m_dockHint = nullptr;
	QWidget* m_centrePlaceholder = nullptr;
	QTimer* m_persistTimer = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_H
