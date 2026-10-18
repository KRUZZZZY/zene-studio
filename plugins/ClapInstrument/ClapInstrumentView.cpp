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

#include "PluginNativeEditor.h"

#include "ClapHost.h"
#include "ClapInstrument.h"
#include "ClapKnobGrid.h"

namespace lmms::gui
{

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
	fillClapKnobGrid(grid, instrument->paramModels(), this, tr("This instrument exposes no visible parameters."));
	outer->addLayout(grid);
	// W6: a parameter rescan rebuilt the models - the knobs follow (names, ranges, added and removed ones).
	connect(instrument, &ClapInstrument::parametersChanged, this, [this, instrument, grid] {
		fillClapKnobGrid(grid, instrument->paramModels(), this, tr("This instrument exposes no visible parameters."));
		setFixedSize(sizeHint());
	});

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

	auto* outputs = new QLabel(this);
	outputs->setWordWrap(true);
	auto updateOutputs = [plugin, outputs] {
		outputs->setText(tr("Audio output: %1 channel(s) (clap.audio-ports).")
			.arg(plugin->busLayout().outputs));
	};
	updateOutputs();
	outer->addWidget(outputs);
	// BUG 6: a plug-in may change its audio-port layout while it runs; reprepare() re-reads it and says so,
	// and the channel count on the surface follows the new layout instead of keeping the old one.
	connect(instrument, &ClapInstrument::outputLayoutChanged, this, [this, updateOutputs] {
		updateOutputs();
		setFixedSize(sizeHint());
	});

	// R4.3/R4.4: the instrument's own (clap.gui) editor opens in a window of its own; these
	// knobs stay as the fallback. A plug-in with no embeddable gui says so in place.
	outer->addWidget(makeNativeEditorButton(instrument, this));
	outer->addStretch();

	setLayout(outer);
	adjustSize();
	setFixedSize(sizeHint());
}

} // namespace lmms::gui
