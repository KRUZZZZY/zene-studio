/*
 * Vst3InstrumentView.cpp - parameter view for the VST3 instrument host
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

#include "Vst3InstrumentView.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "Knob.h"
#include "Vst3Instrument.h"

namespace lmms::gui
{

namespace
{
//! number of knobs per row in the generated parameter view
constexpr int KnobsPerRow = 4;
}

Vst3InstrumentView::Vst3InstrumentView(Vst3Instrument* instrument, QWidget* parent) :
	InstrumentView(instrument, parent)
{
	setAutoFillBackground(true);

	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(8, 8, 8, 8);
	outer->setSpacing(6);

	outer->addWidget(new QLabel(tr("Controls for %1")
		.arg(instrument->vst3Plugin()->className()), this));

	auto* grid = new QGridLayout();
	grid->setSpacing(6);

	int visible = 0;
	for (auto* model : instrument->paramModels())
	{
		const auto& descriptor = model->descriptor();
		if (descriptor.hidden) { continue; }

		auto* knob = new Knob(KnobType::Bright26, descriptor.title, this);
		knob->setModel(model);
		knob->setHintText(descriptor.title, QString{});
		grid->addWidget(knob, visible / KnobsPerRow, visible % KnobsPerRow,
			Qt::AlignCenter);
		++visible;
	}

	if (visible == 0)
	{
		grid->addWidget(new QLabel(tr("This instrument exposes no visible parameters."),
			this), 0, 0);
	}
	outer->addLayout(grid);

	// Disclosed, not hidden: the instrument's own window is not available.
	// A plug-in with no user interface at all (the in-tree test fixture is
	// one) is therefore perfectly usable, and this label is all a user sees.
	auto* note = new QLabel(tr("The instrument's own editor is not shown in this "
		"version - these are its parameters."), this);
	note->setWordWrap(true);
	outer->addWidget(note);
	outer->addStretch();

	setLayout(outer);
	adjustSize();
	setFixedSize(sizeHint());
}

} // namespace lmms::gui
