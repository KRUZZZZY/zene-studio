/*
 * AudioOutputStatusBox.h - Settings > Audio: the running output, the configured one, and a test tone
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
 */

#ifndef LMMS_GUI_AUDIO_OUTPUT_STATUS_BOX_H
#define LMMS_GUI_AUDIO_OUTPUT_STATUS_BOX_H

#include <QGroupBox>
#include <QJsonObject>
#include <QString>

class QLabel;
class QPushButton;

namespace lmms::gui
{

//! Q050-183.1. Every read and every button goes through the command registry: audio.device_list,
//! audio.test_tone and audio.test_tone_stop.
class AudioOutputStatusBox : public QGroupBox
{
public:
	//! 'unavailableChoice' is the saved device that was not in the list when the dialog opened; the
	//! dialog then replaced it with the running one, so the box says so once.
	explicit AudioOutputStatusBox(QWidget* parent, const QString& unavailableChoice = QString());

	//! Reads audio.device_list and shows it.
	void refresh();

	//! The text for an audio.device_list result: the running and configured outputs and, after a fallback,
	//! what could not start and why.
	static QString describe(const QJsonObject& deviceList);

private:
	void runCommand(const QString& command);

	QLabel* m_status;
	QPushButton* m_play;
	QPushButton* m_stop;
	QString m_unavailableChoice;
	QString m_error;
};

} // namespace lmms::gui

#endif // LMMS_GUI_AUDIO_OUTPUT_STATUS_BOX_H
