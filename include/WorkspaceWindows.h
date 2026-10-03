/*
 * WorkspaceWindows.h - the 0.5.0 W3 small windows: ask, what's new, large clock, tuner
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

#ifndef LMMS_GUI_WORKSPACE_WINDOWS_H
#define LMMS_GUI_WORKSPACE_WINDOWS_H

#include <QDialog>
#include <QJsonObject>

#include "lmms_export.h"

class QLabel;
class QLineEdit;
class QTextBrowser;
class QTimer;

namespace lmms::gui
{

/*! 0.5.0 W3 L14 (WF-142): Help > Ask. A question typed here runs app.ask; the answers link to the manual
 *  section (app.help) or the command's reference entry. Offline, like app.ask. */
class LMMS_EXPORT AssistantDialog : public QDialog
{
public:
	explicit AssistantDialog(QWidget* parent = nullptr);
	//! Ask @a question and show the answers; how many there are.
	int ask(const QString& question);
	QString answersText() const;

private:
	QLineEdit* m_question;
	QTextBrowser* m_answers;
};

/*! 0.5.0 W3 L14 (WF-141): Help > What's New - the release notes app.whats_new answers, for this version
 *  and the ones before. */
class LMMS_EXPORT WhatsNewDialog : public QDialog
{
public:
	explicit WhatsNewDialog(const QJsonObject& whatsNew, QWidget* parent = nullptr);
	QString notesText() const;

private:
	QTextBrowser* m_notes;
};

/*! 0.5.0 W3 L13 (WF-101): a large clock - the play position in bars, beats and ticks and in minutes and
 *  seconds, big enough to read across a room - in a window of its own, refreshed ten times a second
 *  from transport.get_state. A display: it changes nothing. */
class LMMS_EXPORT LargeClock : public QDialog
{
public:
	explicit LargeClock(QWidget* parent = nullptr);
	//! Re-read the position and redraw; what it shows.
	QString refresh();

private:
	QLabel* m_bars;
	QLabel* m_time;
	QTimer* m_timer;
};

/*! 0.5.0 W3 L14 (WF-147): the tuner - the note nearest the pitch of its source, how many cents off it is
 *  and a needle, refreshed ten times a second from tuner.get_state; Input and Master choose the source
 *  (tuner.set). Opening it listens to the input when the tuner was off; closing it switches it off. */
class LMMS_EXPORT TunerWindow : public QDialog
{
public:
	explicit TunerWindow(QWidget* parent = nullptr);
	~TunerWindow() override;
	//! Read the tuner and redraw; the note shown ("-" when nothing is clear).
	QString refresh();

private:
	QLabel* m_note;
	QLabel* m_detail;
	class QSlider* m_needle;
	QTimer* m_timer;
};

} // namespace lmms::gui

#endif // LMMS_GUI_WORKSPACE_WINDOWS_H
