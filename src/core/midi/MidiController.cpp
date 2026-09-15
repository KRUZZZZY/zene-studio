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

				if (m_softTakeoverEnabled && !m_softTakeoverCaptured)
				{
					// Capture when the incoming value crosses the target,
					// or is already very close to it (within 1 MIDI step).
					const float epsilon = 1.0f / 127.0f;
					if ((m_lastValue <= m_softTakeoverTarget && incoming >= m_softTakeoverTarget) ||
					    (m_lastValue >= m_softTakeoverTarget && incoming <= m_softTakeoverTarget) ||
					    std::abs(incoming - m_softTakeoverTarget) <= epsilon)
					{
						m_softTakeoverCaptured = true;
					}
					else
					{
						// Ignore: the hardware has not yet reached the stored value.
						break;
					}
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
	m_midiPort.processOutEvent(event, time);
}

void MidiController::setSoftTakeoverEnabled(bool enabled)
{
	m_softTakeoverEnabled = enabled;
	if (!enabled)
	{
		m_softTakeoverCaptured = false;
	}
}

void MidiController::setSoftTakeoverTarget(float target)
{
	m_softTakeoverTarget = std::clamp(target, 0.0f, 1.0f);
	m_softTakeoverCaptured = false;
}

void MidiController::resetSoftTakeover()
{
	m_softTakeoverCaptured = false;
}

void MidiController::setFeedbackEnabled(bool enabled)
{
	m_feedbackEnabled = enabled;
	if (enabled)
	{
		// Ensure output is enabled on the port so feedback can travel.
		if (!m_midiPort.isOutputEnabled())
		{
			m_midiPort.setMode(MidiPort::Mode::Duplex);
		}
		sendFeedback();
	}
}

void MidiController::sendFeedback()
{
	const int channel = m_midiPort.realOutputChannel();
	const int controller = m_midiPort.inputController();
	if (controller < 0) { return; }
	const int value = static_cast<int>(std::clamp(m_lastValue * 127.0f, 0.0f, 127.0f));
	processOutEvent(MidiEvent(MidiControlChange, channel, controller, value), TimePos());
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
	_this.setAttribute("feedback", m_feedbackEnabled ? "1" : "0");
}



void MidiController::loadSettings( const QDomElement & _this )
{
	Controller::loadSettings( _this );

	m_midiPort.loadSettings( _this );
	m_softTakeoverEnabled = (_this.attribute("softtakeover", "0") == "1");
	m_feedbackEnabled = (_this.attribute("feedback", "0") == "1");
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