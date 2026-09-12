/*
 * MidiLearnGui.h - GUI side of global MIDI learn.
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
#include <QPointer>
#include <QTimer>

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
//
// It also owns the *delivery* of a learn: a control-change arrives on the MIDI
// input thread, which may not touch a model, so it leaves the request in
// MidiLearn's lock-free slot and this object's timer drains it on the GUI
// thread. A short poll is used deliberately - posting a queued invocation or an
// event would allocate on the MIDI input thread, which is exactly what the
// hand-off exists to avoid (see docs/MIDI-LEARN-RACE.md).
class LMMS_EXPORT MidiLearnGui : public QObject
{
	Q_OBJECT
public:
	static MidiLearnGui* instance();

	//! Arm/disarm learn mode. Installing the filter is the only GUI side effect.
	void setArmed(bool armed);
	bool isArmed() const;

	//! The checkable menu action to keep in sync (may be null).
	//!
	//! Held through QPointer, not a raw pointer: `instance()` is a function-local
	//! static that outlives the window that owns the action, so a raw pointer
	//! here is a use-after-free waiting for the next `setArmed()` call - which is
	//! exactly what the offscreen GUI test hit (a stack-local QAction outlived by
	//! this singleton crashed `setArmed(false)` at the dereference below). A
	//! QPointer clears itself when the action dies, so the "may be null" contract
	//! this class documents is what it actually enforces.
	void setAction(QAction* action);

protected:
	bool eventFilter(QObject* watched, QEvent* event) override;

private:
	MidiLearnGui();

	//! Drain a learn request the MIDI input thread left behind, then reconcile
	//! the armed state. Runs on the GUI thread - this is the thread the binding
	//! is created on.
	void onBindTimer();

	//! Bring the menu tick, the event filter and the timer in line with the real
	//! armed state, whoever ended the learn: the user (the menu action) or a
	//! control-change on the MIDI input thread. Without this the Edit menu tick
	//! stays set until the menu is reopened.
	void syncArmedState();

	//! Guarded: clears itself when the action is destroyed (see setAction).
	QPointer<QAction> m_action = nullptr;

	//! How often the GUI thread checks for a learn the MIDI input thread left
	//! behind. Only runs while learn mode is armed, and each tick is one atomic
	//! load when there is nothing to do.
	QTimer m_bindTimer;
};

} // namespace lmms::gui

#endif // LMMS_GUI_MIDI_LEARN_GUI_H
