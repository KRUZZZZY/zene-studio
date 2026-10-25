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

#include <atomic>

#include "AutomationClip.h"
#include "AutomationLatency.h"
#include "AutomationRenderScratch.h"
#include "embed.h"
#include "Engine.h"
#include "TrackContainer.h"
#include "TrackFolder.h"
#include "PatternClip.h"
#include "PatternStore.h"
#include "PatternTrack.h"
#include "Song.h"
#include "UnattendedRun.h"
#include "UnclaimedElements.h"

// The structural-undo helpers (task #664): journalling a reorder at the ONE
// place every reorder passes through, and the replay guard that keeps a
// recorded step's own move from being recorded again.
#include "ControlStructuralSupport.h"

#include "GuiApplication.h"
#include "MainWindow.h"
#include "TextFloat.h"

namespace lmms
{

namespace
{

/*! Claim \a element as a track of \a container, or keep it verbatim in \a into
 *  (SPEC-ARCH-4 1.6.1 / 1.6.2). File-local, the shape Track.cpp's own
 *  claimClipOrPreserveChild() uses, because it is a rule of ONE walk.
 *
 *  The two tests are ordered, and the order is load-bearing:
 *
 *   - `Track::create()` reads `type` with a CHECKED QVariant::toInt(), so an
 *     element whose `type` is missing or unreadable is refused there (it used
 *     to read as 0 - an Instrument track). Without the name test first, every
 *     element a newer writer put inside `<trackcontainer>` would be offered to
 *     the factory at all. That is the same trap SPEC-stable-ids.md 3.1 records
 *     for `<track>`'s children, one level up. The name is the tag
 *     `Track::saveTrack` sets on every track's own element (src/core/Track.cpp)
 *     and the same literal Song::loadProject's pre-count walk compares
 *     against.
 *   - A `<track>` whose `type` this build has no class for hits
 *     `Track::create`'s `default: break` arm and returns nullptr. That arm
 *     STAYS: it is the forward-compatibility contract documented beside the
 *     folder track's own case (an older build reading type=7 drops the row).
 *     This helper does not change what the build can MODEL; it changes what it
 *     LOSES.
 */
void claimTrackOrPreserveChild( TrackContainer & container, const QDomElement & element,
	QVector<UnclaimedElement> & into )
{
	if( element.nodeName() == QStringLiteral( "track" )
		&& Track::create( element, &container ) != nullptr )
	{
		return;
	}
	captureUnclaimed( element, element.nodeName(), into );
}

} // namespace


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

	// SPEC-ARCH-4 1.6.1: the tracks this build refused, put back AFTER the
	// tracks it wrote - the position Song::saveProjectFile already gives the
	// <song> sections nothing claimed, and the only one available to a build
	// that cannot know where a newer writer put them. An empty list appends
	// nothing, which is why a project whose every track was claimed still
	// re-saves byte for byte.
	reemitUnclaimed( m_unclaimedChildren, _doc, _this );

	// The named visibility sets are written by Song::saveProjectFile into the
	// project's own content element, not here: see visibilitySetsNodeName().
}




