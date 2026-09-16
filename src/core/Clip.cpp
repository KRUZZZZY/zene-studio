/*
 * Clip.cpp - implementation of Clip class
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

#include "Clip.h"

#include <algorithm>

#include <QDomDocument>
#include <QDomElement>

#include "AutomationEditor.h"
#include "AutomationClip.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "ProjectIds.h"
#include "Song.h"
#include "Track.h"
#include "TrackContainer.h"


namespace lmms
{

/*! \brief Create a new Clip
 *
 *  Creates a new clip for the given track.
 *
 * \param _track The track that will contain the new object
 */
Clip::Clip( Track * track ) :
	Model( track ),
	m_track( track ),
	// The stable id, handed out once, here, at creation (SPEC-stable-ids.md
	// 2.1, slice 2). It is the number in clip-<n> and it never changes while
	// the clip lives, so a client that was told clip-4 keeps clip-4 when a
	// sibling clip is inserted, deleted, split, reordered or undone - and it
	// is the same number after a save/open cycle, because saveState() writes
	// it into the project file and restoreState() takes it back. The counter
	// is the project-scoped one, shared with the track ids.
	m_id( ProjectIds::allocate() ),
	m_startPosition(),
	m_length(),
	m_mutedModel( false, this, tr( "Mute" ) ),
	m_selectViewOnCreate{false}
{
	if( getTrack() )
	{
		getTrack()->addClip( this );
	}
	setJournalling( false );
	movePosition( 0 );
	changeLength( 0 );
	setJournalling( true );
}


/*! \brief Copy a Clip
 *
 *  Creates a duplicate clip of the one provided.
 *
 * \param other The clip object which will be copied.
 */
Clip::Clip(const Clip& other):
	Model(other.m_track),
	m_track(other.m_track),
	m_name(other.m_name),
	// A COPY IS A NEW CLIP: it gets its own id and does NOT inherit the
	// source's. clone() is what clip.duplicate, clip.split and the piano
	// roll's own copy/paste are built on, and two clips wearing one id would
	// make an id an ambiguous address - the one thing the contract forbids.
	m_id(ProjectIds::allocate()),
	m_startPosition(other.m_startPosition),
	m_length(other.m_length),
	m_startTimeOffset(other.m_startTimeOffset),
	m_mutedModel(other.m_mutedModel.value(), this, tr( "Mute" )),
	m_autoResize(other.m_autoResize),
	m_selectViewOnCreate{other.m_selectViewOnCreate},
	m_color(other.m_color),
	m_edits(other.m_edits),
	m_laneIndex(other.m_laneIndex),
	m_linkId(other.m_linkId)
{
	if (getTrack())
	{
		getTrack()->addClip(this);
	}
}

/*! The clip's own element, plus the STABLE ID as an `id` attribute
 *  (SPEC-stable-ids.md slice 2).
 *
 *  ONE override covers all four clip types: MidiClip, SampleClip, PatternClip
 *  and AutomationClip each override saveSettings/loadSettings but NONE of them
 *  overrides saveState/restoreState, so every clip that is written into a
 *  project file - through Track::saveTrack's `clip->saveState(doc, element)`
 *  and through a Journal checkpoint - carries its id, and a checkpoint restore
 *  (which re-loads the element) puts the SAME id back on the clip.
 *
 *  JournallingObject::saveState is called BY NAME, and not SerializingObject's:
 *  it is the override this class inherited before this feature existed, and it
 *  is the one that appends the `<journallingObject id=N metadata="true">` child
 *  every journal checkpoint and every undo step is recorded against. Calling
 *  SerializingObject::saveState directly - as the first cut of this slice did -
 *  drops that child from every clip element the build writes, which is the
 *  defect docs/UNDO-BOUNDS.md records for exactly this reader/writer pair: a
 *  re-created object carries a NEW journal id, every step recorded before the
 *  re-load names a dead object, ProjectJournal::undo() skips it and unwinds an
 *  OLDER one, and ONE control.undo takes back edits the caller never asked for.
 *
 *  The id attribute is written only when the clip is going into a DOCUMENT
 *  (ProjectIds::isDocumentElement): a copy payload carries the source clip's
 *  attributes verbatim, and a clip built from one is a NEW clip that must keep
 *  the id its constructor handed out (rule R4), or two live clips would answer
 *  to one `clip-<n>`.
 */
QDomElement Clip::saveState( QDomDocument & doc, QDomElement & parent )
{
	QDomElement element = JournallingObject::saveState( doc, parent );

	if( ProjectIds::isDocumentElement( element ) )
	{
		element.setAttribute( QStringLiteral( "id" ), m_id );
	}

	return element;
}

