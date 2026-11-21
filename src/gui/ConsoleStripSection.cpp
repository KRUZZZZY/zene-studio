/*
 * ConsoleStripSection.cpp - the mixer strip's optional Console section
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

#include "ConsoleStripSection.h"

#include <QGridLayout>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QToolButton>
#include <QVBoxLayout>

#include "AutomatableModel.h"
#include "ConsoleStrip.h"
#include "ControlMixerSupport.h"
#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Knob.h"

namespace lmms::gui
{

struct ConsoleStripSection::Field
{
	QString name;
	bool console = false;
	Knob* knob = nullptr;
	QToolButton* button = nullptr;
	std::unique_ptr<FloatModel> model;
};

namespace
{

constexpr int Columns = 3;

} // namespace

ConsoleStripSection::ConsoleStripSection(const QString& channel, QWidget* parent) :
	QWidget(parent),
	m_channel(channel)
{
	setObjectName(QStringLiteral("consoleStripSection"));
	setAccessibleName(tr("Console section"));
	auto* column = new QVBoxLayout(this);
	column->setContentsMargins(0, 2, 0, 2);
	column->setSpacing(1);
	auto* title = new QLabel(tr("Console"), this);
	title->setFont(tokens::font(tokens::TextRole::Caption));
	title->setAlignment(Qt::AlignHCenter);
	column->addWidget(title);
	column->addLayout(new QGridLayout);
	addKnob("trim_db", tr("Trim"), -24.f, 24.f, 0.1f, false);
	const QString inverted(QChar(0x00D8));  // the polarity sign, as a code point (no source-charset dependence)
	addSwitch("polarity_left", inverted + tr("L"), false);
	addSwitch("polarity_right", inverted + tr("R"), false);
	addKnob("eq_low_db", tr("EQ low"), -18.f, 18.f, 0.1f, false);
	addKnob("eq_mid_db", tr("EQ mid"), -18.f, 18.f, 0.1f, false);
	addKnob("eq_high_db", tr("EQ high"), -18.f, 18.f, 0.1f, false);
	addKnob("eq_mid_freq", tr("EQ mid frequency"), 100.f, 10000.f, 1.f, false);
	addKnob("drive", tr("Console drive"), 0.f, 100.f, 0.1f, true);
	addSwitch("glue", tr("Glue"), true);
	addKnob("glue_threshold_db", tr("Glue threshold"), -40.f, 0.f, 0.1f, true);
	addKnob("glue_ratio", tr("Glue ratio"), 1.f, 20.f, 0.1f, true);
	addKnob("glue_makeup_db", tr("Glue makeup"), 0.f, 24.f, 0.1f, true);
	m_noConsole = new QLabel(tr("No Console"), this);
	m_noConsole->setObjectName(QStringLiteral("consoleStripNoConsole"));
	m_noConsole->setFont(tokens::font(tokens::TextRole::Caption));
	m_noConsole->setAlignment(Qt::AlignHCenter);
	m_noConsole->setToolTip(tr("This channel has no Console device - Console Across the Mix puts one on every channel"));
	column->addWidget(m_noConsole);
	refresh();
}

ConsoleStripSection::~ConsoleStripSection()
{
	// The knobs go before the models they show.
	for (const auto& field : m_fields) { delete field->knob; }
}

void ConsoleStripSection::addKnob(const char* field, const QString& label, float low, float high, float step, bool console)
{
	auto entry = std::make_unique<Field>();
	entry->name = QLatin1String(field);
	entry->console = console;
	entry->model = std::make_unique<FloatModel>(low, low, high, step, nullptr, label);
	entry->model->setJournalling(false);  // the undo step is mixer.console_strip's, not this view's
	entry->knob = new Knob(KnobType::Small17, this, label);
	entry->knob->setModel(entry->model.get());
	entry->knob->setHintText(label + QLatin1Char(':'), QString());
	entry->knob->setAccessibleName(label);
	entry->knob->setToolTip(label);
	entry->knob->setProperty("controlCommand", QStringLiteral("mixer.console_strip"));
	const QString name = entry->name;
	FloatModel* model = entry->model.get();
	connect(model, &FloatModel::dataChanged, this, [this, name, model] { send(name, static_cast<double>(model->value())); });
	auto* grid = static_cast<QGridLayout*>(layout()->itemAt(1)->layout());
	const int at = static_cast<int>(m_fields.size());
	grid->addWidget(entry->knob, at / Columns, at % Columns, Qt::AlignHCenter);
	m_fields.push_back(std::move(entry));
}

void ConsoleStripSection::addSwitch(const char* field, const QString& label, bool console)
{
	auto entry = std::make_unique<Field>();
	entry->name = QLatin1String(field);
	entry->console = console;
	entry->button = new QToolButton(this);
	entry->button->setCheckable(true);
	entry->button->setText(label);
	entry->button->setAccessibleName(label);
	entry->button->setToolTip(console ? tr("Glue compressor on or off") : tr("Invert this side's polarity"));
	entry->button->setProperty("controlCommand", QStringLiteral("mixer.console_strip"));
	const QString name = entry->name;
	QToolButton* button = entry->button;
	connect(button, &QToolButton::toggled, this, [this, name](bool on) { send(name, on); });
	auto* grid = static_cast<QGridLayout*>(layout()->itemAt(1)->layout());
	const int at = static_cast<int>(m_fields.size());
	grid->addWidget(entry->button, at / Columns, at % Columns, Qt::AlignHCenter);
	m_fields.push_back(std::move(entry));
}

void ConsoleStripSection::send(const QString& field, const QJsonValue& value)
{
	if (m_syncing) { return; }
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("mixer.console_strip"),
		{{QStringLiteral("channel"), m_channel}, {field, value}});
	if (!result.ok) { refresh(); }  // refused: the control shows the value the channel kept
}

void ConsoleStripSection::refresh()
{
	ControlResult ignored;
	const MixerChannel* channel = control::resolveMixerChannel(m_channel, &ignored);
	if (channel == nullptr) { return; }
	const QJsonObject values = consolestrip::toJson(consolestrip::read(*channel));
	const bool hasConsole = values.value(QStringLiteral("console")).toBool();
	m_syncing = true;
	for (const auto& field : m_fields)
	{
		const QJsonValue value = values.value(field->name);
		if (field->model != nullptr && !value.isNull() && field->model->value() != static_cast<float>(value.toDouble()))
		{
			field->model->setValue(static_cast<float>(value.toDouble()));
		}
		if (field->button != nullptr && field->button->isChecked() != value.toBool()) { field->button->setChecked(value.toBool()); }
		QWidget* widget = field->knob != nullptr ? static_cast<QWidget*>(field->knob) : field->button;
		widget->setEnabled(!field->console || hasConsole);
	}
	m_syncing = false;
	m_noConsole->setVisible(!hasConsole);
}

QWidget* ConsoleStripSection::control(const QString& field) const
{
	for (const auto& entry : m_fields)
	{
		if (entry->name == field) { return entry->knob != nullptr ? static_cast<QWidget*>(entry->knob) : entry->button; }
	}
	return nullptr;
}

} // namespace lmms::gui
