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
#include "MidiClip.h"
#include "PatternClip.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "ProjectIds.h"
#include "Sample.h"
#include "SampleBuffer.h"
#include "SampleClip.h"
#include "SamplePlayHandle.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TrackContainer.h"
#include "TrackFolder.h"
#include "UnclaimedElements.h"


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

/*! The child of a <track> element that loadTrack's walk recognised as nothing
 *  else: a CLIP element is loaded into a real clip, exactly as that branch has
 *  always done, and anything else is kept verbatim as an unclaimed child so
 *  saveTrack() re-emits it (SPEC-ARCH-4 1.6.1/1.6.3).
 *
 *  The name test is what keeps 1.6.3's rule true. That branch used to make a
 *  real Clip out of EVERY child it saw, and that is how every clip in a project
 *  was loaded: a clip element is named for its class and reached that branch
 *  only because no earlier branch matched it. Replacing the coercion with a
 *  capture therefore removed the clip loader with it - no clip element matched
 *  anything, nothing was added to m_clips, TrackContainer::isEmpty() answered
 *  true, and every project loaded empty.
 *
 *  So the accepted names are exactly the ones a clip element can carry, and
 *  nothing else is coerced:
 *
 *   - The four clip classes this build writes. Their names are asked for, not
 *     repeated: Track::createClip() returns one of the four, Clip::saveState()
 *     writes that class's nodeName(), and the class publishes that literal, so
 *     the loader and the writer cannot drift apart. (The four are the complete
 *     set of Clip subclasses, so no clip element this build writes can miss
 *     this test.)
 *   - The four spellings DataFile::upgrade_bbTcoRename renames those elements
 *     FROM. A document that reaches this walk without having been upgraded - a
 *     current `version` attribute carrying a pre-rename tag, which is what the
 *     tree's own legacy-shaped fixtures are - was loaded as a clip before this
 *     change and still is, so nothing that loads today can stop loading.
 *
 *  A clip element of a class that is not this track's own (a <sampleclip> under
 *  an <instrumenttrack>) is loaded into this track's own clip class, exactly as
 *  the name-agnostic branch always did. That is unchanged here, and 1.6.3 does
 *  not ask for it: what it forbids is a child the FORMAT has never heard of
 *  coming back as a phantom clip, and such a child is preserved now. */
void claimClipOrPreserveChild(Track& track, const QDomElement& element,
	QVector<UnclaimedElement>& unclaimed)
{
	const QString name = element.nodeName();
	if (name == MidiClip::classNodeName()
		|| name == SampleClip::classNodeName()
		|| name == PatternClip::classNodeName()
		|| name == AutomationClip::classNodeName()
		|| name == QLatin1String( "pattern" )
		|| name == QLatin1String( "sampletco" )
		|| name == QLatin1String( "bbtco" )
		|| name == QLatin1String( "automationpattern" ))
	{
		Clip* clip = track.createClip(TimePos(0));
		clip->restoreState(element);
		return;
	}

	captureUnclaimed(element, name, unclaimed);
}

/*! The clips a region freeze muted, as one attribute: "start:length" pairs,
 *  comma-separated (ticks). A grammar rather than a child element, because the
 *  take has to live in attributes - see Track::saveTrack's comment. */
QString mutedClipsAttribute(const std::vector<Track::FrozenTake::MutedClip>& muted)
{
	QStringList parts;
	parts.reserve(static_cast<int>(muted.size()));
	for (const Track::FrozenTake::MutedClip& clip : muted)
	{
		parts.append(QStringLiteral("%1:%2")
			.arg(static_cast<qint64>(clip.startTicks))
			.arg(static_cast<qint64>(clip.lengthTicks)));
	}
	return parts.join(QLatin1Char(','));
}

