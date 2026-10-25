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

#include <functional>
#include <utility>

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

#include "AccessibleWidgets.h"
#include "CommandForm.h"
#include "ModulationLayer.h"
#include "ControlRegistry.h"

namespace lmms::gui
{

namespace
{

class RefreshingComboBox final : public QComboBox
{
public:
	explicit RefreshingComboBox(QWidget* parent) : QComboBox(parent) {}
	void setRefresh(std::function<void()> refresh) { m_refresh = std::move(refresh); }

	void showPopup() override
	{
		if (m_refresh) { m_refresh(); }
		QComboBox::showPopup();
	}

private:
	std::function<void()> m_refresh;
};

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

//! The modulator @a id as modulator.get_state reports it, or an empty object.
QJsonObject modulatorById(const QString& id)
{
	for (const QJsonValue& value : modulators())
	{
		if (value.toObject().value(QStringLiteral("id")).toString() == id) { return value.toObject(); }
	}
	return {};
}

//! The Steps and Envelope form's starting values: the modulator's own steps and attack when it has
//! them (modulator.get_state reports each for its own shape only), else an example of each.
QJsonObject shapeValueDefaults(const QJsonObject& source)
{
	QJsonObject defaults;
	if (source.contains(QStringLiteral("steps"))) { defaults.insert(QStringLiteral("steps"), source.value(QStringLiteral("steps"))); }
	if (source.contains(QStringLiteral("attack"))) { defaults.insert(QStringLiteral("attack"), source.value(QStringLiteral("attack"))); }
	if (defaults.isEmpty())
	{
		defaults = {{QStringLiteral("steps"), QJsonArray{1.0, -1.0, 0.5, -0.5}}, {QStringLiteral("attack"), 0.1}};
	}
	return defaults;
}

//! A button that acts on the selected modulator: ModulatorPanel::showSelected enables it only while one is.
QPushButton* selectionButton(const QString& label, const char* command, QWidget* parent)
{
	QPushButton* button = commandButton(label, command, parent);
	button->setProperty("needsModulator", true);
	return button;
}

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
	return modulationShapeNames();
}

} // namespace

