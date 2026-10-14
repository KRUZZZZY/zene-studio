/*
 * AutomatableModel.cpp - some implementations of AutomatableModel-class
 *
 * Copyright (c) 2008-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "AutomatableModel.h"

#include <bit>
#include <chrono>
#include <new>

#include <QRegularExpression>

#include "lmms_math.h"

#include "AudioEngine.h"
#include "AutomationClip.h"
#include "ControllerConnection.h"
#include "LocaleHelper.h"
#include "ProjectJournal.h"
#include "RealtimeScope.h"
#include "Song.h"

namespace lmms
{

namespace
{

static_assert(std::atomic<std::uint64_t>::is_always_lock_free,
	"AutomatableModel value handoff requires an always-lock-free 64-bit atomic");
static_assert(std::atomic<bool>::is_always_lock_free,
	"AutomatableModel registration flag requires an always-lock-free bool atomic");

AudioEngine::RequestChangesGuard modelChangeGuard()
{
	AudioEngine* const engine = Engine::audioEngine();
	return RealtimeScope::isActive() || engine == nullptr
		? AudioEngine::RequestChangesGuard{}
		: engine->requestChangesGuard();
}

[[maybe_unused]] bool modelDestructionWouldBlockOnRender(AudioEngine* engine) noexcept
{
	return engine != nullptr && RealtimeScope::isActive() && !engine->isRenderThread();
}

} // namespace

long AutomatableModel::s_periodCounter = 0;



AutomatableModel::AutomatableModel(
						const float val, const float min, const float max, const float step,
						Model* parent, const QString & displayName, bool defaultConstructed ) :
	Model( parent, displayName, defaultConstructed ),
	m_scaleType( ScaleType::Linear ),
	m_minValue( min ),
	m_maxValue( max ),
	m_step( step ),
	m_range( max - min ),
	m_centerValue( m_minValue ),
	m_valueChanged( false ),
	m_oldValue(val),
	m_hasStrictStepSize( false ),
	m_nextLink(this),
	m_controllerConnection( nullptr ),
	m_valueBuffer( static_cast<int>( Engine::audioEngine()->framesPerPeriod() ) ),
	m_lastUpdatedPeriod( -1 ),
	m_hasSampleExactData(false),
	m_useControllerValue(true)

{
	m_value = fittedValue( val );
	m_oldValue = m_value;
	m_renderValue = m_value;
	m_publishedValuePair.store(packValuePair(m_value, m_value), std::memory_order_relaxed);
	setInitValue( val );
	if (!Engine::audioEngine()->registerAutomatableModel(this)) { throw std::bad_alloc{}; }
}




AutomatableModel::~AutomatableModel()
{
	AudioEngine* const engine = Engine::audioEngine();
	Q_ASSERT_X(!modelDestructionWouldBlockOnRender(engine),
		"AutomatableModel::~AutomatableModel",
		"an AutomatableModel must not be destroyed on an audio worker during a period");

	emit aboutToBeDestroyed();

	// The song caches the models it automated on the last frame as RAW pointers
	// (Song::m_oldAutomatedValues) and walks that cache in two places -
	// Song::processAutomations(), on the first frame of every tick, and
	// Song::stop() - dereferencing every entry. The song does not own the
	// models, so it has to be told when one goes away: without this call a
	// control destroyed while the transport runs (an instrument or an automation
	// track deleted mid-playback, a project container whose tracks go away, and
	// the rigs in tests/src/core/AutomationModesTest.cpp) is setUseControllerValue()
	// on freed memory, which is a SIGSEGV inside Qt's signal activation -
	// AutomationModesTest reported exactly that on all seven CI platforms.
	const auto removeModelReferences = [this] {
		Song::forgetAutomatedModel(this);
		unlink();
		if (m_controllerConnection) { delete m_controllerConnection; }
	};

	if (engine != nullptr)
	{
		// Close any external lifetime gates before taking the render lock. Once
		// callbacks return, unregister and remove the render-visible references
		// atomically with respect to the next period.
		auto changeGuard = engine->requestChangesGuard();
		engine->unregisterAutomatableModel(this);
		removeModelReferences();
	}
	else { removeModelReferences(); }

	m_valueBuffer.clear();

	emit destroyed( id() );
}



#ifdef LMMS_TESTING
bool AutomatableModel::destructorWouldRequestBlockingChangeGuard() noexcept
{
	return modelDestructionWouldBlockOnRender(Engine::audioEngine());
}
#endif




bool AutomatableModel::isAutomated() const
{
	return AutomationClip::isAutomated( this );
}



bool AutomatableModel::mustQuoteName(const QString& name)
{
	QRegularExpression reg("^[A-Za-z0-9._-]+$");
	return !reg.match(name).hasMatch();
}

//! WF-033: the attribute an undo snapshot carries a plain model's journal id in, beside its value.
static QString snapshotIdSuffix()
{
	return QStringLiteral( "-jid" );
}

void AutomatableModel::saveSettings( QDomDocument& doc, QDomElement& element, const QString& name )
{
	bool mustQuote = mustQuoteName(name);

	if( isAutomated() || m_scaleType != ScaleType::Linear )
	{
		// automation needs tuple of data (name, id, value)
		// scale type also needs an extra value
		// => it must be appended as a node

		QDomElement me = doc.createElement( mustQuote ? QString("automatablemodel") : name );
		me.setAttribute( "id", ProjectJournal::idToWrite( id() ) );
		me.setAttribute( "value", publishedValue() );
		me.setAttribute( "scale_type", m_scaleType == ScaleType::Logarithmic ? "log" : "linear" );
		if(mustQuote) {
			me.setAttribute( "nodename", name );
		}
		element.appendChild( me );
	}
	else
	{
		if(mustQuote)
		{
			QDomElement me = doc.createElement( "automatablemodel" );
			me.setAttribute( "nodename", name );
			me.setAttribute( "value", publishedValue() );
			// WF-033: in an undo snapshot the node carries the id the node branch of loadSettings reads.
			if( ProjectJournal::snapshotIds() ) { me.setAttribute( "id", id() ); }
			element.appendChild( me );
		}
		else
		{
			// non automation, linear scale (default), can be saved as attribute
			element.setAttribute( name, publishedValue() );
			// WF-033 (docs/UNDO-HISTORY-PERSISTENCE.md): an undo snapshot carries EVERY model's
			// journal id, beside the value and in the same form - some loaders read a model's
			// presence from its attribute (MidiPort's basevelocity compat), so the form must not
			// change. A project save never writes it.
			if( ProjectJournal::snapshotIds() ) { element.setAttribute( name + snapshotIdSuffix(), id() ); }
		}
	}

	// Skip saving MIDI connections if we're saving project and
	// the discardMIDIConnections option is true.
	auto controllerType = m_controllerConnection
			? m_controllerConnection->getController()->type()
			: Controller::ControllerType::Dummy;
	bool skipMidiController = Engine::getSong()->isSavingProject()
							  && Engine::getSong()->getSaveOptions().discardMIDIConnections.value();
	if (m_controllerConnection && controllerType != Controller::ControllerType::Dummy
		&& !(skipMidiController && controllerType == Controller::ControllerType::Midi))
	{
		QDomElement controllerElement;

		// get "connection" element (and create it if needed)
		QDomNode node = element.namedItem( "connection" );
		if( node.isElement() )
		{
			controllerElement = node.toElement();
		}
		else
		{
			controllerElement = doc.createElement( "connection" );
			element.appendChild( controllerElement );
		}

		bool mustQuote = mustQuoteName(name);
		QString elementName = mustQuote ? "controllerconnection"
						: name;

		QDomElement element = doc.createElement( elementName );
		if(mustQuote)
			element.setAttribute( "nodename", name );
		m_controllerConnection->saveSettings( doc, element );

		controllerElement.appendChild( element );
	}
}




void AutomatableModel::loadSettings( const QDomElement& element, const QString& name )
{
	// compat code
	QDomNode node = element.namedItem( AutomationClip::classNodeName() );
	if( node.isElement() )
	{
		node = node.namedItem( name );
		if( node.isElement() )
		{
			AutomationClip * p = AutomationClip::globalAutomationClip( this );
			p->loadSettings( node.toElement() );
			setValue( p->valueAt( 0 ) );
			// in older projects we sometimes have odd automations
			// with just one value in - eliminate if necessary
			if( !p->hasAutomation() )
			{
				delete p;
			}
			return;
		}
		// logscales were not existing at this point of time
		// so they can be ignored
	}

	QDomNode connectionNode = element.namedItem( "connection" );
	// reads controller connection
	if( connectionNode.isElement() )
	{
		QDomNode thisConnection = connectionNode.toElement().namedItem( name );
		if( !thisConnection.isElement() )
		{
			thisConnection = connectionNode.toElement().namedItem( "controllerconnection" );
			QDomElement tcElement = thisConnection.toElement();
			// sanity check
			if( tcElement.isNull() || tcElement.attribute( "nodename" ) != name )
			{
				// no, that wasn't it, act as if we never found one
				thisConnection.clear();
			}
		}
		if( thisConnection.isElement() )
		{
			setControllerConnection(new ControllerConnection(nullptr));
			m_controllerConnection->loadSettings( thisConnection.toElement() );
			//m_controllerConnection->setTargetName( displayName() );
		}
	}

	// models can be stored as elements (port00) or attributes (port10):
	// <ladspacontrols port10="4.41">
	//   <port00 value="4.41" id="4249278"/>
	// </ladspacontrols>
	// element => there is automation data, or scaletype information

	node = element.namedItem( name ); // maybe we have luck?

	// either: no node with name "name" found
	//  => look for nodes with attribute name="nodename"
	// or: element with namedItem() "name" was found, but it's real nodename
	// is given as attribute and does not match
	//  => look for the right node
	if(node.isNull() ||
		( node.isElement() &&
		node.toElement().hasAttribute("nodename") &&
		node.toElement().attribute("nodename") != name))
	{
		for(QDomElement othernode = element.firstChildElement();
			!othernode.isNull();
			othernode = othernode.nextSiblingElement())
		{
			if((!othernode.hasAttribute("nodename") &&
				othernode.nodeName() == name) ||
				othernode.attribute("nodename") == name)
			{
				node = othernode;
				break;
			}
		}
	}
	if( node.isElement() )
	{
		QDomElement nodeElement = node.toElement();
		changeID( nodeElement.attribute( "id" ).toInt() );
		setValue( LocaleHelper::toFloat( nodeElement.attribute( "value" ) ) );
		if( nodeElement.hasAttribute( "scale_type" ) )
		{
			if( nodeElement.attribute( "scale_type" ) == "linear" )
			{
				setScaleType( ScaleType::Linear );
			}
			else if( nodeElement.attribute( "scale_type" ) == "log" )
			{
				setScaleType( ScaleType::Logarithmic );
			}
		}
	}
	else
	{

		setScaleType( ScaleType::Linear );

		if( element.hasAttribute( name ) )
			// attribute => read the element's value from the attribute list
		{
			setInitValue( LocaleHelper::toFloat( element.attribute( name ) ) );
			// WF-033: the id an undo snapshot carries (saveSettings); absent from every project file.
			const QString snapshotId = element.attribute( name + snapshotIdSuffix() );
			if( !snapshotId.isEmpty() ) { changeID( snapshotId.toInt() ); }
		}
		else
		{
			reset();
		}
	}
}




std::atomic<jo_id_t> AutomatableModel::s_lastTouched{0};

jo_id_t AutomatableModel::lastTouched()
{
	return s_lastTouched.load(std::memory_order_relaxed);
}

void AutomatableModel::setValue(const float value, const bool isAutomated, const bool journal)
{
	auto changeGuard = modelChangeGuard();
	if (fittedValue(value) == m_value)
	{
		// TODO check why we need this signal, is there a better solution?
		if (!isAutomated) { emit dataUnchanged(); }
		return;
	}

	if (!isAutomated)
	{
		if (journal) { addJournalCheckPoint(); }
		s_lastTouched.store(id(), std::memory_order_relaxed);
	}

	// set value for this and the other linked models
	setValueInternal(value);
	for (auto model = m_nextLink; model != this; model = model->m_nextLink)
	{
		model->setValueInternal(value);
	}
}




void AutomatableModel::setValueInternal(const float value)
{
	const float oldValue = m_value;
	m_value = fittedValue(value);
	publishValuePair(oldValue, m_value);
	invalidateValueBufferCache();

	if (oldValue != m_value)
	{
		m_valueChanged = true;
		emit dataChanged();
	}
}



template<class T> T AutomatableModel::logToLinearScale( T value ) const
{
	return castValue<T>( lmms::logToLinearScale( minValue<float>(), maxValue<float>(), static_cast<float>( value ) ) );
}


float AutomatableModel::scaledValue( float value ) const
{
	return m_scaleType == ScaleType::Linear
		? value
		: logToLinearScale<float>( ( value - minValue<float>() ) / m_range );
}


float AutomatableModel::inverseScaledValue( float value ) const
{
	return m_scaleType == ScaleType::Linear
		? value
		: lmms::linearToLogScale( minValue<float>(), maxValue<float>(), value );
}




template<class T>
void AutomatableModel::roundAt( T& value, const T& where ) const
{
	lmms::roundAt(value, where, m_step);
}








void AutomatableModel::setRange( const float min, const float max,
							const float step )
{
	if( ( m_maxValue != max ) || ( m_minValue != min ) )
	{
		m_minValue = min;
		m_maxValue = max;
		if( m_minValue > m_maxValue )
		{
			qSwap<float>( m_minValue, m_maxValue );
		}
		m_range = m_maxValue - m_minValue;

		setStep( step );

		// re-adjust value
		setValue( value<float>() );

		emit propertiesChanged();
	}
}




void AutomatableModel::setStep( const float step )
{
	if( m_step != step )
	{
		m_step = step;
		emit propertiesChanged();
	}
}




float AutomatableModel::fittedValue(float value) const
{
	value = std::clamp(value, m_minValue, m_maxValue);

	if (m_step != 0 && m_hasStrictStepSize)
	{
		value = std::round(value / m_step) * m_step;
	}

	roundAt(value, m_maxValue);
	roundAt(value, m_minValue);
	roundAt(value, 0.0f);
	return std::clamp(value, m_minValue, m_maxValue);
}




AutomatableModel* AutomatableModel::getLastLinkedModel() const
{
	for (auto model = m_nextLink; ; model = model->m_nextLink)
	{
		// The last model in the circular reference links back to this
		if (model->m_nextLink == this) { return model; }
	}
}




bool AutomatableModel::isLinkedToModel(AutomatableModel* model) const
{
	if (model == this) { return true; }
	for (auto next = m_nextLink; next != this; next = next->m_nextLink)
	{
		if (next == model) { return true; }
	}
	return false;
}

size_t AutomatableModel::countLinks() const
{
	size_t output = 0;
	for (auto model = m_nextLink; model != this; model = model->m_nextLink)
	{
		output++;
	}
	return output;
}




void AutomatableModel::linkToModel(AutomatableModel* other)
{
	auto changeGuard = modelChangeGuard();
	if (isLinkedToModel(other)) { return; }

	if (valueBuffer() && other->valueBuffer())
	{
		std::copy_n(other->valueBuffer()->data(),
			valueBuffer()->length(),
			valueBuffer()->data());
	}
	// copy data from other to this
	setValue(other->publishedValue());
	// link the models
	AutomatableModel* thisEnd = getLastLinkedModel();
	AutomatableModel* otherEnd = other->getLastLinkedModel();
	thisEnd->m_nextLink = other;
	otherEnd->m_nextLink = this;
	invalidateLinkedValueBufferCaches();
}




void AutomatableModel::unlink()
{
	auto changeGuard = modelChangeGuard();
	invalidateLinkedValueBufferCaches();
	getLastLinkedModel()->m_nextLink = m_nextLink;
	m_nextLink = this;
}




void AutomatableModel::setControllerConnection( ControllerConnection* c )
{
	auto changeGuard = modelChangeGuard();
	m_controllerConnection = c;
	invalidateLinkedValueBufferCaches();
	if( c )
	{
		QObject::connect( m_controllerConnection, SIGNAL(valueChanged()),
				this, SIGNAL(dataChanged()), Qt::DirectConnection );
		QObject::connect( m_controllerConnection, SIGNAL(destroyed()), this, SLOT(unlinkControllerConnection()));
		m_valueChanged = true;
		emit dataChanged();
	}
}




float AutomatableModel::controllerValue( int frameOffset ) const
{
	assert(m_controllerConnection != nullptr);

	float v = 0;
	switch (m_scaleType)
	{
		case ScaleType::Linear:
			v = minValue<float>() + ( range() * controllerConnection()->currentValue( frameOffset ) );
			break;
		case ScaleType::Logarithmic:
			v = logToLinearScale(
				controllerConnection()->currentValue( frameOffset ));
			break;
		default:
			qFatal("AutomatableModel::controllerValue(int)"
				"lacks implementation for a scale type (%d, model %p)",
				static_cast<int>(m_scaleType), static_cast<const void*>(this));
			break;
	}
	if (approximatelyEqual(m_step, 1) && m_hasStrictStepSize)
	{
		return std::round(v);
	}
	return v;
}

ValueBuffer * AutomatableModel::valueBuffer()
{
	AudioEngine* const engine = Engine::audioEngine();
	if (!m_registeredWithAudioEngine.load(std::memory_order_acquire))
	{
		engine->recordUnregisteredAutomatableBufferRequest();
		return nullptr;
	}
	if (RealtimeScope::isActive())
	{
		if (m_lastUpdatedPeriod != s_periodCounter)
		{
			if (engine->isRenderThread())
			{
				prepareValueBufferForRender();
				return cachedValueBuffer();
			}
			engine->recordUnpreparedAutomatableBufferRequest();
			return nullptr;
		}
		return cachedValueBuffer();
	}

	// Keep the historic control-side query useful for editors and unit tests.
	// During a render the caller above is strictly read-only; this guarded path
	// can prepare a cache only after render has quiesced.
	auto changeGuard = modelChangeGuard();
	if (!m_registeredWithAudioEngine.load(std::memory_order_acquire))
	{
		engine->recordUnregisteredAutomatableBufferRequest();
		return nullptr;
	}
	prepareValueBufferForRender();
	return cachedValueBuffer();
}


ValueBuffer* AutomatableModel::cachedValueBuffer() noexcept
{
	return m_lastUpdatedPeriod == s_periodCounter && m_hasSampleExactData ? &m_valueBuffer : nullptr;
}



void AutomatableModel::invalidateValueBufferCache() noexcept
{
	// A control setter is excluded by m_changeMutex; Song's serial render
	// owner can also invalidate before the later registry pass. Worker jobs do
	// not mutate this render-owned stamp.
	AudioEngine* const engine = Engine::audioEngine();
	if (!RealtimeScope::isActive() || (engine != nullptr && engine->isRenderThread()))
	{
		m_lastUpdatedPeriod = -1;
	}
}



void AutomatableModel::invalidateLinkedValueBufferCaches() noexcept
{
	AudioEngine* const engine = Engine::audioEngine();
	if (RealtimeScope::isActive() && (engine == nullptr || !engine->isRenderThread())) { return; }
	invalidateValueBufferCache();
	for (auto model = m_nextLink; model != this; model = model->m_nextLink)
	{
		model->invalidateValueBufferCache();
	}
}



std::uint64_t AutomatableModel::packValuePair(float oldValue, float newValue) noexcept
{
	return (static_cast<std::uint64_t>(std::bit_cast<std::uint32_t>(oldValue)) << 32)
		| std::bit_cast<std::uint32_t>(newValue);
}



float AutomatableModel::unpackOldValue(std::uint64_t pair) noexcept
{
	return std::bit_cast<float>(static_cast<std::uint32_t>(pair >> 32));
}



float AutomatableModel::unpackNewValue(std::uint64_t pair) noexcept
{
	return std::bit_cast<float>(static_cast<std::uint32_t>(pair));
}



float AutomatableModel::publishedValue() const noexcept
{
	return unpackNewValue(m_publishedValuePair.load(std::memory_order_acquire));
}



void AutomatableModel::publishValuePair(float oldValue, float newValue) noexcept
{
	m_publishedValuePair.store(packValuePair(oldValue, newValue), std::memory_order_release);
	m_valueRevision.fetch_add(1, std::memory_order_release);
}



void AutomatableModel::consumePublishedValuePair() noexcept
{
	const std::uint64_t revision = m_valueRevision.load(std::memory_order_acquire);
	if (revision == m_renderValueRevision) { return; }
	const std::uint64_t pair = m_publishedValuePair.load(std::memory_order_acquire);
	m_oldValue = unpackOldValue(pair);
	m_renderValue = unpackNewValue(pair);
	m_renderValueRevision = revision;
}



void AutomatableModel::prepareValueBufferForRender(bool skipLinkedControllerLookup)
{
	if (!m_registeredWithAudioEngine.load(std::memory_order_acquire))
	{
		Engine::audioEngine()->recordUnregisteredAutomatableBufferRequest();
		return;
	}
	// If we've already calculated the buffer this period, keep it unchanged.
	if (m_lastUpdatedPeriod == s_periodCounter) { return; }
	consumePublishedValuePair();

	// Sample-accurate automation (feature-list row 9): the automation
	// evaluation published this parameter's curve for the block being rendered
	// (one knot per tick boundary - an AutomationRamp), so the per-sample
	// buffer IS the curve: the block's start value plus the move the curve
	// makes inside the block, at the frame the curve makes it. Without a
	// published ramp for this period the model falls through to the
	// interpolation below, which is the behaviour every existing project has.
	//
	// The transform is the one Song::processAutomations() applies to a tick
	// value - scaledValue() then the automation-mode trim, clamped to the
	// model's own range - so the per-sample path and the per-tick path agree.
	if( m_automationRampPeriod == s_periodCounter && m_automationRamp.knotCount() > 0 )
	{
		float* nvalues = m_valueBuffer.values();
		const int length = m_valueBuffer.length();
		for( int i = 0; i < length; ++i )
		{
			nvalues[i] = fittedValue(effectiveAutomationValue(
				scaledValue(m_automationRamp.valueAt(static_cast<f_cnt_t>(i)))));
		}
		m_oldValue = m_renderValue;
		m_lastUpdatedPeriod = s_periodCounter;
		m_hasSampleExactData = true;
		return;
	}

	const float val = m_renderValue;

	// TODO
	// Let the Controller set the value of connected Models,
	// instead of each Model checking the Controller value every time.

	// Get value buffer from our controller
	if (m_controllerConnection && m_useControllerValue)
	{
		m_controllerConnection->getController()->prepareValueBufferForRender();
	}
	if (m_controllerConnection && m_useControllerValue && m_controllerConnection->getController()->isSampleExact())
	{
		auto vb = m_controllerConnection->valueBuffer();
		if( vb )
		{
			float * values = vb->values();
			float * nvalues = m_valueBuffer.values();
			switch( m_scaleType )
			{
			case ScaleType::Linear:
				for( int i = 0; i < m_valueBuffer.length(); i++ )
				{
					nvalues[i] = minValue<float>() + ( range() * values[i] );
				}
				break;
			case ScaleType::Logarithmic:
				for( int i = 0; i < m_valueBuffer.length(); i++ )
				{
					nvalues[i] = logToLinearScale( values[i] );
				}
				break;
			default:
				qFatal("AutomatableModel::valueBuffer() "
					"lacks implementation for a scale type");
				break;
			}
			m_lastUpdatedPeriod = s_periodCounter;
			m_hasSampleExactData = true;
			return;
		}
	}

	// Get value buffer from one of the linked models' controller
	if (m_useControllerValue && !skipLinkedControllerLookup)
	{
		for (auto next = m_nextLink; next != this; next = next->m_nextLink)
		{
				if (next->controllerConnection() && next->useControllerValue() &&
						next->controllerConnection()->getController()->isSampleExact())
				{
					next->prepareValueBufferForRender(true);
					auto vb = next->cachedValueBuffer();
					if (vb == nullptr) { continue; }
					float* values = vb->values();
					float* nvalues = m_valueBuffer.values();
					for (int i = 0; i < vb->length(); i++)
					{
						nvalues[i] = fittedValue(values[i]);
					}
					m_lastUpdatedPeriod = s_periodCounter;
					m_hasSampleExactData = true;
					return;
				}
		}
	}

	// Note: if there are linked models but no controller, `setValue()` will have
	// published the same old/new pair to all linked models, so the `valueBuffer()`
	// will be the same (even if it is calculated separatly for each model).

	// Populate value buffer by interpolatating between the old and new value
	if (m_oldValue != val)
	{
		m_valueBuffer.interpolate(m_oldValue, val);
		m_oldValue = val;
		m_lastUpdatedPeriod = s_periodCounter;
		m_hasSampleExactData = true;
		return;
	}

	// if we have no sample-exact source for a ValueBuffer, return NULL to signify that no data is available at the moment
	// in which case the recipient knows to use the static value() instead
	m_lastUpdatedPeriod = s_periodCounter;
	m_hasSampleExactData = false;
}


void AutomatableModel::publishAutomationRamp(const AutomationRamp& ramp) noexcept
{
	// The period is stamped, not the model's own value: the ramp is valid for
	// the block the automation evaluation built it for, and a block that
	// publishes nothing leaves the model on its ordinary path. This is what
	// keeps the feature per-block opt-in and a project that never asks for it
	// byte-identical.
	m_automationRamp = ramp;
	m_automationRampPeriod = s_periodCounter;
	invalidateValueBufferCache();
}


const AutomationRamp* AutomatableModel::automationRamp() const noexcept
{
	if (m_automationRampPeriod != s_periodCounter || m_automationRamp.knotCount() == 0)
	{
		return nullptr;
	}
	return &m_automationRamp;
}


void AutomatableModel::unlinkControllerConnection()
{
	auto changeGuard = modelChangeGuard();
	if( m_controllerConnection )
	{
		m_controllerConnection->disconnect( this );
	}

	m_controllerConnection = nullptr;
	invalidateLinkedValueBufferCaches();
}




void AutomatableModel::setInitValue( const float value )
{
	auto changeGuard = modelChangeGuard();
	m_initValue = fittedValue( value );
	bool journalling = testAndSetJournalling( false );
	setValue( value );
	m_oldValue = m_value;
	publishValuePair(m_value, m_value);
	invalidateValueBufferCache();
	setJournalling( journalling );
	emit initValueChanged( value );
}




void AutomatableModel::reset()
{
	setValue( initValue<float>() );
}




float AutomatableModel::globalAutomationValueAt( const TimePos& time )
{
	// get clips that connect to this model
	auto clips = AutomationClip::clipsForModel(this);
	if (clips.empty())
	{
		// if no such clips exist, return current value
		return publishedValue();
	}
	else
	{
		// of those clips:
		// find the clips which overlap with the time position
		std::vector<AutomationClip*> clipsInRange;
		for (const auto& clip : clips)
		{
			int s = clip->startPosition();
			int e = clip->endPosition();
			if (s <= time && e >= time) { clipsInRange.push_back(clip); }
		}

		AutomationClip * latestClip = nullptr;

		if (!clipsInRange.empty())
		{
			// if there are more than one overlapping clips, just use the first one because
			// multiple clip behaviour is undefined anyway
			latestClip = clipsInRange[0];
		}
		else
		// if we find no clips at the exact time, we need to search for the last clip before time and use that
		{
			int latestPosition = 0;

			for (const auto& clip : clips)
			{
				int e = clip->endPosition();
				if (e <= time && e > latestPosition)
				{
					latestPosition = e;
					latestClip = clip;
				}
			}
		}

		if( latestClip )
		{
			// scale/fit the value appropriately and return it
			const float value = latestClip->valueAt(time - latestClip->startPosition() + latestClip->startTimeOffset());
			const float scaled_value = scaledValue( value );
			return fittedValue( scaled_value );
		}
		// if we still find no clip, the value at that time is undefined so
		// just return current value as the best we can do
		else return publishedValue();
	}
}

void AutomatableModel::setUseControllerValue(bool b)
{
	auto changeGuard = modelChangeGuard();
	if (b)
	{
		m_useControllerValue = true;
		invalidateLinkedValueBufferCaches();
		emit dataChanged();
	}
	else if (m_controllerConnection && m_useControllerValue)
	{
		m_useControllerValue = false;
		invalidateLinkedValueBufferCaches();
		emit dataChanged();
	}
}

float FloatModel::getRoundedValue() const
{
	return std::round(value() / step<float>()) * step<float>();
}




int FloatModel::getDigitCount() const
{
	return static_cast<int>(std::ceil(-std::log10(step<float>())));
}



QString FloatModel::displayValue( const float val ) const
{
	return QString::number( castValue<float>( scaledValue( val ) ) );
}

QString IntModel::displayValue( const float val ) const
{
	return QString::number( castValue<int>( scaledValue( val ) ) );
}

QString BoolModel::displayValue( const float val ) const
{
	return QString::number( castValue<bool>( scaledValue( val ) ) );
}


// ---------------------------------------------------------------------------
// Automation modes (post-alpha/automation-modes).
//
// The state this block publishes is what the write decision in
// automationWantsWrite() reads on the audio thread. See the declaration block in
// include/AutomatableModel.h for the semantics of each mode and the thread
// contract; this is the implementation of the state machine only.
// ---------------------------------------------------------------------------

std::atomic<quint64> AutomatableModel::s_transportRun{ 0 };
std::atomic<quint64> AutomatableModel::s_transportRunsStarted{ 0 };
std::atomic<bool> AutomatableModel::s_transportActive{ false };
std::atomic<qint64> AutomatableModel::s_touchTimeoutNs{ 1000 * 1000 * 1000 }; // 1 s




AutomatableModel::AutomationMode AutomatableModel::automationMode() const
{
	return m_automationMode.load( std::memory_order_relaxed );
}




void AutomatableModel::setAutomationMode( AutomationMode mode )
{
	// Leaving Latch ends the engagement. A latch is bound to the transport run
	// it was made in, so it survives the control being released - but a mode
	// change is not the run ending, and coming back to Latch must need a fresh
	// touch rather than resume the engagement a mode change interrupted.
	if( mode != AutomationMode::Latch )
	{
		m_latchRun.store( 0, std::memory_order_relaxed );
	}
	m_automationMode.store( mode, std::memory_order_relaxed );
}




void AutomatableModel::observeAutomationTransport( bool active )
{
	// Called once per rendered period, from the render thread. Only the
	// transport's *edges* change anything, so one run keeps one token for its
	// whole length and a Latch survives it.
	if( active == s_transportActive.load( std::memory_order_relaxed ) )
	{
		return;
	}
	s_transportActive.store( active, std::memory_order_relaxed );
	if( active )
	{
		// A fresh run: every Latch armed under the previous token is over,
		// because its token can no longer match.
		const quint64 run = s_transportRunsStarted.fetch_add( 1, std::memory_order_relaxed ) + 1;
		s_transportRun.store( run, std::memory_order_relaxed );
	}
	else
	{
		// Run 0 matches no Latch engagement, so stopping the transport ends
		// every latch without sweeping the models. An offline render reports
		// here too, which is why an export can never modify automation.
		s_transportRun.store( 0, std::memory_order_relaxed );
	}
}




quint64 AutomatableModel::automationTransportRun()
{
	return s_transportRun.load( std::memory_order_relaxed );
}




qint64 AutomatableModel::automationClockNs()
{
	return std::chrono::duration_cast<std::chrono::nanoseconds>(
			std::chrono::steady_clock::now().time_since_epoch() ).count();
}




qint64 AutomatableModel::automationTouchTimeoutNs()
{
	return s_touchTimeoutNs.load( std::memory_order_relaxed );
}




void AutomatableModel::setAutomationTouchTimeoutNs( qint64 ns )
{
	s_touchTimeoutNs.store( ns < 0 ? 0 : ns, std::memory_order_relaxed );
}




void AutomatableModel::noteAutomationTouchStart( qint64 nowNs, quint64 transportRun )
{
	m_touchStampNs.store( nowNs, std::memory_order_relaxed );
	m_touching.store( true, std::memory_order_relaxed );
	// Arm the latch against the run the engagement was made in. Storing the run
	// (rather than setting a flag) is what stops a latch from a previous run
	// writing into the next one.
	m_latchRun.store( transportRun, std::memory_order_relaxed );
}




void AutomatableModel::noteAutomationTouchEnd()
{
	m_touching.store( false, std::memory_order_relaxed );
}




bool AutomatableModel::automationWantsWrite( quint64 transportRun, qint64 nowNs ) const
{
	if( transportRun == 0 )
	{
		// Nothing writes while the transport is not running - not even Write,
		// and not during an offline render, which reports no run.
		return false;
	}

	switch( automationMode() )
	{
	case AutomationMode::Read:
		// The status quo: follow the automation, never write it.
		return false;
	case AutomationMode::Touch:
	{
		if( !m_touching.load( std::memory_order_relaxed ) )
		{
			return false;
		}
		const qint64 elapsed = nowNs - m_touchStampNs.load( std::memory_order_relaxed );
		// The timeout bounds the window, so a release the widget never saw
		// (mouse-up outside the window, a dropped event) cannot leave a control
		// writing into its automation forever.
		return elapsed <= s_touchTimeoutNs.load( std::memory_order_relaxed );
	}
	case AutomationMode::Latch:
		// Held from the first touch until the run it was armed under ends:
		// releasing the control does not stop it, and a later run does not
		// match it.
		return m_latchRun.load( std::memory_order_relaxed ) == transportRun;
	case AutomationMode::Write:
		// Overwrites the pass for as long as the transport runs.
		return true;
	case AutomationMode::Off:
		// Off: the control neither follows its automation nor writes one. The
		// reading half is enforced where the values are APPLIED - Song's apply
		// pass skips an Off model (Song::processAutomations) - so this side is
		// the same "no write" Read gives.
		return false;
	}
	return false;
}




void AutomatableModel::setTrimOffset( float offset )
{
	m_trimOffset.store( offset, std::memory_order_relaxed );
}




float AutomatableModel::trimOffset() const
{
	return m_trimOffset.load( std::memory_order_relaxed );
}




float AutomatableModel::effectiveAutomationValue( float writtenValue ) const
{
	const float offset = m_trimOffset.load( std::memory_order_relaxed );
	// Deliberately no arithmetic at all when the offset is zero: a zero trim
	// has to be a bit-exact no-op, including for negative zero and subnormals,
	// because the Read render of an untrimmed project must stay byte-identical.
	return offset != 0.0f ? writtenValue + offset : writtenValue;
}


} // namespace lmms
