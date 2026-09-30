/*
 * Vst3InstrumentView.h - parameter view for the VST3 instrument host
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

#ifndef LMMS_VST3_INSTRUMENT_VIEW_H
#define LMMS_VST3_INSTRUMENT_VIEW_H

#include "InstrumentView.h"

namespace lmms
{

class Vst3Instrument;

namespace gui
{

/**
 * One knob per visible VST3 parameter, rendered with the plug-in's own
 * value-to-string conversion - the same generated grid the VST3 effect host
 * uses (plugins/Vst3Effect/Vst3EffectControlDialog.cpp).
 *
 * This is NOT the plug-in's own editor: that opens in a window of its own (R4.2/R4.4,
 * Vst3EditorSession + PluginNativeEditor), from this view's "Show plugin editor" button or
 * plugin.editor_open. The grid stays as the fallback and the agent-visible parameter view.
 *
 * A plug-in with no parameters, or with every parameter hidden, is handled
 * too: the view then says so instead of showing an empty grid.
 */
class Vst3InstrumentView : public InstrumentView
{
	Q_OBJECT
public:
	explicit Vst3InstrumentView(Vst3Instrument* instrument, QWidget* parent);
};

} // namespace gui

} // namespace lmms

#endif // LMMS_VST3_INSTRUMENT_VIEW_H
