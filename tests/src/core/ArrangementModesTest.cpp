/*
 * ArrangementModesTest.cpp - ripple, overlap and edge snapping
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

/*! ED-020/043/044 (0.5.0 L04): the arranger's editing modes - arrangement.modes round-trips; a
 *  clip moved onto another trims it away or crossfades with it; a move snaps to another clip's
 *  edge or a marker within the tolerance and nowhere else; clip.delete closes the gap in a ripple
 *  mode, one undo step; range.delete follows the ripple mode when not told.
 *  0.5.0 themes 2 and 9: with nothing chosen a dropped clip trims what it lands on; the preview lists
 *  what a drop trims and which of it is locked; a drop or a clip.move onto a locked clip is refused and
 *  changes nothing; a snap says what it snapped to; a zoom keeps its anchor where it was on screen. */

#include <QtTest>

#include <algorithm>
#include <limits>

#include "ArrangementEdits.h"
#include "Clip.h"
#include "ClipEdits.h"
#include "ControlRegistry.h"
#include "ConfigManager.h"
#include "ControlVocabulary.h"
#include "EditLocks.h"
#include "Engine.h"
#include "ProjectMarkers.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

constexpr tick_t Bar = 192;

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QList<QPair<int, int>> spans(Track* track)
{
	QList<QPair<int, int>> out;
	for (Clip* clip : track->getClips()) { out.append({clip->startPosition().getTicks(), clip->endPosition().getTicks()}); }
	std::sort(out.begin(), out.end());
	return out;
}

Clip* clipAt(Track* track, tick_t start, tick_t length)
{
	Clip* clip = track->createClip(TimePos(start));
	clip->changeLength(TimePos(length));
	clip->setAutoResize(false);
	return clip;
}

} // namespace

class ArrangementModesTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("off")}, {QStringLiteral("overlap"), QStringLiteral("free")}});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Review W2a-1 #8: the modes go back to the defaults after every case, a failing one included.
	void cleanup()
	{
		run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("off")},
			{QStringLiteral("overlap"), QStringLiteral("free")}, {QStringLiteral("snap_to_edges"), true}});
	}

	void init()
	{
		Engine::getSong()->clearProject();
		m_track = Track::create(Track::Type::Instrument, Engine::getSong());
	}

	void theModesRoundTrip()
	{
		const ControlResult set = run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("all")},
			{QStringLiteral("overlap"), QStringLiteral("crossfade")}, {QStringLiteral("snap_to_edges"), false}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(arrangement::modes().ripple, arrangement::Ripple::All);
		QCOMPARE(arrangement::modes().overlap, arrangement::Overlap::Crossfade);
		QVERIFY(!arrangement::modes().snapToEdges);
		QCOMPARE(run(QStringLiteral("arrangement.modes")).result.value(QStringLiteral("ripple")).toString(), QStringLiteral("all"));
		run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("off")}, {QStringLiteral("overlap"), QStringLiteral("free")},
			{QStringLiteral("snap_to_edges"), true}});
	}

	void aClipDroppedOnAnotherTrimsOrCrossfadesIt()
	{
		Clip* under = clipAt(m_track, 0, 2 * Bar);
		Clip* moved = clipAt(m_track, Bar, 2 * Bar);
		QCOMPARE(arrangement::applyOverlap(moved, arrangement::Overlap::Trim), 1);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, Bar}, {Bar, 3 * Bar}}));

		// The trim kept the left part in the original clip object: lengthen it back over the moved clip.
		under->changeLength(TimePos(Bar + 48));  // overlap the moved clip by 48 ticks
		QCOMPARE(arrangement::applyOverlap(moved, arrangement::Overlap::Crossfade), 1);
		QCOMPARE(under->clipEdits().fadeOutTicks, 48);
		QCOMPARE(moved->clipEdits().fadeInTicks, 48);
	}

	//! A crossfade is as long as the clips' SHARED span: when the later clip is wholly inside the
	//! earlier one, the overlap stops at the later clip's end, not at the earlier one's.
	void aCrossfadeIntoAContainedClipStopsAtItsEnd()
	{
		Clip* earlier = clipAt(m_track, 0, 100);          // [0, 100)
		Clip* later = clipAt(m_track, 20, 20);            // [20, 40), wholly inside it
		QCOMPARE(arrangement::applyOverlap(later, arrangement::Overlap::Crossfade), 1);
		// The shared span is [20, 40) - 20 ticks, not the earlier clip's remaining 80.
		QCOMPARE(earlier->clipEdits().fadeOutTicks, 20);
		QCOMPARE(later->clipEdits().fadeInTicks, 20);
	}

	void aMoveSnapsToAnEdgeOrAMarkerOnlyWhenClose()
	{
		Clip* other = clipAt(m_track, 0, Bar);
		ProjectMarkers::instance().add(400, QStringLiteral("snap"), QString());
		QCOMPARE(arrangement::snapToEdges(190, 100, nullptr, 10), Bar);       // its start lands on the clip's end
		QCOMPARE(arrangement::snapToEdges(305, 100, nullptr, 10), 300);       // its end lands on the marker
		QCOMPARE(arrangement::snapToEdges(250, 100, nullptr, 10), 250);       // nothing near
		QCOMPARE(arrangement::snapToEdges(190, 100, other, 10), 190);         // its own edges do not count
	}

	//! A tolerance at the very top of tick_t still snaps: measuring the distance must not wrap.
	void aMaximumToleranceStillSnaps()
	{
		clipAt(m_track, 0, Bar);
		const tick_t widest = std::numeric_limits<tick_t>::max();
		QCOMPARE(arrangement::snapToEdges(190, 100, nullptr, widest), Bar);
		// A candidate far above the proposed start (the difference overflows a 32-bit tick) is not a match.
		QCOMPARE(arrangement::snapToEdges(-1000, 100, nullptr, 10), -1000);
	}

	void deleteClosesTheGapInARippleModeAsOneStep()
	{
		clipAt(m_track, 0, Bar);
		Clip* middle = clipAt(m_track, Bar, Bar);
		clipAt(m_track, 2 * Bar, Bar);
		QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("track")}}).ok);
		QVERIFY(run(QStringLiteral("clip.delete"), {{QStringLiteral("clip"), control::clipIdOf(middle)}}).ok);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, Bar}, {Bar, 2 * Bar}}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, Bar}, {Bar, 2 * Bar}, {2 * Bar, 3 * Bar}}));

		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), Bar}, {QStringLiteral("end"), 2 * Bar},
			{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(m_track)}}}).ok);
		QVERIFY(run(QStringLiteral("range.delete")).ok);  // no 'ripple': the mode says close the gap
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, Bar}, {Bar, 2 * Bar}}));
		run(QStringLiteral("arrangement.modes"), {{QStringLiteral("ripple"), QStringLiteral("off")}});
	}

	void aCommandMoveTrimsUnderTheOverlapModeAndUndoesAsOne()
	{
		clipAt(m_track, 0, 2 * Bar);
		Clip* moved = clipAt(m_track, 4 * Bar, Bar);
		QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("overlap"), QStringLiteral("trim")}}).ok);
		QVERIFY(run(QStringLiteral("clip.move"), {{QStringLiteral("clip"), control::clipIdOf(moved)}, {QStringLiteral("position"), Bar / 2}}).ok);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, Bar / 2}, {Bar / 2, Bar + Bar / 2}, {Bar + Bar / 2, 2 * Bar}}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, 2 * Bar}, {4 * Bar, 5 * Bar}}));
		run(QStringLiteral("arrangement.modes"), {{QStringLiteral("overlap"), QStringLiteral("free")}});
	}

	//! ED-009: an audition silences the other tracks while it lasts and puts everything back -
	//! without a single undo step.
	void anAuditionSilencesTheOthersAndRestoresEverything()
	{
		Track* other = Track::create(Track::Type::Instrument, Engine::getSong());
		Track* alreadyMuted = Track::create(Track::Type::Instrument, Engine::getSong());
		alreadyMuted->setMuted(true);
		const int depth = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		const ControlResult started = run(QStringLiteral("transport.audition_start"),
			{{QStringLiteral("position"), Bar}, {QStringLiteral("track"), control::trackIdOf(m_track)}});
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		QVERIFY(other->isMuted());
		QVERIFY(!m_track->isMuted());
		QVERIFY(run(QStringLiteral("transport.audition_stop")).ok);
		QVERIFY(!other->isMuted());
		QVERIFY2(alreadyMuted->isMuted(), "a track muted before the audition came back unmuted");
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), depth);
	}

	//! Theme 2, the owner's decision: a dragged clip trims what it lands on unless told otherwise.
	void withNothingChosenADropTrims()
	{
		ConfigManager::inst()->deleteValue(QStringLiteral("arrangement"), QStringLiteral("overlap"));
		QCOMPARE(arrangement::modes().overlap, arrangement::Overlap::Trim);
		QCOMPARE(run(QStringLiteral("arrangement.modes")).result.value(QStringLiteral("overlap")).toString(), QStringLiteral("trim"));
	}

	//! Theme 2: the preview's list is what a drop changes - the part of each clip under the span - and a
	//! locked clip is marked, and left alone by the trim.
	void theOverlapPreviewListsWhatADropTrims()
	{
		Clip* left = clipAt(m_track, 0, 2 * Bar);
		Clip* right = clipAt(m_track, 3 * Bar, 2 * Bar);
		Clip* moved = clipAt(m_track, 8 * Bar, 2 * Bar);
		const QVector<arrangement::OverlapEffect> effects = arrangement::overlapEffects(moved, Bar, 4 * Bar);
		QCOMPARE(effects.size(), 2);
		QCOMPARE(effects.at(0).clip, left);
		QCOMPARE(effects.at(0).from, Bar);
		QCOMPARE(effects.at(0).to, 2 * Bar);
		QCOMPARE(effects.at(1).clip, right);
		QCOMPARE(effects.at(1).from, 3 * Bar);
		QCOMPARE(effects.at(1).to, 4 * Bar);
		QVERIFY(!effects.at(0).locked);

		EditLocks::instance().setClipLocked(right->id(), true);
		QVERIFY(arrangement::overlapEffects(moved, Bar, 4 * Bar).at(1).locked);
		QCOMPARE(arrangement::lockedUnder(moved, Bar, 4 * Bar, arrangement::Overlap::Trim), right);
		QCOMPARE(arrangement::lockedUnder(moved, Bar, 4 * Bar, arrangement::Overlap::Free), nullptr);  // free changes nothing
		QCOMPARE(arrangement::lockedUnder(moved, 0, Bar, arrangement::Overlap::Trim), nullptr);
		moved->changeLength(TimePos(3 * Bar));
		moved->movePosition(TimePos(Bar));  // now over both: the unlocked one is trimmed, the locked one is not touched
		QCOMPARE(arrangement::applyOverlap(moved, arrangement::Overlap::Trim), 1);
		QCOMPARE(left->length().getTicks(), Bar);
		QCOMPARE(right->startPosition().getTicks(), 3 * Bar);
		QCOMPARE(right->length().getTicks(), 2 * Bar);
		EditLocks::instance().clear();
	}

	//! Theme 2: a drop onto a locked clip goes back where it came from, and nothing else moves.
	void aDropOntoALockedClipIsRefused()
	{
		Clip* locked = clipAt(m_track, 0, 2 * Bar);
		Clip* moved = clipAt(m_track, 4 * Bar, Bar);
		QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("overlap"), QStringLiteral("trim")}}).ok);
		EditLocks::instance().setClipLocked(locked->id(), true);
		moved->movePosition(TimePos(Bar));  // what the drag did
		QVERIFY(!arrangement::afterUserMove(moved, 4 * Bar));
		QCOMPARE(moved->startPosition().getTicks(), 4 * Bar);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, 2 * Bar}, {4 * Bar, 5 * Bar}}));

		// Onto empty time the same drop is taken.
		moved->movePosition(TimePos(6 * Bar));
		QVERIFY(arrangement::afterUserMove(moved, 4 * Bar));
		QCOMPARE(moved->startPosition().getTicks(), 6 * Bar);
		EditLocks::instance().clear();
	}

	//! Theme 2: the control surface refuses the same move, typed and with the reason, and changes nothing.
	void aCommandMoveOntoALockedClipIsRefused()
	{
		Clip* locked = clipAt(m_track, 0, 2 * Bar);
		Clip* moved = clipAt(m_track, 4 * Bar, Bar);
		QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("overlap"), QStringLiteral("trim")}}).ok);
		QVERIFY(run(QStringLiteral("clip.set_locked"), {{QStringLiteral("clip"), control::clipIdOf(locked)}}).ok);
		const ControlResult refused = run(QStringLiteral("clip.move"), {{QStringLiteral("clip"), control::clipIdOf(moved)}, {QStringLiteral("position"), Bar}});
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QVERIFY2(refused.errorMessage.contains(control::clipIdOf(locked)), qPrintable(refused.errorMessage));
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, 2 * Bar}, {4 * Bar, 5 * Bar}}));
		// With clips free to overlap, the move trims nothing, so the lock has nothing to refuse.
		QVERIFY(run(QStringLiteral("arrangement.modes"), {{QStringLiteral("overlap"), QStringLiteral("free")}}).ok);
		QVERIFY(run(QStringLiteral("clip.move"), {{QStringLiteral("clip"), control::clipIdOf(moved)}, {QStringLiteral("position"), Bar}}).ok);
		QCOMPARE(locked->length().getTicks(), 2 * Bar);
		EditLocks::instance().clear();
	}

	//! Theme 2: the snap line's source - which edge or marker won, where the line goes, what it says.
	void aSnapSaysWhatItSnappedTo()
	{
		clipAt(m_track, 0, Bar);
		ProjectMarkers::instance().add(400, QStringLiteral("Drop"), QString());
		const arrangement::SnapHit edge = arrangement::snapToEdgesHit(190, 100, nullptr, 10);
		QCOMPARE(edge.kind, arrangement::SnapHit::Kind::ClipEdge);
		QCOMPARE(edge.start, Bar);
		QCOMPARE(edge.line, Bar);
		const arrangement::SnapHit marker = arrangement::snapToEdgesHit(305, 100, nullptr, 10);
		QCOMPARE(marker.kind, arrangement::SnapHit::Kind::Marker);
		QCOMPARE(marker.start, 300);
		QCOMPARE(marker.line, 400);  // the clip's END landed on the marker: the line is drawn there
		QCOMPARE(marker.label, QStringLiteral("Marker: Drop"));
		QCOMPARE(arrangement::snapToEdgesHit(250, 100, nullptr, 10).kind, arrangement::SnapHit::Kind::None);
		ProjectMarkers::instance().clear();
	}

	//! Theme 9: a zoom keeps its anchor at the same place on screen, centres one that was off it, and never
	//! scrolls before the song's start.
	void aZoomKeepsItsAnchorInPlace()
	{
		// 1000 px at 100 px a bar shows ten bars; the anchor halfway across stays halfway across at 200 px.
		QCOMPARE(arrangement::anchoredViewStart(0, 1000, 100.0, 200.0, 5 * Bar), 5 * Bar - 5 * Bar / 2);
		// The left edge as the anchor keeps the left edge (the zoom before 0.5.0).
		QCOMPARE(arrangement::anchoredViewStart(3 * Bar, 1000, 100.0, 50.0, 3 * Bar), 3 * Bar);
		// Off screen before: into the middle of the new view.
		QCOMPARE(arrangement::anchoredViewStart(0, 1000, 100.0, 100.0, 40 * Bar), 40 * Bar - 5 * Bar);
		// Zooming out around an anchor near the start does not go before tick 0.
		QCOMPARE(arrangement::anchoredViewStart(0, 1000, 100.0, 10.0, Bar), 0);
	}

private:
	Track* m_track = nullptr;
};

QTEST_MAIN(ArrangementModesTest)
#include "ArrangementModesTest.moc"
