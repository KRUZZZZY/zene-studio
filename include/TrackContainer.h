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
	static AutomatedValueMap automatedValuesFromTracks(const TrackList &tracks, TimePos timeStart, int clipNum = -1);

	mutable QReadWriteLock m_tracksMutex;

private:
	TrackList m_tracks;

	Type m_TrackContainerType;


	friend class gui::TrackContainerView;
	friend class Track;

} ;

} // namespace lmms

#endif // LMMS_TRACK_CONTAINER_H
