/*
 * HelpCenter.h - the manual, the command reference and the guided tours inside the application
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
#ifndef LMMS_GUI_HELP_CENTER_H
#define LMMS_GUI_HELP_CENTER_H

#include <QDialog>
#include <QJsonArray>
#include <QPointer>
#include <QStringList>

#include "lmms_export.h"

class QFrame;
class QLabel;
class QLineEdit;
class QMenuBar;
class QPushButton;
class QTabWidget;
class QTextBrowser;

namespace lmms::gui
{

/*! 0.5.0 L14 (WF-136): the manual inside the application - the bundled HTML (data/manual) in one tab
 *  and, in another, the command reference built from the registry itself: every command's id, its
 *  description and its arguments, filtered as you type. It cannot drift from the program: the
 *  reference is the registry. */
class LMMS_EXPORT HelpCenter : public QDialog
{
	Q_OBJECT
public:
	//! Show it (one window), on the manual at @a topic (an anchor, e.g. "mix") or on the reference.
	static HelpCenter* showHelp(const QString& topic, bool reference = false);
	QString referenceText() const;
	void filterReference(const QString& text);

private:
	explicit HelpCenter(QWidget* parent);
	QTabWidget* m_tabs;
	QTextBrowser* m_manual;
	QTextBrowser* m_reference;
	QLineEdit* m_filter;
};

/*! 0.5.0 L14 (WF-140): a guided tour - steps (data/tours/<name>.json: target, panel, title, text)
 *  shown one at a time in a frame beside the widget each points at (found by object name), the rest of
 *  the window dimmed. A step whose target is hidden shows its panel first (window.toggle). Back,
 *  Next and Close; Close, Escape or the last Next ends it. */
class LMMS_EXPORT GuidedTour : public QWidget
{
	Q_OBJECT
public:
	//! The tours there are: their names (the file names) and titles.
	static QJsonArray tours();
	//! Start the tour @a name over @a window; nullptr (and @a error) when there is no such tour.
	static GuidedTour* start(const QString& name, QWidget* window, QString* error);
	int stepCount() const { return static_cast<int>(m_steps.size()); }
	int step() const { return m_step; }
	void showStep(int index);

protected:
	void paintEvent(QPaintEvent* event) override;
	//! Escape ends the tour.
	void keyPressEvent(QKeyEvent* event) override;
	//! The window resized: the overlay, the hole and the step's frame follow it.
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	GuidedTour(const QJsonArray& steps, QWidget* window);
	QWidget* targetOf(int index) const;
	//! Cover the window, cut the hole over the step's target and put the frame beside it.
	void place();

	QJsonArray m_steps;
	int m_step = 0;
	QRect m_hole;
	QFrame* m_bubble;
	QLabel* m_title;
	QLabel* m_text;
	QPushButton* m_back = nullptr;
	QPushButton* m_next = nullptr;
};

//! Help menu entries: What's This?, Manual, Command Reference, Tours, Appearance.
LMMS_EXPORT void installHelpEntries(QMenuBar* bar);

} // namespace lmms::gui

#endif // LMMS_GUI_HELP_CENTER_H
