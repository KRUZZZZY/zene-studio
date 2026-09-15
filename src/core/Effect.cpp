/*
 * Effect.cpp - base-class for effects
 *
 * Copyright (c) 2006-2007 Danny McRae <khjklujn/at/users.sourceforge.net>
 * Copyright (c) 2006-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#include "Effect.h"

#include <QDomElement>

#include "AudioBuffer.h"
#include "AudioBus.h"
#include "ConfigManager.h"
#include "EffectChain.h"
#include "EffectControls.h"
#include "EffectView.h"
#include "ProjectIds.h"
#include "SampleFrame.h"

namespace lmms
{


Effect::Effect( const Plugin::Descriptor * _desc,
			Model * _parent,
			const Descriptor::SubPluginFeatures::Key * _key ) :
	Plugin( _desc, _parent, _key ),
	m_parent( nullptr ),
	// The stable id, handed out once, here, at creation (SPEC-stable-ids.md
	// 2.1, slice 2). It is the number in fx-<n> and it never changes while the
	// effect lives, so a client that was told fx-4 keeps fx-4 when a sibling
	// effect is inserted, deleted, reordered or undone - and it is the same
	// number after a save/open cycle, because saveSettings writes it into the
	// project file and loadSettings takes it back. The counter is the
	// project-scoped ProjectIds, shared with the track, clip, note and channel
	// ids.
	m_id( ProjectIds::allocate() ),
	m_okay( true ),
	m_noRun( false ),
	m_awake(false),
	m_enabledModel( true, this, tr( "Effect enabled" ) ),
	m_wetDryModel( 1.0f, -1.0f, 1.0f, 0.01f, this, tr( "Wet/Dry mix" ) ),
	m_autoQuitModel( 1.0f, 1.0f, 8000.0f, 100.0f, 1.0f, this, tr( "Decay" ) ),
	m_autoQuitEnabled(ConfigManager::inst()->value("ui", "disableautoquit", "1").toInt() == 0)
{
	m_wetDryModel.setCenterValue(0);

	// Call the virtual method onEnabledChanged so that effects can react to changes,
	// e.g. by resetting state. A bypassed effect contributes no latency, so the
	// owning chain's PDC graph must be refreshed as well (#605).
	connect(&m_enabledModel, &BoolModel::dataChanged, [this] {
		onEnabledChanged();
		if (m_parent != nullptr) { m_parent->refreshLatency(); }
	});
}

void Effect::setDontRun(bool _state)
{
	if (m_noRun == _state)
	{
		return;
	}
	m_noRun = _state;
	if (m_parent != nullptr)
	{
		// Bypassed effects are not in the signal path, so their reported
		// latency must leave the chain's PDC accounting (#605).
		m_parent->refreshLatency();
	}
}

/*! Replace the id with \a id, and raise the project counter above it so the
 *  number can never be handed out again (SPEC-stable-ids.md rule R3).
 *
 *  A negative value is ignored: it is not a number this surface can address,
 *  and the constructor's id is always a valid answer.
 */
void Effect::setId( int id )
{
	if( id < 0 )
	{
		return;
	}
	m_id = id;
	ProjectIds::observe( id );
}

void Effect::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	m_enabledModel.saveSettings( _doc, _this, "on" );
	m_wetDryModel.saveSettings( _doc, _this, "wet" );
	m_autoQuitModel.saveSettings( _doc, _this, "autoquit" );
	controls()->saveState( _doc, _this );

	// The effect's stable id (SPEC-stable-ids.md slice 2). An ATTRIBUTE on the
	// effect's own element, never a child element: the loader walks the
	// element's children and turns each one into control state, so a child
	// would become a phantom control group on load.
	_this.setAttribute( "id", m_id );
}




void Effect::loadSettings( const QDomElement & _this )
{
	m_enabledModel.loadSettings( _this, "on" );
	m_wetDryModel.loadSettings( _this, "wet" );
	m_autoQuitModel.loadSettings( _this, "autoquit" );

	// The stable id (SPEC-stable-ids.md slice 2; the slice-1 pattern of
	// Track::loadTrack). A file that carries one keeps it; a file that does
	// not leaves the number the constructor already handed out, which is
	// deterministic because EffectChain::loadSettings recreates the effects in
	// document order. Either way ProjectIds::loadAssignments() counts it.
	//
	// An effect element read out of a device-state document KEEPS the id it was
	// constructed with instead (rule R4). That document - the
	// <zenepluginstate> root plugin.state_save/state_load, plugin.preset_save/
	// preset_load and the chain preset's embedded device state all use - is a
	// snapshot of ONE instance, and it is loaded into an instance that is
	// already alive and already has an id of its own: taking the snapshot's id
	// would give that instance the id of the effect the snapshot came from,
	// which the surface may still be addressing (the defect class the contract
	// exists to remove). The wrapper decides it, not the element
	// (ProjectIds::isDocumentElement); a project's effects sit under their
	// channel's chain and are loaded from <song>.
	if( ProjectIds::isDocumentElement( _this ) )
	{
		if( _this.hasAttribute( "id" ) )
		{
			bool ok = false;
			const int stored = _this.attribute( "id" ).toInt( &ok );
			if( ok && stored >= 0 ) { setId( stored ); }
			else { ProjectIds::noteLoadAssignment(); }
		}
		else
		{
			ProjectIds::noteLoadAssignment();
		}
	}

	QDomNode node = _this.firstChild();
	while( !node.isNull() )
	{
		if( node.isElement() )
		{
			if( controls()->nodeName() == node.nodeName() )
			{
				controls()->restoreState( node.toElement() );
			}
		}
		node = node.nextSibling();
	}
}




