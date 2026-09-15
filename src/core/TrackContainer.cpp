/*
 * TrackContainer.cpp - implementation of base class for all trackcontainers
 *                      like Song-Editor, Pattern Editor...
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


#include <QCoreApplication>
#include <QProgressDialog>
#include <QDomElement>
#include <QWriteLocker>

#include "AutomationClip.h"
#include "embed.h"
#include "Engine.h"
#include "TrackContainer.h"
#include "TrackFolder.h"
#include "PatternClip.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "Song.h"
#include "UnattendedRun.h"

// The structural-undo helpers (task #664): journalling a reorder at the ONE
// place every reorder passes through, and the replay guard that keeps a
// recorded step's own move from being recorded again.
#include "ControlStructuralSupport.h"

#include "GuiApplication.h"
#include "MainWindow.h"
#include "TextFloat.h"

namespace lmms
{


TrackContainer::TrackContainer() :
	Model( nullptr ),
	JournallingObject(),
	m_tracksMutex(),
	m_tracks()
{
}




TrackContainer::~TrackContainer()
{
	clearAllTracks();
}




void TrackContainer::saveSettings( QDomDocument & _doc, QDomElement & _this )
{
	_this.setTagName( classNodeName() );
	_this.setAttribute( "type", nodeName() );

	// save settings of each track
	m_tracksMutex.lockForRead();
	for (const auto& track : m_tracks)
	{
		track->saveState(_doc, _this);
	}
	m_tracksMutex.unlock();

	// The named visibility sets are written by Song::saveProjectFile into the
	// project's own content element, not here: see visibilitySetsNodeName().
}




void TrackContainer::loadSettings( const QDomElement & _this )
{
	bool journalRestore = _this.parentNode().nodeName() == "journaldata";
	if( journalRestore )
	{
		// The container's views first, then its tracks. A TrackView is deleted
		// from a DEFERRED event once its track dies (Qt::WA_DeleteOnClose,
		// closed from the track's destroyedTrack() signal), so deleting the
		// tracks first leaves the views - and the instrument windows they own -
		// dereferencing their model after it is gone. Measured: SIGSEGV in
		// ~InstrumentTrackView() on a plain `control.undo`, in ANY session whose
		// Song holds a track whose view touches its model while dying -- an
		// instrument track does; a pattern-track-only fixture does not, which is
		// why an earlier probe appeared to clear it. NOT a Debug/Release
		// difference: both configurations die in the same frame. See
		// TrackContainer::aboutToClearTracks() and docs/UNDO-RELEASE-CONFIG.md.
		emit aboutToClearTracks();
		clearAllTracks();
	}

	static QProgressDialog * pd = nullptr;
	bool was_null = ( pd == nullptr );
	// The progress window is application-modal and its Cancel button is the
	// only way to stop a load. In an unattended run nobody can click it (task
	// #625), so the load runs without the window - it never blocked, but it left
	// an unclosable modal widget over a socket-driven load.
	if (!journalRestore && gui::getGUI() != nullptr && !lmms::isUnattendedRun())
	{
		if( pd == nullptr )
		{
			pd = new QProgressDialog( tr( "Loading project..." ),
						tr( "Cancel" ), 0,
						Engine::getSong()->getLoadingTrackCount(),
						gui::getGUI()->mainWindow());
			pd->setWindowModality( Qt::ApplicationModal );
			pd->setWindowTitle( tr( "Please wait..." ) );
			pd->show();
		}
	}

	QDomNode node = _this.firstChild();
	while( !node.isNull() )
	{
		if( pd != nullptr )
		{
			pd->setValue( pd->value() + 1 );
			QCoreApplication::instance()->processEvents(
						QEventLoop::AllEvents, 100 );
			if( pd->wasCanceled() )
			{
				if (gui::getGUI() != nullptr)
				{
					gui::TextFloat::displayMessage( tr( "Loading cancelled" ),
					tr( "Project loading was cancelled." ),
					embed::getIconPixmap( "project_file", 24, 24 ),
					2000 );
				}
				Engine::getSong()->loadingCancelled();
				break;
			}
		}

		if( node.isElement() &&
			!node.toElement().attribute( "metadata" ).toInt() )
		{
			QString trackName = node.toElement().hasAttribute( "name" ) ?
						node.toElement().attribute( "name" ) :
						node.firstChild().toElement().attribute( "name" );
			if( pd != nullptr )
			{
				pd->setLabelText( tr("Loading Track %1 (%2/Total %3)").arg( trackName ).
						  arg( pd->value() + 1 ).arg( Engine::getSong()->getLoadingTrackCount() ) );
			}
			Track::create( node.toElement(), this );
		}
		node = node.nextSibling();
	}

	if( pd != nullptr )
	{
		if( was_null )
		{
			delete pd;
			pd = nullptr;
		}
	}

	// The named visibility sets are read by Song::loadProject from the project's
	// content element (visibilitySetsNodeName()), which is also where the
	// reset-on-absence lives - this walk builds a TRACK from every child it is
	// not told to skip, so a set element must not be a child of it at all.
}




int TrackContainer::countTracks( Track::Type _tt ) const
{
	int cnt = 0;
	m_tracksMutex.lockForRead();
	for (const auto& track : m_tracks)
	{
		if (track->type() == _tt || _tt == Track::Type::Count)
		{
			++cnt;
		}
	}
	m_tracksMutex.unlock();
	return( cnt );
}




void TrackContainer::addTrack( Track * _track )
{
	if( _track->type() != Track::Type::HiddenAutomation )
	{
		_track->lock();
		m_tracksMutex.lockForWrite();
		m_tracks.push_back( _track );
		m_tracksMutex.unlock();
		_track->unlock();
		emit trackAdded( _track );
	}
}




void TrackContainer::removeTrack( Track * _track )
{
	// need a read locker to ensure that m_tracks doesn't change after reading index.
	//   After checking that index != -1, we need to upgrade the lock to a write locker before changing m_tracks.
	//   But since Qt offers no function to promote a read lock to a write lock, we must start with the write locker.
	QWriteLocker lockTracksAccess(&m_tracksMutex);
	auto it = std::find(m_tracks.begin(), m_tracks.end(), _track);
	if (it != m_tracks.end())
	{
		// If the track is solo, all other tracks are muted. Change this before removing the solo track:
		if (_track->isSolo()) {
			_track->setSolo(false);
		}
		m_tracks.erase(it);
		lockTracksAccess.unlock();

		if( Engine::getSong() )
		{
			Engine::getSong()->setModified();
		}
		emit trackRemoved();
	}
}

void TrackContainer::moveTrack(Track* track, int indexTo)
{
	// A REORDER is journalled HERE, at the one place every reorder passes
	// through, rather than at each caller: the product's own drag and its
	// arrow-key moves both arrive here (TrackContainerView::moveTrackView), and
	// row 75's complaint is exactly that such a path "bypasses
	// addJournalCheckPoint". A container's ORDER is not a serialized property of
	// any track, so the recorded step is an action pair holding the index the
	// track came from (control::journalTrackMove, task #664).
	//
	// Two guards, both load-bearing:
	//   - only the SONG's container is journalled. A pattern track carries a
	//     nested TrackContainer of its own, and a step recorded against it would
	//     replay against the wrong list;
	//   - a reorder that IS a recorded step's replay is not recorded again
	//     (structuralReplayInProgress()), or one control.undo would leave a new
	//     step behind for every unwind and the history would grow as it unwinds.
	Song* song = Engine::getSong();
	const int from = control::trackIndexIn(this, track);
	if (song != nullptr && this == static_cast<const TrackContainer*>(song)
		&& from >= 0 && from != indexTo && !control::structuralReplayInProgress())
	{
		control::journalTrackMove(track, from, indexTo);
	}

	m_tracks.erase(std::find(m_tracks.begin(), m_tracks.end(), track));
	m_tracks.insert(m_tracks.begin() + indexTo, track);

	emit trackMoved();
}



void TrackContainer::updateAfterTrackAdd()
{
}




void TrackContainer::clearAllTracks()
{
	//m_tracksMutex.lockForWrite();
	while (!m_tracks.empty())
	{
		delete m_tracks.front();
	}
	//m_tracksMutex.unlock();
	// The visibility sets belong to the project the tracks belonged to, so they
	// go with them (owner items 3+20+21).
	clearVisibilitySets();
}





bool TrackContainer::isEmpty() const
{
	for (const auto& track : m_tracks)
	{
		if (!track->getClips().empty())
		{
			return false;
		}
	}
	return true;
}



AutomatedValueMap TrackContainer::automatedValuesAt(TimePos time, int clipNum) const
{
	return automatedValuesFromTracks(tracks(), time, clipNum);
}


AutomatedValueMap TrackContainer::automatedValuesFromTracks(const TrackList &tracks, TimePos time, int clipNum)
{
	Track::clipVector clips;

	for (Track* track: tracks)
	{
		if (track->isMuted()) {
			continue;
		}

		switch(track->type())
		{
		case Track::Type::Automation:
		case Track::Type::HiddenAutomation:
		case Track::Type::Pattern:
			if (clipNum < 0) {
				track->getClipsInRange(clips, 0, time);
			} else {
				Q_ASSERT(track->numOfClips() > clipNum);
				clips.push_back(track->getClip(clipNum));
			}
		default:
			break;
		}
	}

	AutomatedValueMap valueMap;

	Q_ASSERT(std::is_sorted(clips.begin(), clips.end(), Clip::comparePosition));

	for(Clip* clip : clips)
	{
		if (clip->isMuted() || clip->startPosition() > time) {
			continue;
		}

		if (auto* p = dynamic_cast<AutomationClip *>(clip))
		{
			if (! p->hasAutomation()) {
				continue;
			}
			TimePos relTime = time - p->startPosition() - p->startTimeOffset();
			if (!p->isInPattern()) {
				relTime = std::min(static_cast<int>(relTime), p->length() - p->startTimeOffset());
			}
			float value = p->valueAt(relTime);

			for (AutomatableModel* model : p->objects())
			{
				valueMap[model] = value;
			}
		}
		else if (auto* pattern = dynamic_cast<PatternClip*>(clip))
		{
			auto patIndex = dynamic_cast<class PatternTrack*>(pattern->getTrack())->patternIndex();
			auto patStore = Engine::patternStore();

			TimePos patTime = time - clip->startPosition();
			patTime = std::min(patTime, clip->length());
			patTime = patTime % (patStore->lengthOfPattern(patIndex) * TimePos::ticksPerBar());

			auto patValues = patStore->automatedValuesAt(patTime, patIndex);
			for (auto it=patValues.begin(); it != patValues.end(); it++)
			{
				// override old values, pattern track with the highest index takes precedence
				valueMap[it.key()] = it.value();
			}
		}
		else
		{
			continue;
		}
	}

	return valueMap;
};


} // namespace lmms
