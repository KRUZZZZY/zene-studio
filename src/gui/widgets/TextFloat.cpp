/*
 * TextFloat.cpp - class textFloat, a floating text-label
 *
 * Copyright (c) 2005-2010 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "TextFloat.h"

#include <QTimer>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>

#include <QRegularExpression>
#include <QScreen>

#include <algorithm>

#include "AccessibleWidgets.h"
#include "GuiApplication.h"
#include "MainWindow.h"

namespace lmms::gui
{

namespace
{

//! @a at moved just enough that a float of @a size there stays on @a anchor's screen (its available area).
QPoint onScreen(QPoint at, const QSize& size, const QWidget* anchor)
{
	const QScreen* screen = anchor->screen();
	if (screen == nullptr) { return at; }
	const QRect area = screen->availableGeometry();
	at.setX(std::clamp(at.x(), area.left(), std::max(area.left(), area.right() + 1 - size.width())));
	at.setY(std::clamp(at.y(), area.top(), std::max(area.top(), area.bottom() + 1 - size.height())));
	return at;
}

} // namespace


TextFloat::TextFloat() :
	TextFloat("", "", QPixmap())
{
}

TextFloat::TextFloat(const QString & title, const QString & text, const QPixmap & pixmap) :
	QWidget(getGUI()->mainWindow(), Qt::ToolTip)
{
	QHBoxLayout * mainLayout = new QHBoxLayout();
	setLayout(mainLayout);

	// Create the label that displays the pixmap
	m_pixmapLabel = new QLabel(this);
	mainLayout->addWidget(m_pixmapLabel);

	// Create the widget that displays the title and the text
	QWidget * titleAndTextWidget = new QWidget(this);
	QVBoxLayout * titleAndTextLayout = new QVBoxLayout();
	titleAndTextWidget->setLayout(titleAndTextLayout);

	m_titleLabel = new QLabel(titleAndTextWidget);
	m_titleLabel->setStyleSheet("font-weight: bold;");
	titleAndTextLayout->addWidget(m_titleLabel);

	m_textLabel = new QLabel(titleAndTextWidget);
	titleAndTextLayout->addWidget(m_textLabel);

	mainLayout->addWidget(titleAndTextWidget);

	// Call the setters so that the hidden state is updated
	setTitle(title);
	setText(text);
	setPixmap(pixmap);
}

void TextFloat::setTitle(const QString & title)
{
	m_titleLabel->setText(title);
	m_titleLabel->setHidden(title.isEmpty());
}

void TextFloat::setText(const QString & text)
{
	m_textLabel->setText(text);
	m_textLabel->setHidden(text.isEmpty());
}

void TextFloat::setPixmap(const QPixmap & pixmap)
{
	m_pixmapLabel->setPixmap(pixmap);
	m_pixmapLabel->setHidden(pixmap.isNull());
}

void TextFloat::setVisibilityTimeOut(int msecs)
{
	QTimer::singleShot(msecs, this, SLOT(hide()));
	show();
}

TextFloat * TextFloat::displayMessage(const QString & title,
					const QString & msg,
					const QPixmap & pixmap,
					int timeout, QWidget * parent)
{
	auto tf = new TextFloat(title, msg, pixmap);

	// Show the widget so that the correct height is calculated in the code that follows
	tf->show();

	// Beside a control (to its right), as before. With no parent - or a WINDOW as the parent (a menu entry's
	// reply floats over the window its menu belongs to; beside a window is past its right edge, off the screen
	// when it fills it, cluster gui-1106) - in the lower left area of that window. Never past the screen's edge.
	const bool beside = parent != nullptr && !parent->isWindow();
	QWidget* anchor = parent != nullptr ? parent : getGUI()->mainWindow();
	const QPoint offset = beside ? QPoint(anchor->width() + 2, 0) : QPoint(32, anchor->height() - tf->height() - 8);
	tf->move(onScreen(anchor->mapToGlobal(QPoint(0, 0)) + offset, tf->size(), anchor));

	// 0.5.0 (theme 17): a status message is spoken too, once, after what the screen reader is saying (the float's
	// name carries it for a Qt without announcement events, AccessibleWidgets.h).
	const QString spoken = QStringList{title, msg}.filter(QRegularExpression(QStringLiteral("\\S"))).join(QStringLiteral(". "));
	tf->setAccessibleName(spoken);
	a11y::announceStatus(tf, spoken);

	if (timeout > 0)
	{
		tf->setAttribute(Qt::WA_DeleteOnClose, true);
		QTimer::singleShot(timeout, tf, SLOT(close()));
	}

	return tf;
}

void TextFloat::mousePressEvent(QMouseEvent *)
{
	close();
}

} // namespace lmms::gui
