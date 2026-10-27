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
#include "FolderSummaryLane.h"
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
		bool childOk = false;
		bool channelOk = false;
		const int childId = pair.at( 0 ).toInt( &childOk );
		const int channel = pair.at( 1 ).toInt( &channelOk );
		// Both halves must CONVERT: an unchecked toInt() yields 0 for "bad",
		// and 0 is the master channel - a real one. A malformed pair is
		// discarded rather than adopted as a binding to the master.
		if( !childOk || !channelOk ) { continue; }
		TrackFolder::ChannelBinding binding;
		binding.childId = childId;
		binding.channel = channel;
		bindings.push_back( binding );
	}
	return bindings;
}

} // namespace

TrackFolder::TrackFolder( TrackContainer * tc ) :
	Track( Type::Folder, tc )
{
}

/*! Releases every child that named this folder before the object goes away.
 *
 *  The order here is the whole point. ~TrackContainer deletes its tracks in
 *  vector order, and the folder's own vector position is wherever it was
 *  created - often BEFORE the tracks it holds. Without this, the folder is freed
 *  first, each child keeps a dangling `m_parentFolder`, and the child's own
 *  ~Track unlink call writes through freed memory (measured: SIGSEGV on
 *  shutdown, exit code -11). The children are put back at the container root
 *  through Track::setParentFolder, the same call every other unlink uses - it
 *  also puts a ROUTING folder's child back on its own recorded channel.
 */
