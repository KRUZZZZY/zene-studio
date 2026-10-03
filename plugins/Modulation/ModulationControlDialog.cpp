/*
 * ModulationControlDialog.cpp - the Modulation device's knobs
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
#include "ModulationControlDialog.h"

#include <QGridLayout>

#include "ComboBox.h"
#include "Knob.h"
#include "ModulationControls.h"

namespace lmms::gui
{

ModulationControlDialog::ModulationControlDialog(ModulationControls* controls) :
	EffectControlDialog(controls)
{
	auto* grid = new QGridLayout(this);
	auto* mode = new ComboBox(this);
	mode->setModel(&controls->m_modeModel);
	mode->setFixedHeight(ComboBox::DEFAULT_HEIGHT);
	grid->addWidget(mode, 0, 0, 1, 4);
	const std::tuple<QString, QString, QString, FloatModel*> knobs[] = {
		{tr("RATE"), tr("Rate:"), QStringLiteral(" Hz"), &controls->m_rateModel},
		{tr("DEPTH"), tr("Depth:"), QString(), &controls->m_depthModel},
		{tr("FDBK"), tr("Feedback:"), QString(), &controls->m_feedbackModel},
		{tr("SPREAD"), tr("Stereo spread:"), QStringLiteral(" deg"), &controls->m_spreadModel}};
	int column = 0;
	for (const auto& [label, hint, unit, model] : knobs)
	{
		auto* knob = new Knob(KnobType::Bright26, label, this);
		knob->setModel(model);
		knob->setHintText(hint, unit);
		grid->addWidget(knob, 1, column++, Qt::AlignHCenter);
	}
	grid->setSizeConstraint(QLayout::SetFixedSize);
}

} // namespace lmms::gui
