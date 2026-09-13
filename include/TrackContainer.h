/*
 * TrackContainer.h - base-class for all track-containers like Song-Editor,
 *                    Pattern Editor...
 *
 * Copyright (c) 2004-2009 Tobias Doerffel <tobydox/at/users.sourceforge.net>
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

#ifndef LMMS_TRACK_CONTAINER_H
#define LMMS_TRACK_CONTAINER_H

#include <QReadWriteLock>
#include <QVector>

#include "Track.h"
#include "JournallingObject.h"

namespace lmms
{

class AutomationClip;
class InstrumentTrack;

namespace gui
{

class TrackContainerView;

}

class TimePos;
class TrackFolder;


class LMMS_EXPORT TrackContainer : public Model, public JournallingObject
{
	Q_OBJECT
public:
	using TrackList = std::vector<Track*>;
	enum class Type
	{
		Pattern,
		Song
	} ;

	TrackContainer();
	~TrackContainer() override;

	void saveSettings( QDomDocument & _doc, QDomElement & _parent ) override;

	void loadSettings( const QDomElement & _this ) override;

	int countTracks( Track::Type _tt = Track::Type::Count ) const;


	void addTrack( Track * _track );
	void removeTrack( Track * _track );
	void moveTrack(Track* track, int indexTo);

	virtual void updateAfterTrackAdd();

	void clearAllTracks();

	const TrackList & tracks() const
	{
		return m_tracks;
	}

	/*! The track whose stable id (the number in trk-<n>) is \a id, or nullptr.
	 *  Addressing by id and never by position - the same rule every trk-<n>
	 *  consumer follows (SPEC-stable-ids.md). */
	Track * findTrackById( int id ) const;

	/*! The FOLDER TRACK whose id is \a id, or nullptr - a track of another type
	 *  carrying that id is reported as none, so a caller cannot parent a child
	 *  to something that cannot hold it. */
	TrackFolder * findTrackFolderById( int id ) const;

	/*! Finishes the folder relation after a load walk (docs/TRACK-FOLDER-DESIGN.md
	 *  section 4.3; owner items 3+20+21), returning how many children named a
	 *  folder that is not in the container.
	 *
	 *  It is a SECOND pass on purpose: a folder created after the tracks it holds
	 *  sits after them in the container and therefore after them in the file, so
	 *  a child's `folder` attribute can name an id that has not been constructed
	 *  when the child's element is read. The tree already has this shape for a
	 *  link serialised by id - ControllerConnection::finalizeConnections and
	 *  AutomationClip::resolveAllIDs, both called beside this one.
	 *
	 *  A child whose folder is GONE (a deleted folder, a hand-edited file, a
	 *  merged .mmpz) is the same class of defect the id pass repairs, and it is
	 *  repaired the same way rather than hidden: the child is left at the
	 *  container root and the count is reported.
	 */
	int resolveTrackFolders();

	// -----------------------------------------------------------------------
	// Named visibility sets (owner items 3+20+21, docs/TRACK-FOLDER-DESIGN.md
	// section 6). A set is a named, id-based list of tracks; applying it makes
	// exactly its members visible and hides every other track of the container.
	// It is project state, saved with the project, and it drives Track::visible,
	// which is a VIEW flag and not a mute - proved by the registered transcript's
	// negative control.
	// -----------------------------------------------------------------------
	struct VisibilitySet
	{
		QString name;
		std::vector<int> trackIds;
	};

	const QVector<VisibilitySet> & visibilitySets() const
	{
		return m_visibilitySets;
	}
	//! The set named \a name, or nullptr.
	const VisibilitySet * findVisibilitySet( const QString & name ) const;
	//! Creates or REPLACES the set. False (and unchanged) for an empty name.
	bool saveVisibilitySet( const QString & name, const std::vector<int> & trackIds );
	bool removeVisibilitySet( const QString & name );
	//! Makes \a name the active set. False (and unchanged) when it does not
	//! exist; \a shown / \a hidden receive the counts.
	bool applyVisibilitySet( const QString & name, int * shown, int * hidden );
	QString activeVisibilitySet() const
	{
		return m_activeVisibilitySet;
	}
	//! Restores the active-set name alone (what the recorded inverse of
	//! track.visibility_set_apply needs: the flags and the name are one action).
	void setActiveVisibilitySet( const QString & name )
	{
		m_activeVisibilitySet = name;
	}
	void clearVisibilitySets();
	//! Writes the sets into \a parent (the project's content element) as ONE
	//! `<visibilitysets>` element, called by Song::saveProjectFile and only when
	//! there is at least one set, so a project that never made one re-saves the
	//! bytes it always had. NOT marked `metadata`, for the reason above.
	void saveVisibilitySetState( QDomDocument & doc, QDomElement & parent ) const;
	//! Reads that element back. Reset on absence is the CALLER's (the Song's)
	//! walk: a project with no `<visibilitysets>` element holds no sets.
	void loadVisibilitySetState( const QDomElement & element );

	bool isEmpty() const;

	static const QString classNodeName()
	{
		return "trackcontainer";
	}

	inline void setType( Type newType )
	{
		m_TrackContainerType = newType;
	}

	inline Type type() const
	{
		return m_TrackContainerType;
	}

	virtual AutomatedValueMap automatedValuesAt(TimePos time, int clipNum = -1) const;