void TrackContainer::loadSettings( const QDomElement & _this )
{
	bool journalRestore = _this.parentNode().nodeName() == "journaldata";
	if( journalRestore )
	{
		// These definitions live beside the track container in the project file,
		// so a track-only journal checkpoint cannot restore them. Preserve them
		// across the clear; a project load still resets them on absence.
		const QVector<VisibilitySet> visibilitySets = m_visibilitySets;
		const QString activeVisibilitySet = m_activeVisibilitySet;
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
		m_visibilitySets = visibilitySets;
		m_activeVisibilitySet = activeVisibilitySet;
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
			const QDomElement child = node.toElement();
			QString trackName = child.hasAttribute( "name" ) ?
						child.attribute( "name" ) :
						child.firstChild().toElement().attribute( "name" );
			if( pd != nullptr )
			{
				pd->setLabelText( tr("Loading Track %1 (%2/Total %3)").arg( trackName ).
						  arg( pd->value() + 1 ).arg( Engine::getSong()->getLoadingTrackCount() ) );
			}
			// SPEC-ARCH-4 1.6.1/1.6.2: the element is claimed only when this
			// build can construct a track from it. Otherwise it is kept verbatim
			// and re-emitted on save, instead of being dropped (a `type` with no
			// class here) or coerced into an instrument track (an element that
			// is not a <track> at all, or a <track> whose `type` is missing or
			// unreadable - Track::create() reads that attribute with a CHECKED
			// QVariant::toInt() and refuses the element, where it used to read
			// an absent `type` as 0).
			//
			// The `metadata` guard above is unchanged, and it deliberately does
			// NOT preserve: a marked element is REMOVED on write by
			// DataFile::cleanMetaNodes(), so re-emitting one would only
			// resurrect a one-way flag.
			claimTrackOrPreserveChild( *this, child, m_unclaimedChildren );
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




#ifdef LMMS_TESTING
namespace
{

//! The seam behind TrackContainer::trackIndexScanCount(): candidates a linear
//! position lookup examined. The maintained index never increments it, so the
//! regression fails if the scan comes back. Relaxed because the test reads it
//! on one thread.
std::atomic<std::uint64_t> s_trackIndexScans{0};

} // namespace
#endif


int TrackContainer::trackIndex( const Track * _track ) const
{
	// The position is maintained on the track by addTrack/removeTrack/moveTrack,
	// so this is one atomic load. The container check keeps a track of a nested
	// container from being mistaken for one of ours (a pattern track's own list).
	if( _track == nullptr || _track->trackContainer() != this ) { return -1; }
	return _track->containerIndex();
}


#ifdef LMMS_TESTING
std::uint64_t TrackContainer::trackIndexScanCount() noexcept
{
	return s_trackIndexScans.load( std::memory_order_relaxed );
}


void TrackContainer::resetTrackIndexScanCount() noexcept
{
	s_trackIndexScans.store( 0, std::memory_order_relaxed );
}
#endif


void TrackContainer::addTrack( Track * _track )
{
	if( _track->type() != Track::Type::HiddenAutomation )
	{
		_track->lock();
		m_tracksMutex.lockForWrite();
		m_tracks.push_back( _track );
		_track->setContainerIndex( static_cast<int>( m_tracks.size() ) - 1 );
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
		const int removed = static_cast<int>(it - m_tracks.begin());
		m_tracks.erase(it);
		_track->setContainerIndex(-1);
		// The tracks after the gap moved one place down; publish their new
		// positions before the lock is released (off the audio thread).
		for (int i = removed; i < static_cast<int>(m_tracks.size()); ++i)
		{
			m_tracks[static_cast<std::size_t>(i)]->setContainerIndex(i);
		}
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

	const auto found = std::find(m_tracks.begin(), m_tracks.end(), track);
	if (found != m_tracks.end())
	{
		const int fromIndex = static_cast<int>(found - m_tracks.begin());
		m_tracks.erase(found);
		m_tracks.insert(m_tracks.begin() + indexTo, track);
		// Every track between the two ends of the move shifted; publish the new
		// positions before returning (off the audio thread, under no render).
		for (int i = std::min(fromIndex, indexTo); i < static_cast<int>(m_tracks.size()); ++i)
		{
			m_tracks[static_cast<std::size_t>(i)]->setContainerIndex(i);
		}
	}

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
	// ...and so do the children no build here could construct (ARCH-4 S1c). They
	// are what this container would otherwise RE-EMIT into the NEXT project's
	// file, so they are cleared at the one place the tracks they arrived with
	// go: Song::clearProject() reaches this through its own clearAllTracks()
	// call, a journal restore reaches it below, and ~TrackContainer reaches it
	// on the way out.
	m_unclaimedChildren.clear();
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



namespace
{

//! 0.5.0 ED-199: each relative clip's distance from the middle of its range, added to what the
//! absolute clips set (or the parameter's own value for the run), clamped to the parameter.
void applyRelativeClips(const std::vector<Clip*>& clips, TimePos time, AutomationRenderScratch& values,
	const AutomationLatency::ReadSnapshot& snapshot)
{
	const quint64 run = AutomatableModel::automationTransportRun();
	for (Clip* clip : clips)
	{
		auto* p = dynamic_cast<AutomationClip*>(clip);
		// One relative curve has one read time; reject it if its targets disagree.
		if (p == nullptr || snapshot.latencyFrames(p) < 0) { continue; }
		// Read where the absolute clips are read: the latent audio's time when compensation is on (review L19 #3).
		const TimePos readTime = AutomationLatency::instance().readTime(clip, time, snapshot);
		if (!p->isRelative() || clip->isMuted() || clip->startPosition() > readTime || !p->hasAutomation()) { continue; }
		TimePos relTime = readTime - p->startPosition() - p->startTimeOffset();
		relTime = std::min(static_cast<int>(relTime), p->length() - p->startTimeOffset());
		const float offset = p->valueAt(relTime) - (p->getMin() + p->getMax()) / 2.f;
		for (AutomatableModel* model : p->objects())
		{
			if (model == nullptr) { continue; }
			// The map holds inverse-scaled values (Song applies scaledValue); so must the base and the bounds.
			const float* known = values.find(model);
			const float base = known != nullptr ? *known : model->inverseScaledValue(model->trimBase(run));
			values.set(model, std::clamp(base + offset, model->inverseScaledValue(model->minValue<float>()),
				model->inverseScaledValue(model->maxValue<float>())));
		}
	}
}

} // namespace

AutomatedValueMap TrackContainer::automatedValuesAt(TimePos time, int clipNum) const
{
	// The control thread's read: unbounded scratch, the same code the audio thread runs on its own.
	const auto snapshot = AutomationLatency::instance().readSnapshot();
	return automatedValuesAt(time, clipNum, snapshot);
}

AutomatedValueMap TrackContainer::automatedValuesAt(TimePos time, int clipNum,
	const AutomationLatency::ReadSnapshot& snapshot) const
{
	AutomationRenderScratch level;
	AutomationRenderScratch inner;
	fillAutomatedValues(level, &inner, time, clipNum, snapshot);
	AutomatedValueMap valueMap;
	for (const AutomationRenderScratch::Entry& entry : level.entries()) { valueMap.insert(entry.model, entry.value); }
	return valueMap;
}


void TrackContainer::fillAutomatedValues(AutomationRenderScratch& level, AutomationRenderScratch* inner,
	TimePos time, int clipNum) const
{
	// Without a period's own snapshot the caller pins one now (control thread, tests, editors).
	const auto snapshot = AutomationLatency::instance().readSnapshot();
	fillAutomatedValues(level, inner, time, clipNum, snapshot);
}


void TrackContainer::fillAutomatedValues(AutomationRenderScratch& level, AutomationRenderScratch* inner,
	TimePos time, int clipNum, const AutomationLatency::ReadSnapshot& snapshot) const
{
	level.clear();
	for (Track* track : tracks()) { gatherAutomationClips(level, track, time, clipNum); }
	collectAutomatedValues(level, inner, time, snapshot);
}


void TrackContainer::gatherAutomationClips(AutomationRenderScratch& level, Track* track, TimePos time, int clipNum)
{
	if (track == nullptr || track->isMuted()) { return; }

	switch(track->type())
	{
	case Track::Type::Automation:
	case Track::Type::HiddenAutomation:
	case Track::Type::Pattern:
		if (clipNum < 0) {
			level.insertClipsInRange(*track, 0, time);
		} else {
			Q_ASSERT(track->numOfClips() > clipNum);
			level.appendClip(track->getClip(clipNum));
		}
	default:
		break;
	}
}


void TrackContainer::collectAutomatedValues(AutomationRenderScratch& level, AutomationRenderScratch* inner,
	TimePos time, const AutomationLatency::ReadSnapshot& snapshot)
{
	const std::vector<Clip*>& clips = level.clips();

	Q_ASSERT(std::is_sorted(clips.begin(), clips.end(), Clip::comparePosition));

	for(Clip* clip : clips)
	{
		auto* automation = dynamic_cast<AutomationClip*>(clip);
		// One curve read cannot serve mixed target latencies, so reject the whole clip.
		if( automation != nullptr && snapshot.latencyFrames(automation) < 0 ) { continue; }
		// 0.5.0 MX-160: an automation clip is read where the latent audio is - `time` itself unless
		// the project turned latency-compensated automation on.
		const TimePos readTime = AutomationLatency::instance().readTime(clip, time, snapshot);
		if (clip->isMuted() || clip->startPosition() > readTime) {
			continue;
		}

		if (automation != nullptr)
		{
			auto* p = automation;
			// A relative (trim) clip offsets rather than sets: applied after every absolute one.
			if (! p->hasAutomation() || p->isRelative()) {
				continue;
			}
			TimePos relTime = readTime - p->startPosition() - p->startTimeOffset();
			if (!p->isInPattern()) {
				relTime = std::min(static_cast<int>(relTime), p->length() - p->startTimeOffset());
			}
			// Q050-037: before the first node each target holds its OWN authored
			// initial value. valueAt() reports the pre-first read in the same
			// lock it already took, so this pass adds no mutex acquisition.
			bool beforeFirst = false;
			const float value = p->valueAt(relTime, &beforeFirst);

			for (AutomatableModel* model : p->objects())
			{
				if (model == nullptr) { continue; }
				level.set(model, beforeFirst
					? model->inverseScaledValue(model->initValue<float>()) : value);
			}
		}
		else if (auto* pattern = dynamic_cast<PatternClip*>(clip))
		{
			// The pattern store holds no pattern clips of its own, so its read gets no scratch to nest in.
			if (inner == nullptr) { continue; }

			auto patIndex = dynamic_cast<class PatternTrack*>(pattern->getTrack())->patternIndex();
			auto patStore = Engine::patternStore();

			TimePos patTime = time - clip->startPosition();
			patTime = std::min(patTime, clip->length());
			patTime = patTime % (patStore->lengthOfPattern(patIndex) * TimePos::ticksPerBar());

			patStore->fillAutomatedValues(*inner, nullptr, patTime, patIndex, snapshot);
			for (const AutomationRenderScratch::Entry& entry : inner->entries())
			{
				// override old values, pattern track with the highest index takes precedence
				level.set(entry.model, entry.value);
			}
		}
		else
		{
			continue;
		}
	}

	applyRelativeClips(clips, time, level, snapshot);
}


} // namespace lmms
