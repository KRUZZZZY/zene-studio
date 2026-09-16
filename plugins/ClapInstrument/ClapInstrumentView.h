/*
 * ClapInstrumentView.h - parameter view for the CLAP instrument host
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

#ifndef LMMS_CLAP_INSTRUMENT_VIEW_H
#define LMMS_CLAP_INSTRUMENT_VIEW_H

#include "InstrumentView.h"

namespace lmms
{

class ClapInstrument;

namespace gui
{

/**
 * One knob per visible CLAP parameter, rendered with the plug-in's own
 * value-to-string conversion - the same generated grid the CLAP effect host
 * uses (plugins/ClapEffect/ClapEffectControlDialog.cpp).
 *
 * This is NOT the plug-in's own editor. `CLAP_EXT_GUI` is not fetched anywhere
 * in this tree (the check is on ClapHost.cpp), so a user of a CLAP instrument
 * gets this grid and not the instrument's own window. That limitation is
 * stated in the view itself and in docs/KNOWN-LIMITATIONS.md.
 *
 * A plug-in with no parameters, or with every parameter hidden, is handled
 * too: the view then says so instead of showing an empty grid.
 */
class ClapInstrumentView : public InstrumentView
{
	Q_OBJECT
public:
	explicit ClapInstrumentView(ClapInstrument* instrument, QWidget* parent);
};

} // namespace gui

} // namespace lmms

#endif // LMMS_CLAP_INSTRUMENT_VIEW_H
