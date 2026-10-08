/*
 * ClapKnobGrid.h - the generated knob grid of the CLAP hosts
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

#ifndef LMMS_CLAP_KNOB_GRID_H
#define LMMS_CLAP_KNOB_GRID_H

#include <vector>

#include <QGridLayout>
#include <QLabel>
#include <QString>

#include "ClapParameter.h"
#include "Knob.h"

namespace lmms::gui
{

//! The knob grid of the CLAP effect's dialog and the CLAP instrument's view: @a grid emptied, then one knob per
//! visible parameter of @a models, four a row - or @a emptyText when none is visible - with @a parent owning the
//! widgets. Run again when the plug-in rescans its parameters (W6). The number of rows it used.
inline int fillClapKnobGrid(QGridLayout* grid, const std::vector<ClapParamModel*>& models, QWidget* parent,
	const QString& emptyText)
{
	constexpr int KnobsPerRow = 4;
	while (QLayoutItem* item = grid->takeAt(0))
	{
		if (QWidget* widget = item->widget())
		{
			widget->hide();
			widget->deleteLater();
		}
		delete item;
	}
	int visible = 0;
	for (auto* model : models)
	{
		const auto& descriptor = model->descriptor();
		// A read-only parameter ignores every write (setParamPlain drops it), so an editable knob
		// would only move and snap back on the next poll - it is not offered at all.
		if (descriptor.hidden || descriptor.readOnly) { continue; }
		auto* knob = new Knob(KnobType::Bright26, descriptor.title, parent);
		knob->setModel(model);
		knob->setHintText(descriptor.title, QString{});
		grid->addWidget(knob, visible / KnobsPerRow, visible % KnobsPerRow, Qt::AlignCenter);
		++visible;
	}
	if (visible == 0)
	{
		grid->addWidget(new QLabel(emptyText, parent), 0, 0);
		return 1;
	}
	return (visible + KnobsPerRow - 1) / KnobsPerRow;
}

} // namespace lmms::gui

#endif // LMMS_CLAP_KNOB_GRID_H
