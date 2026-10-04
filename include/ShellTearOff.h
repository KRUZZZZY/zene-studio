/*
 * ShellTearOff.h - drag a shell panel's header out to float it, and back onto its dock zone
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

#ifndef LMMS_GUI_SHELL_TEAR_OFF_H
#define LMMS_GUI_SHELL_TEAR_OFF_H

#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QSize>
#include <QWidget>

#include "lmms_export.h"

class QMenu;
class QTimer;

namespace lmms::gui
{

class Shell;
class ShellPanel;

//! The drop highlight the shell lays over a dock zone while a floating panel is dragged
//! (Shell::showDockHint): a dashed outline where the panel will go once the pointer is over
//! the main window, a filled one when releasing there docks it, and - when the window is
//! dragged by its system title bar, which cannot dock it - a short-lived note saying how to.
class LMMS_EXPORT ShellDockHint : public QWidget
{
	Q_OBJECT

public:
	enum Mode
	{
		Hidden,
		Hint,   //!< the pointer is over the main window: this is where the panel docks
		Armed,  //!< the pointer is in the zone: a release docks it here
		Teach   //!< a title-bar drag over the zone: says to drag the header; hides itself
	};

	explicit ShellDockHint(QWidget* parent);

	//! Cover @a zone (parent coordinates) saying @a text.
	void showOver(const QRect& zone, const QString& text, Mode mode);
	void clear();
	Mode mode() const { return m_mode; }
	QString text() const { return m_text; }

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QString m_text;
	Mode m_mode = Hidden;
	QTimer* m_fade;
};

//! 0.5.0 G (theme 8): the tear-off gesture on one panel's header. While the panel is docked,
//! dragging its header shows a preview of the window it will become, following the pointer;
//! pulled off the header band and released, the panel floats there (window.detach); released
//! on the band or with Esc, nothing changes. While it floats, dragging its header moves its window; over the
//! main window the shell highlights the panel's dock zone, and released there the panel docks
//! (window.attach). Double-click floats or docks; right-click offers the same and, floating,
//! a screen to move to. On Wayland, where a program cannot place its windows, the move is
//! handed to the compositor and docking is the Dock button's.
class LMMS_EXPORT ShellTearOff : public QObject
{
	Q_OBJECT

public:
	enum class Drag
	{
		None,
		Pending,  //!< pressed, not yet past the drag distance
		Tearing,  //!< a docked panel's preview follows the pointer
		Moving    //!< a floating panel's window follows the pointer
	};

	//! Installed on @a frame's header, which owns it.
	ShellTearOff(Shell* shell, ShellPanel* frame);

	Drag drag() const { return m_drag; }
	//! The gesture by global pointer positions (the event filter's, and the test's, way in);
	//! @a local is the press in the header's coordinates.
	void press(const QPoint& global, const QPoint& local);
	void move(const QPoint& global);
	void release(const QPoint& global);
	void cancel();
	//! The tear preview's text, empty when there is none (the test reads it).
	QString previewText() const;

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	bool onPress(QEvent* event);
	bool onMouseMove(QEvent* event);
	bool onRelease(QEvent* event);
	bool onDoubleClick(QEvent* event);
	bool onKey(QEvent* event);
	void begin(const QPoint& global);
	void beginTear();
	void beginMove();
	void followPreview(const QPoint& global);
	void followWindow(const QPoint& global);
	void finish();
	//! Still on the header band (a header's height either side): a release there tears nothing.
	bool nearHeader(const QPoint& global) const;
	bool overDockZone(const QPoint& global) const;
	void showMenu(const QPoint& global);
	void addScreenMenu(QMenu& menu);
	//! A control in the header keeps the arrow pointer; the open hand is the header's own.
	static void keepArrow(QObject* child);

	Shell* m_shell;
	ShellPanel* m_frame;
	QString m_id;
	Drag m_drag = Drag::None;
	QPoint m_pressGlobal;
	QPoint m_pressLocal;
	QPoint m_windowStart;
	QSize m_size;
	QPointer<QWidget> m_preview;
	bool m_cursorSet = false;
};

//! A floating panel's window is being moved by its system title bar (ShellFloating.cpp's
//! watcher): when the pointer is over the panel's dock zone, say how docking is done.
LMMS_EXPORT void noteFloatingMoved(Shell* shell, const QString& id);

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_TEAR_OFF_H
