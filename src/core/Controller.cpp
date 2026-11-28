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

#include <QCoreApplication>
#include <QDomElement>
#include <QThread>
#include <QTimer>

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


/*! How often the control thread reports a controller value the render thread published
 *  (cluster new-p12-56). It is the throttle the notification did not have: a period is
 *  5.8 ms long at 44.1 kHz / 256 frames and 23 ms at 1024 frames, while a screen repaints
 *  at 60 Hz (16.7 ms), so one tick per 10 ms loses nothing a user can see and bounds the
 *  work to one signal per controller per tick however fast the engine runs. Coarse (not
 *  VeryCoarse: Qt rounds a very coarse interval to whole seconds). */
constexpr int frameNotificationIntervalMs = 10;

/*! The control thread's half of the per-period controller notification (cluster new-p12-56).
 *  Controller::triggerFrameCounter() runs on the render thread inside the realtime scope, so it
 *  may only publish an atomic; this object owns the timer that turns what it published into
 *  Controller::valueChanged(), on a thread that is allowed to allocate, lock and post events.
 *  Created on the control thread with the first controller and parented to the application, so
 *  it dies with it and can never outlive the controllers it walks. The timer is a QObject child
 *  of this object and carries an object name, which is how this directory's tests fire it
 *  without waiting for the clock (see Song's automationWriteDrainTimer). */
class FrameNotificationDispatcher final : public QObject
{
public:
	explicit FrameNotificationDispatcher( QObject* parent ) : QObject( parent )
	{
		m_timer = new QTimer( this );
		m_timer->setObjectName( QStringLiteral( "controllerFrameNotificationTimer" ) );
		m_timer->setTimerType( Qt::CoarseTimer );
		m_timer->setInterval( frameNotificationIntervalMs );
		QObject::connect( m_timer, &QTimer::timeout, this, [] {
			Controller::dispatchFrameNotifications();
		} );
		m_timer->start();
	}

	FrameNotificationDispatcher( const FrameNotificationDispatcher& ) = delete;
	FrameNotificationDispatcher& operator=( const FrameNotificationDispatcher& ) = delete;

private:
	QTimer* m_timer = nullptr;
};

//! The dispatcher, or nullptr until a controller is constructed on the control thread.
FrameNotificationDispatcher* s_frameNotificationDispatcher = nullptr;

//! Arms the dispatcher. A controller is constructed on the control thread (the GUI, project
//! load, a test), which is the only thread whose event loop can run its timer, so this is
//! called from there and does nothing at all on any other thread.
void ensureFrameNotificationDispatcher()
{
	if( s_frameNotificationDispatcher != nullptr ) { return; }
	QCoreApplication* const app = QCoreApplication::instance();
	if( app == nullptr || QThread::currentThread() != app->thread() ) { return; }
	s_frameNotificationDispatcher = new FrameNotificationDispatcher( app );
}

} // namespace


static_assert(std::atomic<f_cnt_t>::is_always_lock_free,
	"the render thread must never take a lock for the period counter");
std::atomic<f_cnt_t> Controller::s_periods{0};
std::atomic<f_cnt_t> Controller::s_notifiedPeriod{0};
std::vector<Controller*> Controller::s_controllers;



Controller::Controller( ControllerType _type, Model * _parent,
					const QString & _display_name ) :
	Model( _parent, _display_name ),
	JournallingObject(),
	m_valueBuffer( Engine::audioEngine()->framesPerPeriod() ),
	m_bufferLastUpdated( static_cast<f_cnt_t>(-1) ),
	m_connectionCount( 0 ),
	m_type( _type )
{
	auto changeGuard = controllerChangeGuard();
	if( _type != ControllerType::Dummy && _type != ControllerType::Midi )
	{
		// A registered controller is one the render thread publishes periods for, so the
		// control thread needs its notification timer from here on (cluster new-p12-56).
		ensureFrameNotificationDispatcher();
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
f_cnt_t Controller::runningFrames()
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
	// Publishing the period is all the render thread may do (cluster new-p12-56): the signal
	// this function used to emit here is delivered to the GUI thread through a chain of direct
	// connections that ends at a widget slot in that thread, so Qt queued it - and allocated a
	// QMetaCallEvent, plus room in the GUI thread's event queue, on the audio thread once per
	// controller per period. The notification is Controller::dispatchFrameNotifications()'s,
	// run by the control thread's own timer.
	s_periods ++;
}



void Controller::dispatchFrameNotifications()
{
	// The render thread never notifies, even if something calls this from inside the realtime
	// scope: queuing an event there is the allocation this function exists to keep off it. The
	// period stays pending and the next control-thread tick reports it.
	if( RealtimeScope::isActive() ) { return; }

	const f_cnt_t published = s_periods.load( std::memory_order_acquire );
	if( published == s_notifiedPeriod.load( std::memory_order_relaxed ) ) { return; }
	// Stamped before the notifications go out: a receiver that calls back in must not make the
	// walk re-enter itself, and the values it reads are the ones being reported.
	s_notifiedPeriod.store( published, std::memory_order_relaxed );

	// The live controllers, taken under the change guard and then notified outside it. The
	// guard is what every other reader and writer of s_controllers takes (the constructor, the
	// destructor, resetFrameCounter); it excludes a controller being registered or destroyed
	// while the walk runs. The slots are GUI work and must not run while the render thread is
	// waiting for that guard, so the walk itself is all that holds it. This thread may
	// allocate, so the copy is a control-thread allocation, not an audio-thread one.
	ControllerVector controllers;
	{
		auto changeGuard = controllerChangeGuard();
		controllers.assign( s_controllers.begin(), s_controllers.end() );
	}
	for( Controller* controller : controllers )
	{
		// This signal is for updating values for both stubborn knobs and for painting; both
		// read currentValue(), so one notification per tick shows the latest value. It used to
		// be emitted per period from the render thread.
		emit controller->valueChanged();
	}
}



void Controller::resetFrameCounter()
{
	auto changeGuard = controllerChangeGuard();
	for (Controller * controller : s_controllers)
	{
		controller->m_bufferLastUpdated = static_cast<f_cnt_t>(-1);
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
