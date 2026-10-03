/*
 * ParameterGridDialog.cpp - a device's control dialog drawn from its parameter list
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

#include "ParameterGridDialog.h"

#include <QGridLayout>

#include "AutomatableModel.h"
#include "ComboBox.h"
#include "ComboBoxModel.h"
#include "Knob.h"
#include "LedCheckBox.h"
#include "ParameterControls.h"

namespace lmms::gui
{

namespace
{

constexpr int kColumns = 4;

//! The widget for one model, or nullptr for a kind the grid does not draw (an integer).
QWidget* controlFor(AutomatableModel* model, QWidget* parent)
{
	const QString name = model->displayName();
	if (auto* choice = qobject_cast<ComboBoxModel*>(model))
	{
		auto* box = new ComboBox(parent);
		box->setModel(choice);
		box->setFixedHeight(ComboBox::DEFAULT_HEIGHT);
		box->setToolTip(name);
		return box;
	}
	if (auto* toggle = qobject_cast<BoolModel*>(model))
	{
		auto* led = new LedCheckBox(name, parent);
		led->setModel(toggle);
		return led;
	}
	if (auto* number = qobject_cast<FloatModel*>(model))
	{
		auto* knob = new Knob(KnobType::Bright26, name.left(8).toUpper(), parent);
		knob->setModel(number);
		knob->setHintText(name + QLatin1Char(':'), QString());
		return knob;
	}
	return nullptr;
}

} // namespace

ParameterGridDialog::ParameterGridDialog(ParameterControls* controls) :
	EffectControlDialog(controls)
{
	auto* grid = new QGridLayout(this);
	int row = 0;
	QList<QWidget*> knobs;
	QList<QWidget*> switches;
	for (const auto& [model, key] : controls->parameters())
	{
		QWidget* widget = controlFor(model, this);
		if (widget == nullptr) { continue; }
		if (qobject_cast<ComboBoxModel*>(model) != nullptr) { grid->addWidget(widget, row++, 0, 1, kColumns); continue; }
		(qobject_cast<BoolModel*>(model) != nullptr ? switches : knobs) << widget;
	}
	// The choices on top whatever order the device registered them in, then the knobs, then the switches.
	for (int i = 0; i < knobs.size(); ++i) { grid->addWidget(knobs.at(i), row + i / kColumns, i % kColumns, Qt::AlignHCenter); }
	row += (static_cast<int>(knobs.size()) + kColumns - 1) / kColumns;
	for (int i = 0; i < switches.size(); ++i) { grid->addWidget(switches.at(i), row + i / 2, (i % 2) * 2, 1, 2); }
	grid->setSizeConstraint(QLayout::SetFixedSize);
}

} // namespace lmms::gui
