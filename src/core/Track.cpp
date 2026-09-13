/*
 * Track.cpp - implementation of Track class
 *
 * Copyright (c) 2004-2014 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

/** \file Track.cpp
 *  \brief Implementation of Track class
 */


#include "Track.h"

#include <QDomElement>
#include <QVariant>

#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "AudioEngine.h"
#include "Clip.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "ProjectIds.h"
#include "Sample.h"
#include "SampleBuffer.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"


namespace lmms
{

namespace
{

/*! The clip of a track whose position and length are exactly these ticks, or
 *  nullptr.
 *
 *  A freeze records a clip it muted by its TICKS, never by index or pointer:
 *  the record is serialised with the track and replayed on load, and clip ids
 *  in this engine are still index-derived (SPEC-stable-ids.md slice 2), so an
 *  index would name a different clip after any edit before it.
 */
Clip* clipAt(Track::clipVector& clips, tick_t startTicks, tick_t lengthTicks)
{
	for (Clip* clip : clips)
	{
		if (clip != nullptr && clip->startPosition().getTicks() == startTicks
			&& clip->length().getTicks() == lengthTicks)
		{
			return clip;
		}
	}
	return nullptr;
}

} // namespace

/*! \brief Create a new (empty) track object
 *
 *  The track object is the whole track, linking its contents, its
 *  automation, name, type, and so forth.
 *
 * \param type The type of track (Song Editor or Pattern Editor)
 * \param tc The track Container object to encapsulate in this track.
 *
 * \todo check the definitions of all the properties - are they OK?
 */
Track::Track( Type type, TrackContainer * tc ) :
	Model( tc ),                   /*!< The track Model */
	m_trackContainer( tc ),        /*!< The track container object */
	m_type( type ),                /*!< The track type */
	// The stable id, handed out once, here, at creation (SPEC-stable-ids.md
	// 2.1). It is the number in trk-<n> and it never changes while the track
	// lives, so a client that was told trk-7 keeps trk-7 when a sibling is
	// added or removed. loadTrack() overrides it with the file's value on a
	// project load, and a file element with no id keeps this one - which is
	// what makes legacy assignment deterministic, because the load walks the
	// containers in document order.
	m_id( ProjectIds::allocate() ),
	m_name(),                       /*!< The track's name */
	m_mutedModel( false, this, tr( "Mute" ) ), /*!< For controlling track muting */
	m_soloModel( false, this, tr( "Solo" ) ), /*!< For controlling track soloing */
	m_mutedBeforeSolo( false ),     /*!< Transient pre-solo mute state; written to
	                                 * the file by saveTrack as an int, so an
	                                 * uninitialised value made every save
	                                 * nondeterministic (measured: the same fresh
	                                 * track saved as mutedBeforeSolo=1 in one run
	                                 * and 48 in the next, which StableTrackIdsTest
	                                 * compares). This entry sits AFTER
	                                 * m_soloModel because include/Track.h declares
	                                 * m_mutedBeforeSolo after it (merge train 3F:
	                                 * the incoming unit listed it before, which the
	                                 * release configuration's -Werror=reorder
	                                 * rejects; C++ initialises in declaration order
	                                 * either way, so the order here is a no-op). */
	m_clips()        /*!< The clips (segments) */
{	
m_trackContainer->addTrack( this );
m_height = -1;
}

/*! Subscribe to the signals that END a pass through a frozen take (freeze /
 *  bounce-in-place): a stop, a seek or loop, and a mute change.
 *
 *  Without these, the "one handle per pass" flag would still be set after a stop
 *  inside the take and the next play would return early, leaving the track
 *  silent until a seek past the take - which is exactly why the engine's own
 *  clip state is reset on these same signals (SampleClip.cpp's constructor,
 *  "playbutton clicked or space key / on Export Song set isPlaying to false").
 *
 *  Connected lazily, when a take is installed: the tracks a Song creates in its
 *  own constructor are built before Engine::s_song is assigned, so connecting in
 *  the Track constructor would connect to nothing for exactly the tracks a
 *  default project has.
 */
void Track::subscribeFrozenTakeSignals()
{
	if (m_frozenSignalsConnected) { return; }
	m_frozenSignalsConnected = true;
	auto endPass = [this] { m_frozenTakePlaying.store(false, std::memory_order_relaxed); };
	connect(Engine::getSong(), &Song::playbackStateChanged, this, endPass, Qt::DirectConnection);
	connect(Engine::getSong(), &Song::playbackPositionJumped, this, endPass, Qt::DirectConnection);
	connect(getMutedModel(), &BoolModel::dataChanged, this, endPass, Qt::DirectConnection);
}

void Track::setId(int id)
{
	m_id = id;
	// Never let the counter hand this number to anything else, even if the file
	// that carried it had no (or a stale) next-id.
	ProjectIds::observe(id);
}




/*! \brief Destroy this track
 *
 *  Delete the clips and remove this track from the track container.
 */
Track::~Track()
{
	lock();
	emit destroyedTrack();

	while (!m_clips.empty())
	{
		delete m_clips.back();
	}

	m_trackContainer->removeTrack( this );
	unlock();
}




/*! \brief Create a track based on the given track type and container.
 *
 *  \param tt The type of track to create
 *  \param tc The track container to attach to
 */
Track * Track::create( Type tt, TrackContainer * tc )
{
	Engine::audioEngine()->requestChangeInModel();

	Track * t = nullptr;

	switch( tt )
	{
		case Type::Instrument: t = new class InstrumentTrack( tc ); break;
		case Type::Pattern: t = new class PatternTrack( tc ); break;
		case Type::Sample: t = new class SampleTrack( tc ); break;
//		case Type::Event:
//		case Type::Video:
		case Type::Automation: t = new class AutomationTrack( tc ); break;
		case Type::HiddenAutomation:
						t = new class AutomationTrack( tc, true ); break;
		default: break;
	}

	if (tc == Engine::patternStore() && t)
	{
		t->createClipsForPattern(Engine::patternStore()->numOfPatterns() - 1);
	}

	tc->updateAfterTrackAdd();

	Engine::audioEngine()->doneChangeInModel();

	return t;
}




/*! \brief Create a track inside TrackContainer from track type in a QDomElement and restore state from XML
 *
 *  \param element The QDomElement containing the type of track to create
 *  \param tc The track container to attach to
 */
Track * Track::create( const QDomElement & element, TrackContainer * tc )
{
	Engine::audioEngine()->requestChangeInModel();

	Track * t = create(
		static_cast<Type>( element.attribute( "type" ).toInt() ),
									tc );
	if( t != nullptr )
	{
		t->restoreState( element );
	}

	Engine::audioEngine()->doneChangeInModel();

	return t;
}




/*! \brief Clone a track from this track
 *
 */
Track* Track::clone()
{
	// Save track to temporary XML and load it to create a new identical track
	QDomDocument doc;
	QDomElement parent = doc.createElement("clonedtrack");
	saveState(doc, parent);
	Track* t = create(parent.firstChild().toElement(), m_trackContainer);

	AutomationClip::resolveAllIDs();
	return t;
}


/*! \brief Save this track's settings to file
 *
 *  We save the track type and its muted state and solo state, then append the track-
 *  specific settings.  Then we iterate through the clips
 *  and save all their states in turn.
 *
 *  \param doc The QDomDocument to use to save
 *  \param element The The QDomElement to save into
 *  \param presetMode Describes whether to save the track as a preset or not.
 *  \todo Does this accurately describe the parameters?  I think not!?
 *  \todo Save the track height
 */
void Track::saveTrack(QDomDocument& doc, QDomElement& element, bool presetMode)
{
	if (!presetMode)
	{
		element.setTagName( "track" );
	}
	element.setAttribute( "type", static_cast<int>(type()) );
	element.setAttribute( "name", name() );
	if (!presetMode)
	{
		// SPEC-stable-ids.md 3.1: an ATTRIBUTE on the track's own element,
		// never a child element - Track::loadTrack turns an unrecognised child
		// element into a real Clip, so an id element would make every track
		// grow a phantom clip on load, on every track, in every project.
		element.setAttribute( "id", m_id );
	}
	m_mutedModel.saveSettings( doc, element, "muted" );
	m_soloModel.saveSettings( doc, element, "solo" );
	// Save the mutedBeforeSolo value so we can recover the muted state if any solo was active (issue 5562)
	element.setAttribute( "mutedBeforeSolo", int(m_mutedBeforeSolo) );

	if( m_height >= MINIMAL_TRACK_HEIGHT )
	{
		element.setAttribute( "trackheight", m_height );
	}
	
	if (m_color.has_value())
	{
		element.setAttribute("color", m_color->name());
	}
	
	QDomElement tsDe = doc.createElement( nodeName() );
	// let actual track (InstrumentTrack, PatternTrack, SampleTrack etc.) save its settings
	element.appendChild( tsDe );
	saveTrackSpecificSettings(doc, tsDe, presetMode);

	if (presetMode)
	{
		return;
	}

	// Take lanes and the composite (comping; docs/COMPING.md). ONE element
	// holds both lists, and it is written ONLY when the model is not empty, so a
	// project that never comped serialises byte for byte as it did before this
	// feature existed (invariant I9).
	//
	// `metadata="1"` is load-bearing, not decoration: Track::loadTrack turns an
	// unrecognised child element of <track> into a REAL Clip, and so would an
	// older build reading this file without the attribute present. The same trap
	// SPEC-stable-ids.md §3.1 records for the track id is why the take-lane model
	// is a marked element and not a plain one.
	if (!m_takeLanes.isEmpty())
	{
		QDomElement lanesElement = doc.createElement(QStringLiteral("takelanes"));
		lanesElement.setAttribute(QStringLiteral("metadata"), 1);
		m_takeLanes.saveSettings(doc, lanesElement);
		element.appendChild(lanesElement);
	}

	// The frozen take (freeze / bounce-in-place). ONE element, written ONLY when
	// a take is installed, so a track that was never frozen serialises exactly
	// the bytes it always did. `metadata="1"` is load-bearing for the reason the
	// take-lane element's comment gives: Track::loadTrack turns an unrecognised
	// child element of <track> into a REAL Clip, and so would an older build
	// reading this file.
	if (m_frozen.isFrozen())
	{
		QDomElement frozenElement = doc.createElement(QStringLiteral("frozen"));
		frozenElement.setAttribute(QStringLiteral("metadata"), 1);
		frozenElement.setAttribute(QStringLiteral("audio"), m_frozen.path);
		frozenElement.setAttribute(QStringLiteral("start"),
			QString::number(static_cast<qint64>(m_frozen.startTicks)));
		frozenElement.setAttribute(QStringLiteral("end"),
			QString::number(static_cast<qint64>(m_frozen.endTicks)));
		for (const FrozenTake::MutedClip& muted : m_frozen.mutedClips)
		{
			QDomElement mutedElement = doc.createElement(QStringLiteral("mutedclip"));
			mutedElement.setAttribute(QStringLiteral("start"),
				QString::number(static_cast<qint64>(muted.startTicks)));
			mutedElement.setAttribute(QStringLiteral("length"),
				QString::number(static_cast<qint64>(muted.lengthTicks)));
			frozenElement.appendChild(mutedElement);
		}
		element.appendChild(frozenElement);
	}

	// now save settings of all Clip's
	for (const auto& clip : m_clips)
	{
		clip->saveState(doc, element);
	}
}

/*! \brief Load the settings from a file
 *
 *  We load the track's type and muted state and solo state, then clear out our
 *  current Clip.
 *
 *  Then we step through the QDomElement's children and load the
 *  track-specific settings and clip states from it
 *  one at a time.
 *
 *  \param element the QDomElement to load track settings from
 *  \param presetMode Indicates if a preset or a full track is loaded
 *  \todo Load the track height.
 */
void Track::loadTrack(const QDomElement& element, bool presetMode)
{
	if( static_cast<Type>(element.attribute( "type" ).toInt()) != type() )
	{
		qWarning( "Current track-type does not match track-type of "
							"settings-node!\n" );
	}

	setName( element.hasAttribute( "name" ) ? element.attribute( "name" ) :
			element.firstChild().toElement().attribute( "name" ) );

	m_mutedModel.loadSettings( element, "muted" );
	m_soloModel.loadSettings( element, "solo" );
	// Get the mutedBeforeSolo value so we can recover the muted state if any solo was active.
	// Older project files that didn't have this attribute will set the value to false (issue 5562)
	m_mutedBeforeSolo = QVariant( element.attribute( "mutedBeforeSolo", "0" ) ).toBool();

	// Reset the frozen take before reading the element (freeze / bounce-in-place):
	// a track element with no <frozen> child is NOT frozen, whatever this object
	// held before the call. A journal checkpoint restores by re-loading, so state
	// that survived its own absence could never be undone - the same rule
	// m_takeLanes follows below. A preset never carries a take either.
	clearFrozenTake();

	if (element.hasAttribute("color"))
	{
		setColor(QColor{element.attribute("color")});
	}

	if (presetMode)
	{
		QDomNode node = element.firstChild();
		while( !node.isNull() )
		{
			if( node.isElement() && node.nodeName() == nodeName() )
			{
				loadTrackSpecificSettings( node.toElement() );
				break;
			}
			node = node.nextSibling();
		}

		return;
	}

	// The stable id (SPEC-stable-ids.md rules R1/R2). A file that carries one
	// keeps it; a legacy file that does not gets the number the constructor
	// already handed out, which is deterministic because Song::loadProject walks
	// the containers in document order. Either way ProjectIds::loadAssignments()
	// counts it, and project.open reports the count as `ids_assigned`, so a
	// legacy file's one-time upgrade is stated rather than silent.
	if (element.hasAttribute("id"))
	{
		bool ok = false;
		const int stored = element.attribute("id").toInt(&ok);
		if (ok && stored >= 0) { setId(stored); }
		else { ProjectIds::noteLoadAssignment(); }
	}
	else
	{
		ProjectIds::noteLoadAssignment();
	}

	{
		auto guard = Engine::audioEngine()->requestChangesGuard();
		deleteClips();
	}

	// Reset the take-lane model before reading the element: a track element with
	// no <takelanes> child (every file written before comping) must empty it, not
	// inherit whatever the model held before this call - a journal checkpoint
	// restores by re-loading, so state that survived its own absence could never
	// be undone. The same reset-on-absence rule Clip::loadClipEdits follows for
	// the lane tag itself.
	m_takeLanes.clear();

	QDomNode node = element.firstChild();
	while( !node.isNull() )
	{
		if( node.isElement() )
		{
			if( node.nodeName() == nodeName() )
			{
				loadTrackSpecificSettings( node.toElement() );
			}
			else if( node.nodeName() == "takelanes" )
			{
				// The take lanes and the composite (comping; docs/COMPING.md),
				// a marked element so that neither this loader nor an older
				// build's turns it into a phantom Clip.
				m_takeLanes.loadSettings( node.toElement() );
			}
			else if( node.nodeName() == "frozen" )
			{
				// The frozen take (freeze / bounce-in-place), a marked element
				// for the same reason. The audio is opened here, on the loading
				// thread: a take whose file has moved is still frozen state -
				// freezing it is what the user asked for and the file is
				// reported as unloaded by track.get_state - but it cannot sound,
				// and nothing pretends otherwise.
				loadFrozenTake(node.toElement());
			}
			else if( node.nodeName() != "muted"
			&& node.nodeName() != "solo"
			&& !node.toElement().attribute( "metadata" ).toInt() )
			{
				Clip * clip = createClip(
								TimePos( 0 ) );
				clip->restoreState( node.toElement() );
			}
		}
		node = node.nextSibling();
	}

	int storedHeight = element.attribute( "trackheight" ).toInt();
	if( storedHeight >= MINIMAL_TRACK_HEIGHT )
	{
		m_height = storedHeight;
	}
}

void Track::savePreset(QDomDocument & doc, QDomElement & element)
{
	saveTrack(doc, element, true);
}

void Track::loadPreset(const QDomElement & element)
{
	loadTrack(element, true);
}

void Track::saveSettings(QDomDocument& doc, QDomElement& element)
{
	// Assume that everything should be saved if we are called through SerializingObject::saveSettings
	saveTrack(doc, element, false);
}

void Track::loadSettings(const QDomElement& element)
{
	// Assume that everything should be loaded if we are called through SerializingObject::loadSettings 
	loadTrack(element, false);
}




/*! \brief Add another Clip into this track
 *
 *  \param clip The Clip to attach to this track.
 */
Clip * Track::addClip( Clip * clip )
{
	m_clips.push_back( clip );

	emit clipAdded( clip );

	return clip; // just for convenience
}




/*! \brief Remove a given Clip from this track
 *
 *  \param clip The Clip to remove from this track.
 */
void Track::removeClip( Clip * clip )
{
	clipVector::iterator it = std::find( m_clips.begin(), m_clips.end(), clip );
	if( it != m_clips.end() )
	{
		m_clips.erase( it );
		if( Engine::getSong() )
		{
			Engine::getSong()->updateLength();
			Engine::getSong()->setModified();
		}
	}
}


/*! \brief Remove all Clips from this track */
void Track::deleteClips()
{
	while (!m_clips.empty())
	{
		delete m_clips.front();
	}
}


/*! \brief Return the number of clips we contain
 *
 *  \return the number of clips we currently contain.
 */
int Track::numOfClips()
{
	return m_clips.size();
}




/*! \brief Get a Clip by number
 *
 *  If the Clip number is less than our Clip array size then fetch that
 *  numbered object from the array.  Otherwise we warn the user that
 *  we've somehow requested a Clip that is too large, and create a new
 *  Clip for them.
 *  \param clipNum The number of the Clip to fetch.
 *  \return the given Clip or a new one if out of range.
 *  \todo reject Clip numbers less than zero.
 *  \todo if we create a Clip here, should we somehow attach it to the
 *     track?
 */
auto Track::getClip(std::size_t clipNum) -> Clip*
{
	if( clipNum < m_clips.size() )
	{
		return m_clips[clipNum];
	}
	printf( "called Track::getClip( %zu ), "
			"but Clip %zu doesn't exist\n", clipNum, clipNum );
	return createClip( clipNum * TimePos::ticksPerBar() );

}




/*! \brief Determine the given Clip's number in our array.
 *
 *  \param clip The Clip to search for.
 *  \return its number in our array.
 */
int Track::getClipNum( const Clip * clip )
{
//	for( int i = 0; i < getTrackContentWidget()->numOfClips(); ++i )
	clipVector::iterator it = std::find( m_clips.begin(), m_clips.end(), clip );
	if( it != m_clips.end() )
	{
/*		if( getClip( i ) == _clip )
		{
			return i;
		}*/
		return it - m_clips.begin();
	}
	qWarning( "Track::getClipNum(...) -> _clip not found!\n" );
	return 0;
}




/*! \brief Retrieve a list of clips that fall within a period.
 *
 *  Here we're interested in a range of clips that intersect
 *  the given time period.
 *
 *  We return the Clips we find in order by time, earliest Clips first.
 *
 *  \param clipV The list to contain the found clips.
 *  \param start The MIDI start time of the range.
 *  \param end   The MIDI endi time of the range.
 */
void Track::getClipsInRange( clipVector & clipV, const TimePos & start,
							const TimePos & end )
{
	for( Clip* clip : m_clips )
	{
		int s = clip->startPosition();
		int e = clip->endPosition();
		if( ( s <= end ) && ( e >= start ) )
		{
			// Clip is within given range
			// Insert sorted by Clip's position
			clipV.insert(std::upper_bound(clipV.begin(), clipV.end(), clip, Clip::comparePosition),
						clip);
		}
	}
}




/*! \brief Swap the position of two clips.
 *
 *  First, we arrange to swap the positions of the two Clips in the
 *  clips list.  Then we swap their start times as well.
 *
 *  \param clipNum1 The first Clip to swap.
 *  \param clipNum2 The second Clip to swap.
 */
void Track::swapPositionOfClips( int clipNum1, int clipNum2 )
{
	qSwap( m_clips[clipNum1], m_clips[clipNum2] );

	const TimePos pos = m_clips[clipNum1]->startPosition();

	m_clips[clipNum1]->movePosition( m_clips[clipNum2]->startPosition() );
	m_clips[clipNum2]->movePosition( pos );
}




void Track::createClipsForPattern(int pattern)
{
	while( numOfClips() < pattern + 1 )
	{
		TimePos position = TimePos( numOfClips(), 0 );
		Clip * clip = createClip( position );
		clip->changeLength( TimePos( 1, 0 ) );
	}
}




/*! \brief Move all the clips after a certain time later by one bar.
 *
 *  \param pos The time at which we want to insert the bar.
 *  \todo if we stepped through this list last to first, and the list was
 *    in ascending order by Clip time, once we hit a Clip that was earlier
 *    than the insert time, we could fall out of the loop early.
 */
void Track::insertBar( const TimePos & pos )
{
	// we'll increase the position of every Clip, positioned behind pos, by
	// one bar
	for (const auto& clip : m_clips)
	{
		if (clip->startPosition() >= pos)
		{
			clip->movePosition(clip->startPosition() + TimePos::ticksPerBar());
		}
	}
}




/*! \brief Move all the clips after a certain time earlier by one bar.
 *
 *  \param pos The time at which we want to remove the bar.
 */
void Track::removeBar( const TimePos & pos )
{
	// we'll decrease the position of every Clip, positioned behind pos, by
	// one bar
	for (const auto& clip : m_clips)
	{
		if (clip->startPosition() >= pos)
		{
			clip->movePosition(clip->startPosition() - TimePos::ticksPerBar());
		}
	}
}




/*! \brief Return the length of the entire track in bars
 *
 *  We step through our list of Clips and determine their end position,
 *  keeping track of the latest time found in ticks.  Then we return
 *  that in bars by dividing by the number of ticks per bar.
 */
bar_t Track::length() const
{
	// find last end-position
	tick_t last = 0;
	for (const auto& clip : m_clips)
	{
		if (Engine::getSong()->isExporting() && clip->isMuted())
		{
			continue;
		}

		const tick_t cur = clip->endPosition();
		if( cur > last )
		{
			last = cur;
		}
	}

	// A frozen take sounds even where the clips it replaced do not: freeze
	// mutes nothing for a whole-track take, but a REGION freeze mutes the clips
	// inside the region, and Track::length() skips a muted clip during an
	// export - so without this floor a render of a project whose last clips were
	// frozen would stop before the take's own audio (the take would be silent in
	// exactly the render it was made to feed).
	if (m_frozen.isFrozen() && m_frozen.endTicks > last)
	{
		last = m_frozen.endTicks;
	}

	return last / TimePos::ticksPerBar();
}


/*! \brief Make this track play a rendered take instead of its own clips.
 *
 *  The take's audio is opened HERE, on the calling thread (a control-surface
 *  handler or a project load), never on the audio thread. Nothing is changed
 *  when the file cannot be opened: the caller gets a false and a reason, and the
 *  track keeps playing its own clips.
 *
 *  \param path The rendered audio file
 *  \param startTicks Where the take begins on the timeline
 *  \param endTicks Where it ends
 *  \param mutedClips The clips this freeze muted, so unfreeze restores exactly
 *  those (empty for a whole-track freeze, which mutes nothing)
 *  \param error Filled with a reason when this returns false
 */
bool Track::freezeTo(const QString& path, tick_t startTicks, tick_t endTicks,
		const std::vector<FrozenTake::MutedClip>& mutedClips, QString* error)
{
	if (path.isEmpty())
	{
		if (error != nullptr) { *error = QStringLiteral("the take names no audio file"); }
		return false;
	}
	std::shared_ptr<const SampleBuffer> buffer = SampleBuffer::fromFile(path);
	if (buffer == nullptr || buffer->empty())
	{
		if (error != nullptr)
		{
			*error = QStringLiteral("'%1' could not be opened as audio").arg(path);
		}
		return false;
	}
	subscribeFrozenTakeSignals();

	for (const FrozenTake::MutedClip& muted : mutedClips)
	{
		if (Clip* clip = clipAt(m_clips, muted.startTicks, muted.lengthTicks))
		{
			clip->setMuted(true);
		}
	}

	m_frozen.path = path;
	m_frozen.startTicks = startTicks;
	m_frozen.endTicks = endTicks;
	m_frozen.mutedClips = mutedClips;
	m_frozenBuffer = buffer;
	m_frozenTakePlaying.store(false, std::memory_order_relaxed);
	return true;
}


void Track::unfreeze()
{
	// Exactly the clips this freeze muted come back: a clip the user had muted
	// themselves is not in the record, so it stays muted.
	for (const FrozenTake::MutedClip& muted : m_frozen.mutedClips)
	{
		if (Clip* clip = clipAt(m_clips, muted.startTicks, muted.lengthTicks))
		{
			clip->setMuted(false);
		}
	}
	clearFrozenTake();
}


void Track::clearFrozenTake()
{
	m_frozen = FrozenTake{};
	m_frozenBuffer.reset();
	m_frozenTakePlaying.store(false, std::memory_order_relaxed);
}


void Track::loadFrozenTake(const QDomElement& element)
{
	// Track::loadTrack has already cleared the take (reset on absence), so this
	// only fills in what the file carries.
	m_frozen.path = element.attribute(QStringLiteral("audio"));
	if (m_frozen.path.isEmpty()) { return; }

	m_frozen.startTicks = static_cast<tick_t>(
		element.attribute(QStringLiteral("start")).toLongLong());
	m_frozen.endTicks = static_cast<tick_t>(
		element.attribute(QStringLiteral("end")).toLongLong());
	for (QDomNode node = element.firstChild(); !node.isNull(); node = node.nextSibling())
	{
		if (!node.isElement() || node.nodeName() != QLatin1String("mutedclip")) { continue; }
		const QDomElement mutedElement = node.toElement();
		FrozenTake::MutedClip muted;
		muted.startTicks = static_cast<tick_t>(
			mutedElement.attribute(QStringLiteral("start")).toLongLong());
		muted.lengthTicks = static_cast<tick_t>(
			mutedElement.attribute(QStringLiteral("length")).toLongLong());
		m_frozen.mutedClips.push_back(muted);
	}

	// The clips the file carries are already muted (the freeze saved them that
	// way), so the record is NOT replayed here: a load must not write to the
	// clips it is still reading.
	subscribeFrozenTakeSignals();
	m_frozenBuffer = SampleBuffer::fromFile(m_frozen.path);
}


bool Track::playFrozenTake(const TimePos& start, f_cnt_t /*frames*/, f_cnt_t offset)
{
	if (!m_frozen.isFrozen()) { return false; }
	// A muted track is silent and a frozen one is no exception - and solo, which
	// is expressed as the other tracks' mute flags in this engine, is covered by
	// the same line. A muted pass is not a pass: the flag is cleared so that
	// unmuting resumes the take.
	if (isMuted())
	{
		m_frozenTakePlaying.store(false, std::memory_order_relaxed);
		return false;
	}

	const tick_t position = start.getTicks();
	const bool pastTheEnd = m_frozen.endTicks > m_frozen.startTicks
		&& position >= m_frozen.endTicks;
	if (position < m_frozen.startTicks || pastTheEnd)
	{
		// Outside the take's window, or the transport was moved back before it:
		// a handle queued by an earlier pass belongs to that pass, and the next
		// pass through the window starts a new one.
		m_frozenTakePlaying.store(false, std::memory_order_relaxed);
		return false;
	}
	if (m_frozenBuffer == nullptr) { return false; }

	// One handle per pass through the take. A handle renders to the end of what
	// it was given, so queueing one per audio block would stack a copy of the
	// take on every block (SampleTrack::play's clip handles are created for the
	// same reason: a clip is started once, not once per block).
	if (m_frozenTakePlaying.exchange(true)) { return false; }

	const float framesPerTick = Engine::framesPerTick(m_frozenBuffer->sampleRate());
	const qint64 sourceIn = static_cast<qint64>(
		std::llround((position - m_frozen.startTicks) * framesPerTick));
	const qint64 sourceOut = static_cast<qint64>(m_frozenBuffer->size());
	if (sourceIn >= sourceOut)
	{
		m_frozenTakePlaying.store(false, std::memory_order_relaxed);
		return false;
	}

	// The window is [where this pass starts in the take, the take's end): the
	// Sample shares the loaded buffer, so this copies no audio, and the handle
	// owns both its own mix-level bus handle and the Sample (the preview and
	// metronome path's shape).
	Sample* sample = new Sample(m_frozenBuffer);
	sample->setStartFrame(static_cast<int>(sourceIn));
	sample->setEndFrame(static_cast<int>(sourceOut));
	auto* handle = new SamplePlayHandle(sample);
	handle->setOffset(offset);
	Engine::audioEngine()->addPlayHandle(handle);
	return true;
}



/*! \brief Invert the track's solo state.
 *
 *  We have to go through all the tracks determining if any other track
 *  is already soloed.  Then we have to save the mute state of all tracks,
 *  and set our mute state to on and all the others to off.
 */
void Track::toggleSolo()
{
	const TrackContainer::TrackList & tl = m_trackContainer->tracks();

	bool soloBefore = false;
	for (const auto& track : tl)
	{
		if (track != this)
		{
			if (track->m_soloModel.value())
			{
				soloBefore = true;
				break;
			}
		}
	}

	const bool solo = m_soloModel.value();
	// Should we use the new behavior of solo or the older/legacy one?
	const bool soloLegacyBehavior = ConfigManager::inst()->value("app", "sololegacybehavior", "0").toInt();

	for (const auto& track : tl)
	{
		if (solo)
		{
			// save mute-state in case no track was solo before
			if (!soloBefore)
			{
				track->m_mutedBeforeSolo = track->isMuted();
			}
			// Don't mute AutomationTracks (keep their original state) unless we are on the sololegacybehavior mode
			if (track == this)
			{
				track->setMuted(false);
			}
			else if (soloLegacyBehavior || track->type() != Type::Automation)
			{
				track->setMuted(true);
			}
			if (track != this)
			{
				track->m_soloModel.setValue(false);
			}
		}
		else if (!soloBefore)
		{
			// Unless we are on the sololegacybehavior mode, only restores the
			// mute state if the track isn't an Automation Track
			if (soloLegacyBehavior || track->type() != Type::Automation)
			{
				track->setMuted(track->m_mutedBeforeSolo);
			}
		}
	}
}

void Track::setColor(const std::optional<QColor>& color)
{
	m_color = color;
	emit colorChanged();
}

BoolModel *Track::getMutedModel()
{
	return &m_mutedModel;
}

void Track::setName(const QString& newName)
{
	if (m_name != newName)
	{
		m_name = newName;

		if (auto song = Engine::getSong())
		{
			song->setModified();
		}
		
		emit nameChanged();
	}
}

} // namespace lmms