ModulatorPanel::ModulatorPanel(QWidget* parent) :
	QDialog(parent)
{
	setWindowTitle(tr("Modulators"));
	setAccessibleName(tr("Modulators"));
	m_modulators = new QListWidget(this);
	m_modulators->setAccessibleName(tr("Modulators"));
	m_status = new QLabel(this);
	m_status->setAccessibleName(tr("Last refused command"));
	m_status->setWordWrap(true);
	m_shape = new QComboBox(this);
	m_shape->addItems(shapes());
	m_shape->setProperty("controlCommand", QStringLiteral("modulator.rate_set"));
	m_rate = new QDoubleSpinBox(this);
	m_rate->setRange(ModulationLayer::MinRateHz, ModulationLayer::MaxRateHz);  // what modulator.rate_set accepts
	m_rate->setDecimals(2);
	m_rate->setSuffix(tr(" Hz"));
	m_rate->setProperty("controlCommand", QStringLiteral("modulator.rate_set"));
	m_targets = new QTableWidget(0, 4, this);
	m_targets->setHorizontalHeaderLabels({tr("Channel"), tr("Effect"), tr("Parameter"), tr("Depth")});
	m_targets->horizontalHeader()->setStretchLastSection(true);
	m_targets->setSelectionBehavior(QAbstractItemView::SelectRows);
	m_targets->setEditTriggers(QAbstractItemView::NoEditTriggers);
	m_targets->setAccessibleName(tr("Parameters this modulator drives"));
	auto* channel = new RefreshingComboBox(this);
	m_channel = channel;
	auto* effect = new RefreshingComboBox(this);
	m_effect = effect;
	auto* parameter = new RefreshingComboBox(this);
	m_parameter = parameter;
	m_depth = new QDoubleSpinBox(this);
	m_depth->setRange(-1.0, 1.0);
	m_depth->setSingleStep(0.05);
	m_depth->setValue(0.25);
	m_channel->setAccessibleName(tr("Mixer channel to modulate"));
	m_channel->setToolTip(m_channel->accessibleName());
	m_effect->setAccessibleName(tr("Effect to modulate"));
	m_effect->setToolTip(m_effect->accessibleName());
	m_parameter->setAccessibleName(tr("Parameter to modulate"));
	m_parameter->setToolTip(m_parameter->accessibleName());
	m_depth->setAccessibleName(tr("Modulation depth"));
	m_depth->setToolTip(tr("Modulation depth, -1 to 1"));

	auto* add = commandButton(tr("Add LFO"), "modulator.create", this);
	auto* remove = selectionButton(tr("Remove"), "modulator.remove", this);
	auto* bindButton = selectionButton(tr("Bind"), "modulator.target_set", this);
	auto* unbind = selectionButton(tr("Unbind"), "modulator.target_remove", this);
	// The command a refused button ran reports why, and the user has to see it: the panel showed
	// nothing at all, so Add at the modulator limit (or Remove with nothing selected) just did nothing.
	connect(add, &QPushButton::clicked, this, [this] { say(addModulator(m_shape->currentText(), m_rate->value())); });
	connect(remove, &QPushButton::clicked, this, [this] { say(removeSelected()); });
	connect(bindButton, &QPushButton::clicked, this, [this] {
		say(bind(m_channel->currentData().toString(), m_effect->currentData().toInt(), m_parameter->currentText(),
			m_depth->value()));
	});
	connect(unbind, &QPushButton::clicked, this, [this] { say(unbindRow(m_targets->currentRow())); });
	connect(m_modulators, &QListWidget::currentRowChanged, this, [this] { showSelected(); });
	connect(m_shape, &QComboBox::currentTextChanged, this, [this] { setShape(m_shape->currentText()); });
	connect(m_rate, &QDoubleSpinBox::editingFinished, this, [this] { setShapeAndRate(m_shape->currentText(), m_rate->value()); });
	connect(m_channel, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { fillEffects(); });
	connect(m_effect, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] { fillParameters(); });
	channel->setRefresh([this] { fillChannels(); });
	effect->setRefresh([this] { fillEffects(); });
	parameter->setRefresh([this] { fillParameters(); });

	auto* listButtons = new QHBoxLayout;
	listButtons->addWidget(add);
	listButtons->addWidget(remove);
	listButtons->addStretch();
	auto* source = new QFormLayout;
	source->addRow(tr("Shape:"), m_shape);
	source->addRow(tr("Rate:"), m_rate);
	// MX-117: the step values and the envelope's attack, through the command's own form.
	// The form starts from the modulator's own values, so accepting it unchanged keeps them.
	auto* shapeValues = selectionButton(tr("Steps and Envelope..."), "modulator.rate_set", this);
	connect(shapeValues, &QPushButton::clicked, this, [this] {
		if (selectedId().isEmpty()) { return; }
		const QJsonObject source = modulatorById(selectedId()).value(QStringLiteral("source")).toObject();
		runCommand(this, CommandCall{QStringLiteral("modulator.rate_set"), {{QStringLiteral("modulator"), selectedId()}},
			shapeValueDefaults(source), {}, true});
	});
	source->addRow(QString(), shapeValues);
	// 0.5.0 L10 (MX-118 / MX-121): a formula of the modulator's own, and restarting it on a track's notes.
	auto* formula = selectionButton(tr("Formula..."), "modulator.formula_set", this);
	connect(formula, &QPushButton::clicked, this, [this] {
		if (selectedId().isEmpty()) { return; }
		const QString own = modulatorById(selectedId()).value(QStringLiteral("formula")).toString();
		runCommand(this, CommandCall{QStringLiteral("modulator.formula_set"), {{QStringLiteral("modulator"), selectedId()}},
			{{QStringLiteral("formula"), own.isEmpty() ? QStringLiteral("sin(2*pi*t)") : own}}, {}, true});
	});
	auto* follow = selectionButton(tr("Restart on a Track's Notes..."), "modulator.trigger_set", this);
	connect(follow, &QPushButton::clicked, this, [this] {
		if (selectedId().isEmpty()) { return; }
		runCommand(this, CommandCall{QStringLiteral("modulator.trigger_set"), {{QStringLiteral("modulator"), selectedId()}},
			selectionContext(QStringLiteral("modulator.trigger_set")), {}, true});
	});
	source->addRow(QString(), formula);
	source->addRow(QString(), follow);
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
	layout->addWidget(m_status);
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

