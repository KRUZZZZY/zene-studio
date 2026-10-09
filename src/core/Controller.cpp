/*
 * Controller.cpp - implementation of class controller which handles
 *                  remote-control of AutomatableModels
 *
 * Copyright (c) 2008 Paul Giblock <drfaygo/at/gmail.com>
 * Copyright (c) 2014 Lukas W <lukaswhl/at/gmail.com>
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

#include <QDomElement>

#include <vector>

#include "AudioEngine.h"
#include "ControllerConnection.h"
#include "ControllerDialog.h"
#include "LfoController.h"
#include "MidiController.h"
#include "PeakController.h"
#include "RealtimeScope.h"

namespace lmms
{

namespace
{

AudioEngine::RequestChangesGuard controllerChangeGuard()
{
	AudioEngine* const engine = Engine::audioEngine();
	return RealtimeScope::isActive() || engine == nullptr
		? AudioEngine::RequestChangesGuard{}
		: engine->requestChangesGuard();
}

} // namespace


std::atomic<long> Controller::s_periods{0};
std::vector<Controller*> Controller::s_controllers;



Controller::Controller( ControllerType _type, Model * _parent,
					const QString & _display_name ) :
	Model( _parent, _display_name ),
	JournallingObject(),
	m_valueBuffer( Engine::audioEngine()->framesPerPeriod() ),
	m_bufferLastUpdated( -1 ),
	m_connectionCount( 0 ),
	m_type( _type )
{
	auto changeGuard = controllerChangeGuard();
	if( _type != ControllerType::Dummy && _type != ControllerType::Midi )
	{
		s_controllers.push_back(this);
		// Determine which name to use
		for ( uint i=s_controllers.size(); ; i++ )
		{
			QString new_name = QString( tr( "Controller %1" ) )
					.arg( i );

			// Check if name is already in use
			bool name_used = false;
			for (Controller * controller : s_controllers)
			{
				if ( controller->name() == new_name )
				{
					name_used = true;
					break;
				}
			}
			if ( ! name_used )
			{
				m_name = new_name;
				break;
			}
		}
	}
	updateValueBuffer();
}



Controller::~Controller()
{
	auto changeGuard = controllerChangeGuard();
	auto it = std::find(s_controllers.begin(), s_controllers.end(), this);
	if (it != s_controllers.end())
	{
		s_controllers.erase(it);
	}

	m_valueBuffer.clear();
	// Remove connections by destroyed signal
}



// Get current value, with an offset into the current buffer for sample exactness
float Controller::currentValue( int _offset )
{
	auto changeGuard = controllerChangeGuard();
	if( _offset == 0 || isSampleExact() )
	{
		const float current = fittedValue(value(_offset));
		if (!RealtimeScope::isActive()) { m_currentValue = current; }
		return current;
	}
	return m_currentValue;
}



float Controller::value( int offset )
{
	auto changeGuard = controllerChangeGuard();
	if( m_bufferLastUpdated != s_periods )
	{
		if (RealtimeScope::isActive() && !Engine::audioEngine()->isRenderThread())
		{
			Engine::audioEngine()->recordUnpreparedControllerBufferRequest();
			return m_currentValue;
		}
		prepareValueBufferForRender();
	}
	return m_valueBuffer.values()[ offset ];
}
	

ValueBuffer * Controller::valueBuffer()
{
	auto changeGuard = controllerChangeGuard();
	if( m_bufferLastUpdated != s_periods )
	{
		if (RealtimeScope::isActive() && !Engine::audioEngine()->isRenderThread())
		{
			Engine::audioEngine()->recordUnpreparedControllerBufferRequest();
			return nullptr;
		}
		prepareValueBufferForRender();
	}
	return &m_valueBuffer;
}



void Controller::prepareValueBufferForRender()
{
	if (m_bufferLastUpdated != s_periods)
	{
		updateValueBuffer();
		// currentValue(offset > 0) historically uses the value observed at
		// offset 0. Publish that scalar during the serial pass so workers never
		// need to write shared controller state while reading the buffer.
		m_currentValue = fittedValue(m_valueBuffer.values()[0]);
	}
}


void Controller::updateValueBuffer()
{
	m_valueBuffer.fill(0.5f);
	m_bufferLastUpdated = s_periods;
}


// Get position in frames
unsigned int Controller::runningFrames()
{
	return s_periods * Engine::audioEngine()->framesPerPeriod();
}



// Get position in seconds
float Controller::runningTime()
{
	return runningFrames() / Engine::audioEngine()->outputSampleRate();
}



void Controller::triggerFrameCounter()
{
	auto changeGuard = controllerChangeGuard();
	for (Controller * controller : s_controllers)
	{
		// This signal is for updating values for both stubborn knobs and for
		// painting.  If we ever get all the widgets to use or at least check
		// currentValue() then we can throttle the signal and only use it for
		// GUI.
		emit controller->valueChanged();
	}

	s_periods ++;
	//emit s_signaler.triggerValueChanged();
}



void Controller::resetFrameCounter()
{
	auto changeGuard = controllerChangeGuard();
	for (Controller * controller : s_controllers)
	{
		controller->m_bufferLastUpdated = 0;
	}
	s_periods = 0;
}



Controller * Controller::create( ControllerType _ct, Model * _parent )
{
	static Controller * dummy = nullptr;
	Controller * c = nullptr;

	switch( _ct )
	{
		case ControllerType::Dummy:
			if (!dummy)
				dummy = new Controller( ControllerType::Dummy, nullptr,
								QString() );
			c = dummy;
			break;

		case ControllerType::Lfo:
			c = new class LfoController( _parent );
			break;

		case ControllerType::Peak:
			//Already instantiated in EffectChain::loadSettings()
			Q_ASSERT( false );
			break;

		case ControllerType::Midi:
			c = new class MidiController( _parent );
			break;

		default: 
			break;
	}

	return( c );
}



Controller * Controller::create( const QDomElement & _this, Model * _parent )
{
	const auto controllerType = static_cast<ControllerType>(_this.attribute("type").toInt());
	auto controller = controllerType == ControllerType::Peak
		? PeakController::getControllerBySetting(_this)
		: create(controllerType, _parent);
	if (controller) { controller->restoreState(_this); }
	return controller;
}



bool Controller::hasModel( const Model * m ) const
{
	for (QObject * c : children())
	{
		auto am = qobject_cast<AutomatableModel*>(c);
		if( am != nullptr )
		{
			if( am == m )
			{
				return true;
			}

			ControllerConnection * cc = am->controllerConnection();
			if( cc != nullptr && cc->getController()->hasModel( m ) )
			{
				return true;
			}
		}
	}

	return false;
}



void Controller::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	_this.setAttribute( "type", static_cast<int>(type()) );
	_this.setAttribute( "name", name() );
}



void Controller::loadSettings( const QDomElement & _this )
{
	if( static_cast<ControllerType>(_this.attribute( "type" ).toInt()) != type() )
	{
		qWarning( "controller-type does not match controller-type of "
							"settings-node!\n" );
	}

	setName( _this.attribute( "name" ) );
}


QString Controller::nodeName() const
{
	return( "Controller" );
}



gui::ControllerDialog * Controller::createDialog( QWidget * _parent )
{
	auto d = new gui::ControllerDialog(this, _parent);

	return d;
}




void Controller::addConnection( ControllerConnection * )
{
	auto changeGuard = controllerChangeGuard();
	m_connectionCount++;
}




void Controller::removeConnection( ControllerConnection * )
{
	auto changeGuard = controllerChangeGuard();
	m_connectionCount--;
	Q_ASSERT( m_connectionCount >= 0 );
}




int Controller::connectionCount() const{
	return m_connectionCount;
}


} // namespace lmms
