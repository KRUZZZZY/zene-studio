/*
 * MidiController.h - A controller to receive MIDI control-changes
 *
 * Copyright (c) 2008 Paul Giblock <drfaygo/at/gmail.com>
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

#ifndef LMMS_MIDI_CONTROLLER_H
#define LMMS_MIDI_CONTROLLER_H

#include <QWidget>

#include "AutomatableModel.h"
#include "Controller.h"
#include "MidiEventProcessor.h"
#include "MidiPort.h"


namespace lmms
{

class MidiPort;

namespace gui
{
class ControllerConnectionDialog;
}


class MidiController : public Controller, public MidiEventProcessor
{
	Q_OBJECT
public:
	static constexpr int NONE = -1;
	MidiController( Model * _parent );
	~MidiController() override = default;

	void processInEvent( const MidiEvent & _me,
					const TimePos & _time, f_cnt_t offset = 0 ) override;

	void processOutEvent( const MidiEvent& _me,
					const TimePos & _time, f_cnt_t offset = 0 ) override;

	void saveSettings( QDomDocument & _doc, QDomElement & _this ) override;
	void loadSettings( const QDomElement & _this ) override;
	QString nodeName() const override;

	// Used by controllerConnectionDialog to copy
	void subscribeReadablePorts( const MidiPort::Map & _map );

	// The port this controller listens on. Public so MIDI learn can configure a
	// fresh binding, and so the binding can be read back (channel, controller
	// number, subscribed ports) by the UI and the tests.
	MidiPort & midiPort()
	{
		return m_midiPort;
	}
	const MidiPort & midiPort() const
	{
		return m_midiPort;
	}

	// Soft-takeover: a hardware control only takes over when it crosses the
	// stored value. Enabled per-controller so a surface can mix bound styles.
	bool softTakeoverEnabled() const { return m_softTakeoverEnabled; }
	void setSoftTakeoverEnabled(bool enabled);
	void setSoftTakeoverTarget(float target); // 0..1
	float softTakeoverTarget() const { return m_softTakeoverTarget; }
	bool softTakeoverCaptured() const { return m_softTakeoverCaptured; }
	void resetSoftTakeover();

	// LED/feedback output: send the current value back to the controller.
	bool feedbackEnabled() const { return m_feedbackEnabled; }
	void setFeedbackEnabled(bool enabled);

	/*! Write the value the model holds back to the hardware as a control-change
	 *  on the channel this control transmits on. Returns true when the event was
	 *  handed to the port's output path - which is NOT the same claim as "the
	 *  bytes left the machine": the port's own counters
	 *  (MidiPort::outputEventsWritten()) are the measurement of what reached the
	 *  MIDI client, and with no hardware attached nothing consumes the bytes.
	 *  Called by setFeedbackEnabled() and after every control-change that moves
	 *  the model while feedback is on; public so a caller (and the proof) can
	 *  force one write. */
	bool sendFeedback();

	//! The model's value as the control-change byte a feedback write sends:
	//! round(value * 127), clamped. Reported by controller.feedback so a caller
	//! can read the mapping without re-deriving it.
	int feedbackByte() const;

public slots:
	gui::ControllerDialog* createDialog( QWidget * _parent ) override;
	void updateName();


protected:
	// The internal per-controller get-value function
	void updateValueBuffer() override;

private:
	/*! Whether an incoming hardware value \a value takes this control over:
	 *  either it is already AT the soft-takeover target (within one MIDI step,
	 *  because a knob cannot be placed on an arbitrary stored value and
	 *  refusing that step would mean a surface that never takes over), or the
	 *  hardware is seen to CROSS the target since the last sample.
	 *
	 *  Only an OBSERVED crossing counts: the comparison is against the previous
	 *  RAW hardware sample, not against m_lastValue, which is the last value the
	 *  model accepted and so is stale until capture. Before the first sample
	 *  there is nothing to have crossed, so the first value after the gate is
	 *  armed can only capture by being at the target.
	 *
	 *  Records \a value as the new previous raw sample either way: the hardware
	 *  moved, whether or not the model followed it. */
	bool softTakeoverCapturedBy(float value);

	/*! Forget the crossing history, so the next value is treated as a first
	 *  sample again. Called whenever the gate is (re)armed. */
	void rearmSoftTakeover()
	{
		m_softTakeoverCaptured = false;
		m_haveRawSample = false;
	}


	MidiPort m_midiPort;

	float m_lastValue;
	float m_previousValue;

	bool m_softTakeoverEnabled = false;
	bool m_softTakeoverCaptured = false;
	float m_softTakeoverTarget = 0.0f;

	/*! The previous RAW hardware value, and whether one has been seen yet.
	 *  The crossing test compares the incoming value against these, NOT
	 *  against m_lastValue: that is the last value the MODEL accepted, which
	 *  is stale (or still its initial 0.0) until this control takes over, so
	 *  comparing against it let the first event after arming capture whenever
	 *  it happened to sit on the far side of the target. The hardware moving
	 *  without the model following is exactly the case soft takeover is for.
	 *  Cleared whenever the gate is (re)armed, so the first sample after that
	 *  is treated as a first sample - it can be AT the target, but it cannot
	 *  CROSS it. */
	float m_previousRawValue = 0.0f;
	bool m_haveRawSample = false;

	bool m_feedbackEnabled = false;

	friend class gui::ControllerConnectionDialog;
	friend class AutoDetectMidiController;

} ;


} // namespace lmms

#endif // LMMS_MIDI_CONTROLLER_H
