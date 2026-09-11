/*
 * MidiLearnGui.h - GUI side of global MIDI learn: tracks the control the user
 *                  focused and keeps the menu action in sync.
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

#ifndef LMMS_GUI_MIDI_LEARN_GUI_H
#define LMMS_GUI_MIDI_LEARN_GUI_H

#include <QObject>

#include "lmms_export.h"

class QAction;

namespace lmms::gui
{

//! GUI half of the global MIDI-learn mode.
//
// While learn mode is armed this installs itself as an application event filter
// and remembers the model-backed widget the user last pressed or focused. The
// filter is only installed while armed, so with learn mode off (the default) the
// GUI event stream is untouched.
class LMMS_EXPORT MidiLearnGui : public QObject
{
	Q_OBJECT
public:
	static MidiLearnGui* instance();

	//! Arm/disarm learn mode. Installing the filter is the only GUI side effect.
	void setArmed(bool armed);
	bool isArmed() const;

	//! The checkable menu action to keep in sync (may be null).
	void setAction(QAction* action);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	MidiLearnGui() = default;

	QAction* m_action = nullptr;
};

} // namespace lmms::gui

#endif // LMMS_GUI_MIDI_LEARN_GUI_H
