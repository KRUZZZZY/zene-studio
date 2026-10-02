/*
 * RangeEditsTest.cpp - the time range and the edits over it
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

/*! ED-011/012 (0.5.0 L04): range.* over a real track - split at the edges, delete with and
 *  without ripple, crop, insert time, copy/paste, duplicate - each ONE undo step that puts every
 *  clip back; no range, nothing copied: refused, typed. */

#include <QtTest>

#include <algorithm>

#include "ArrangementEdits.h"
#include "Clip.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
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

//! The track's clips as (start, end) pairs, in time order.
QList<QPair<int, int>> spans(Track* track)
{
	QList<QPair<int, int>> out;
	for (Clip* clip : track->getClips()) { out.append({clip->startPosition().getTicks(), clip->endPosition().getTicks()}); }
	std::sort(out.begin(), out.end());
	return out;
}

} // namespace

class RangeEditsTest : public QObject
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
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		m_track = Track::create(Track::Type::Instrument, Engine::getSong());
		Clip* clip = m_track->createClip(TimePos(0));
		clip->changeLength(TimePos(3 * Bar));
		clip->setAutoResize(false);
		QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), Bar}, {QStringLiteral("end"), 2 * Bar},
			{QStringLiteral("tracks"), QJsonArray{control::trackIdOf(m_track)}}}).ok);
	}

	void splitAtTheEdgesIsOneUndoStep()
	{
		QVERIFY(run(QStringLiteral("range.split")).ok);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, Bar}, {Bar, 2 * Bar}, {2 * Bar, 3 * Bar}}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, 3 * Bar}}));
	}

	void deleteWithAndWithoutRipple()
	{
		QVERIFY(run(QStringLiteral("range.delete")).ok);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, Bar}, {2 * Bar, 3 * Bar}}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("range.delete"), {{QStringLiteral("ripple"), true}}).ok);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, Bar}, {Bar, 2 * Bar}}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, 3 * Bar}}));
	}

	void cropAndInsertTime()
	{
		QVERIFY(run(QStringLiteral("range.crop")).ok);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{Bar, 2 * Bar}}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(run(QStringLiteral("range.insert_time")).ok);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, Bar}, {2 * Bar, 4 * Bar}}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(spans(m_track), (QList<QPair<int, int>>{{0, 3 * Bar}}));
	}

	void copyPasteAndDuplicate()
	{
		// Nothing has been copied yet in this process: paste is refused, typed.
		QCOMPARE(run(QStringLiteral("range.paste")).errorKind, ControlErrorKind::Refused);
		QVERIFY(run(QStringLiteral("range.split")).ok);
		const ControlResult copied = run(QStringLiteral("range.copy"));
		QCOMPARE(copied.result.value(QStringLiteral("copied")).toInt(), 1);
		QVERIFY(run(QStringLiteral("range.paste"), {{QStringLiteral("position"), 3 * Bar}}).ok);
		QCOMPARE(spans(m_track).last(), (QPair<int, int>{3 * Bar, 4 * Bar}));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(spans(m_track).size(), 3);
		QVERIFY(run(QStringLiteral("range.duplicate")).ok);
		QCOMPARE(run(QStringLiteral("range.get")).result.value(QStringLiteral("start")).toInt(), 2 * Bar);
	}

	void withoutARangeTheEditsAreRefused()
	{
		QVERIFY(run(QStringLiteral("range.clear")).ok);
		QCOMPARE(run(QStringLiteral("range.delete")).errorKind, ControlErrorKind::Refused);
		QCOMPARE(run(QStringLiteral("range.copy")).errorKind, ControlErrorKind::Refused);
	}

	void splitClipAtRefusesTheEdges()
	{
		Clip* clip = m_track->getClips().front();
		QVERIFY(arrangement::splitClipAt(clip, 0) == nullptr);
		QVERIFY(arrangement::splitClipAt(clip, 3 * Bar) == nullptr);
		Clip* right = arrangement::splitClipAt(clip, Bar);
		QVERIFY(right != nullptr);
		QCOMPARE(right->startPosition().getTicks(), Bar);
	}

private:
	Track* m_track = nullptr;
};

QTEST_MAIN(RangeEditsTest)
#include "RangeEditsTest.moc"
