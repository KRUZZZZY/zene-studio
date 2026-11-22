/*
 * AuInstrumentView.h - an Audio Unit instrument's generated controls (Q050-491, macOS only)
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

#ifndef LMMS_AU_INSTRUMENT_VIEW_H
#define LMMS_AU_INSTRUMENT_VIEW_H

#include "InstrumentView.h"

namespace lmms
{

class AuInstrument;

namespace gui
{

//! The unit's name, one knob per parameter and "Show plugin editor" (plugin.editor_open, its Cocoa view).
class AuInstrumentView : public InstrumentView
{
	Q_OBJECT
public:
	AuInstrumentView(AuInstrument* instrument, QWidget* parent);
};

} // namespace gui

} // namespace lmms

#endif // LMMS_AU_INSTRUMENT_VIEW_H
