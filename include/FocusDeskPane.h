/*
 * FocusDeskPane.h - the Focus Desk as a page of the main window.
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

#ifndef LMMS_GUI_FOCUS_DESK_PANE_H
#define LMMS_GUI_FOCUS_DESK_PANE_H

#include <QHash>
#include <QList>
#include <QPair>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QWidget>

class QBoxLayout;
class QMdiArea;
class QMdiSubWindow;
class QMenu;

namespace lmms::gui
{

class FocusDesk;

//! The Focus Desk (`FocusDesk`) wired to the editors the product already has.
//!
//! UI plan §5.1: "the shell is a *layout layer over the existing MDI windows*,
//! not their replacement". This is that layer and nothing more. It does not
//! create an editor, own one, or know how one is built: it is handed the
//! editors that exist, takes each out of the `QMdiSubWindow` that wraps it, and
//! puts it on the desk. Deactivating hands every one of them back to the exact
//! subwindow it came from, which is what makes the switch reversible rather
//! than a one-way door.
//!
//! Switching is deliberately the *whole* state: either the workspace keeps its
//! subwindows, or the desk holds the modules. There is no half-and-half mode,
//! because a half-and-half mode is a second layout system to keep correct.
class FocusDeskPane : public QWidget
{
	Q_OBJECT

public:
	//! The register rows this build can mount, and the editor each one is.
	//! Rows with no editor are absent, and the desk greys their chip with the
	//! register's own reason - so a module this build cannot show is said to be
	//! missing instead of silently not appearing (§4.1 X4).
	//!
	//! The editor is a QPointer because it is borrowed and can be gone before
	//! this pane is: `MainWindow::~MainWindow` deletes three of these editors
	//! before the pane it holds is destroyed, and handing a freed widget back
	//! with `setWidget()` is the same use-after-free the desk guards against.
	using Mountables = QList<QPair<QString, QPointer<QWidget>>>;

	//! The mountables of the running application, from `getGUI()`.
	static Mountables editorsFromGui();

	//! `workspacePage` is the widget the desk replaces on screen: activating
	//! hides it, deactivating shows it again. `hostLayout` is the layout the
	//! pane is added to - the workspace page's own parent's layout - so a
	//! caller installs the pane by constructing it and nothing else.
	explicit FocusDeskPane(QMdiArea* workspace, QWidget* workspacePage,
		QBoxLayout* hostLayout, QWidget* parent = nullptr);
	//! The same, with the editors supplied rather than looked up - the seam a
	//! test uses to exercise claiming and handing back without a MainWindow.
	FocusDeskPane(QMdiArea* workspace, QWidget* workspacePage,
		QBoxLayout* hostLayout, const Mountables& mountables, QWidget* parent = nullptr);
	~FocusDeskPane() override;

	bool deskActive() const { return m_active; }
	//! Claim the editors onto the desk (true), or hand them back (false).
	//! Returns whether the desk is active afterwards: activating with no
	//! mountable editor is refused rather than leaving an empty shell covering
	//! the workspace.
	bool setDeskActive(bool active);

	//! Carry out the mode the config asked for, once the editors exist.
	//!
	//! The pane is built inside `MainWindow`'s constructor and the editors are
	//! built after it, so the constructor can record the request and nothing
	//! more. This is the seam that acts on it: `MainWindow::finalize()` calls
	//! it when every editor has been built and wrapped in its subwindow.
	//! Applying earlier does not fail gracefully - it reads editor pointers
	//! `GuiApplication` has not assigned yet, which is a segfault, not a
	//! refusal. Safe to call more than once.
	bool applyConfiguredState();

	FocusDesk* desk() const { return m_desk; }

	//! The rows actually on the desk, in register order.
	QStringList mountedIds() const;
	//! The subwindow a mounted editor came from, for inspection.
	QMdiSubWindow* sourceWindow(const QString& moduleId) const;

private:
	//! Where a claimed editor came from, and whether the product had it on
	//! screen: handing it back has to restore the window, not just the widget.
	struct Origin
	{
		QPointer<QMdiSubWindow> window;
		bool wasVisible = false;
	};

	void build(QBoxLayout* hostLayout);
	void claim();
	void release();
	void readConfig();
	void saveToConfig() const;

	QMdiArea* m_workspace = nullptr;
	QPointer<QWidget> m_workspacePage;
	Mountables m_mountables;
	FocusDesk* m_desk = nullptr;

	QHash<QString, Origin> m_sources;
	bool m_active = false;
	//! True while this pane is changing its own state or writing that state
	//! out. The pane follows the `ui/focusdesk` setting, so it is one of the
	//! listeners to its own writes: `ConfigManager::setValue` emits
	//! `valueChanged` synchronously, and without this the observer answers the
	//! write it just made and the two never agree. `mutable` because
	//! `saveToConfig()` is const.
	mutable bool m_settling = false;
	//! The mode the config asks for, recorded by `readConfig()` and acted on by
	//! `applyConfiguredState()`. Never acted on earlier: the editors are built
	//! by a constructor that is still running when this pane is built.
	bool m_wantActive = false;
	//! True when the mountables are the running application's, resolved at
	//! claim time rather than now. MainWindow builds this pane *before*
	//! GuiApplication builds the editors (they are constructed after `new
	//! MainWindow`), so looking them up in the constructor would capture six
	//! nulls and the desk would never be able to activate.
	bool m_resolveFromGui = false;
};

//! Put the Focus Desk toggle in `menu`, wired to `pane`.
//!
//! The View menu is rebuilt on every open, so the entry has to be re-created
//! with it. `pane` may be null - MainWindow's first `updateViewMenu` runs
//! before the pane is built - in which case the entry is added greyed rather
//! than offering a mode this build cannot enter.
void addFocusDeskToggle(QMenu* menu, FocusDeskPane* pane);

} // namespace lmms::gui

#endif // LMMS_GUI_FOCUS_DESK_PANE_H
