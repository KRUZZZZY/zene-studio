/*
 * ClapEffectControlDialog.cpp - parameter view for the CLAP effect host
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

#include "ClapEffectControlDialog.h"

#include <QGridLayout>
#include <QPalette>
#include <QVBoxLayout>

#include "ClapEffect.h"
#include "ClapEffectControls.h"
#include "ClapKnobGrid.h"
#include "PluginNativeEditor.h"
#include "embed.h"

namespace lmms::gui
{

ClapEffectControlDialog::ClapEffectControlDialog(ClapEffectControls* controls) :
	EffectControlDialog(controls)
{
	setAutoFillBackground(true);
	QPalette pal;
	pal.setBrush(backgroundRole(), PLUGIN_NAME::getIconPixmap("logo"));
	setPalette(pal);

	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);
	auto* grid = new QGridLayout();
	grid->setSpacing(6);
	layout->addLayout(grid);
	fillClapKnobGrid(grid, controls->paramModels(), this, tr("This plug-in has no visible parameters."));
	// R4.4: the plug-in's own editor, beside the generic knobs, through plugin.editor_open.
	layout->addWidget(makeNativeEditorButton(controls->effect(), this));

	setLayout(layout);
	adjustSize();
	setFixedSize(sizeHint());
	// W6: a parameter rescan rebuilt the models - the knobs follow (names, ranges, added and removed ones).
	connect(controls, &ClapEffectControls::parametersChanged, this, [this, controls, grid] {
		fillClapKnobGrid(grid, controls->paramModels(), this, tr("This plug-in has no visible parameters."));
		setFixedSize(sizeHint());
	});
}

} // namespace lmms::gui
