/*
 * ExternalDeviceCard.h - the External MIDI device of an instrument track, as a card (Q050-252.1)
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

#ifndef LMMS_GUI_EXTERNAL_DEVICE_CARD_H
#define LMMS_GUI_EXTERNAL_DEVICE_CARD_H

#include <QDialog>
#include <QJsonObject>
#include <QString>

#include "lmms_export.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;

namespace lmms::gui
{

/*! 0.5.0 L09 (Q050-252.1, MX-062): the card of an instrument track's External MIDI device, from Track > MIDI Out >
 *  "External Device...". The MIDI output port, the channel, the bank select (MSB / LSB) and the program the gear is
 *  told to select, and the device's saved presets. Every control is one device.external_* command run through the
 *  registry (the socket's, Lua's and MCP's path), so each edit is one undo step and the card shows exactly what
 *  device.external_get_state answers. */
class LMMS_EXPORT ExternalDeviceCard : public QDialog
{
	Q_OBJECT
public:
	explicit ExternalDeviceCard(const QString& trackId, QWidget* parent = nullptr);

	//! The card of the track @a trackId under @a parent: the one already open, raised, else a new one shown.
	static ExternalDeviceCard* open(const QString& trackId, QWidget* parent);

	//! Reads the device again (after an undo, or an agent's edit).
	void refresh();
	//! What device.external_get_state last answered.
	const QJsonObject& state() const { return m_state; }
	//! The reason the last command was refused; empty after one that went through.
	QString message() const { return m_message; }

protected:
	void changeEvent(QEvent* event) override;

private:
	//! Runs @a command with the track and @a args; false (and message()) when refused. Re-reads the device either way.
	bool run(const char* command, QJsonObject args);
	void portChosen(int index);
	void fieldChanged(const char* key, int value);
	void saveClicked();
	void recallClicked();
	void removeClicked();
	QString presetName() const;

	QString m_track;
	QJsonObject m_state;
	QString m_message;
	bool m_refreshing = false;
	QComboBox* m_port = nullptr;
	QSpinBox* m_channel = nullptr;
	QSpinBox* m_bankMsb = nullptr;
	QSpinBox* m_bankLsb = nullptr;
	QSpinBox* m_program = nullptr;
	QComboBox* m_presets = nullptr;
	QPushButton* m_save = nullptr;
	QPushButton* m_recall = nullptr;
	QPushButton* m_remove = nullptr;
	QLabel* m_status = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_EXTERNAL_DEVICE_CARD_H
