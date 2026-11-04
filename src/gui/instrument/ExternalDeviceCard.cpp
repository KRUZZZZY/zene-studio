/*
 * ExternalDeviceCard.cpp - the External MIDI device of an instrument track, as a card (Q050-252.1)
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

#include "ExternalDeviceCard.h"

#include <QComboBox>
#include <QEvent>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

ControlResult invoke(const char* command, const QJsonObject& args)
{
	return ControlRegistry::instance()->invoke(QString::fromLatin1(command), args);
}

QSpinBox* makeSpin(QWidget* parent, const QString& objectName, const QString& accessible, int low, int high)
{
	auto* spin = new QSpinBox(parent);
	spin->setObjectName(objectName);
	spin->setAccessibleName(accessible);
	spin->setRange(low, high);
	spin->setKeyboardTracking(false);  // a typed number is one edit, when it is entered, not one per digit
	return spin;
}

} // namespace

ExternalDeviceCard::ExternalDeviceCard(const QString& trackId, QWidget* parent) :
	QDialog(parent),
	m_track(trackId)
{
	setObjectName(QStringLiteral("externalDeviceCard-") + trackId);
	setWindowTitle(tr("External MIDI Device"));
	setAccessibleName(tr("External MIDI device"));

	m_port = new QComboBox(this);
	m_port->setObjectName(QStringLiteral("externalDevicePort"));
	m_port->setAccessibleName(tr("MIDI output port"));
	m_port->setToolTip(tr("The MIDI output the track's notes and the patch below are sent to."));
	m_channel = makeSpin(this, QStringLiteral("externalDeviceChannel"), tr("MIDI channel"), 0, 16);
	m_channel->setSpecialValueText(tr("--"));
	m_channel->setToolTip(tr("1-16; -- keeps each note's own channel."));
	m_bankMsb = makeSpin(this, QStringLiteral("externalDeviceBankMsb"), tr("Bank select MSB"), -1, 127);
	m_bankMsb->setSpecialValueText(tr("Off"));
	m_bankMsb->setToolTip(tr("Bank select, high byte (CC 0). Off sends none."));
	m_bankLsb = makeSpin(this, QStringLiteral("externalDeviceBankLsb"), tr("Bank select LSB"), -1, 127);
	m_bankLsb->setSpecialValueText(tr("Off"));
	m_bankLsb->setToolTip(tr("Bank select, low byte (CC 32). Off sends none."));
	m_program = makeSpin(this, QStringLiteral("externalDeviceProgram"), tr("Program"), 1, 128);
	m_program->setToolTip(tr("The program the gear is told to select, after the bank."));

	auto* form = new QFormLayout;
	form->addRow(tr("&Port"), m_port);
	form->addRow(tr("&Channel"), m_channel);
	form->addRow(tr("Bank &MSB"), m_bankMsb);
	form->addRow(tr("Bank &LSB"), m_bankLsb);
	form->addRow(tr("P&rogram"), m_program);

	m_presets = new QComboBox(this);
	m_presets->setObjectName(QStringLiteral("externalDevicePresetName"));
	m_presets->setAccessibleName(tr("Device preset"));
	m_presets->setEditable(true);
	m_presets->setInsertPolicy(QComboBox::NoInsert);
	m_presets->lineEdit()->setPlaceholderText(tr("Preset name"));
	m_save = new QPushButton(tr("&Save"), this);
	m_save->setObjectName(QStringLiteral("externalDevicePresetSave"));
	m_save->setToolTip(tr("Save the port, channel, bank and program above as this preset"));
	m_recall = new QPushButton(tr("&Recall"), this);
	m_recall->setObjectName(QStringLiteral("externalDevicePresetRecall"));
	m_recall->setToolTip(tr("Apply the preset and send its patch to the output"));
	m_remove = new QPushButton(tr("&Delete"), this);
	m_remove->setObjectName(QStringLiteral("externalDevicePresetRemove"));
	m_remove->setToolTip(tr("Delete the preset"));
	auto* presetRow = new QHBoxLayout;
	presetRow->addWidget(m_presets, 1);
	presetRow->addWidget(m_save);
	presetRow->addWidget(m_recall);
	presetRow->addWidget(m_remove);

	m_status = new QLabel(this);
	m_status->setObjectName(QStringLiteral("externalDeviceStatus"));
	m_status->setWordWrap(true);
	m_status->setAccessibleName(tr("External MIDI device status"));

	auto* layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addWidget(new QLabel(tr("Presets"), this));
	layout->addLayout(presetRow);
	layout->addWidget(m_status);

	connect(m_port, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ExternalDeviceCard::portChosen);
	connect(m_channel, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { fieldChanged("channel", v); });
	connect(m_bankMsb, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { fieldChanged("bank_msb", v); });
	connect(m_bankLsb, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { fieldChanged("bank_lsb", v); });
	connect(m_program, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int v) { fieldChanged("program", v); });
	connect(m_save, &QPushButton::clicked, this, &ExternalDeviceCard::saveClicked);
	connect(m_recall, &QPushButton::clicked, this, &ExternalDeviceCard::recallClicked);
	connect(m_remove, &QPushButton::clicked, this, &ExternalDeviceCard::removeClicked);
	refresh();
}

ExternalDeviceCard* ExternalDeviceCard::open(const QString& trackId, QWidget* parent)
{
	auto* card = parent != nullptr ? parent->findChild<ExternalDeviceCard*>(QStringLiteral("externalDeviceCard-") + trackId) : nullptr;
	if (card == nullptr)
	{
		card = new ExternalDeviceCard(trackId, parent);
		card->setAttribute(Qt::WA_DeleteOnClose);
		card->setWindowFlag(Qt::Window);
	}
	card->refresh();
	card->show();
	card->raise();
	card->activateWindow();
	return card;
}

void ExternalDeviceCard::refresh()
{
	const ControlResult result = invoke("device.external_get_state", {{QStringLiteral("track"), m_track}});
	if (!result.ok)
	{
		m_status->setText(result.errorMessage);
		return;
	}
	m_state = result.result;
	m_refreshing = true;
	{
		const QSignalBlocker blockPort(m_port);
		m_port->clear();
		m_port->addItem(tr("(output off)"), QString());
		for (const QJsonValue& port : m_state.value(QStringLiteral("available_ports")).toArray())
		{
			m_port->addItem(port.toString(), port.toString());
		}
		const QString current = m_state.value(QStringLiteral("port")).toString();
		const int index = m_state.value(QStringLiteral("enabled")).toBool() ? m_port->findData(current) : 0;
		m_port->setCurrentIndex(index >= 0 ? index : 0);
	}
	const struct { QSpinBox* spin; const char* key; } fields[] = {{m_channel, "channel"}, {m_bankMsb, "bank_msb"},
		{m_bankLsb, "bank_lsb"}, {m_program, "program"}};
	for (const auto& field : fields)
	{
		const QSignalBlocker block(field.spin);
		field.spin->setValue(m_state.value(QString::fromLatin1(field.key)).toInt());
	}
	{
		const QSignalBlocker blockPresets(m_presets);
		const QString typed = m_presets->currentText();
		m_presets->clear();
		for (const QJsonValue& preset : m_state.value(QStringLiteral("presets")).toArray())
		{
			m_presets->addItem(preset.toObject().value(QStringLiteral("name")).toString());
		}
		const int index = m_presets->findText(typed);
		if (index >= 0) { m_presets->setCurrentIndex(index); }
		else { m_presets->setCurrentIndex(-1); m_presets->setEditText(typed); }
	}
	m_refreshing = false;
	m_status->setText(m_message);
}

void ExternalDeviceCard::changeEvent(QEvent* event)
{
	QDialog::changeEvent(event);
	// Back from another window: an undo or an agent's edit may have changed the device meanwhile.
	if (event->type() == QEvent::ActivationChange && isActiveWindow()) { refresh(); }
}

bool ExternalDeviceCard::run(const char* command, QJsonObject args)
{
	args.insert(QStringLiteral("track"), m_track);
	const ControlResult result = invoke(command, args);
	m_message = result.ok ? QString() : result.errorMessage;
	refresh();
	return result.ok;
}

void ExternalDeviceCard::portChosen(int index)
{
	if (m_refreshing || index < 0) { return; }
	const QString port = m_port->itemData(index).toString();
	if (port.isEmpty()) { run("device.external_set", {{QStringLiteral("enabled"), false}}); }
	else { run("device.external_set", {{QStringLiteral("port"), port}}); }
}

void ExternalDeviceCard::fieldChanged(const char* key, int value)
{
	if (m_refreshing) { return; }
	run("device.external_set", {{QString::fromLatin1(key), value}});
}

QString ExternalDeviceCard::presetName() const
{
	return m_presets->currentText().trimmed();
}

void ExternalDeviceCard::saveClicked()
{
	run("device.external_preset_save", {{QStringLiteral("name"), presetName()}});
}

void ExternalDeviceCard::recallClicked()
{
	run("device.external_preset_recall", {{QStringLiteral("name"), presetName()}});
}

void ExternalDeviceCard::removeClicked()
{
	run("device.external_preset_remove", {{QStringLiteral("name"), presetName()}});
}

} // namespace lmms::gui
