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

#include <algorithm>

#include <QRegularExpression>

#include "AccessibleWidgets.h"
#include "GuiApplication.h"
#include "MainWindow.h"

namespace lmms::gui
{

namespace
{

/*! Where a float of `size` goes next to `anchor` without leaving `bounds` (all global coordinates): to its
 *  right if that fits, else to its left, else (an anchor that fills the window, such as the main window
 *  itself) over the lower left of the anchor. The top follows the anchor but stays inside `bounds`. */
QPoint besideWithin(const QRect& anchor, const QRect& bounds, const QSize& size)
{
	const int top = std::clamp(anchor.top(), bounds.top(), std::max(bounds.top(), bounds.bottom() + 1 - size.height()));
	const int right = anchor.right() + 1 + 2;
	if (right + size.width() <= bounds.right() + 1) { return {right, top}; }
	const int left = anchor.left() - 2 - size.width();
	if (left >= bounds.left()) { return {left, top}; }
	const QRect inner = anchor.intersected(bounds);
	const int x = std::clamp(inner.left() + 32, bounds.left(), std::max(bounds.left(), bounds.right() + 1 - size.width()));
	const int y = std::max(bounds.top(), inner.bottom() + 1 - size.height() - 8);
	return {x, y};
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

	if(parent != nullptr)
	{
		// Beside the widget, but never outside the window it is in: next to the main window itself there is no
		// room, and a float placed at its width + 2 is off the app (nobody sees the message).
		const QWidget* window = parent->window();
		const QRect anchor(parent->mapToGlobal(QPoint(0, 0)), parent->size());
		const QRect bounds(window->mapToGlobal(QPoint(0, 0)), window->size());
		tf->move(besideWithin(anchor, bounds, tf->size()));
	}
	else
	{
		// If no parent is given move the window to the lower left area of the main window
		QWidget * mw = getGUI()->mainWindow();
		tf->moveGlobal(mw, QPoint(32, mw->height() - tf->height() - 8));
	}

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
