/*
 * TrackFolder.cpp - a track that CONTAINS other tracks
 *                   (docs/TRACK-FOLDER-DESIGN.md; owner items 3+20+21).
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
 */

#include "TrackFolder.h"

#include <QDomElement>
#include <QStringList>

#include "AudioEngine.h"
#include "Engine.h"
#include "Mixer.h"
#include "Song.h"
#include "TimePos.h"
#include "TrackContainer.h"
#include "TrackContainerView.h"
#include "TrackView.h"

namespace lmms
{

namespace
{

//! Fills \a reason when there is one; the callers always pass one.
void refuse( QString * reason, const char * text )
{
	if( reason != nullptr ) { *reason = QString::fromLatin1( text ); }
}

/*! The children's own channels as ONE attribute: `id:channel` pairs,
 *  comma-separated.
 *
 *  An attribute on the folder's own `<trackfolder>` element, never a child
 *  element - Track::loadTrack turns an unrecognised child element into a real
 *  Clip (SPEC-stable-ids.md section 3.1), the trap the track id and the
 *  frozen take both record. It is written only when the list is not empty, so a
 *  project that never used routing mode re-saves the bytes it always had.
 */
QString bindingsAttribute( const std::vector<TrackFolder::ChannelBinding> & bindings )
{
	QStringList parts;
	parts.reserve( static_cast<int>( bindings.size() ) );
	for( const TrackFolder::ChannelBinding & binding : bindings )
	{
		parts.append( QStringLiteral("%1:%2").arg( binding.childId )
			.arg( static_cast<int>( binding.channel ) ) );
	}
	return parts.join( QLatin1Char(',') );
}

std::vector<TrackFolder::ChannelBinding> parseBindings( const QString & text )
{
	std::vector<TrackFolder::ChannelBinding> bindings;
	for( const QString & part : text.split( QLatin1Char(','), Qt::SkipEmptyParts ) )
	{
		const QStringList pair = part.split( QLatin1Char(':') );
		if( pair.size() != 2 ) { continue; }
		TrackFolder::ChannelBinding binding;
		binding.childId = pair.at( 0 ).toInt();
		binding.channel = pair.at( 1 ).toInt();
		bindings.push_back( binding );
	}
	return bindings;
}

} // namespace

TrackFolder::TrackFolder( TrackContainer * tc ) :
	Track( Type::Folder, tc )
{
}

/*! A folder makes no sound of its own.
 *
 *  In GROUP mode the children play themselves, exactly as they would without a
 *  folder. In ROUTING mode they still play themselves - what changed is where
 *  their handle mixes to (`AudioBusHandle::setNextMixerChannel`), so the sum
 *  happens in the mixer and not here. Either way there is nothing for this
 *  track to schedule, and returning false is what keeps `Track::play`'s
 *  contract ("this track contributed no handle") true.
 */
bool TrackFolder::play( const TimePos &, const f_cnt_t, const f_cnt_t, int )
{
	return false;
}

/*! The folder's row: an ordinary `gui::TrackView`.
 *
 *  0.3.0 draws NO folder affordance on it - no chevron, no pin, no child
 *  indent, no colour header - and collapses nothing in the list. That absence is
 *  stated in one line in docs/RELEASE-NOTES-v0.3.0-alpha.md and in
 *  docs/KNOWN-LIMITATIONS.md; the state it would drive (`collapsed`, `pinned`)
 *  is real, persisted engine state reachable through `track.folder_set_collapsed`
 *  and `track.set_pinned`.
 */
gui::TrackView * TrackFolder::createView( gui::TrackContainerView * view )
{
	return new gui::TrackView( this, view );
}

Clip * TrackFolder::createClip( const TimePos & )
{
	return nullptr;
}

// ---------------------------------------------------------------------------
// Membership. DERIVED from the children's own parent pointer, in container
// (document) order: one source of truth, so no restore can leave the folder
// holding a child list the model no longer agrees with.
// ---------------------------------------------------------------------------
std::vector<Track*> TrackFolder::children() const
{
	std::vector<Track*> members;
	TrackContainer * container = trackContainer();
	if( container == nullptr ) { return members; }
	for( Track * track : container->tracks() )
	{
		if( track->parentFolder() == this ) { members.push_back( track ); }
	}
	return members;
}

int TrackFolder::childCount() const
{
	return static_cast<int>( children().size() );
}

/*! Whether \a child may be parented here - a typed reason when it may not.
 *
 *  Three refusals, and the third is the one that matters: a folder may not hold
 *  itself, a child must live in the same container, and the relation may not
 *  close a CYCLE (the child is this folder or one of its ancestors), because a
 *  cycle makes `children()` and the load-time resolution both non-terminating
 *  in the user's hands.
 */
bool TrackFolder::canHold( const Track * child, QString * reason ) const
{
	// A successful answer leaves NO reason behind: a caller that reuses one
	// QString across calls must not read a stale refusal out of a later success.
	if( reason != nullptr ) { reason->clear(); }
	if( child == nullptr ) { refuse( reason, "no track was named" ); return false; }
	if( child == this ) { refuse( reason, "a folder cannot hold itself" ); return false; }
	if( child->trackContainer() != trackContainer() )
	{
		refuse( reason, "the track is in another track container" );
		return false;
	}
	for( const Track * walker = this; walker != nullptr; walker = walker->parentFolder() )
	{
		if( walker == child )
		{
			refuse( reason, "the track already contains this folder" );
			return false;
		}
	}
	return true;
}

void TrackFolder::childLinked( Track * child )
{
	if( m_mode != Mode::Routing ) { return; }
	routeChild( child );
}

void TrackFolder::childUnlinked( Track * child )
{
	// A child LEAVING a routing folder goes back to the channel it was on: the
	// folder's channel sums its children, so a track that is no longer a child
	// must not keep feeding it. The binding recorded for \a child is what it goes
	// back to, and it stays in the list so a child that rejoins can be routed
	// again without inventing a channel for it.
	if( m_mode != Mode::Routing ) { return; }
	unrouteChild( child );
}

/*! Puts \a child back on its own recorded channel, if it is currently on the
 *  folder's. Shared by releaseRouting() and childUnlinked() so the two cannot
 *  disagree about what "back where it was" means.
 */
void TrackFolder::unrouteChild( Track * child )
{
	IntModel * model = child->mixerChannelModel();
	if( model == nullptr ) { return; }
	const int previous = savedChannelFor( child->id() );
	if( previous < 0 || !alreadyRouted( child ) ) { return; }
	model->setValue( static_cast<int>( previous ) );
}

int TrackFolder::savedChannelFor( int childId ) const
{
	for( const ChannelBinding & binding : m_savedChildChannels )
	{
		if( binding.childId == childId ) { return binding.channel; }
	}
	return -1;
}

// ---------------------------------------------------------------------------
// The routing half.
// ---------------------------------------------------------------------------

/*! Points \a child's own mixer channel at this folder's channel, remembering
 *  where it was. A child that already points there (a folder reloaded from a
 *  project saved in routing mode) records nothing: the file's `prevch` is the
 *  record, and re-recording would overwrite it with the folder's own channel.
 */
void TrackFolder::routeChild( Track * child )
{
	IntModel * model = child->mixerChannelModel();
	if( model == nullptr ) { return; }   // a track type with no channel of its own
	if( alreadyRouted( child ) ) { return; }
	// The child's model has to be able to HOLD the folder's channel index.
	// InstrumentTrack / SampleTrack set their range from the mixer's channel
	// count when they are constructed or loaded, and Mixer::createChannel() does
	// NOT re-range anybody - so without this widening, setValue() would be fitted
	// back down to the old maximum and the child would silently stay where it
	// was: a wrong route with no error at all. MixerView does the same widening
	// when a channel is added (src/gui/MixerView.cpp).
	if( model->maxValue() < static_cast<float>( m_mixerChannel ) )
	{
		model->setRange( model->minValue(), static_cast<float>( m_mixerChannel ) );
	}
	m_savedChildChannels.push_back(
		ChannelBinding{ child->id(), model->value() } );
	model->setValue( static_cast<int>( m_mixerChannel ) );
}

bool TrackFolder::alreadyRouted( Track * child ) const
{
	if( m_mixerChannel < 0 ) { return false; }
	IntModel * model = child->mixerChannelModel();
	return model != nullptr && model->value() == m_mixerChannel;
}

/*! Turns routing mode ON: the folder gets one regular mixer channel and every
 *  child's binding is pointed at it.
 *
 *  `Mixer::createChannel()` and NOT `createBusChannel()`: a parallel bus refuses
 *  instrument output outright (Mixer.cpp: "a parallel bus never receives
 *  instrument output directly"), so a bus could not be the summing point this
 *  mode is. A channel created here is an ordinary channel - the thing the tree
 *  already supports for two tracks that share a mixing channel - which is what
 *  makes "the children's output summed through the folder" ride the existing
 *  model rather than a new one.
 */
bool TrackFolder::enableRouting( QString * error )
{
	if( m_mode == Mode::Routing ) { return true; }
	if( children().empty() )
	{
		refuse( error, "a routing folder needs at least one child to sum" );
		return false;
	}
	if( m_mixerChannel < 0 )
	{
		const int created = Engine::mixer()->createChannel();
		if( created < 0 )
		{
			refuse( error, "the mixer refused a new channel" );
			return false;
		}
		m_mixerChannel = created;
		if( MixerChannel * channel = Engine::mixer()->mixerChannel( m_mixerChannel ) )
		{
			channel->m_name = name();
		}
	}
	m_mode = Mode::Routing;
	for( Track * child : children() ) { routeChild( child ); }
	Engine::getSong()->setModified();
	return true;
}

/*! Turns routing mode OFF: every child goes back to the channel it was on, and
 *  the folder's own channel is released.
 *
 *  The ORDER is load-bearing. The children are moved off the folder's channel
 *  FIRST and the channel is deleted LAST, because a child left pointing at the
 *  deleted index would be routing into whatever channel the mixer renumbered
 *  into that slot - a silent wrong route rather than a crash, which is the
 *  failure mode worth designing out. `Mixer::deleteChannel` renumbers the
 *  channels above the one it removes; the folder's own channel is created on
 *  demand and is therefore the highest in the ordinary case, so the renumber
 *  moves nothing the children were told about.
 */
bool TrackFolder::releaseRouting()
{
	for( Track * child : children() ) { unrouteChild( child ); }
	m_savedChildChannels.clear();
	if( m_mixerChannel >= 0 )
	{
		Engine::mixer()->deleteChannel( static_cast<int>( m_mixerChannel ) );
		m_mixerChannel = -1;
	}
	if( m_mode != Mode::Group )
	{
		m_mode = Mode::Group;
		Engine::getSong()->setModified();
	}
	return true;
}

bool TrackFolder::setMode( Mode mode, QString * error )
{
	if( mode == m_mode ) { return true; }
	if( mode == Mode::Routing ) { return enableRouting( error ); }
	return releaseRouting();
}

void TrackFolder::setCollapsed( bool collapsed )
{
	if( m_collapsed == collapsed ) { return; }
	m_collapsed = collapsed;
	Engine::getSong()->setModified();
}

void TrackFolder::setPinned( bool pinned )
{
	if( m_pinned == pinned ) { return; }
	m_pinned = pinned;
	Engine::getSong()->setModified();
}

// ---------------------------------------------------------------------------
// Serialisation. The folder's own state is one <trackfolder> child element of
// its <track> element - the same shape <instrumenttrack> has, and the only shape
// Track::loadTrack routes to loadTrackSpecificSettings.
// ---------------------------------------------------------------------------
void TrackFolder::saveTrackSpecificSettings( QDomDocument &, QDomElement & parent, bool )
{
	parent.setAttribute( QStringLiteral("mode"),
		m_mode == Mode::Routing ? QStringLiteral("routing") : QStringLiteral("group") );
	if( m_collapsed ) { parent.setAttribute( QStringLiteral("collapsed"), 1 ); }
	if( m_pinned ) { parent.setAttribute( QStringLiteral("pinned"), 1 ); }
	if( m_mixerChannel >= 0 )
	{
		parent.setAttribute( QStringLiteral("mixch"), static_cast<int>( m_mixerChannel ) );
	}
	if( !m_savedChildChannels.empty() )
	{
		parent.setAttribute( QStringLiteral("prevch"), bindingsAttribute( m_savedChildChannels ) );
	}
}

/*! Reads the folder's own element, RESETTING every field on absence - the rule
 *  a journal checkpoint restore depends on: the checkpoint captures this XML
 *  BEFORE a write, so a field that survived its own absence could never be taken
 *  back off by one `control.undo`.
 */
void TrackFolder::loadTrackSpecificSettings( const QDomElement & element )
{
	m_mode = element.attribute( QStringLiteral("mode") ) == QLatin1String("routing")
		? Mode::Routing : Mode::Group;
	m_collapsed = element.attribute( QStringLiteral("collapsed") ).toInt() != 0;
	m_pinned = element.attribute( QStringLiteral("pinned") ).toInt() != 0;
	m_mixerChannel = element.hasAttribute( QStringLiteral("mixch") )
		? element.attribute( QStringLiteral("mixch") ).toInt() : -1;
	m_savedChildChannels = element.hasAttribute( QStringLiteral("prevch") )
		? parseBindings( element.attribute( QStringLiteral("prevch") ) )
		: std::vector<ChannelBinding>();
}

} // namespace lmms
