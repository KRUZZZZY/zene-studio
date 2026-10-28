/*
 * ModularSynthView.h - Q050-059: the modular instrument's window - a knob for every module parameter and the
 *                      graph's wiring
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_GUI_MODULAR_SYNTH_VIEW_H
#define LMMS_GUI_MODULAR_SYNTH_VIEW_H

#include "InstrumentView.h"

class QScrollArea;

namespace lmms
{

class ModularSynth;

namespace gui
{

//! One row per module (its label and a knob bound to each of its parameters), then the wiring; rebuilt whenever
//! the instrument's graph is replaced.
class ModularSynthView : public InstrumentViewFixedSize
{
	Q_OBJECT
public:
	ModularSynthView(Instrument* instrument, QWidget* parent);
	~ModularSynthView() override = default;

protected:
	void paintEvent(QPaintEvent* event) override;

private slots:
	void rebuild();

private:
	void modelChanged() override;

	QScrollArea* m_scroll;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_MODULAR_SYNTH_VIEW_H
