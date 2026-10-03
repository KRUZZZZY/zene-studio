/*
 * ReceiverControlDialog.cpp - the Receiver device's panel (0.5.0 MX-063)
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
#include "ReceiverControlDialog.h"

#include <QGridLayout>

#include "Knob.h"
#include "LedCheckBox.h"
#include "ReceiverControls.h"

namespace lmms::gui
{

ReceiverControlDialog::ReceiverControlDialog(ReceiverControls* controls) :
	EffectControlDialog(controls)
{
	auto* grid = new QGridLayout(this);
	auto* level = new Knob(KnobType::Bright26, tr("LEVEL"), this);
	level->setModel(&controls->m_levelModel);
	level->setHintText(tr("Level:"), QString());
	grid->addWidget(level, 0, 0, Qt::AlignHCenter);
	auto* replace = new LedCheckBox(tr("Replace"), this, tr("Replace"), LedCheckBox::LedColor::Green);
	replace->setModel(&controls->m_replaceModel);
	grid->addWidget(replace, 1, 0);
	grid->setSizeConstraint(QLayout::SetFixedSize);
}

} // namespace lmms::gui
