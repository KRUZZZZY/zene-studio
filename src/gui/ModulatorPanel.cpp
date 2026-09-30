/*
 * ModulatorPanel.cpp - Edit > Modulators: the modulation layer's LFOs and the parameters they drive
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

#include "ModulatorPanel.h"

#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QTableWidget>
#include <QVBoxLayout>

#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

ControlResult run(const char* command, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(QString::fromLatin1(command), args);
}

QString refusal(const ControlResult& result) { return result.ok ? QString() : result.errorMessage; }

QPushButton* commandButton(const QString& label, const char* command, QWidget* parent)
{
	auto* button = new QPushButton(label, parent);
	button->setProperty("controlCommand", QString::fromLatin1(command));
	return button;
}

QJsonArray modulators() { return run("modulator.get_state").result.value(QStringLiteral("modulators")).toArray(); }

//! The mixer channels' chains from dsp.get_state (the modulation layer addresses channel racks only).
QJsonArray channelChains()
{
	QJsonArray out;
	for (const QJsonValue& chain : run("dsp.get_state").result.value(QStringLiteral("chains")).toArray())
	{
		if (chain.toObject().value(QStringLiteral("id")).toString().startsWith(QStringLiteral("ch-"))) { out.append(chain); }
	}
	return out;
}

const QStringList& shapes()
{
	static const QStringList names{QStringLiteral("sine"), QStringLiteral("triangle"), QStringLiteral("square"),
		QStringLiteral("saw")};
	return names;
}

} // namespace

ModulatorPanel::ModulatorPanel(QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Modulators"));
	setAccessibleName(tr("Modulators"));
	m_modulators = new QListWidget(this);
	m_modulators->setAccessibleName(tr("Modulators"));
	m_shape = new QComboBox(this);
	m_shape->addItems(shapes());
	m_shape->setProperty("controlCommand", QStringLiteral("modulator.rate_set"));
	m_rate = new QDoubleSpinBox(this);
	m_rate->setRange(0.01, 50.0);
	m_rate->setDecimals(2);
	m_rate->setSuffix(tr(" Hz"));
	m_rate->setProperty("controlCommand", QStringLiteral("modulator.rate_set"));
	m_targets = new QTableWidget(0, 4, this);
	m_targets->setHorizontalHeaderLabels({tr("Channel"), tr("Effect"), tr("Parameter"), tr("Depth")});
	m_targets->horizontalHeader()->setStretchLastSection(true);
	m_targets->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_targets->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_targets->setAccessibleName(tr("Parameters this modulator drives"));
	m_channel = new QComboBox(this);
	m_effect = new QComboBox(this);
	m_parameter = new QComboBox(this);
	m_depth = new QDoubleSpinBox(this);
	m_depth->setRange(-1.0, 1.0);
	m_depth->setSingleStep(0.05);
	m_depth->setValue(0.25);

	auto* add = commandButton(tr("Add LFO"), "modulator.create", this);
	auto* remove = commandButton(tr("Remove"), "modulator.remove", this);
	auto* bindButton = commandButton(tr("Bind"), "modulator.target_set", this);
	auto* unbind = commandButton(tr("Unbind"), "modulator.target_remove", this);
	connect(add, &QPushButton::clicked, this, [this] { addModulator(m_shape->currentText(), m_rate->value()); });
	connect(remove, &QPushButton::clicked, this, [this] { removeSelected(); });
	connect(bindButton, &QPushButton::clicked, this, [this] {
		bind(m_channel->currentData().toString(), m_effect->currentData().toInt(), m_parameter->currentText(),
			m_depth->value());
	});
	connect(unbind, &QPushButton::clicked, this, [this] { unbindRow(m_targets->currentRow()); });
	connect(m_modulators, &QListWidget::currentRowChanged, this, [this] { showSelected(); });
	connect(m_shape, &QComboBox::currentTextChanged, this, [this] { setShapeAndRate(m_shape->currentText(), m_rate->value()); });
	connect(m_rate, &QDoubleSpinBox::editingFinished, this, [this] { setShapeAndRate(m_shape->currentText(), m_rate->value()); });
	connect(m_channel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { fillEffects(); });
	connect(m_effect, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { fillParameters(); });

	auto* listButtons = new QHBoxLayout;
	listButtons->addWidget(add);
	listButtons->addWidget(remove);
	listButtons->addStretch();
	auto* source = new QFormLayout;
	source->addRow(tr("Shape:"), m_shape);
	source->addRow(tr("Rate:"), m_rate);
	auto* bindRow = new QHBoxLayout;
	for (QWidget* widget : std::initializer_list<QWidget*>{m_channel, m_effect, m_parameter, m_depth, bindButton, unbind})
	{
		bindRow->addWidget(widget);
	}
	auto* layout = new QVBoxLayout(this);
	layout->addWidget(m_modulators);
	layout->addLayout(listButtons);
	layout->addLayout(source);
	layout->addWidget(new QLabel(tr("Parameters it drives (a mixer channel's effects):"), this));
	layout->addWidget(m_targets, 1);
	layout->addLayout(bindRow);
	resize(620, 480);
	fillChannels();
	refresh();
}

void ModulatorPanel::refresh()
{
	const int keep = std::max(0, m_modulators->currentRow());
	const QSignalBlocker block(m_modulators);
	m_modulators->clear();
	for (const QJsonValue& value : modulators())
	{
		const QJsonObject modulator = value.toObject();
		const QJsonObject source = modulator.value(QStringLiteral("source")).toObject();
		auto* item = new QListWidgetItem(tr("%1 - %2, %3 Hz, %4 target(s)")
			.arg(modulator.value(QStringLiteral("name")).toString(), source.value(QStringLiteral("shape")).toString())
			.arg(source.value(QStringLiteral("rate")).toDouble(), 0, 'f', 2)
			.arg(modulator.value(QStringLiteral("target_count")).toInt()));
		item->setData(Qt::UserRole, modulator.value(QStringLiteral("id")).toString());
		m_modulators->addItem(item);
	}
	if (m_modulators->count() > 0) { m_modulators->setCurrentRow(std::min(keep, m_modulators->count() - 1)); }
	showSelected();
}

void ModulatorPanel::showSelected()
{
	m_targets->setRowCount(0);
	const QString id = selectedId();
	for (const QJsonValue& value : modulators())
	{
		const QJsonObject modulator = value.toObject();
		if (modulator.value(QStringLiteral("id")).toString() != id) { continue; }
		const QJsonObject source = modulator.value(QStringLiteral("source")).toObject();
		const QSignalBlocker blockShape(m_shape);
		const QSignalBlocker blockRate(m_rate);
		m_shape->setCurrentText(source.value(QStringLiteral("shape")).toString());
		m_rate->setValue(source.value(QStringLiteral("rate")).toDouble());
		const QJsonArray targets = modulator.value(QStringLiteral("targets")).toArray();
		m_targets->setRowCount(static_cast<int>(targets.size()));
		for (int row = 0; row < targets.size(); ++row)
		{
			const QJsonObject target = targets.at(row).toObject();
			const QStringList cells{target.value(QStringLiteral("channel")).toString(),
				QStringLiteral("fx-%1").arg(target.value(QStringLiteral("effect")).toInt()),
				target.value(QStringLiteral("parameter")).toString(),
				QString::number(target.value(QStringLiteral("depth")).toDouble(), 'f', 2)};
			for (int column = 0; column < cells.size(); ++column)
			{
				m_targets->setItem(row, column, new QTableWidgetItem(cells.at(column)));
			}
		}
	}
}

void ModulatorPanel::fillChannels()
{
	const QSignalBlocker block(m_channel);
	m_channel->clear();
	for (const QJsonValue& chain : channelChains())
	{
		const QJsonObject object = chain.toObject();
		if (object.value(QStringLiteral("count")).toInt() == 0) { continue; }
		m_channel->addItem(object.value(QStringLiteral("id")).toString(), object.value(QStringLiteral("id")).toString());
	}
	fillEffects();
}

void ModulatorPanel::fillEffects()
{
	const QSignalBlocker block(m_effect);
	m_effect->clear();
	for (const QJsonValue& chain : channelChains())
	{
		if (chain.toObject().value(QStringLiteral("id")).toString() != m_channel->currentData().toString()) { continue; }
		const QJsonArray devices = chain.toObject().value(QStringLiteral("devices")).toArray();
		for (int i = 0; i < devices.size(); ++i)
		{
			const QJsonObject device = devices.at(i).toObject();
			m_effect->addItem(QStringLiteral("fx-%1 %2").arg(i).arg(device.value(QStringLiteral("display_name")).toString()), i);
		}
	}
	fillParameters();
}

void ModulatorPanel::fillParameters()
{
	m_parameter->clear();
	for (const QJsonValue& chain : channelChains())
	{
		if (chain.toObject().value(QStringLiteral("id")).toString() != m_channel->currentData().toString()) { continue; }
		const QJsonArray devices = chain.toObject().value(QStringLiteral("devices")).toArray();
		const int effect = m_effect->currentData().toInt();
		if (effect < 0 || effect >= devices.size()) { return; }
		for (const QJsonValue& parameter : devices.at(effect).toObject().value(QStringLiteral("parameters")).toArray())
		{
			m_parameter->addItem(parameter.toObject().value(QStringLiteral("name")).toString());
		}
	}
}

QString ModulatorPanel::selectedId() const
{
	const QListWidgetItem* item = m_modulators->currentItem();
	return item != nullptr ? item->data(Qt::UserRole).toString() : QString();
}

int ModulatorPanel::modulatorCount() const { return m_modulators->count(); }
int ModulatorPanel::targetCount() const { return m_targets->rowCount(); }

void ModulatorPanel::selectModulator(int row)
{
	m_modulators->setCurrentRow(row);
	showSelected();
}

QString ModulatorPanel::addModulator(const QString& shape, double rateHz)
{
	const QString result = refusal(run("modulator.create", {{QStringLiteral("name"), tr("LFO %1").arg(modulatorCount() + 1)},
		{QStringLiteral("shape"), shape}, {QStringLiteral("rate"), rateHz}}));
	refresh();
	if (result.isEmpty()) { selectModulator(modulatorCount() - 1); }
	return result;
}

QString ModulatorPanel::removeSelected()
{
	if (selectedId().isEmpty()) { return tr("no modulator is selected"); }
	const QString result = refusal(run("modulator.remove", {{QStringLiteral("modulator"), selectedId()}}));
	refresh();
	return result;
}

QString ModulatorPanel::setShapeAndRate(const QString& shape, double rateHz)
{
	if (selectedId().isEmpty()) { return tr("no modulator is selected"); }
	const QString result = refusal(run("modulator.rate_set", {{QStringLiteral("modulator"), selectedId()},
		{QStringLiteral("shape"), shape}, {QStringLiteral("rate"), rateHz}}));
	refresh();
	return result;
}

QString ModulatorPanel::bind(const QString& channel, int effect, const QString& parameter, double depth)
{
	if (selectedId().isEmpty()) { return tr("no modulator is selected"); }
	const QString result = refusal(run("modulator.target_set", {{QStringLiteral("modulator"), selectedId()},
		{QStringLiteral("channel"), channel}, {QStringLiteral("chain"), 0}, {QStringLiteral("effect"), effect},
		{QStringLiteral("parameter"), parameter}, {QStringLiteral("depth"), depth}}));
	refresh();
	return result;
}

QString ModulatorPanel::unbindRow(int row)
{
	if (selectedId().isEmpty() || row < 0) { return tr("no target is selected"); }
	const QString result = refusal(run("modulator.target_remove", {{QStringLiteral("modulator"), selectedId()},
		{QStringLiteral("target"), row}}));
	refresh();
	return result;
}

} // namespace lmms::gui
