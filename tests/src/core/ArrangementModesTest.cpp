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
 *  mode, one undo step; range.delete follows the ripple mode when not told. */

#include <QtTest>

#include <algorithm>

#include "ArrangementEdits.h"
#include "Clip.h"
#include "ClipEdits.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
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

	void aMoveSnapsToAnEdgeOrAMarkerOnlyWhenClose()
	{
		Clip* other = clipAt(m_track, 0, Bar);
		ProjectMarkers::instance().add(400, QStringLiteral("snap"), QString());
		QCOMPARE(arrangement::snapToEdges(190, 100, nullptr, 10), Bar);       // its start lands on the clip's end
		QCOMPARE(arrangement::snapToEdges(305, 100, nullptr, 10), 300);       // its end lands on the marker
		QCOMPARE(arrangement::snapToEdges(250, 100, nullptr, 10), 250);       // nothing near
		QCOMPARE(arrangement::snapToEdges(190, 100, other, 10), 190);         // its own edges do not count
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

private:
	Track* m_track = nullptr;
};

QTEST_MAIN(ArrangementModesTest)
#include "ArrangementModesTest.moc"