std::vector<Track::FrozenTake::MutedClip> parseMutedClipsAttribute(const QString& text)
{
	std::vector<Track::FrozenTake::MutedClip> muted;
	for (const QString& part : text.split(QLatin1Char(','), Qt::SkipEmptyParts))
	{
		const QStringList pair = part.split(QLatin1Char(':'));
		if (pair.size() != 2) { continue; }
		Track::FrozenTake::MutedClip clip;
		clip.startTicks = static_cast<tick_t>(pair.at(0).toLongLong());
		clip.lengthTicks = static_cast<tick_t>(pair.at(1).toLongLong());
		muted.push_back(clip);
	}
	return muted;
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
	// Unlink from the folder FIRST, before the signal: TrackView closes itself
	// from destroyedTrack(), so a slot that runs there must not be able to reach
	// a half-destroyed track through its folder's child list - the same
	// ordering rule the 0.2.0 line's use-after-free fix established for the
	// destroyedTrack() signal itself (docs/CONTROL-UNDO-CONNECTION-DROP.md).
	if (m_parentFolder != nullptr)
	{
		m_parentFolder->childUnlinked(this);
		m_parentFolder = nullptr;
	}
	emit destroyedTrack();

	while (!m_clips.empty())
	{
		delete m_clips.back();
	}

	m_trackContainer->removeTrack( this );
	unlock();
}

/*! \brief Put this track into \a folder (nullptr = the container root).
 *
 *  Reference-only in both directions: the container stays the single owner, so
 *  this never deletes anything. The folder is told, because routing mode's
 *  invariant is that every child of a routing folder points at the folder's own
 *  mixer channel - a child that joins later must be routed too.
 */
