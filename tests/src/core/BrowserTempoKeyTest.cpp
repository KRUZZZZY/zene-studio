/*
 * BrowserTempoKeyTest.cpp - 0.5.0 L11 (Q050-316.1): filtering local browser
 *                           results by measured tempo and key.
 *
 * The measurement is the library index's (the import detection, BrowserFeatures.h),
 * and the ONE predicate both searches call is browserItemMatchesFilters(). This
 * file proves the rules a musician and an agent depend on:
 *   - an active tempo range or key matches only a MEASURED value, in range
 *     (bounds inclusive, an open side when it is 0), and an unknown value never
 *     matches - the detector's "nothing found" (0 BPM / -1 key) and a file the
 *     index never measured alike;
 *   - the facets AND with the tag and category filters;
 *   - browser.query (walking the folders) and browser.index_query (reading the
 *     index) answer the SAME files for the same filter, through the registry;
 *   - the item wire form says what was measured, and says when nothing was;
 *   - an older index store, built with no measurements at all, still loads and
 *     simply matches no active facet;
 *   - bad facet arguments are typed refusals, and a saved collection keeps the
 *     filter.
 *
 * Everything is a local temporary file: no content service, no network. The index
 * store is written BY THIS TEST (the documented browser-index.json format), so the
 * tempo and key values are exact and the test does not depend on how well the
 * detector measures a synthetic click track.
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

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "BrowserCatalog.h"
#include "BrowserIndex.h"
#include "BrowserTestSupport.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"

using namespace lmms;
using namespace browserfixture;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! One measured record, in the index store's own format (BrowserIndex.cpp's featuresJson).
QJsonObject featureRecord(double bpm, int keyPitchClass)
{
	return {{QStringLiteral("dur"), 1.5}, {QStringLiteral("db"), -20.0}, {QStringLiteral("cent"), 1000.0},
		{QStringLiteral("zcr"), 100.0}, {QStringLiteral("bpm"), bpm}, {QStringLiteral("key"), keyPitchClass}};
}

//! One index row for @a path; \a measured false writes an entry with no "f" table,
//! which is what an older index (browser.reindex with features:false) holds.
QJsonObject indexEntry(const QString& path, bool measured, double bpm = 0.0, int keyPitchClass = -1)
{
	const QFileInfo info(path);
	QJsonObject entry{{QStringLiteral("p"), info.canonicalFilePath()}, {QStringLiteral("k"), QStringLiteral("audio")},
		{QStringLiteral("s"), static_cast<double>(info.size())},
		{QStringLiteral("m"), static_cast<double>(info.lastModified().toMSecsSinceEpoch())}};
	if (measured) { entry.insert(QStringLiteral("f"), featureRecord(bpm, keyPitchClass)); }
	return entry;
}

//! Writes @a entries as the library index store the browser reads.
bool writeIndexStore(const QString& path, const QJsonArray& entries)
{
	const QJsonObject all{{QStringLiteral("version"), 1}, {QStringLiteral("built_ms"), 0.0},
		{QStringLiteral("truncated"), false}, {QStringLiteral("entries"), entries}};
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	const QByteArray bytes = QJsonDocument(all).toJson(QJsonDocument::Compact);
	return file.write(bytes) == bytes.size();
}

QStringList namesOf(const ControlResult& listed)
{
	QStringList names;
	for (const QJsonValue& value : listed.result.value(QStringLiteral("items")).toArray())
	{
		names << value.toObject().value(QStringLiteral("name")).toString();
	}
	names.sort();
	return names;
}

} // namespace

class BrowserTempoKeyTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY2(m_dir.isValid(), "no temporary directory for the browser fixtures");
		// A private working directory, made BEFORE the engine reads it: the browser's
		// user-samples root, its tag store, its index and its collections are all under
		// the temporary tree, never the developer's own. The fixtures live in the
		// samples root itself, so a collection (which runs over the roots) reaches them.
		ConfigManager::inst()->setWorkingDir(QDir(m_dir.path()).filePath(QStringLiteral("work")));
		QVERIFY(QDir().mkpath(ConfigManager::inst()->workingDir()));
		Engine::init(true);
		ControlRegistry::setReady(true);
		const QString fixtures = QDir(ConfigManager::inst()->userSamplesDir()).absolutePath();
		QVERIFY(QDir().mkpath(fixtures));
		qputenv("ZENE_BROWSER_INDEX", QDir(m_dir.path()).filePath(QStringLiteral("index.json")).toUtf8());
		qputenv("ZENE_BROWSER_COLLECTIONS", QDir(m_dir.path()).filePath(QStringLiteral("collections.json")).toUtf8());
		QVERIFY(BrowserTagStore::instance().openAt(QDir(m_dir.path()).filePath(QStringLiteral("browser-tags.json"))));
		m_fast = QDir(fixtures).filePath(QStringLiteral("fast_120_a.wav"));
		m_slow = QDir(fixtures).filePath(QStringLiteral("slow_90_d.wav"));
		m_unknown = QDir(fixtures).filePath(QStringLiteral("unknown.wav"));
		m_unmeasured = QDir(fixtures).filePath(QStringLiteral("unmeasured.wav"));
		QVERIFY(writeWave(m_fast, WaveSpec()));
		QVERIFY(writeWave(m_slow, WaveSpec()));
		QVERIFY(writeWave(m_unknown, WaveSpec()));
		QVERIFY(writeWave(m_unmeasured, WaveSpec()));
		writeFullIndex();
	}

	void cleanupTestCase()
	{
		qunsetenv("ZENE_BROWSER_INDEX");
		qunsetenv("ZENE_BROWSER_COLLECTIONS");
		BrowserTagStore::instance().openAt(QDir(m_dir.path()).filePath(QStringLiteral("done.json")));
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The key filter speaks pitch classes, and every spelling a musician or an
	//! agent may send resolves to the same class - while a spelling that names
	//! none is refused rather than silently matching nothing.
	void theKeyVocabularyRoundTripsEverySpelling()
	{
		QCOMPARE(browserKeyName(0), QStringLiteral("C"));
		QCOMPARE(browserKeyName(1), QStringLiteral("C#"));
		QCOMPARE(browserKeyName(9), QStringLiteral("A"));
		QCOMPARE(browserKeyName(11), QStringLiteral("B"));
		QCOMPARE(browserKeyName(-1), QString());
		QCOMPARE(browserKeyName(12), QString());

		const auto classOf = [](const QString& text) -> int {
			int pitchClass = -2;
			if (!browserKeyFromName(text, &pitchClass))
			{
				QTest::qFail(qPrintable(text), __FILE__, __LINE__);
				return -2;
			}
			return pitchClass;
		};
		QCOMPARE(classOf(QStringLiteral("C")), 0);
		QCOMPARE(classOf(QStringLiteral("c#")), 1);
		QCOMPARE(classOf(QStringLiteral("Db")), 1);  // the flat spelling of the same class
		QCOMPARE(classOf(QStringLiteral(" B ")), 11);
		QCOMPARE(classOf(QStringLiteral("9")), 9);
		QCOMPARE(classOf(QStringLiteral("any")), -1);
		QCOMPARE(classOf(QString()), -1);
		for (const char* bad : {"H", "C#3", "13", "Cb", "do"})
		{
			int pitchClass = -2;
			QVERIFY2(!browserKeyFromName(QLatin1String(bad), &pitchClass), bad);
		}
	}

	//! The rule both searches rest on: only a MEASURED value can match an active
	//! facet. None of "no measurement", "measured, nothing found", "out of range"
	//! or "another key" is a match, and an inactive facet passes everything.
	void anActiveFacetNeedsAMeasuredValue()
	{
		QVERIFY(!browserTempoKeyActive(0.0, 0.0, -1));
		QVERIFY(browserTempoKeyActive(60.0, 0.0, -1));
		QVERIFY(browserTempoKeyActive(0.0, 200.0, -1));
		QVERIFY(browserTempoKeyActive(0.0, 0.0, 0));

		BrowserItem unmeasured;  // the walk never read the index for this file
		QVERIFY(!browserTempoKeyPasses(unmeasured, 1.0, 400.0, -1));
		QVERIFY(!browserTempoKeyPasses(unmeasured, 0.0, 0.0, 9));
		QVERIFY(browserTempoKeyPasses(unmeasured, 0.0, 0.0, -1));  // no facet: every file

		BrowserItem none;  // measured, and the detector found no tempo and no key
		none.hasMeasuredTempoKey = true;
		QVERIFY(!browserTempoKeyPasses(none, 1.0, 400.0, -1));
		QVERIFY(!browserTempoKeyPasses(none, 0.0, 0.0, 0));

		BrowserItem a;  // 120.5 BPM, A
		a.hasMeasuredTempoKey = true;
		a.measuredBpm = 120.5;
		a.measuredKeyPitchClass = 9;
		QVERIFY(browserTempoKeyPasses(a, 100.0, 130.0, -1));
		QVERIFY(browserTempoKeyPasses(a, 120.5, 120.5, -1));  // both bounds inclusive
		QVERIFY(!browserTempoKeyPasses(a, 121.0, 0.0, -1));
		QVERIFY(browserTempoKeyPasses(a, 0.0, 130.0, -1));  // only a maximum: an open minimum
		QVERIFY(!browserTempoKeyPasses(a, 0.0, 120.0, -1));
		QVERIFY(browserTempoKeyPasses(a, 0.0, 0.0, 9));
		QVERIFY(!browserTempoKeyPasses(a, 0.0, 0.0, 8));
	}

	//! A walked query reads the index's measurement and answers only the file in
	//! the range; the key filter does the same, and both AND together.
	void aWalkedQueryFiltersOnTheIndexMeasurement()
	{
		BrowserQuery tempo;
		tempo.path = QDir(ConfigManager::inst()->userSamplesDir()).absolutePath();
		tempo.minBpm = 100.0;
		tempo.maxBpm = 130.0;
		const BrowserQueryResult inRange = runBrowserQuery(tempo);
		QCOMPARE(inRange.matched, 1);
		QCOMPARE(inRange.items.first().path, QFileInfo(m_fast).absoluteFilePath());
		QVERIFY2(inRange.items.first().hasMeasuredTempoKey, "a filtered item did not carry its measurement");
		QCOMPARE(inRange.items.first().measuredBpm, 120.0);
		QCOMPARE(inRange.items.first().measuredKeyPitchClass, 9);

		BrowserQuery byKey;
		byKey.path = tempo.path;
		byKey.keyPitchClass = 2;  // D
		const BrowserQueryResult keyed = runBrowserQuery(byKey);
		QCOMPARE(keyed.matched, 1);
		QCOMPARE(keyed.items.first().path, QFileInfo(m_slow).absoluteFilePath());

		// Both facets AND: the 120 BPM file is in A, so asking for 100-130 and D
		// matches nothing rather than either file on one facet alone.
		BrowserQuery both = tempo;
		both.keyPitchClass = 2;
		QCOMPARE(runBrowserQuery(both).matched, 0);
		BrowserQuery matchingBoth = tempo;
		matchingBoth.keyPitchClass = 9;
		QCOMPARE(runBrowserQuery(matchingBoth).matched, 1);

		// No facet reads no index: the same walk answers every fixture, and the
		// items say nobody looked (hasMeasuredTempoKey false) instead of carrying
		// numbers the search did not read.
		BrowserQuery plain;
		plain.path = tempo.path;
		const BrowserQueryResult all = runBrowserQuery(plain);
		QCOMPARE(all.matched, 4);
		for (const BrowserItem& item : all.items) { QVERIFY(!item.hasMeasuredTempoKey); }
	}

	//! Unknown is not a value: the two files whose tempo/key the index does not
	//! know (0 / -1, and no measurement at all) match neither an active tempo
	//! range nor an active key, while the two measured files still do.
	void unknownAndUnmeasuredNeverMatchAnActiveFacet()
	{
		BrowserQuery tempo;
		tempo.path = QDir(ConfigManager::inst()->userSamplesDir()).absolutePath();
		tempo.minBpm = 1.0;
		tempo.maxBpm = 400.0;
		QCOMPARE(runBrowserQuery(tempo).matched, 2);

		BrowserQuery key;
		key.path = tempo.path;
		key.keyPitchClass = 0;  // C: no fixture was measured in C
		QCOMPARE(runBrowserQuery(key).matched, 0);

		// The measured "nothing found" record is in the index (the detector ran and
		// found none), so its item carries the measurement but still does not match.
		// A walk with no facet never reads the index (so it attaches no measurement),
		// and one with a facet reads it and refuses the record all the same.
		BrowserQuery measuredNone;
		measuredNone.path = tempo.path;
		measuredNone.text = QStringLiteral("unknown");
		QCOMPARE(runBrowserQuery(measuredNone).matched, 1);
		measuredNone.minBpm = 1.0;
		QCOMPARE(runBrowserQuery(measuredNone).matched, 0);
	}

	//! The tempo/key facets AND with the existing category and tag filters: a file
	//! must satisfy every active facet to be listed.
	void facetsCombineWithCategoryAndTags()
	{
		QVERIFY(run(QStringLiteral("browser.category.set"),
			{{QStringLiteral("path"), m_fast}, {QStringLiteral("category"), QStringLiteral("Drums")}}).ok);
		QVERIFY(run(QStringLiteral("browser.category.set"),
			{{QStringLiteral("path"), m_slow}, {QStringLiteral("category"), QStringLiteral("Loops")}}).ok);
		QVERIFY(run(QStringLiteral("browser.tag.add"),
			{{QStringLiteral("path"), m_fast}, {QStringLiteral("tag"), QStringLiteral("punchy")}}).ok);
		QVERIFY(run(QStringLiteral("browser.tag.add"),
			{{QStringLiteral("path"), m_slow}, {QStringLiteral("tag"), QStringLiteral("smooth")}}).ok);
		const QString fixtures = QDir(ConfigManager::inst()->userSamplesDir()).absolutePath();
		const auto names = [&fixtures](const QJsonObject& extra) {
			QJsonObject args{{QStringLiteral("path"), fixtures}};
			for (auto it = extra.begin(); it != extra.end(); ++it) { args.insert(it.key(), it.value()); }
			return namesOf(run(QStringLiteral("browser.query"), args));
		};

		QCOMPARE(names({{QStringLiteral("category"), QStringLiteral("Drums")},
			{QStringLiteral("tempo_min"), 100}, {QStringLiteral("tempo_max"), 130}}),
			QStringList{QStringLiteral("fast_120_a.wav")});
		QCOMPARE(names({{QStringLiteral("category"), QStringLiteral("Loops")},
			{QStringLiteral("tempo_min"), 100}, {QStringLiteral("tempo_max"), 130}}), QStringList());
		QCOMPARE(names({{QStringLiteral("tags"), QJsonArray{QStringLiteral("smooth")}},
			{QStringLiteral("key"), QStringLiteral("D")}}),
			QStringList{QStringLiteral("slow_90_d.wav")});
		QCOMPARE(names({{QStringLiteral("category"), QStringLiteral("Drums")},
			{QStringLiteral("tags"), QJsonArray{QStringLiteral("smooth")}},
			{QStringLiteral("key"), QStringLiteral("A")}}), QStringList());

		// Clear the library state for the tests that follow.
		QVERIFY(run(QStringLiteral("browser.category.clear"), {{QStringLiteral("path"), m_fast}}).ok);
		QVERIFY(run(QStringLiteral("browser.category.clear"), {{QStringLiteral("path"), m_slow}}).ok);
		QVERIFY(run(QStringLiteral("browser.tag.remove"),
			{{QStringLiteral("path"), m_fast}, {QStringLiteral("tag"), QStringLiteral("punchy")}}).ok);
		QVERIFY(run(QStringLiteral("browser.tag.remove"),
			{{QStringLiteral("path"), m_slow}, {QStringLiteral("tag"), QStringLiteral("smooth")}}).ok);
	}

	//! The acceptance case: the walked query and the indexed query answer the SAME
	//! files for the same tempo/key filter (and for no filter), through the
	//! registry an agent reaches.
	void directAndIndexedCommandsAgreeOnTheSameFilter()
	{
		const QString fixtures = QDir(ConfigManager::inst()->userSamplesDir()).absolutePath();
		const auto walked = [&fixtures](const QJsonObject& facets) -> QStringList {
			QJsonObject args{{QStringLiteral("path"), fixtures}};
			for (auto it = facets.begin(); it != facets.end(); ++it) { args.insert(it.key(), it.value()); }
			const ControlResult result = run(QStringLiteral("browser.query"), args);
			if (!result.ok) { QTest::qFail(qPrintable(result.errorMessage), __FILE__, __LINE__); return {}; }
			return namesOf(result);
		};
		const auto indexed = [](const QJsonObject& facets) -> QStringList {
			const ControlResult result = run(QStringLiteral("browser.index_query"), facets);
			if (!result.ok) { QTest::qFail(qPrintable(result.errorMessage), __FILE__, __LINE__); return {}; }
			return namesOf(result);
		};
		const QStringList all{QStringLiteral("fast_120_a.wav"), QStringLiteral("slow_90_d.wav"),
			QStringLiteral("unknown.wav"), QStringLiteral("unmeasured.wav")};

		QCOMPARE(walked({}), all);
		QCOMPARE(indexed({}), all);
		QCOMPARE(walked({{QStringLiteral("tempo_min"), 100}, {QStringLiteral("tempo_max"), 130}}),
			QStringList{QStringLiteral("fast_120_a.wav")});
		QCOMPARE(indexed({{QStringLiteral("tempo_min"), 100}, {QStringLiteral("tempo_max"), 130}}),
			QStringList{QStringLiteral("fast_120_a.wav")});
		QCOMPARE(walked({{QStringLiteral("key"), QStringLiteral("Db")}}), QStringList());
		QCOMPARE(indexed({{QStringLiteral("key"), QStringLiteral("Db")}}), QStringList());
		QCOMPARE(walked({{QStringLiteral("key"), QStringLiteral("D")}}),
			QStringList{QStringLiteral("slow_90_d.wav")});
		QCOMPARE(indexed({{QStringLiteral("key"), QStringLiteral("2")}}),
			QStringList{QStringLiteral("slow_90_d.wav")});
		QCOMPARE(walked({{QStringLiteral("tempo_min"), 80}, {QStringLiteral("tempo_max"), 120}}),
			(QStringList{QStringLiteral("fast_120_a.wav"), QStringLiteral("slow_90_d.wav")}));
		QCOMPARE(indexed({{QStringLiteral("tempo_min"), 80}, {QStringLiteral("tempo_max"), 120}}),
			(QStringList{QStringLiteral("fast_120_a.wav"), QStringLiteral("slow_90_d.wav")}));
		// An unknown value never matches in either engine, and a range that only
		// covers an unmeasured file still answers nothing.
		QCOMPARE(walked({{QStringLiteral("tempo_min"), 40}, {QStringLiteral("tempo_max"), 60}}), QStringList());
		QCOMPARE(indexed({{QStringLiteral("tempo_min"), 40}, {QStringLiteral("tempo_max"), 60}}), QStringList());
	}

	//! The wire form tells a client what the search read: the measured tempo and
	//! key with their known flags when the facet made the search consult the index,
	//! and NOTHING when nobody looked - so a reported 0 is never read as a tempo.
	void theItemWireFormReportsWhatWasMeasured()
	{
		const QString fixtures = QDir(ConfigManager::inst()->userSamplesDir()).absolutePath();
		const ControlResult filtered = run(QStringLiteral("browser.query"),
			{{QStringLiteral("path"), fixtures}, {QStringLiteral("tempo_min"), 100}, {QStringLiteral("tempo_max"), 130}});
		QVERIFY2(filtered.ok, qPrintable(filtered.errorMessage));
		const QJsonObject item = filtered.result.value(QStringLiteral("items")).toArray().first().toObject();
		QCOMPARE(item.value(QStringLiteral("tempo_bpm")).toDouble(), 120.0);
		QCOMPARE(item.value(QStringLiteral("tempo_known")).toBool(), true);
		QCOMPARE(item.value(QStringLiteral("key_pitch_class")).toInt(), 9);
		QCOMPARE(item.value(QStringLiteral("key")).toString(), QStringLiteral("A"));

		// A walked search with no facet never read the index: the fields are absent.
		const ControlResult plain = run(QStringLiteral("browser.query"),
			{{QStringLiteral("path"), fixtures}, {QStringLiteral("text"), QStringLiteral("slow")}});
		QVERIFY2(plain.ok, qPrintable(plain.errorMessage));
		const QJsonObject walked = plain.result.value(QStringLiteral("items")).toArray().first().toObject();
		QVERIFY(!walked.contains(QStringLiteral("tempo_bpm")));
		QVERIFY(!walked.contains(QStringLiteral("tempo_known")));

		// The indexed query read every entry, so its items say what the index holds -
		// and the detected-nothing record says tempo_known false with key "".
		const ControlResult indexed = run(QStringLiteral("browser.index_query"),
			{{QStringLiteral("text"), QStringLiteral("unknown")}});
		QVERIFY2(indexed.ok, qPrintable(indexed.errorMessage));
		const QJsonObject unknown = indexed.result.value(QStringLiteral("items")).toArray().first().toObject();
		QCOMPARE(unknown.value(QStringLiteral("tempo_known")).toBool(), false);
		QCOMPARE(unknown.value(QStringLiteral("tempo_bpm")).toDouble(), 0.0);
		QCOMPARE(unknown.value(QStringLiteral("key_pitch_class")).toInt(), -1);
		QCOMPARE(unknown.value(QStringLiteral("key")).toString(), QString());
	}

	//! An index built before tempo and key were measured (browser.reindex with
	//! features:false) still loads: its entries simply carry no measurement, so an
	//! active facet matches nothing while an unfiltered search lists every file.
	void anOlderIndexWithoutMeasurementsLoadsAndMatchesNoFacet()
	{
		const QString store = qEnvironmentVariable("ZENE_BROWSER_INDEX");
		QJsonArray entries;
		for (const QString& path : {m_fast, m_slow, m_unknown, m_unmeasured})
		{
			entries.append(indexEntry(path, false));
		}
		QVERIFY2(writeIndexStore(store, entries), "the older index store was not written");
		BrowserIndex::instance().reload();
		const QString fixtures = QDir(ConfigManager::inst()->userSamplesDir()).absolutePath();
		const ControlResult plain = run(QStringLiteral("browser.query"), {{QStringLiteral("path"), fixtures}});
		QVERIFY2(plain.ok, qPrintable(plain.errorMessage));
		QCOMPARE(plain.result.value(QStringLiteral("matched")).toInt(), 4);
		const ControlResult filtered = run(QStringLiteral("browser.query"),
			{{QStringLiteral("path"), fixtures}, {QStringLiteral("tempo_min"), 1}, {QStringLiteral("key"), QStringLiteral("A")}});
		QVERIFY2(filtered.ok, qPrintable(filtered.errorMessage));
		QCOMPARE(filtered.result.value(QStringLiteral("matched")).toInt(), 0);
		QCOMPARE(run(QStringLiteral("browser.index_query"),
			{{QStringLiteral("tempo_min"), 1}}).result.value(QStringLiteral("matched")).toInt(), 0);
		writeFullIndex();
	}

	//! A facet that cannot be expressed is a typed refusal naming the value, not a
	//! silent "no results": a reversed range, a negative bound, a key that names no
	//! pitch class, and a non-numeric bound.
	void badFacetArgumentsAreTypedRefusals()
	{
		const auto refused = [](const QString& command, const QJsonObject& args) -> ControlErrorKind {
			const ControlResult result = run(command, args);
			if (result.ok) { QTest::qFail(qPrintable(command), __FILE__, __LINE__); return ControlErrorKind::Refused; }
			return result.errorKind;
		};
		for (const QString& command : {QStringLiteral("browser.query"), QStringLiteral("browser.index_query")})
		{
			QCOMPARE(refused(command, {{QStringLiteral("tempo_min"), 130}, {QStringLiteral("tempo_max"), 90}}),
				ControlErrorKind::InvalidArgs);
			QCOMPARE(refused(command, {{QStringLiteral("tempo_min"), -5}}), ControlErrorKind::InvalidArgs);
			QCOMPARE(refused(command, {{QStringLiteral("key"), QStringLiteral("H")}}), ControlErrorKind::InvalidArgs);
			QCOMPARE(refused(command, {{QStringLiteral("key"), QStringLiteral("C#3")}}), ControlErrorKind::InvalidArgs);
			QCOMPARE(refused(command, {{QStringLiteral("tempo_min"), QStringLiteral("fast")}}),
				ControlErrorKind::InvalidArgs);
		}
	}

	//! A saved collection keeps the tempo and key facets, so the same filtered
	//! search runs again later; a collection whose facets could never run is
	//! refused at save, before anything is written.
	void collectionsKeepTheTempoAndKeyFacets()
	{
		const ControlResult saved = run(QStringLiteral("browser.collection_save"),
			{{QStringLiteral("name"), QStringLiteral("Q050 316 tempo")}, {QStringLiteral("tempo_min"), 100},
				{QStringLiteral("tempo_max"), 130}, {QStringLiteral("key"), QStringLiteral("A")}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QCOMPARE(saved.result.value(QStringLiteral("query")).toObject().value(QStringLiteral("tempo_min")).toDouble(), 100.0);
		QCOMPARE(saved.result.value(QStringLiteral("query")).toObject().value(QStringLiteral("key")).toString(), QStringLiteral("A"));

		const ControlResult again = run(QStringLiteral("browser.collection_run"),
			{{QStringLiteral("name"), QStringLiteral("Q050 316 tempo")}});
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
		QCOMPARE(namesOf(again), QStringList{QStringLiteral("fast_120_a.wav")});

		QCOMPARE(run(QStringLiteral("browser.collection_save"),
			{{QStringLiteral("name"), QStringLiteral("Q050 316 bad")}, {QStringLiteral("tempo_min"), 130},
				{QStringLiteral("tempo_max"), 90}}).errorKind, ControlErrorKind::InvalidArgs);
		bool badListed = false;
		for (const QJsonValue& value : run(QStringLiteral("browser.collections")).result
			.value(QStringLiteral("collections")).toArray())
		{
			badListed = badListed || value.toObject().value(QStringLiteral("name")).toString()
				== QLatin1String("Q050 316 bad");
		}
		QVERIFY2(!badListed, "a collection that cannot run was saved anyway");

		QVERIFY(run(QStringLiteral("browser.collection_remove"),
			{{QStringLiteral("name"), QStringLiteral("Q050 316 tempo")}}).ok);
	}

private:
	//! The full measurement table this test's fixtures are described by: 120 BPM in
	//! A, 90 in D, one record the detector found nothing in, and one never measured.
	void writeFullIndex()
	{
		QJsonArray entries;
		entries.append(indexEntry(m_fast, true, 120.0, 9));
		entries.append(indexEntry(m_slow, true, 90.0, 2));
		entries.append(indexEntry(m_unknown, true, 0.0, -1));
		entries.append(indexEntry(m_unmeasured, false));
		QVERIFY2(writeIndexStore(qEnvironmentVariable("ZENE_BROWSER_INDEX"), entries),
			"the index store was not written");
		BrowserIndex::instance().reload();
	}

	QTemporaryDir m_dir;
	QString m_fast;
	QString m_slow;
	QString m_unknown;
	QString m_unmeasured;
};

QTEST_GUILESS_MAIN(BrowserTempoKeyTest)
#include "BrowserTempoKeyTest.moc"
