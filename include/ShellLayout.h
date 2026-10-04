/*
 * ShellLayout.h - the 0.5.0 shell's panels, regions and views, as data
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

#ifndef LMMS_GUI_SHELL_LAYOUT_H
#define LMMS_GUI_SHELL_LAYOUT_H

#include <QHash>
#include <QList>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include "lmms_export.h"

namespace lmms::gui
{

//! Where a panel can sit. The body is a fixed template (docs/UI-SHELL.md §2): the
//! Inspector on the left, the view's main editor in the centre, one secondary panel at the
//! bottom, the Browser on the right. Each side region shows at most ONE panel at a time;
//! its footer buttons are a radio group, not independent toggles.
enum class ShellRegion
{
	Left,
	Center,
	Bottom,
	Right
};

//! The three task views. A view names its centre panel and the panels it offers.
enum class ShellView
{
	Arrange,
	Mix,
	Edit
};

struct ShellPanelSpec
{
	QString id;        //!< stable id: window.toggle's `editor` and window.get_state's `panels[].id`
	QString title;     //!< what the panel header and its footer button say
	ShellRegion home;  //!< the side region it opens in when it is not a view's centre
	QString shortcut;  //!< portable key text, e.g. "Alt+I"; empty for none
};

//! The panel register, the view table and the open/closed state, with no widgets: the shell
//! (Shell.cpp) renders whatever this says, and every rule - one panel per region, a view's
//! centre cannot be hidden, a panel a view does not offer is refused with a reason - lives
//! here, where ShellLayoutTest can prove it without a display.
class LMMS_EXPORT ShellLayout
{
public:
	ShellLayout();

	static QList<ShellPanelSpec> panels();
	static bool isPanel(const QString& id);
	static ShellPanelSpec spec(const QString& id);

	//! The region's word, as window.get_state's `region` and a saved layout spell it.
	static QString regionName(ShellRegion region);

	static QString viewName(ShellView view);
	//! false for an unknown name; @a out is untouched then.
	static bool viewFromName(const QString& name, ShellView& out);
	static QStringList viewNames();

	//! The view's centre panel: arranger, mixer or detail.
	static QString centerOf(ShellView view);
	//! Whether @a view offers @a id at all (Bitwig's availability matrix: Edit has no
	//! arranger or launcher, Mix has no arranger).
	static bool offers(ShellView view, const QString& id);

	ShellView view() const { return m_view; }
	void setView(ShellView view);

	//! Where @a id is on screen in the current view, or nothing: Center when it is the view's
	//! centre (or the launcher beside it), its home region when it is open there.
	bool isVisible(const QString& id) const;
	ShellRegion regionOf(const QString& id) const;
	//! The panel open in a side region in the current view; empty when the region is closed.
	QString openIn(ShellRegion region) const;
	//! The launcher shares the centre with the arranger (Arrange) or the mixer (Mix).
	bool launcherBesideCenter() const;

	//! Open @a id, closing whatever its region held. A view's centre is already open. Returns
	//! false with @a reason set when the current view does not offer the panel.
	bool open(const QString& id, QString* reason = nullptr);
	//! Close @a id. The view's centre cannot be closed (false, with a reason).
	bool close(const QString& id, QString* reason = nullptr);
	//! open() when hidden, close() when shown; the result is the visibility afterwards.
	bool toggle(const QString& id, QString* reason = nullptr);

	//! Panel that most recently took focus (empty until one does).
	QString focused() const { return m_focused; }
	void setFocused(const QString& id);

	//! 0.5.0 L08 (MX-007): a pinned side panel stays open in every view that offers it (it opens when a view is
	//! switched to). Only side panels pin; false for any other id. Saved with the layout (a preference).
	bool setPinned(const QString& id, bool pinned);
	bool isPinned(const QString& id) const { return m_pinned.contains(id); }

	QVariantMap save() const;
	//! Applies what it recognises and ignores the rest: a layout saved by a later build, or a
	//! hand-edited config, cannot leave the shell in a state its rules forbid.
	void restore(const QVariantMap& state);

private:
	struct ViewState
	{
		QHash<int, QString> openPanel;  // ShellRegion -> panel id (side regions only)
		bool launcher = false;
	};

	ViewState& current() { return m_states[static_cast<int>(m_view)]; }
	const ViewState& current() const { return m_states[static_cast<int>(m_view)]; }
	QString refusal(const QString& id) const;
	void reopenPinned();
	//! Add @a id to the pinned panels without opening it; false when it is not a side panel.
	bool setPinnedQuietly(const QString& id);
	//! A side panel: the panels setPinned() accepts.
	static bool pinnable(const QString& id);

	ShellView m_view = ShellView::Arrange;
	ViewState m_states[3];
	QString m_focused;
	QStringList m_pinned;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_LAYOUT_H