void Track::setParentFolder( TrackFolder* folder )
{
	if (m_parentFolder == folder) { return; }
	TrackFolder* previous = m_parentFolder;
	m_parentFolder = nullptr;
	if (previous != nullptr) { previous->childUnlinked(this); }
	m_parentFolder = folder;
	if (folder != nullptr) { folder->childLinked(this); }
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
		// A folder track (docs/TRACK-FOLDER-DESIGN.md): one more row of the
		// SAME flat track list. An older build reading type=7 hits
		// `default: break` below and drops the row - the forward-compatibility
		// cost TRACK-FOLDER-DESIGN.md section 4.4 states rather than hides.
		case Type::Folder: t = new class TrackFolder( tc ); break;
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

	// The folder relation and the visibility flag (docs/TRACK-FOLDER-DESIGN.md
	// section 4.1; owner items 3+20+21): ATTRIBUTES on the track's own element,
	// never child elements - Track::loadTrack turns an unrecognised child
	// element into a REAL Clip, which is the trap SPEC-stable-ids.md section
	// 3.1 records for the track id. Both are written ONLY when they are not the
	// default, so a project that uses neither re-saves the bytes it always had.
	if (!presetMode && m_parentFolder != nullptr)
	{
		element.setAttribute("folder", m_parentFolder->id());
	}
	if (!presetMode && !m_visible)
	{
		element.setAttribute("visible", 0);
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

	// The frozen take (freeze / bounce-in-place), as ATTRIBUTES on the track's
	// own element - never a child element, for the reason SPEC-stable-ids.md
	// §3.1 gives for the track id: Track::loadTrack turns an unrecognised child
	// element of <track> into a REAL Clip, and so would an older build reading
	// this file. Written ONLY when a take is installed, so a track that was never
	// frozen serialises exactly the bytes it always did.
	//
	// A child element marked metadata="1" - the pattern the take lanes use -
	// cannot be used here: DataFile::write calls cleanMetaNodes(), which REMOVES
	// every element carrying that attribute from a saved project, so a marked
	// take would not survive a save at all (measured on this tree, 2026-09-13:
	// a <frozen metadata="1"> child and a <takelanes metadata="1"> child are both
	// absent from the file project.save writes).
	if (m_frozen.isFrozen())
	{
		element.setAttribute(QStringLiteral("frozenAudio"), m_frozen.path);
		element.setAttribute(QStringLiteral("frozenStart"),
			QString::number(static_cast<qint64>(m_frozen.startTicks)));
		element.setAttribute(QStringLiteral("frozenEnd"),
			QString::number(static_cast<qint64>(m_frozen.endTicks)));
		element.setAttribute(QStringLiteral("frozenMuted"),
			mutedClipsAttribute(m_frozen.mutedClips));
	}

	// now save settings of all Clip's
	for (const auto& clip : m_clips)
	{
		clip->saveState(doc, element);
	}

	// SPEC-ARCH-4 1.6.1: the track's own unclaimed children, put back after
	// everything this build writes, exactly as the clips above. Nothing is
	// appended for a track whose load claimed every child, so a project this
	// build understands completely still round-trips unchanged.
	reemitUnclaimed( m_unclaimedChildren, doc, element );
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
	// a track element with no frozenAudio attribute is NOT frozen, whatever this
	// object held before the call. A journal checkpoint restores by re-loading, so
	// state that survived its own absence could never be undone - the same rule
	// m_takeLanes follows below. A preset never carries a take either.
	clearFrozenTake();
	loadFrozenTake(element);

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
	//
	// A track read out of a COPY payload keeps the id its constructor handed
	// out instead (rule R4): Track::clone() - the duplicate-track action - and
	// the Ctrl-drag of a track both re-load the SOURCE track's element, whose
	// id names a track that is still alive, and two live tracks answering to
	// one `trk-<n>` would make the id an ambiguous address. The wrapper decides
	// it (ProjectIds::isDocumentElement): the clone document is
	// <clonedtrack>, a drag payload <dnddata>, a project <song>. A copy is not
	// an assignment, so nothing is counted for it.
	if (ProjectIds::isDocumentElement(element))
	{
		if (element.hasAttribute("id"))
		{
			bool ok = false;
			const int stored = element.attribute("id").toInt(&ok);
			if (ok && stored >= 0) { setId(stored); }
			else { ProjectIds::noteLoadAssignment(id()); }
		}
		else
		{
			ProjectIds::noteLoadAssignment(id());
		}
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

	// The unclaimed children are reset on absence too, and for a sharper reason:
	// they are what this track will RE-EMIT on the next save, so a child kept
	// from the previous project would be written into this one.
	m_unclaimedChildren.clear();

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
			else if( node.nodeName() != "muted"
			&& node.nodeName() != "solo"
			&& !node.toElement().attribute( "metadata" ).toInt() )
			{
				// SPEC-ARCH-4 1.6.3: the child this build does not read - a
				// clip element, or one it has never heard of. The three
				// exclusions above stay: `muted` and `solo` are read by the
				// models just before this walk, and a metadata marker is
				// deleted on write by DataFile::cleanMetaNodes(), so preserving
				// one would resurrect a one-way flag.
				claimClipOrPreserveChild( *this, node.toElement(), m_unclaimedChildren );
			}
		}
		node = node.nextSibling();
	}

	int storedHeight = element.attribute( "trackheight" ).toInt();
	if( storedHeight >= MINIMAL_TRACK_HEIGHT )
	{
		m_height = storedHeight;
	}

	// The folder relation and the visibility flag, both RESET ON ABSENCE
	// (docs/TRACK-FOLDER-DESIGN.md sections 4.3/4.4; owner items 3+20+21). A
	// track element with no `folder` attribute is not in a folder, whatever this
	// object held before the call, and a missing `visible` means visible: a
	// journal checkpoint restores by RE-LOADING, so state that survived its own
	// absence could never be taken back off - the rule m_takeLanes and the
	// frozen take follow above.
	//
	// It is done HERE, at the END of the walk, and not before it: the child's
	// own `mixch` is loaded by its own loadTrackSpecificSettings above, and
	// linking a child earlier would make a routing-mode folder record a binding
	// for a channel the child has not read out of the file yet.
	//
	// The link is finished here when the folder already exists - a checkpoint
	// restore re-loads ONE track and no post-load walk runs then - and after the
	// walk by TrackContainer::resolveTrackFolders when it does not, because a
	// file may name a folder that is constructed later in the same walk (a
	// folder created after the tracks it holds sits after them in the file).
	m_pendingFolderId = element.hasAttribute( "folder" )
		? element.attribute( "folder" ).toInt() : -1;
	m_visible = element.attribute( "visible", QStringLiteral("1") ).toInt() != 0;
	{
		TrackFolder* resolved = m_pendingFolderId >= 0
			? m_trackContainer->findTrackFolderById( m_pendingFolderId ) : nullptr;
		if( resolved != m_parentFolder ) { setParentFolder( resolved ); }
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
	// only fills in what the file carries. The audio is opened HERE, on the
	// loading thread - never on the audio thread - and a take whose file has
	// moved is still frozen state (the file is what the user asked for): it is
	// reported as unloaded by track.get_state rather than silently dropped.
	m_frozen.path = element.attribute(QStringLiteral("frozenAudio"));
	if (m_frozen.path.isEmpty()) { return; }

	m_frozen.startTicks = static_cast<tick_t>(
		element.attribute(QStringLiteral("frozenStart")).toLongLong());
	m_frozen.endTicks = static_cast<tick_t>(
		element.attribute(QStringLiteral("frozenEnd")).toLongLong());
	m_frozen.mutedClips = parseMutedClipsAttribute(
		element.attribute(QStringLiteral("frozenMuted")));

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