/*! The clip's own element, plus the id it was written with.
 *
 *  A file that carries one keeps it; a legacy file (or a file that came back
 *  through a build without this feature, which drops the attribute) keeps the
 *  number the constructor already handed out. That fallback is deterministic
 *  because the load walks the containers and their clips in that order, and
 *  the assignment is COUNTED - ProjectIds::loadAssignments() - so project.open
 *  reports it as `ids_assigned` instead of upgrading the file silently.
 *
 *  JournallingObject::restoreState is called BY NAME for the reason saveState
 *  above records: it is the reader that hands the clip back the journal id the
 *  document carries, so the undo steps already recorded against this clip still
 *  find it after a re-load (a checkpoint restore, a track undo).
 *
 *  A clip built from a COPY payload is skipped entirely: it keeps the id its
 *  constructor handed out, because the payload's id names the clip that was
 *  copied, which is still alive (rule R4). A skip is not an assignment, so it
 *  is not counted.
 */
void Clip::restoreState( const QDomElement & element )
{
	JournallingObject::restoreState( element );

	if( !ProjectIds::isDocumentElement( element ) )
	{
		return;
	}

	const QString stored = element.attribute( QStringLiteral( "id" ) );
	bool ok = false;
	const int id = stored.toInt( &ok );
	if( !stored.isEmpty() && ok && id >= 0 )
	{
		setId( id );
	}
	else
	{
		// m_id, not id(): the local above is named `id` and shadows the accessor.
		ProjectIds::noteLoadAssignment( m_id );
	}
}

/*! Replace the id with \a id, and raise the project counter above it so the
 *  number can never be handed out again (SPEC-stable-ids.md rule R3).
 *
 *  A negative value is ignored: it is not a number this surface can address,
 *  and the constructor's id is always a valid answer.
 */
void Clip::setId( int id )
{
	if( id < 0 )
	{
		return;
	}
	m_id = id;
	ProjectIds::observe( id );
}

/*! \brief Destroy a Clip
 *
 *  Destroys the given clip.
 *
 */
Clip::~Clip()
{
	emit destroyedClip();

	if( getTrack() )
	{
		getTrack()->removeClip( this );
	}
}




/*! \brief Move this Clip's position in time
 *
 *  If the clip has moved, update its position.  We
 *  also add a journal entry for undo and update the display.
 *
 * \param _pos The new position of the clip.
 */
void Clip::movePosition( const TimePos & pos )
{
	TimePos newPos = std::max(0, pos.getTicks());
	if (m_startPosition != newPos)
	{
		Engine::audioEngine()->requestChangeInModel();
		m_startPosition = newPos;
		Engine::audioEngine()->doneChangeInModel();
		Engine::getSong()->updateLength();
		emit positionChanged();
	}
}




/*! \brief Change the length of this Clip
 *
 *  If the clip's length has changed, update it.  We
 *  also add a journal entry for undo and update the display.
 *
 * \param _length The new length of the clip.
 */
void Clip::changeLength( const TimePos & length )
{
	if (m_length == length) { return; }

	m_length = length;
	Engine::getSong()->updateLength();
	emit lengthChanged();
}




bool Clip::comparePosition(const Clip *a, const Clip *b)
{
	return a->startPosition() < b->startPosition();
}




/*! \brief Copies the state of a Clip to another Clip
 *
 *  This method copies the state of a Clip to another Clip
 */
void Clip::copyStateTo( Clip *src, Clip *dst )
{
	// If the node names match we copy the state
	if( src->nodeName() == dst->nodeName() ){
		QDomDocument doc;
		QDomElement parent = doc.createElement( "StateCopy" );
		src->saveState( doc, parent );

		const TimePos pos = dst->startPosition();
		dst->restoreState( parent.firstChild().toElement() );
		dst->movePosition( pos );

		AutomationClip::resolveAllIDs();
		gui::getGUI()->automationEditor()->m_editor->updateAfterClipChange();
	}
}

bool Clip::hasTrackContainer() const
{
	return getTrack() != nullptr && getTrack()->trackContainer() != nullptr;
}

bool Clip::isInPattern() const
{
	return hasTrackContainer()
		&& getTrack()->trackContainer()->type() == TrackContainer::Type::Pattern;
}

bool Clip::manuallyResizable() const
{
	return !isInPattern();
}



/*! \brief Mutes this Clip
 *
 *  Restore the previous state of this clip. This will
 *  restore the position or the length of the clip
 *  depending on what was changed.
 *
 * \param _je The journal entry to undo
 */
void Clip::toggleMute()
{
	m_mutedModel.setValue( !m_mutedModel.value() );
	emit dataChanged();
}




TimePos Clip::startTimeOffset() const
{
	return m_startTimeOffset;
}




void Clip::setStartTimeOffset( const TimePos &startTimeOffset )
{
	m_startTimeOffset = startTimeOffset;
}




