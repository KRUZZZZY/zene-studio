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

#include <algorithm>

#include <QApplication>
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

//! gui-1030: a message is placed beside the widget it is about, which puts it past the screen's
//! edge when that widget reaches the edge (a maximized main window): slide it back inside the
//! available area of the screen it is on, so it is still read where it can be seen.
void keepOnScreen(QWidget* tf, QWidget* anchor)
{
	const QScreen* screen = anchor->screen();
	if (screen == nullptr) { return; }
	const QRect area = screen->availableGeometry();
	const QSize size = tf->frameGeometry().size();
	const int x = std::clamp(tf->x(), area.left(), std::max(area.left(), area.right() + 1 - size.width()));
	const int y = std::clamp(tf->y(), area.top(), std::max(area.top(), area.bottom() + 1 - size.height()));
	tf->move(x, y);
}

} // namespace


TextFloat::TextFloat() :
	TextFloat("", "", QPixmap())
{
}

TextFloat::TextFloat(const QString & title, const QString & text, const QPixmap & pixmap) :
	// gui-1115: a notice must outlive the menu that ran its command. A Qt::ToolTip is an override-redirect
	// transient on X11, so the reactivated main window can cover it when the menu closes; a frameless tool
	// window, kept above its parent, stays readable. Qt::WindowDoesNotAcceptFocus keeps it a notice rather
	// than a focus target: WA_ShowWithoutActivating alone still let the tool window take the application's
	// focus, which cleared the focused panel's widget (PanelShortcutsTest). A notice is read, not typed in.
	QWidget(getGUI()->mainWindow(), Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint
		| Qt::WindowDoesNotAcceptFocus)
{
	setAttribute(Qt::WA_ShowWithoutActivating);
	setAttribute(Qt::WA_QuitOnClose, false);  // a tooltip never closed the application; a notice must not either
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

	if (parent != nullptr && !parent->isWindow())
	{
		tf->moveGlobal(parent, QPoint(parent->width() + 2, 0));
		keepOnScreen(tf, parent);
	}
	else
	{
		// No parent, or a whole window as the anchor (a menu command reports over the window it belongs to): beside
		// a window is off it - often off the screen (gui-1105) - so the message goes to the window's lower left area.
		QWidget * mw = parent != nullptr ? parent : getGUI()->mainWindow();
		tf->moveGlobal(mw, QPoint(32, mw->height() - tf->height() - 8));
		keepOnScreen(tf, mw);
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

bool TextFloat::event(QEvent * event)
{
	// gui-1115: the notice must never hold the application's focus. Qt still made this tool window the active
	// window when the window that reported the message (a command form, the menu) closed, even with
	// WA_ShowWithoutActivating and Qt::WindowDoesNotAcceptFocus; the panel that had focus then lost it
	// (PanelShortcutsTest). Hand activation straight back to the window the notice belongs to.
	if (event->type() == QEvent::WindowActivate && !m_returningActivation && parentWidget() != nullptr
		&& QApplication::applicationState() == Qt::ApplicationActive)
	{
		m_returningActivation = true;
		parentWidget()->window()->activateWindow();
		m_returningActivation = false;
	}
	return QWidget::event(event);
}

} // namespace lmms::gui
