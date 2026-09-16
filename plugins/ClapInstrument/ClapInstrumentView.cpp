/*
 * ClapInstrumentView.cpp - parameter view for the CLAP instrument host
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

#include "ClapInstrumentView.h"

#include <QGridLayout>
#include <QLabel>
#include <QVBoxLayout>

#include "ClapHost.h"
#include "ClapInstrument.h"
#include "Knob.h"

namespace lmms::gui
{

namespace
{
//! number of knobs per row in the generated parameter view
constexpr int KnobsPerRow = 4;
}

ClapInstrumentView::ClapInstrumentView(ClapInstrument* instrument, QWidget* parent) :
	InstrumentView(instrument, parent)
{
	setAutoFillBackground(true);

	auto* outer = new QVBoxLayout(this);
	outer->setContentsMargins(8, 8, 8, 8);
	outer->setSpacing(6);

	// The class name is what the plug-in's own factory declared, and what a
	// dev-<n> id names: the same string plugin.list reports.
	auto* plugin = instrument->clapPlugin();
	outer->addWidget(new QLabel(tr("Controls for %1").arg(plugin->className()), this));

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

	// The two facts an agent or a user needs to see about the note path and
	// the audio-output configuration, on the surface rather than in a log.
	auto* notePorts = new QLabel(plugin->acceptsNotes()
		? tr("Notes: %1 input port(s) (clap.note-ports) - notes from the track's "
			 "clips and from live input reach the instrument.")
			.arg(plugin->noteInputPorts().size())
		: tr("Notes: this plug-in declares no note input port, so track MIDI is "
			 "not delivered to it."), this);
	notePorts->setWordWrap(true);
	outer->addWidget(notePorts);

	auto* outputs = new QLabel(tr("Audio output: %1 channel(s) (clap.audio-ports).")
		.arg(plugin->busLayout().outputs), this);
	outputs->setWordWrap(true);
	outer->addWidget(outputs);

	// Disclosed, not hidden: the instrument's own window is not available.
	auto* note = new QLabel(tr("The instrument's own editor is not shown in this "
		"version (CLAP_EXT_GUI is not implemented) - these are its parameters."), this);
	note->setWordWrap(true);
	outer->addWidget(note);
	outer->addStretch();

	setLayout(outer);
	adjustSize();
	setFixedSize(sizeHint());
}

} // namespace lmms::gui