void ModulatorPanel::say(const QString& message)
{
	m_status->setText(message);
	// A refusal is an error, so it is spoken at once rather than after whatever is being said.
	if (!message.isEmpty()) { a11y::announceStatus(m_status, message, a11y::Politeness::Assertive); }
}

void ModulatorPanel::showSelected()
{
	m_targets->setRowCount(0);
	const QString id = selectedId();
	for (QPushButton* button : findChildren<QPushButton*>())
	{
		if (button->property("needsModulator").toBool()) { button->setEnabled(!id.isEmpty()); }
	}
	const QJsonObject modulator = modulatorById(id);
	if (!modulator.isEmpty())
	{
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
	const QString selected = m_channel->currentData().toString();
	m_channel->clear();
	for (const QJsonValue& chain : channelChains())
	{
		const QJsonObject object = chain.toObject();
		if (object.value(QStringLiteral("count")).toInt() == 0) { continue; }
		m_channel->addItem(object.value(QStringLiteral("id")).toString(), object.value(QStringLiteral("id")).toString());
	}
	const int selectedIndex = m_channel->findData(selected);
	if (selectedIndex >= 0) { m_channel->setCurrentIndex(selectedIndex); }
	fillEffects();
}

void ModulatorPanel::fillEffects()
{
	const QSignalBlocker block(m_effect);
	const QString selected = m_effect->currentData(Qt::UserRole + 1).toString();
	m_effect->clear();
	for (const QJsonValue& chain : channelChains())
	{
		if (chain.toObject().value(QStringLiteral("id")).toString() != m_channel->currentData().toString()) { continue; }
		const QJsonArray devices = chain.toObject().value(QStringLiteral("devices")).toArray();
		for (int i = 0; i < devices.size(); ++i)
		{
			const QJsonObject device = devices.at(i).toObject();
			m_effect->addItem(QStringLiteral("fx-%1 %2").arg(i).arg(device.value(QStringLiteral("display_name")).toString()), i);
			m_effect->setItemData(m_effect->count() - 1, device.value(QStringLiteral("id")).toString(), Qt::UserRole + 1);
		}
	}
	const int selectedIndex = m_effect->findData(selected, Qt::UserRole + 1);
	if (selectedIndex >= 0) { m_effect->setCurrentIndex(selectedIndex); }
	fillParameters();
}

void ModulatorPanel::fillParameters()
{
	const QString selected = m_parameter->currentText();
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
	const int selectedIndex = m_parameter->findText(selected);
	if (selectedIndex >= 0) { m_parameter->setCurrentIndex(selectedIndex); }
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

QString ModulatorPanel::setShape(const QString& shape)
{
	if (selectedId().isEmpty()) { return tr("no modulator is selected"); }
	// The rate box shows two decimals, so its value is not the modulator's exact rate: sending it
	// back would round a rate like 1.234 Hz on a shape change. rate_set keeps what a call omits.
	const QString result = refusal(run("modulator.rate_set", {{QStringLiteral("modulator"), selectedId()},
		{QStringLiteral("shape"), shape}}));
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
