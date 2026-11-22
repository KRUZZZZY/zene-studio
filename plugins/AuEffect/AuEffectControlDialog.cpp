/*
 * AuEffectControlDialog.cpp - an Audio Unit effect's generated controls (Q050-491, macOS only)
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

#include "AuEffectControlDialog.h"

#include <QGridLayout>
#include <QVBoxLayout>

#include "AuEffectControls.h"
#include "AuKnobGrid.h"
#include "PluginNativeEditor.h"

namespace lmms::gui
{

AuEffectControlDialog::AuEffectControlDialog(AuEffectControls* controls) :
	EffectControlDialog(controls)
{
	auto* layout = new QVBoxLayout(this);
	layout->setContentsMargins(8, 8, 8, 8);
	layout->setSpacing(6);
	auto* grid = new QGridLayout();
	grid->setSpacing(6);
	layout->addLayout(grid);
	fillAuKnobGrid(grid, controls->paramModels(), this);
	layout->addWidget(makeNativeEditorButton(controls->effect(), this));
	setLayout(layout);
	adjustSize();
	setFixedSize(sizeHint());
}

} // namespace lmms::gui
