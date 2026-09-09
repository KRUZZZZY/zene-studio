/*
 * ClapEffectControlDialog.cpp - parameter view for the CLAP effect host
 *
 * Copyright (c) 2026 LMMS contributors
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

#include "ClapEffectControlDialog.h"

#include <QGridLayout>
#include <QLabel>
#include <QPalette>

#include "ClapEffect.h"
#include "ClapEffectControls.h"
#include "Knob.h"
#include "embed.h"

namespace lmms::gui
{

namespace
{
//! number of knobs per row in the generated parameter view
constexpr int KnobsPerRow = 4;
}

ClapEffectControlDialog::ClapEffectControlDialog(ClapEffectControls* controls) :
	EffectControlDialog(controls)
{
	setAutoFillBackground(true);
	QPalette pal;
	pal.setBrush(backgroundRole(), PLUGIN_NAME::getIconPixmap("logo"));
	setPalette(pal);

	auto* layout = new QGridLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);

	int visible = 0;
	for (auto* model : controls->paramModels())
	{
		const auto& descriptor = model->descriptor();
		if (descriptor.hidden) { continue; }

		auto* knob = new Knob(KnobType::Bright26, descriptor.title, this);
		knob->setModel(model);
		knob->setHintText(descriptor.title, QString{});
		layout->addWidget(knob, visible / KnobsPerRow, visible % KnobsPerRow,
			Qt::AlignCenter);
		++visible;
	}

	if (visible == 0)
	{
		layout->addWidget(new QLabel(tr("This plug-in has no visible parameters."), this),
			0, 0);
	}

	setLayout(layout);
	adjustSize();
	setFixedSize(sizeHint());
}

} // namespace lmms::gui