const AudioBuffer* Effect::sidechainBuffer() const
{
	return m_parent ? m_parent->sidechainBuffer() : nullptr;
}




bool Effect::processAudioBuffer(AudioBuffer& inOut)
{
	if (!isAwake())
	{
		if (!inOut.hasSignal(0b11))
		{
			// Sleeping plugins need to zero any track channels their output is routed to in order to
			// prevent sudden track channel passthrough behavior when the plugin is put to sleep.
			// Otherwise auto-quit could become audibly noticeable, which is not intended.

			inOut.silenceChannels(0b11);

			return false;
		}

		wakeUp();
	}

	if (!isProcessingAudio())
	{
		// Plugin is awake but not processing audio
		processBypassedImpl();
		return false;
	}

	const auto status = processImpl(inOut.interleavedBuffer().asSampleFrames().data(), inOut.frames());

	// Copy interleaved plugin output to planar
	toPlanar(inOut.interleavedBuffer(), inOut.groupBuffers(0));

	const auto sanitized = Engine::audioEngine()->sanitizationEnabled() ? inOut.sanitize(0b11) : false;
	m_corrupted.store(sanitized, std::memory_order_relaxed);

	// Update silence status for track channels the processor wrote to
	const bool silentOutput = inOut.updateSilenceFlags(0b11);

	switch (status)
	{
		case ProcessStatus::Continue:
			break;
		case ProcessStatus::ContinueIfNotQuiet:
			handleAutoQuit(silentOutput);
			break;
		case ProcessStatus::Sleep:
			goToSleep();
			return false;
		default:
			break;
	}

	return isAwake();
}


bool Effect::processAudioBuffer(AudioBus& inOut)
{
	const auto* apm = audioPortsModel();

	if (!isAwake())
	{
		// Sleeping plugins need to zero any track channels their output is routed to in order to
		// prevent sudden track channel passthrough behavior when the plugin is put to sleep.
		// Otherwise auto-quit could become audibly noticeable, which is not intended.

		const auto hasInputNoise = apm
			? inOut.hasInputNoise(*apm)
			: !(inOut.quietChannels()[0] && inOut.quietChannels()[1]);

		if (!hasInputNoise)
		{
			if (apm)
			{
				inOut.silenceChannels(*apm);
			}
			else
			{
				inOut.silenceAllChannels();
			}

			return false;
		}

		wakeUp();
	}

	if (!isProcessingAudio())
	{
		// Plugin is awake but not processing audio
		processBypassedImpl();
		return false;
	}

	// Effects without audio ports process the first track channel pair, which
	// matches the single-buffer AudioBuffer interface. Effects with audio ports
	// (AudioPlugin) override this method in order to route their ports instead.
	const auto status = processImpl(inOut.bus()[0], inOut.frames());

	const auto sanitized = Engine::audioEngine()->sanitizationEnabled() ? inOut.sanitizeAll() : false;
	m_corrupted.store(sanitized, std::memory_order_relaxed);

	// Update silence status for the track channels the processor wrote to
	const bool silentOutput = apm ? inOut.update(*apm) : inOut.updateAll();

	switch (status)
	{
		case ProcessStatus::Continue:
			break;
		case ProcessStatus::ContinueIfNotQuiet:
			handleAutoQuit(silentOutput);
			break;
		case ProcessStatus::Sleep:
			goToSleep();
			return false;
		default:
			break;
	}

	return isAwake();
}




Effect * Effect::instantiate( const QString& pluginName,
				Model * _parent,
				Descriptor::SubPluginFeatures::Key * _key )
{
	Plugin * p = Plugin::instantiateWithKey( pluginName, _parent, _key );
	// check whether instantiated plugin is an effect
	if( dynamic_cast<Effect *>( p ) != nullptr )
	{
		// everything ok, so return pointer
		auto effect = dynamic_cast<Effect*>(p);
		effect->m_parent = dynamic_cast<EffectChain *>(_parent);
		return effect;
	}

	// not quite... so delete plugin and leave it up to the caller to instantiate a DummyEffect
	delete p;

	return nullptr;
}




void Effect::handleAutoQuit(bool silentOutput)
{
	if (!m_autoQuitEnabled)
	{
		return;
	}

	// Check whether we need to continue processing input. Restart the
	// counter if the threshold has been exceeded.

	if (silentOutput)
	{
		// The output buffer is quiet, so check if auto-quit should be activated yet
		if (++m_quietBufferCount > timeout())
		{
			// Activate auto-quit
			goToSleep();
		}
	}
	else
	{
		// The output buffer is not quiet
		m_quietBufferCount = 0;
	}
}




gui::PluginView * Effect::instantiateView( QWidget * _parent )
{
	return new gui::EffectView( this, _parent );
}

} // namespace lmms
