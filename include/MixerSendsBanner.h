/*
 * MixerSendsBanner.h - the mixer's mode banner while every fader drives one return's sends
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

#ifndef LMMS_GUI_MIXER_SENDS_BANNER_H
#define LMMS_GUI_MIXER_SENDS_BANNER_H

#include <QWidget>

#include "lmms_export.h"

class QComboBox;
class QLabel;
class QPushButton;

namespace lmms::gui
{

/*! 0.5.0 theme 6 (the owner's decision: one chosen send flips across every fader, Pro Tools / Logic
 *  style). While the faders show one return's sends (mixeroverlays::sendsOnFaders()) this bar runs across
 *  the top of the mixer in the accent colour and names that return, so no one has to guess whether a
 *  fader is a level or a send. Its picker moves the faders to another return and its button puts them
 *  back on the channels' levels - both through window.mixer_view, like every other way in. Hidden on the
 *  channel faders. MixerView::syncWithMixer refreshes it with the strips. */
class LMMS_EXPORT MixerSendsBanner : public QWidget
{
	Q_OBJECT
public:
	explicit MixerSendsBanner(QWidget* parent);

	//! Follow the mode: shown and naming the return while there is one, hidden otherwise.
	void refresh();

	QLabel* message() const { return m_message; }
	QComboBox* picker() const { return m_picker; }
	QPushButton* exitButton() const { return m_exit; }

private:
	//! Put the faders on @a channel's sends (empty: back on the channel faders).
	void choose(const QString& channel);
	//! The returns the picker offers, and @a target (a channel index) chosen in it.
	void fillPicker(int target);

	QLabel* m_message;
	QComboBox* m_picker;
	QPushButton* m_exit;
	QString m_pickerSignature;  //!< what the picker was last filled from (ids and names)
};

} // namespace lmms::gui

#endif // LMMS_GUI_MIXER_SENDS_BANNER_H
