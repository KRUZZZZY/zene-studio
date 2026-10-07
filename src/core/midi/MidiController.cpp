/*
 * MidiController.cpp - implementation of class midi-controller which handles
 *                      MIDI control change messages
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


#include <algorithm>
#include <cmath>

#include <QDomElement>

#include "AudioEngine.h"
#include "MidiController.h"

namespace lmms
{


MidiController::MidiController( Model * _parent ) :
	Controller( ControllerType::Midi, _parent, tr( "MIDI Controller" ) ),
	MidiEventProcessor(),
	m_midiPort( tr( "unnamed_midi_controller" ), Engine::audioEngine()->midiClient(), this, this, MidiPort::Mode::Input ),
	m_lastValue( 0.0f ),
	m_previousValue( 0.0f )
{
	setSampleExact( true );
	connect( &m_midiPort, SIGNAL(modeChanged()),
			this, SLOT(updateName()));
}



void MidiController::updateValueBuffer()
{
	if( m_previousValue != m_lastValue )
	{
		m_valueBuffer.interpolate( m_previousValue, m_lastValue );
		m_previousValue = m_lastValue;
	}
	else
	{
		m_valueBuffer.fill( m_lastValue );
	}
	m_bufferLastUpdated = s_periods;
}

void MidiController::updateName()
{
	setName( QString("MIDI ch%1 ctrl%2").
			arg( m_midiPort.inputChannel() ).
			arg( m_midiPort.inputController() ) );
}




void MidiController::processInEvent(const MidiEvent& event, const TimePos& time, f_cnt_t offset)
{
	switch(event.type())
	{
		case MidiControlChange:
		{
			unsigned char controllerNum = event.controllerNumber();

			if (m_midiPort.inputController() == controllerNum &&
				(m_midiPort.inputChannel() == event.channel() + 1 || m_midiPort.inputChannel() == 0))
			{
				unsigned char val = event.controllerValue();
				const float incoming = static_cast<float>(val) / 127.0f;

				if (m_softTakeoverEnabled && !m_softTakeoverCaptured
					&& !softTakeoverCapturedBy(incoming))
				{
					// Ignore: the hardware has not yet reached the stored value.
					break;
				}

				m_previousValue = m_lastValue;
				m_lastValue = incoming;
				emit valueChanged();

				if (m_feedbackEnabled)
				{
					sendFeedback();
				}
			}
			break;
		}
		default:
			// Don't care - maybe add special cases for pitch and mod later
			break;
	}
}

void MidiController::processOutEvent(const MidiEvent& event, const TimePos& time, f_cnt_t offset)
{
	m_midiPort.processOutEvent(event, time, offset);
}

bool MidiController::softTakeoverCapturedBy(float value)
{
	// At the target: within one MIDI step, since a knob cannot be placed on an
	// arbitrary stored value and refusing this step would mean a surface that
	// never takes over.
	const float epsilon = 1.0f / 127.0f;
	const bool atTarget = std::abs(value - m_softTakeoverTarget) <= epsilon;

	// Crossing: the hardware has been SEEN to pass the target. m_haveRawSample
	// is false for the first value after arming, which can be at the target but
	// has not crossed it - so m_lastValue (the last value the MODEL accepted,
	// stale until capture) is not what this compares against.
	const bool crossed = m_haveRawSample &&
		((m_previousRawValue <= m_softTakeoverTarget && value >= m_softTakeoverTarget) ||
		 (m_previousRawValue >= m_softTakeoverTarget && value <= m_softTakeoverTarget));

	// The hardware moved, whether or not the model followed it: that is the
	// sample the next event is compared against.
	m_previousRawValue = value;
	m_haveRawSample = true;

	if (atTarget || crossed)
	{
		m_softTakeoverCaptured = true;
		return true;
	}
	return false;
}

void MidiController::setSoftTakeoverEnabled(bool enabled)
{
	m_softTakeoverEnabled = enabled;
	if (!enabled)
	{
		rearmSoftTakeover();
	}
}

void MidiController::setSoftTakeoverTarget(float target)
{
	m_softTakeoverTarget = std::clamp(target, 0.0f, 1.0f);
	rearmSoftTakeover();
}

void MidiController::resetSoftTakeover()
{
	rearmSoftTakeover();
}

void MidiController::setFeedbackEnabled(bool enabled)
{
	m_feedbackEnabled = enabled;
	if (!enabled)
	{
		// Give back exactly what enabling feedback borrowed, so an input-only
		// control does not stay exposed as an output on a channel the user
		// never chose - and does not save those settings.
		if (m_tookOverPortMode)
		{
			m_midiPort.setMode(m_modeBeforeFeedback);
			m_tookOverPortMode = false;
		}
		if (m_tookOverPortChannel)
		{
			m_midiPort.setOutputChannel(m_outputChannelBeforeFeedback);
			m_tookOverPortChannel = false;
		}
		return;
	}

	// A port that is not output-enabled cannot carry the write, and
	// MidiPort::processOutEvent gates on the port's output CHANNEL: a feedback
	// event written on the channel this control transmits on would be filtered
	// out as "not the channel the user selected" against any other setting. So
	// point the output channel at the channel this control receives on, which is
	// the channel the hardware listens to for its own control's lamp.
	if (!m_midiPort.isOutputEnabled())
	{
		// Remember it only if feedback is the one doing it: a port the user
		// already made Duplex is not theirs to have undone.
		m_modeBeforeFeedback = m_midiPort.mode();
		m_tookOverPortMode = true;
		m_midiPort.setMode(MidiPort::Mode::Duplex);
	}
	if (m_midiPort.inputChannel() > 0 && m_midiPort.outputChannel() != m_midiPort.inputChannel())
	{
		m_outputChannelBeforeFeedback = m_midiPort.outputChannel();
		m_tookOverPortChannel = true;
		m_midiPort.setOutputChannel(m_midiPort.inputChannel());
	}
	sendFeedback();
}

int MidiController::feedbackByte() const
{
	return static_cast<int>(std::lround(std::clamp(m_lastValue, 0.0f, 1.0f) * 127.0f));
}

bool MidiController::sendFeedback()
{
	const int controller = m_midiPort.inputController();
	if (controller < 0) { return false; }  // nothing is bound to a controller number

	// "MIDI chX ctrlY" is a 1-based channel; a MidiEvent carries the wire
	// channel, so it is one lower. realOutputChannel() is the same byte for a
	// port whose output channel was pointed at this control's channel above.
	const int channel = m_midiPort.realOutputChannel();
	processOutEvent(MidiEvent(MidiControlChange, channel, controller, feedbackByte()),
		TimePos());

	// The event was handed to the port. Whether it then reached the client is
	// the port's own measurement - see MidiController.h.
	return true;
}



void MidiController::subscribeReadablePorts( const MidiPort::Map & _map )
{
	for( MidiPort::Map::ConstIterator it = _map.constBegin();
						it != _map.constEnd(); ++it )
	{
		m_midiPort.subscribeReadablePort( it.key(), *it );
	}
}




void MidiController::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	Controller::saveSettings( _doc, _this );
	m_midiPort.saveSettings( _doc, _this );
	_this.setAttribute("softtakeover", m_softTakeoverEnabled ? "1" : "0");
	// The target is project state too: restoring only the flag would reopen a
	// control with a target of 0.0, which captures on the very first
	// control-change - the one thing soft takeover exists to prevent.
	_this.setAttribute("softtakeovertarget", QString::number(m_softTakeoverTarget, 'f', 6));
	_this.setAttribute("feedback", m_feedbackEnabled ? "1" : "0");
	if (m_tookOverPortMode)
	{
		_this.setAttribute("feedbackmodebefore", QString::number(static_cast<int>(m_modeBeforeFeedback)));
	}
	else
	{
		_this.removeAttribute("feedbackmodebefore");
	}
	if (m_tookOverPortChannel)
	{
		_this.setAttribute("feedbackoutputchannelbefore", QString::number(m_outputChannelBeforeFeedback));
	}
	else
	{
		_this.removeAttribute("feedbackoutputchannelbefore");
	}
}



void MidiController::loadSettings( const QDomElement & _this )
{
	Controller::loadSettings( _this );

	m_midiPort.loadSettings( _this );
	m_softTakeoverEnabled = (_this.attribute("softtakeover", "0") == "1");
	// A project saved before the target was persisted carries no attribute: it
	// then keeps the default 0.0, as before.
	m_softTakeoverTarget = std::clamp(
		_this.attribute("softtakeovertarget", "0").toFloat(), 0.0f, 1.0f);
	// A freshly loaded control has not been captured by anything yet.
	rearmSoftTakeover();
	m_feedbackEnabled = (_this.attribute("feedback", "0") == "1");
	m_tookOverPortMode = false;
	if (_this.hasAttribute("feedbackmodebefore"))
	{
		bool validMode = false;
		const int mode = _this.attribute("feedbackmodebefore").toInt(&validMode);
		if (validMode && mode >= static_cast<int>(MidiPort::Mode::Disabled)
			&& mode <= static_cast<int>(MidiPort::Mode::Duplex))
		{
			m_modeBeforeFeedback = static_cast<MidiPort::Mode>(mode);
			m_tookOverPortMode = true;
		}
	}
	m_tookOverPortChannel = false;
	if (_this.hasAttribute("feedbackoutputchannelbefore"))
	{
		bool validChannel = false;
		const int channel = _this.attribute("feedbackoutputchannelbefore").toInt(&validChannel);
		if (validChannel && channel >= 0 && channel <= MidiChannelCount)
		{
			m_outputChannelBeforeFeedback = channel;
			m_tookOverPortChannel = true;
		}
	}
	updateName();
}




QString MidiController::nodeName() const
{
	return( "Midicontroller" );
}



gui::ControllerDialog* MidiController::createDialog( QWidget * _parent )
{
	return nullptr;
}



} // namespace lmms
