/*
 * ShellPanel.h - one panel of the 0.5.0 shell: a header and the editor it frames
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

#ifndef LMMS_GUI_SHELL_PANEL_H
#define LMMS_GUI_SHELL_PANEL_H

#include <QPointer>
#include <QWidget>

#include "lmms_export.h"

class QHBoxLayout;
class QLabel;
class QToolButton;
class QVBoxLayout;

namespace lmms::gui
{

//! A panel's chrome: a slim header (icon, title, the editor's own tools on the right) over the
//! content. A mounted editor is the panel's child, exactly as it was its SubWindow's child in
//! the MDI era: it is destroyed with the main window, after ~MainWindow has deleted the
//! editors that must go before the Song (MainWindow.cpp), and GuiApplication's destroyed()
//! hooks null its accessors. Moving an editor between panels (a view switch) re-parents it;
//! nothing is deleted on the way.
class LMMS_EXPORT ShellPanel : public QWidget
{
	Q_OBJECT

public:
	ShellPanel(const QString& id, const QString& title, QWidget* parent = nullptr);

	QString id() const { return m_id; }

	//! Mount @a content (re-parented into the panel). The previous content, if any, is
	//! returned unparented and hidden; the caller decides its fate.
	QWidget* setContent(QWidget* content);
	QWidget* content() const { return m_content; }
	//! Hand the content back (unparented, hidden) and leave the panel empty.
	QWidget* takeContent();

	//! Where an editor puts its own header controls (grid, zoom, tools).
	QHBoxLayout* toolArea() const { return m_tools; }
	//! The header strip itself: the handle the tear-off drags (ShellTearOff.h).
	QWidget* header() const { return m_header; }

	//! The panel that last took keyboard focus draws an accent line under its header.
	void setFocusedPanel(bool focused);
	//! 0.5.0 L08 (MX-007): the header's pin button shows whether the panel is pinned (window.pin).
	void setPinnedMark(bool pinned);
	bool isFocusedPanel() const { return m_focused; }

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QString m_id;
	QWidget* m_header;
	QLabel* m_title;
	QHBoxLayout* m_tools;
	QToolButton* m_pin;
	QVBoxLayout* m_body;
	QPointer<QWidget> m_content;
	bool m_focused = false;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_PANEL_H
