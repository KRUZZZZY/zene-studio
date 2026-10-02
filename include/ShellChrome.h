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

#include <QToolButton>
#include <QWidget>

#include "lmms_export.h"

class QAction;
class QPainter;
class QHBoxLayout;
class QLabel;

namespace lmms::gui
{

//! The shell's one button: flat, painted from the design tokens, never smaller than the
//! 24 x 24 target (WCAG 2.5.8), with a visible keyboard-focus ring (WCAG 2.4.7). An icon
//! button turns its glyph to the accent when checked; a text button (the view words) fills
//! with the accent.
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

	//! A flat icon button in the strip, accessible by @a text; the caller connects it.
	ShellButton* addButton(const QString& icon, const QString& text);
	//! A flat icon button in the strip that runs @a commandId through the registry when clicked.
	ShellButton* addCommandButton(const QString& icon, const QString& text, const QString& commandId);

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	void syncTransport();

	QWidget* m_strip;
	QHBoxLayout* m_stripLayout;
	int m_stretchIndex = -1;  //!< the stretch's position until it exists: append
	ShellButton* m_play = nullptr;
};

//! The bottom band: one button per panel, grouped by the region it opens in (the groups are
//! radio groups: a region shows one panel), and the view words.
class LMMS_EXPORT ShellFooter : public QWidget
{
	Q_OBJECT

public:
	ShellFooter(const QList<QAction*>& viewActions, const QList<QAction*>& panelActions,
		QWidget* parent = nullptr);

protected:
	void paintEvent(QPaintEvent* event) override;

private:
	void addPanelButton(QHBoxLayout* row, QAction* action);
};

} // namespace lmms::gui

#endif // LMMS_GUI_SHELL_CHROME_H
