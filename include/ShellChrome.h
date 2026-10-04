/*
 * ShellChrome.h - the 0.5.0 shell's header, footer and flat button
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

#ifndef LMMS_GUI_SHELL_CHROME_H
#define LMMS_GUI_SHELL_CHROME_H

#include <QJsonArray>
#include <QJsonObject>
#include <QList>
#include <QToolButton>
#include <QWidget>

#include "lmms_export.h"

class QAction;
class QPainter;
class QHBoxLayout;
class QLabel;

namespace lmms::gui
{

class ShellOverflowButton;
class ShellOverflowRow;

//! The shell's one button: flat, painted from the design tokens, never smaller than the
//! 24 x 24 target (WCAG 2.5.8), with a visible keyboard-focus ring (WCAG 2.4.7). An icon
//! button turns its glyph to the accent when checked; a text button (the view words) fills
//! with the accent; a labelled button (Qt::ToolButtonTextBesideIcon, theme 8: the footer's
//! switches while they fit) draws its icon and then its name, the name brighter when checked.
class LMMS_EXPORT ShellButton : public QToolButton
{
	Q_OBJECT

public:
	explicit ShellButton(QWidget* parent = nullptr);
	QSize sizeHint() const override;

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	QColor fillColour() const;
	void paintFace(QPainter& painter);
	void paintLabelled(QPainter& painter);
};

//! The top band: the product mark, the project and transport buttons, and the strip the song
//! editor fills with its tempo, time, meter and master controls.
class LMMS_EXPORT ShellHeader : public QWidget
{
	Q_OBJECT

public:
	explicit ShellHeader(QWidget* parent = nullptr);

	//! objectName "mainToolbar": the agent-surface gate walks its buttons, and
	//! MainWindow::addWidgetToToolBar appends to it.
	QWidget* strip() const { return m_strip; }
	void addToStrip(QWidget* widget);
	void addStripSpacing(int pixels);
	int stripCount() const;
	class TransportReadouts* readouts() const { return m_readouts; }

	//! A flat icon button in the strip, accessible by @a text; the caller connects it.
	ShellButton* addButton(const QString& icon, const QString& text);
	//! A flat icon button in the strip that runs @a commandId through the registry when clicked.
	ShellButton* addCommandButton(const QString& icon, const QString& text, const QString& commandId);

	/*! WF-116: the header the user chose (window.header_set, kept in ui/header): every button
	 *  named in @a doc's `hidden` is hidden, every other shown, and each `extra` entry
	 *  ({command, label}) is a button of its own that runs that registry command. */
	void customise(const QJsonObject& doc);
	//! Each button: its name (the icon it was added with), text, command, whether the user shows
	//! it (`visible`) and whether it is in the More menu for want of room (`overflowed`).
	QJsonArray buttons() const;

	//! Theme 8: the buttons right of the readouts that do not fit go into the header's More
	//! button, the rightmost first (ShellOverflow.h); re-run whenever the header is resized.
	void refit();
	ShellOverflowButton* moreButton() const { return m_more; }

protected:
	void paintEvent(QPaintEvent* event) override;
	void contextMenuEvent(QContextMenuEvent* event) override;
	void resizeEvent(QResizeEvent* event) override;

private:
	void syncTransport();
	//! The strip's buttons after its stretch, in order: the ones that may overflow.
	QList<QToolButton*> rightButtons() const;
	//! The strip's narrowest: everything but those buttons, and the More button.
	int stripFloor(const QList<QToolButton*>& right) const;

	QWidget* m_strip;
	QHBoxLayout* m_stripLayout;
	int m_stretchIndex = -1;  //!< the stretch's position until it exists: append
	ShellButton* m_play = nullptr;
	ShellButton* m_record = nullptr;
	class TransportReadouts* m_readouts = nullptr;
	QList<ShellButton*> m_extra;  //!< the user's own command buttons (customise)
	ShellOverflowButton* m_more = nullptr;
	bool m_fitting = false;
};

//! The bottom band: one button per panel, grouped by the region it opens in (the groups are
//! radio groups: a region shows one panel), the Tools home (ShellToolsMenu.h) and the view
//! words. Theme 8: the switches are labelled while they fit, icons only when the footer is
//! narrower, and the ones that still do not fit go into a More button (ShellOverflowRow); the
//! view words and the Browser's switch always show.
class LMMS_EXPORT ShellFooter : public QWidget
{
	Q_OBJECT

public:
	ShellFooter(const QList<QAction*>& viewActions, const QList<QAction*>& panelActions,
		QWidget* parent = nullptr);

	//! The row of panel switches and the Tools button, which fits itself to its width.
	ShellOverflowRow* switches() const { return m_switches; }

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	ShellButton* panelButton(QAction* action);

	ShellOverflowRow* m_switches = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_CHROME_H
