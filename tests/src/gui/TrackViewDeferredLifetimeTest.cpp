/*
 * TrackViewDeferredLifetimeTest.cpp - a track or clip removed before its queued view is built gets no view
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
 *
 */

/*! m0 blocker, cluster trackview-uaf (2026-10-08): TrackContainerView connects TrackContainer::trackAdded() and
 *  TrackView connects Track::clipAdded() to their view builders with a queued connection, and
 *  TrackOperationsWidget defers a track's deletion the same way. A track or clip deleted in the event-loop turn
 *  it was added in left the queued call holding a dead pointer: SIGSEGV in createTrackView() or createClipView(),
 *  or a second delete of the track. Each case removes the object before the events run, then runs them: the
 *  view of a removed object must never be built, and a view of a live object is still built on the next turn. */

#include <QtTest>

#include <algorithm>

#include "Clip.h"
#include "ClipView.h"
#include "ConfigManager.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "Song.h"
#include "TrackContainer.h"
#include "SongEditor.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "TrackContentWidget.h"
#include "TrackOperationsWidget.h"
#include "TrackView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! One turn of the event loop: queued calls, then the deferred deletions of closed views, then timers.
void runEvents()
{
	QCoreApplication::sendPostedEvents();
	QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
	QCoreApplication::processEvents();
}

TrackContainerView* songView()
{
	return getGUI()->songEditor()->m_editor;
}

TrackView* viewOf(const Track* track)
{
	for (TrackView* view : songView()->trackViews())
	{
		if (view->getTrack() == track) { return view; }
	}
	return nullptr;
}

//! Every view of the song editor is for a track the song still holds. Compares pointers only.
bool everyViewIsForALiveTrack()
{
	const TrackContainer::TrackList& tracks = Engine::getSong()->tracks();
	for (TrackView* view : songView()->trackViews())
	{
		if (std::find(tracks.begin(), tracks.end(), view->getTrack()) == tracks.end()) { return false; }
	}
	return true;
}

int clipViewCount(TrackView* view)
{
	return view->getTrackContentWidget()->findChildren<ClipView*>().size();
}

} // namespace

class TrackViewDeferredLifetimeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
	}

	void cleanupTestCase()
	{
		Engine::destroy();
	}

	//! The queued view of a track deleted in the turn it was added is never built.
	void aTrackRemovedBeforeItsQueuedViewIsBuiltGetsNoView()
	{
		const auto before = songView()->trackViews().size();
		Track* track = Track::create(Track::Type::Sample, Engine::getSong());
		delete track;  // no event turn in between: createTrackView() is still queued for this pointer
		runEvents();   // red: createTrackView() runs on the freed track
		QCOMPARE(songView()->trackViews().size(), before);
		QVERIFY(everyViewIsForALiveTrack());
	}

	//! The normal path is unchanged: a track that survives its turn gets its view on the next one.
	void aTrackThatSurvivesItsTurnGetsItsView()
	{
		const auto before = songView()->trackViews().size();
		Track* track = Track::create(Track::Type::Sample, Engine::getSong());
		QVERIFY(viewOf(track) == nullptr);  // built on the next turn, not inside the call that adds the track
		runEvents();
		QVERIFY(viewOf(track) != nullptr);
		QCOMPARE(songView()->trackViews().size(), before + 1);
		delete track;
		runEvents();
		QCOMPARE(songView()->trackViews().size(), before);
	}

	//! The queued view of a clip deleted before its turn is never built.
	void aClipRemovedBeforeItsQueuedViewIsBuiltGetsNoView()
	{
		Track* track = Track::create(Track::Type::Sample, Engine::getSong());
		runEvents();
		TrackView* view = viewOf(track);
		QVERIFY(view != nullptr);
		Clip* clip = track->createClip(TimePos(0));  // clipAdded is queued to the track's view
		delete clip;                                 // removed before that call runs
		runEvents();                                 // red: createClipView() runs on the freed clip
		QCOMPARE(clipViewCount(view), 0);
		QVERIFY(track->getClips().empty());
		delete track;
		runEvents();
	}

	//! A track whose removal is scheduled by its delete button, and which dies in the same turn, is removed once.
	void aTrackDeletedWhileItsRemovalIsScheduledIsRemovedOnce()
	{
		const auto before = songView()->trackViews().size();
		const QString warning = QStringLiteral("trackdeletionwarning");
		const QString ui = QStringLiteral("ui");
		ConfigManager::inst()->setValue(ui, warning, QStringLiteral("0"));  // no confirmation dialog
		Track* track = Track::create(Track::Type::Sample, Engine::getSong());
		runEvents();
		TrackView* view = viewOf(track);
		QVERIFY(view != nullptr);
		// The delete button: the removal is deferred to a queued deleteTrackView(view) call.
		QVERIFY(QMetaObject::invokeMethod(view->getTrackOperationsWidget(), "removeTrack"));
		delete track;  // the track dies in the same turn, by another path
		runEvents();   // red: deleteTrackView() deletes the view and the track a second time
		QCOMPARE(songView()->trackViews().size(), before);
		QVERIFY(everyViewIsForALiveTrack());
		ConfigManager::inst()->setValue(ui, warning, QStringLiteral("1"));
	}

	//! A periodic tick that lands between a track's death and its closed view's deferred deletion does not read the track.
	void aPeriodicTickAfterTheTrackDiesDoesNotReadIt()
	{
		Track* track = Track::create(Track::Type::Sample, Engine::getSong());
		runEvents();
		QVERIFY(viewOf(track) != nullptr);
		delete track;  // the view closes and waits for its deferred deletion
		emit getGUI()->mainWindow()->periodicUpdate();  // red: the view's corruptStateUpdate() reads the dead track
		runEvents();
		QVERIFY(everyViewIsForALiveTrack());
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(TrackViewDeferredLifetimeTest)
#include "TrackViewDeferredLifetimeTest.moc"