TrackFolder::~TrackFolder()
{
	TrackContainer * container = trackContainer();
	if( container == nullptr ) { return; }
	for( Track * track : container->tracks() )
	{
		if( track != this && track->parentFolder() == this ) { track->setParentFolder( nullptr ); }
	}

	// The folder's OWN channel goes back with it. ~Track unlinks from the
	// folder first, and unlinking a ROUTING child already put it back on its
	// saved channel, so by here no child points at m_mixerChannel - which is
	// the order releaseRouting() documents and must be kept: Mixer::deleteChannel
	// renumbers the channels above the one it removes.
	//
	// Teardown and a loaded `mixch` are both cases this has to survive.
	// Engine::destroy() clears the song BEFORE deleting the mixer, and a
	// folder deleted with it lands here with the model half gone;
	// deleteHelper() nulls what it deletes, so the mixer deleteChannel()
	// drives (and the song it reads) may already be gone. A saved `mixch`
	// can also name a channel this mixer never allocated - it is an index
	// read from the file, not a channel this folder created - and
	// Mixer::deleteChannel() indexes its channel vector unchecked. A channel
	// that is not there is not ours to delete, and there is nothing to
	// renumber around.
	if( m_mixerChannel > 0 && Engine::mixer() != nullptr && Engine::getSong() != nullptr
		&& m_mixerChannel < static_cast<int>( Engine::mixer()->numChannels() ) )
	{
		Engine::mixer()->deleteChannel( static_cast<int>( m_mixerChannel ) );
	}
	m_mixerChannel = -1;
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

/*! The folder's row: an ordinary `gui::TrackView`, with (0.5.0 ED-035) a
 *  FolderSummaryLane over its content: the children's clips as summary clips.
 *
 *  0.3.0 drew NO folder affordance on it - no chevron, no pin, no child
 *  indent, no colour header - and collapses nothing in the list. That absence is
 *  stated in one line in docs/RELEASE-NOTES-v0.3.0-alpha.md and in
 *  docs/KNOWN-LIMITATIONS.md; the state it would drive (`collapsed`, `pinned`)
 *  is real, persisted engine state reachable through `track.folder_set_collapsed`
 *  and `track.set_pinned`.
 */
gui::TrackView * TrackFolder::createView( gui::TrackContainerView * view )
{
	auto* trackView = new gui::TrackView( this, view );
	// 0.5.0 ED-035: the row draws the children's clips as summary clips, each edited as one.
	new gui::FolderSummaryLane( this, trackView );
	return trackView;
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
	emit stateChanged();
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
	emit stateChanged();
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
	Mixer * mixer = Engine::mixer();
	if( mixer == nullptr || m_mixerChannel < 0
		|| m_mixerChannel >= static_cast<int>( mixer->numChannels() ) ) { return; }
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
	recordBinding( child->id(), model->value() );
	model->setValue( static_cast<int>( m_mixerChannel ) );
}

/*! Remembers where \a childId is, as it is RIGHT NOW, for the routing that is
 *  about to take it over.
 *
 *  A binding is REPLACED, never appended behind. A child that leaves a routing
 *  folder keeps its binding (childUnlinked), so when it rejoins - possibly on a
 *  channel the user picked in between - the list would hold two entries for it
 *  and savedChannelFor(), which returns the FIRST match, would hand back the
 *  stale one: releasing the routing would then undo the LATER assignment instead
 *  of the routing that took the child away from it.
 */
void TrackFolder::recordBinding( int childId, int channel )
{
	for( ChannelBinding & binding : m_savedChildChannels )
	{
		if( binding.childId == childId )
		{
			binding.channel = channel;
			return;
		}
	}
	m_savedChildChannels.push_back( ChannelBinding{ childId, channel } );
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
	if( m_mixerChannel > 0 && Engine::mixer() != nullptr
		&& m_mixerChannel < static_cast<int>( Engine::mixer()->numChannels() ) )
	{
		Engine::mixer()->deleteChannel( static_cast<int>( m_mixerChannel ) );
	}
	m_mixerChannel = -1;
	// The children are off the folder's channel and that channel no longer exists,
	// so the RANGE routeChild() widened to hold it goes back to the channels the
	// mixer really has - the same thing MixerView::updateMaxChannelSelector does
	// for every track when the channel list changes. Left widened, a child could
	// still select the deleted index and would feed a channel that is not there.
	for( Track * child : children() ) { shrinkChildRange( child ); }
	if( m_mode != Mode::Group )
	{
		m_mode = Mode::Group;
		Engine::getSong()->setModified();
	}
	return true;
}

void TrackFolder::shrinkChildRange( Track * child )
{
	if( Engine::mixer() == nullptr ) { return; }
	IntModel * model = child->mixerChannelModel();
	if( model == nullptr ) { return; }
	const float top = static_cast<float>( Engine::mixer()->numChannels() - 1 );
	if( model->maxValue() > top ) { model->setRange( model->minValue(), top ); }
}

/*! Follows the mixer renumbering a deleted channel causes.
 *
 *  The rule is the one the track walk applies to the instrument and sample
 *  channel models: the folder that recorded the deleted index loses its channel,
 *  every folder above it comes down by one. What is NOT repeated here is the
 *  children's own models - they are ordinary tracks in the mixer's track lists,
 *  so the walk that renumbers them runs over them exactly as it does over any
 *  other track and would send them to master.
 */
void TrackFolder::channelDeleted( int deletedIndex )
{
	if( m_mixerChannel < 0 ) { return; }
	// The channels the children go BACK to are mixer indices too, and move with
	// the strip they name, the same rule Mixer::deleteChannel applies to the
	// tracks: the deleted one falls back to master, those above come down one.
	// Done first, because the own-channel case below un-routes through them.
	for( ChannelBinding & binding : m_savedChildChannels )
	{
		if( binding.channel == deletedIndex ) { binding.channel = 0; }
		else if( binding.channel > deletedIndex ) { binding.channel -= 1; }
	}
	if( m_mixerChannel == deletedIndex )
	{
		// The folder's own summing point is the one that went. Its children are
		// standing on an index that now names a different strip, so they go back
		// to the channel they were on before routing - the same place
		// releaseRouting() puts them, and the reason this runs BEFORE the track
		// walk, which would otherwise put them on master.
		for( Track * child : children() ) { unrouteChild( child ); }
		m_savedChildChannels.clear();
		m_mixerChannel = -1;
		if( m_mode != Mode::Group )
		{
			// A routing folder with no channel has nothing to sum into; leaving
			// the mode on would advertise a summing point that is not there.
			m_mode = Mode::Group;
		}
		return;
	}
	if( m_mixerChannel > deletedIndex ) { m_mixerChannel -= 1; }
}

/*! Gives a restored routing folder a channel that is really there.
 *
 *  The restored `mixch` is what the folder had when the checkpoint was taken,
 *  and the delete this restore reverses gave that channel back to the mixer, so
 *  the index it names belongs to somebody else now. Adopting it would leave the
 *  folder claiming a strip it does not own - and every child it routes would sum
 *  into that other strip - so the folder takes a fresh channel, the same one
 *  enableRouting() would give it, and the children go where they went before.
 */
bool TrackFolder::ensureRoutingChannel( QString * error )
{
	if( m_mode != Mode::Routing || Engine::mixer() == nullptr ) { return true; }
	if( m_mixerChannel >= 0
		&& m_mixerChannel < static_cast<int>( Engine::mixer()->numChannels() ) )
	{
		// A saved `mixch` that still exists is adopted as-is, EXCEPT for the name:
		// the folder is back, and its channel must advertise the folder again.
		if( MixerChannel * channel = Engine::mixer()->mixerChannel( m_mixerChannel ) )
		{
			channel->m_name = name();
		}
		return true;
	}
	m_mixerChannel = -1;
	// The channel is created here rather than through enableRouting() because that
	// entry point REFUSES a folder with no children - and on the restore path this
	// runs before the children are back, so it would refuse the very folder it is
	// here to fix. What is copied is the part that is about the channel: an
	// ordinary channel, created the same way, named after the folder.
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
	// The children that ARE back (the restore walk rebuilds them one track at a
	// time) go to the new channel; the ones that arrive later are routed by
	// childLinked(), which is the same route this is.
	for( Track * child : children() ) { routeChild( child ); }
	return true;
}

/*! Renaming a routing folder renames its mixer channel too.
 *
 *  enableRouting() copies the folder's name onto the channel it creates, and that
 *  name is what the channel's caption, tooltip and LCD show - and what the project
 *  saves for it (Mixer::saveSettings writes `name`). Without this the channel kept
 *  advertising the name the folder had when routing was turned on.
 */
void TrackFolder::setName( const QString & newName )
{
	Track::setName( newName );
	if( m_mode != Mode::Routing || m_mixerChannel < 0 || Engine::mixer() == nullptr ) { return; }
	if( MixerChannel * channel = Engine::mixer()->mixerChannel( m_mixerChannel ) )
	{
		channel->m_name = newName;
	}
}

bool TrackFolder::setMode( Mode mode, QString * error )
{
	if( mode == m_mode ) { return true; }
	const bool changed = mode == Mode::Routing ? enableRouting( error ) : releaseRouting();
	emit stateChanged();
	return changed;
}

void TrackFolder::setCollapsed( bool collapsed )
{
	if( m_collapsed == collapsed ) { return; }
	m_collapsed = collapsed;
	Engine::getSong()->setModified();
	emit stateChanged();
}

void TrackFolder::setPinned( bool pinned )
{
	if( m_pinned == pinned ) { return; }
	m_pinned = pinned;
	Engine::getSong()->setModified();
	emit stateChanged();
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
