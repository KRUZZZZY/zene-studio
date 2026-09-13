/*
 * TempoMapPersistenceTest.cpp - the tempo map's project-file claim: a project
 *                               that has no map is saved byte-identically, and a
 *                               map that exists round-trips.
 *
 * Split out of TempoMapTest.cpp so both files stay inside the 500-line new-file
 * cap; the arithmetic and the timing-path proofs are that file's.
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
 */

// THE BYTE-IDENTITY CLAIM, file half. This release's reproducibility claim is
// that a project which does not use a feature re-saves and re-renders exactly as
// it did, and the tempo map's write site is gated on TempoMap::shouldPersist()
// (active OR non-empty) so that a project which predates the feature gains
// nothing. This test measures it on a REAL project rather than asserting it: the
// fixture the other control-surface tests drive, with tracks, clips and
// automation, loaded and re-saved twice, byte for byte.

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include "Engine.h"
#include "Song.h"
#include "TempoMap.h"

using namespace lmms;

namespace
{

constexpr int kMappedTempo = 90;
constexpr int kSecondTempo = 180;

QString readText(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) { return QString(); }
	return QString::fromUtf8(file.readAll());
}

//! The three-event map the engine test also uses: a tempo at tick 0, a metre at
//! 384, a second tempo at 768.
TempoMap threeEventMap()
{
	TempoMap map;
	TempoMapEvent first;
	first.tick = 0;
	first.hasTempo = true;
	first.tempo = kMappedTempo;
	TempoMapEvent middle;
	middle.tick = 384;
	middle.hasTimeSignature = true;
	middle.numerator = 3;
	middle.denominator = 4;
	TempoMapEvent last;
	last.tick = 768;
	last.hasTempo = true;
	last.tempo = kSecondTempo;
	const bool built = map.addEvent(first) && map.addEvent(middle) && map.addEvent(last);
	Q_ASSERT(built);
	(void)built;
	map.setActive(true);
	return map;
}

//! The <tempo-map ...> ... </tempo-map> substring of a project file.
QString tempoMapBlock(const QString& project)
{
	const int start = project.indexOf(QStringLiteral("<tempo-map"));
	const int end = project.indexOf(QStringLiteral("</tempo-map>"));
	if (start < 0 || end < 0) { return QString(); }
	return project.mid(start, end + 12 - start);
}

} // namespace


class TempoMapPersistenceTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { Engine::init(true); }

	void cleanupTestCase() { Engine::getSong()->stop(); Engine::destroy(); }

	//! (5) A project that does not use the map is byte-identical, and a project
	//! that does carries the map through a save/load round trip.
	void projectWithoutAMapIsUnchangedAndAMapRoundTrips()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		Song* song = Engine::getSong();
		song->setLoadOnLaunch(false);

		// A REAL project - the fixture the other control-surface tests drive -
		// not an empty document: it carries tracks, clips and automation.
		song->clearProject();
		song->loadProject(QStringLiteral(LMMS_TEST_DATA_DIR "/agent-control-fixture.mmp"));
		const QString first = dir.filePath(QStringLiteral("imported.mmp"));
		QVERIFY(song->saveProjectFile(first));
		const QString firstText = readText(first);
		QVERIFY(!firstText.isEmpty());
		QVERIFY2(!firstText.contains(QStringLiteral("tempo-map")),
			"a project with no tempo map gained a <tempo-map> element");
		QCOMPARE(song->tempoMap().map().size(), 0);

		song->loadProject(first);
		const QString second = dir.filePath(QStringLiteral("reimported.mmp"));
		QVERIFY(song->saveProjectFile(second));
		QCOMPARE(readText(second), firstText);

		// Now give the map three events and switch it on: the element appears,
		// and it comes back through a load, event for event.
		const TempoMap authored = threeEventMap();
		QVERIFY(song->tempoMap().edit([&authored](TempoMap& map) { map = authored; return true; }));
		const QString withMap = dir.filePath(QStringLiteral("with-map.mmp"));
		QVERIFY(song->saveProjectFile(withMap));
		const QString withMapText = readText(withMap);
		QVERIFY2(withMapText.contains(QStringLiteral("<tempo-map")),
			"a project with a tempo map did not carry one");
		QVERIFY2(tempoMapBlock(withMapText).contains(QStringLiteral("active=\"1\"")),
			qPrintable(tempoMapBlock(withMapText)));

		song->clearProject();
		QCOMPARE(song->tempoMap().map().size(), 0);
		song->loadProject(withMap);
		QVERIFY2(song->tempoMap().map() == authored,
			"the tempo map did not survive the round trip");
		const QString again = dir.filePath(QStringLiteral("with-map-again.mmp"));
		QVERIFY(song->saveProjectFile(again));
		QCOMPARE(tempoMapBlock(readText(again)), tempoMapBlock(withMapText));

		// The engine's reading of the reloaded map is the authored one.
		QCOMPARE(song->tempoAtTick(768), kSecondTempo);
		song->clearProject();
		QCOMPARE(song->tempoAtTick(768), song->getTempo());
	}
};

QTEST_GUILESS_MAIN(TempoMapPersistenceTest)
#include "TempoMapPersistenceTest.moc"
