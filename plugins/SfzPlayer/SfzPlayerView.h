/*
 * SfzPlayerView.h - the SFZ Player's window
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#ifndef LMMS_GUI_SFZ_PLAYER_VIEW_H
#define LMMS_GUI_SFZ_PLAYER_VIEW_H

#include "InstrumentView.h"

class QPushButton;

namespace lmms
{

class SfzPlayer;

namespace gui
{

class ComboBox;
class Knob;
class LcdSpinBox;

//! The file that is loaded, a button to open another one, and the controls the agent reads as parameters.
class SfzPlayerView : public InstrumentViewFixedSize
{
	Q_OBJECT
public:
	SfzPlayerView(Instrument* instrument, QWidget* parent);
	~SfzPlayerView() override = default;

protected:
	void paintEvent(QPaintEvent* event) override;

private slots:
	void chooseFile();

private:
	void modelChanged() override;
	SfzPlayer* player();

	QPushButton* m_openButton;
	Knob* m_volumeKnob;
	LcdSpinBox* m_qualityBox;
	LcdSpinBox* m_polyphonyBox;
	ComboBox* m_oversamplingBox;
};

} // namespace gui

} // namespace lmms

#endif // LMMS_GUI_SFZ_PLAYER_VIEW_H