/*! \brief Write the clip's fades and its gain onto its own element.
 *
 *  Additive by construction (docs/CLIP-CAPTURE-DESIGN.md §2.6, invariant I9):
 *  every new attribute is written ONLY for a value that differs from the neutral
 *  default, so a clip nobody has edited serialises exactly as it did before
 *  these values existed. That is the same rule this file's callers already
 *  follow for `srcin`/`srcout` and for the `<warp>` child element, and it is
 *  what makes "an old project with no fade element loads byte-identically" a
 *  property rather than a hope.
 *
 *  The gain is stored in dB (the design's file format) and the fades in ticks,
 *  because ticks are the unit of the timeline they ramp over.
 *
 *  The take lane (comping, docs/COMPING.md) rides the same helper rather than a
 *  second one: it is the clip's other non-default attribute, it follows the same
 *  write-nothing-when-default rule, and every caller of this pair therefore
 *  carries the lane tag by construction.
 */
void Clip::saveClipEdits(QDomElement& element) const
{
	// The design's own attribute name for a clip's take lane (§2.6).
	if (m_laneIndex > 0)
	{
		element.setAttribute("lane", m_laneIndex);
	}
	/* The link group (row 6). Written only when the clip is a member, like the
	 * lane above: an unlinked clip - every clip in every project written before
	 * this feature - carries no `link` attribute at all, so I9 (additive
	 * serialisation) holds without a migration entry. */
	if (m_linkId > 0)
	{
		element.setAttribute("link", m_linkId);
	}
	if (m_edits.gain != 1.0f)
	{
		element.setAttribute("gain", QString::number(gainLinearToDb(m_edits.gain), 'f', 6));
	}
	if (m_edits.fadeInTicks != 0)
	{
		element.setAttribute("fadein", m_edits.fadeInTicks);
	}
	if (m_edits.fadeOutTicks != 0)
	{
		element.setAttribute("fadeout", m_edits.fadeOutTicks);
	}
	if (m_edits.fadeInShape != FadeShape::Linear)
	{
		element.setAttribute("fadeinshape", static_cast<int>(m_edits.fadeInShape));
	}
	if (m_edits.fadeOutShape != FadeShape::Linear)
	{
		element.setAttribute("fadeoutshape", static_cast<int>(m_edits.fadeOutShape));
	}
}




/*! \brief Read the clip's fades and its gain back off its own element.
 *
 *  A file that carries none of these attributes - every project written before
 *  this change - leaves the edits at their neutral defaults, which is what makes
 *  the load side of I9 hold without a migration entry: an unknown attribute is
 *  ignored by an old build, and an absent attribute is neutral in a new one.
 *
 *  A negative length or an out-of-range shape index is a malformed file rather
 *  than a user edit, so it is clamped to the nearest legal value instead of
 *  being carried into the render (the render path must never see a fade that
 *  runs backwards).
 */
void Clip::loadClipEdits(const QDomElement& element)
{
	ClipEdits edits;
	if (element.hasAttribute("gain"))
	{
		edits.gain = gainDbToLinear(element.attribute("gain").toFloat());
	}
	edits.fadeInTicks = std::max(0, element.attribute("fadein", "0").toInt());
	edits.fadeOutTicks = std::max(0, element.attribute("fadeout", "0").toInt());

	const auto shapeFromIndex = [](int index) {
		switch (index)
		{
		case static_cast<int>(FadeShape::Exponential): return FadeShape::Exponential;
		case static_cast<int>(FadeShape::EqualPower): return FadeShape::EqualPower;
		default: return FadeShape::Linear;
		}
	};
	edits.fadeInShape = shapeFromIndex(element.attribute("fadeinshape", "0").toInt());
	edits.fadeOutShape = shapeFromIndex(element.attribute("fadeoutshape", "0").toInt());

	m_edits = edits;
	// Reset-on-absence, deliberately: a clip element with no `lane` attribute
	// (every file written before comping, and every clip that was never assigned
	// to a take lane) loads as lane 0. A field that survived its own absence here
	// would make the pre-edit state unreachable from a journal checkpoint.
	m_laneIndex = std::max(0, element.attribute("lane", "0").toInt());
	/* The link group (row 6), on the same reset-on-absence rule and for the same
	 * reason: the checkpoint a clip.link_create takes BEFORE the first link
	 * carries no `link` attribute, so loading it restores "unlinked" exactly -
	 * the inverse of a first link is a real inverse rather than a no-op.
	 *
	 * An id read from a file is OBSERVED by the project-scoped counter
	 * (SPEC-stable-ids.md R3), so a group number that a project already uses can
	 * never be handed out again by allocateGroupId() - including for a reloaded
	 * document whose ids are read before any new clip is created. */
	m_linkId = std::max(0, element.attribute("link", "0").toInt());
	if (m_linkId > 0)
	{
		ProjectIds::observe(m_linkId);
	}
}

void Clip::setColor(const std::optional<QColor>& color)
{
	m_color = color;
	emit colorChanged();
}

} // namespace lmms
