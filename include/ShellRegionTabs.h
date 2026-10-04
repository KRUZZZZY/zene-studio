/*
 * ShellRegionTabs.h - the labelled tabs over a side region of the 0.5.0 shell
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

#ifndef LMMS_GUI_SHELL_REGION_TABS_H
#define LMMS_GUI_SHELL_REGION_TABS_H

#include <QStringList>
#include <QWidget>

#include "ShellLayout.h"
#include "lmms_export.h"

class QMenu;
class QTabBar;

namespace lmms::gui
{

class Shell;
class ShellButton;

/*! Design theme 8 (a clear home for each tool), the dock zones labelled. A side region shows one panel at a time;
 *  before, the others were only footer icons, and a panel opened there replaced the one shown with nothing on
 *  screen saying what else lived there. Now the region carries a tab per panel it holds in the current view - the
 *  Cubase lower-zone model the research names - the open one current, each tab with the panel's icon and name:
 *  - choosing a tab (a click, or the arrow keys once the tab bar has focus) opens that panel: window.toggle through
 *    the registry, exactly as the footer switch and Alt+key do;
 *  - tabs that do not fit scroll (the tab bar's arrows), and the list button beside them names every panel of the
 *    region, a panel floating in its own window too ("in its own window"), so none is ever out of reach;
 *  - the hide button closes the region (window.toggle on its open panel); the footer brings it back.
 *  The strip shows only when there are two or more panels to choose between: a region with one panel keeps just its
 *  header. A pinned panel (window.pin) says so in its tab's tooltip; its pin button in the header shows it too. */
class LMMS_EXPORT ShellRegionTabs : public QWidget
{
	Q_OBJECT

public:
	ShellRegionTabs(Shell* shell, ShellRegion region, QWidget* parent);

	//! The panels a tab strip over @a region offers in @a shell's current view: those homed there that the view offers,
	//! less the view's main panel and any panel floating in a window of its own.
	static QStringList panelsIn(const Shell& shell, ShellRegion region);

	//! Re-read the shell (Shell::apply calls it): the tabs, the current one, and whether the strip shows.
	void sync();
	QStringList tabIds() const;
	QString currentId() const;
	QTabBar* bar() const { return m_bar; }

	//! Choose the tab of @a id, as a click does: opens the panel when it is not open (false when there is no such tab).
	bool choose(const QString& id);

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	//! window.toggle for @a id through the registry, or straight to a shell the commands cannot reach (a test's).
	void toggle(const QString& id);
	void onCurrentChanged(int index);
	void fillList(QMenu* menu);
	void rebuild(const QStringList& ids);
	QString regionTitle() const;

	Shell* m_shell;
	ShellRegion m_region;
	QTabBar* m_bar;
	ShellButton* m_list;
	ShellButton* m_hide;
	bool m_syncing = false;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_REGION_TABS_H