signals:
	void trackAdded( lmms::Track * _track );
	void trackRemoved();
	void trackMoved();

	/*! Emitted immediately before a serialization restore clears this
	 * container's tracks, so that the container's views can take themselves
	 * down FIRST.
	 *
	 * The order is load-bearing and it is the order the rest of the tree
	 * already keeps: TrackContainerView::deleteTrackView() deletes the view and
	 * then the track, and Song::clearProject() clears the editor views before
	 * it clears the container. A TrackView sets Qt::WA_DeleteOnClose and closes
	 * itself from its track's destroyedTrack() signal, so when a track is
	 * deleted underneath a live view the view's destructor runs later, from a
	 * DEFERRED event - i.e. after the track it views is gone - and
	 * ~InstrumentTrackView() then dereferences that dead track through
	 * model(). Measured as SIGSEGV in the release configuration (members read
	 * at a null base + 0x308) on a plain `control.undo`; see
	 * docs/UNDO-RELEASE-CONFIG.md. The restore path was the only one that did
	 * not announce itself.
	 */
	void aboutToClearTracks();

protected:
	/*! The element the named visibility sets live in, beside <tempo-map> and
	 *  <modulation-layer> in the PROJECT's own content element - NOT inside
	 *  <trackcontainer>. Two measured reasons make that the only workable home:
	 *  the container's loader constructs a Track from every element child it is
	 *  not told to skip, and DataFile::write STRIPS every element that carries
	 *  `metadata="1"` (cleanMetaNodes), so the marker that would tell the loader
	 *  to skip it is exactly the marker the writer deletes. One definition,
	 *  used by both the writer and the Song's load walk, so the two cannot
	 *  disagree about the name. */
	static const QString & visibilitySetsNodeName()
	{
		static const QString name = QStringLiteral("visibilitysets");
		return name;
	}

	static AutomatedValueMap automatedValuesFromTracks(const TrackList &tracks, TimePos timeStart, int clipNum = -1);

	mutable QReadWriteLock m_tracksMutex;

private:
	TrackList m_tracks;

	//! The project's named visibility sets and the active one (owner items
	//! 3+20+21). Reset on absence by Song::loadProject's content walk, which is
	//! the walk that reads them (see saveVisibilitySetState).
	QVector<VisibilitySet> m_visibilitySets;
	QString m_activeVisibilitySet;

	Type m_TrackContainerType;


	friend class gui::TrackContainerView;
	friend class Track;

} ;

} // namespace lmms

#endif // LMMS_TRACK_CONTAINER_H
