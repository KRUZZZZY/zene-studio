/*
 * AuKnobGrid.h - the knob grid of an Audio Unit effect's dialog and instrument's view (Q050-491, macOS only)
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

#ifndef LMMS_AU_KNOB_GRID_H
#define LMMS_AU_KNOB_GRID_H

#include <vector>

#include <QGridLayout>
#include <QLabel>
#include <QString>

#include "AuParameter.h"
#include "Knob.h"

namespace lmms::gui
{

//! One knob per writable parameter, six a row, @a parent owning them. Header-only: both host modules use it.
inline void fillAuKnobGrid(QGridLayout* grid, const std::vector<AuParamModel*>& models, QWidget* parent)
{
	constexpr int KnobsPerRow = 6;
	int visible = 0;
	for (AuParamModel* model : models)
	{
		// A parameter the unit does not let a host write would only snap back on the next poll.
		if (!model->info().writable) { continue; }
		auto* knob = new Knob(KnobType::Bright26, model->info().name, parent);
		knob->setModel(model);
		knob->setHintText(model->info().name, QString{});
		grid->addWidget(knob, visible / KnobsPerRow, visible % KnobsPerRow, Qt::AlignCenter);
		++visible;
	}
	if (visible == 0) { grid->addWidget(new QLabel(QObject::tr("This Audio Unit has no parameters a host can set."), parent), 0, 0); }
}

} // namespace lmms::gui

#endif // LMMS_AU_KNOB_GRID_H
