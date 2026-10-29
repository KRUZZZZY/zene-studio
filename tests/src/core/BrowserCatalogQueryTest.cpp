/*
 * BrowserCatalogQueryTest.cpp - the browser's query half: a query matches on
 *                               name, tag and (with 'probe') embedded metadata;
 *                               the walk's scan and depth bounds; the probe
 *                               budget; and the canonical tag-store key.
 *
 * These cases were moved here unchanged from BrowserCatalogTest.cpp to keep
 * that file under the file-length ratchet. They build their own fixture tree
 * (kick_48k.wav and notes.txt) in a private temporary directory, so they do
 * not depend on the metadata, tag-store or peak-cache cases that stayed behind.
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

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QString>
#include <QTemporaryDir>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#endif

#include "BrowserCatalog.h"
#include "BrowserIndex.h"
#include "BrowserPeakCache.h"
#include "BrowserTestSupport.h"

using namespace lmms;
using namespace browserfixture;

namespace
{

QString writeFixture(const QString& directory, const QString& name, const WaveSpec& spec)
{
	const QString path = QDir(directory).filePath(name);
	writeWave(path, spec);
	return path;
}

//! An LV2 preset: a directory bundle holding a manifest, as an installed plugin does.
QString writeLv2Bundle(const QString& directory)
{
	const QString bundle = QDir(directory).filePath(QStringLiteral("MySynth.lv2"));
	QDir().mkpath(bundle);
	writeText(QDir(bundle).filePath(QStringLiteral("manifest.ttl")),
		QStringLiteral("@prefix lv2: <http://lv2plug.in/ns/lv2core#> ."));
	return bundle;
}

} // namespace

class BrowserCatalogQueryTest : public QObject
{
	Q_OBJECT
private slots:

	void initTestCase()
	{
		QVERIFY2(m_dir.isValid(), "no temporary directory for the browser fixtures");
		m_wave = writeFixture(m_dir.path(), QStringLiteral("kick_48k.wav"), WaveSpec());
		writeText(QDir(m_dir.path()).filePath(QStringLiteral("notes.txt")),
			QStringLiteral("not audio"));
		QVERIFY2(QFileInfo::exists(m_wave), "the fixture wave was not written");
		// The tag store's default file is in the user's working directory: the cases below write
		// tags and categories, so point it inside the temporary tree before the first write. (A
		// runner has no working directory, and the save could not create the file there.)
		QVERIFY(BrowserTagStore::instance().openAt(QDir(m_dir.path()).filePath(QStringLiteral("tags.json"))));
	}

	void cleanupTestCase()
	{
		// Leave the store pointed at a file inside the temporary tree, so nothing
		// outside it is read or written by the rest of this process.
		BrowserTagStore::instance().openAt(QDir(m_dir.path()).filePath(QStringLiteral("done.json")));
	}

	//! A query matches on the name, on a tag and - with 'probe' - on the embedded
	//! metadata. The probe counter is asserted, so "it matched on metadata" is
	//! not a claim the test can make without opening the file.
	void theQueryMatchesNameTagAndMetadata()
	{
		BrowserQuery byName;
		byName.path = m_dir.path();
		byName.text = QStringLiteral("kick");
		const BrowserQueryResult named = runBrowserQuery(byName);
		QCOMPARE(named.matched, 1);
		QCOMPARE(named.items.size(), 1);
		QCOMPARE(named.items.first().path, QFileInfo(m_wave).absoluteFilePath());
		QCOMPARE(named.items.first().kind, BrowserKind::Audio);
		QCOMPARE(named.probed, 0);
		QCOMPARE(named.roots.size(), 1);
		QVERIFY(!named.truncated);

		// The same query as a kind filter: the text file is excluded, and so is
		// an audio-only query that names a kind the file does not have.
		BrowserQuery byKind;
		byKind.path = m_dir.path();
		byKind.kind = QStringLiteral("audio");
		QCOMPARE(runBrowserQuery(byKind).matched, 1);
		BrowserQuery wrongKind;
		wrongKind.path = m_dir.path();
		wrongKind.kind = QStringLiteral("preset");
		QCOMPARE(runBrowserQuery(wrongKind).matched, 0);

		// "drum" appears in the file's embedded title and in no file name, so a
		// match here can only come from the metadata probe.
		// 'kind' keeps the probe to the one audio file in the directory, so the
		// probe counter measures the metadata match rather than the file count.
		BrowserQuery byMetadata;
		byMetadata.path = m_dir.path();
		byMetadata.kind = QStringLiteral("audio");
		byMetadata.text = QStringLiteral("drum");
		QCOMPARE(runBrowserQuery(byMetadata).matched, 0);
		byMetadata.probe = true;
		const BrowserQueryResult probed = runBrowserQuery(byMetadata);
		QCOMPARE(probed.matched, 1);
		QCOMPARE(probed.probed, 1);
		QVERIFY(probed.items.first().hasMetadata);
		QCOMPARE(probed.items.first().metadata.title, QStringLiteral("Kick Drum"));

		// A tag filter needs the tag to be there, and matches nothing on its own
		// without one.
		BrowserTagStore::instance().add(m_wave, QStringLiteral("favourite"));
		BrowserQuery byTag;
		byTag.path = m_dir.path();
		byTag.tags = QStringList{QStringLiteral("favourite")};
		QCOMPARE(runBrowserQuery(byTag).matched, 1);
		BrowserQuery missingTag;
		missingTag.path = m_dir.path();
		missingTag.tags = QStringList{QStringLiteral("nope")};
		QCOMPARE(runBrowserQuery(missingTag).matched, 0);
		// Every listed tag must be present: two tags with one absent is no match.
		missingTag.tags.append(QStringLiteral("favourite"));
		QCOMPARE(runBrowserQuery(missingTag).matched, 0);

		// The limit and offset page the same list.
		BrowserQuery paged;
		paged.path = m_dir.path();
		paged.limit = 1;
		const BrowserQueryResult first = runBrowserQuery(paged);
		QCOMPARE(first.items.size(), 1);
		QVERIFY(first.matched >= 2);
		paged.offset = 1;
		const BrowserQueryResult second = runBrowserQuery(paged);
		QCOMPARE(second.items.size(), 1);
		QVERIFY(second.items.first().path != first.items.first().path);
	}

	//! The category facet (0.5.0 L11, WF-007): ONE category per file, a query's
	//! `category` narrows to the files carrying it (case-insensitively, like a
	//! tag), and an empty `category` is "any" - it matches every file the other
	//! filters let through. The matched item reports the category it carries, so
	//! a caller can tell which facet value a row came from.
	void categoryNarrowsTheQueryAndEmptyMeansAny()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString kick = writeFixture(dir.path(), QStringLiteral("kick_48k.wav"), WaveSpec());
		const QString snare = writeFixture(dir.path(), QStringLiteral("snare_48k.wav"), WaveSpec());
		writeText(QDir(dir.path()).filePath(QStringLiteral("notes.txt")), QStringLiteral("x"));
		BrowserTagStore& store = BrowserTagStore::instance();
		QVERIFY(store.setCategory(kick, QStringLiteral("Drums")));
		QVERIFY(store.setCategory(snare, QStringLiteral("Loops")));

		BrowserQuery byCategory;
		byCategory.path = dir.path();
		byCategory.category = QStringLiteral("Drums");
		const BrowserQueryResult drums = runBrowserQuery(byCategory);
		QCOMPARE(drums.matched, 1);
		QCOMPARE(drums.items.first().path, QFileInfo(kick).absoluteFilePath());
		QCOMPARE(drums.items.first().category, QStringLiteral("Drums"));

		// Case-insensitive, as the tag comparison is.
		byCategory.category = QStringLiteral("drums");
		QCOMPARE(runBrowserQuery(byCategory).matched, 1);
		// A category no file carries matches nothing.
		byCategory.category = QStringLiteral("Hats");
		QCOMPARE(runBrowserQuery(byCategory).matched, 0);

		// Empty is "any category": the other filters alone answer, and the
		// files that carry none are in the result set too.
		byCategory.category.clear();
		const BrowserQueryResult any = runBrowserQuery(byCategory);
		QCOMPARE(any.matched, 3);  // two waves + the text file
		QVERIFY(store.clearCategory(snare));
	 QCOMPARE(runBrowserQuery(byCategory).matched, 3);  // a file with no category is still in "any"
		byCategory.category = QStringLiteral("Loops");
		QCOMPARE(runBrowserQuery(byCategory).matched, 0);  // ... and out of a named one

		// Category and tag filter together (AND), like every other pair. Kick
		// still carries "Drums" from the first set above (only snare was
		// cleared), so re-setting it is refused - an edit that changes nothing.
		QString why;
		QVERIFY2(!store.setCategory(kick, QStringLiteral("Drums"), &why), qPrintable(why));
		QVERIFY2(why.contains(QStringLiteral("already carries")), qPrintable(why));
		QCOMPARE(store.categoryOf(kick), QStringLiteral("Drums"));
		QVERIFY(store.add(kick, QStringLiteral("favourite")));
		byCategory.category = QStringLiteral("Drums");
		byCategory.tags = QStringList{QStringLiteral("favourite")};
		QCOMPARE(runBrowserQuery(byCategory).matched, 1);
		byCategory.tags.append(QStringLiteral("missing"));
		QCOMPARE(runBrowserQuery(byCategory).matched, 0);

		// Leave the store without categories for the tests that follow.
		QVERIFY(store.clearCategory(kick));
	}

	//! The store's category table round-trips through its file: what setCategory
	//! wrote is what a load reads back, and a store this facet predates (a file
	//! with no "categories" key) loads as one with no categories rather than as
	//! an error - an old library reads exactly as it did.
	void theCategoryTableRoundTripsAndAnOldStoreLoadsEmpty()
	{
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("category-round-trip.json"));
		{
			QFile old(QDir(m_dir.path()).filePath(QStringLiteral("category-old.json")));
			QVERIFY(old.open(QIODevice::WriteOnly));
			old.write(R"({"version":1,"tags":{"/tmp/x.wav":["a"]}})");  // no categories key
			old.close();
		}
		BrowserTagStore& store = BrowserTagStore::instance();
		QVERIFY2(store.openAt(path), qPrintable(store.lastError()));
		QCOMPARE(store.allCategories(), QStringList());

		const QString wave = writeFixture(m_dir.path(), QStringLiteral("roundtrip_48k.wav"), WaveSpec());
		QVERIFY2(store.setCategory(wave, QStringLiteral("Ambient")), qPrintable(store.lastError()));
		QCOMPARE(store.categoryOf(wave), QStringLiteral("Ambient"));
		QCOMPARE(store.allCategories(), QStringList{QStringLiteral("Ambient")});
		QCOMPARE(store.filesWithCategory(QStringLiteral("Ambient")), 1);

		// Re-read from the file: the write reached the disk.
		store.clear();  // the in-memory half is gone; only the file remains
		QVERIFY2(store.load(), qPrintable(store.lastError()));
		QCOMPARE(store.categoryOf(wave), QStringLiteral("Ambient"));

		// An older store file - tags only, no categories table - is not an error.
		QVERIFY2(store.openAt(QDir(m_dir.path()).filePath(QStringLiteral("category-old.json"))),
			qPrintable(store.lastError()));
		QCOMPARE(store.allCategories(), QStringList());

		// setCategory over the category a file already carries is not an edit.
		QVERIFY2(store.openAt(path), qPrintable(store.lastError()));
		QString why;
		QVERIFY(!store.setCategory(wave, QStringLiteral("ambient"), &why));  // case-insensitive match
		QVERIFY2(why.contains(QStringLiteral("already carries")), qPrintable(why));
		// And clearing a file that carries none is refused, not a silent no-op.
		QVERIFY(!store.clearCategory(QStringLiteral("/never/seen.wav"), &why));
		QVERIFY(why.contains(QStringLiteral("carries no category")));
	}

	//! The paths the browser groups, from the browser's own extension lists.
	void kindsAndRootsAreTheBrowsersOwn()
	{
		QCOMPARE(browserKindForPath(QStringLiteral("/tmp/a.wav")), BrowserKind::Audio);
		QCOMPARE(browserKindForPath(QStringLiteral("/tmp/a.FLAC")), BrowserKind::Audio);
		QCOMPARE(browserKindForPath(QStringLiteral("/tmp/a.xpf")), BrowserKind::Preset);
		QCOMPARE(browserKindForPath(QStringLiteral("/tmp/standard.cs.xml")), BrowserKind::Preset);
		QCOMPARE(browserKindForPath(QStringLiteral("/tmp/a.xml")), BrowserKind::Other);
		QCOMPARE(browserKindForPath(QStringLiteral("/tmp/a.mmpz")), BrowserKind::Project);
		QCOMPARE(browserKindForPath(QStringLiteral("/tmp/a.mid")), BrowserKind::Midi);
		QCOMPARE(browserKindForPath(QStringLiteral("/tmp/a.txt")), BrowserKind::Other);
		QCOMPARE(browserKindName(BrowserKind::Audio), QStringLiteral("audio"));

		BrowserKind parsed = BrowserKind::Other;
		QVERIFY(browserKindFromName(QStringLiteral("preset"), &parsed));
		QCOMPARE(parsed, BrowserKind::Preset);
		QCOMPARE(browserKindFromName(QStringLiteral("nope"), &parsed), false);
		QVERIFY(browserKindFromName(browserKindName(parsed), &parsed));

		// The roots are the sidebar's directories, and each has a stable id.
		const QList<BrowserRoot> roots = browserRoots();
		QCOMPARE(roots.size(), 6);
		QCOMPARE(roots.first().id, QStringLiteral("samples"));
		QVERIFY(!browserRootById(QStringLiteral("factory-presets")).id.isEmpty());
		QVERIFY(browserRootById(QStringLiteral("nope")).id.isEmpty());
	}

	//! The store's key is the canonical path, so a symlink and its target are one
	//! entry rather than two. The symlink half is Unix-only: on Windows
	//! QFile::link writes a `.lnk` shortcut, which is a different object and not
	//! what the canonicalisation is being measured for.
	void theStoreKeyIsCanonical()
	{
		QCOMPARE(browserCanonicalKey(m_wave), QFileInfo(m_wave).canonicalFilePath());
		// A path that does not resolve falls back to its absolute form, so a tag
		// on a file that is not there yet is still one entry.
		QCOMPARE(browserCanonicalKey(QStringLiteral("relative.wav")),
			QFileInfo(QStringLiteral("relative.wav")).absoluteFilePath());
#ifdef Q_OS_UNIX
		const QString link = QDir(m_dir.path()).filePath(QStringLiteral("link.wav"));
		QFile::link(m_wave, link);
		QVERIFY2(QFileInfo(link).isSymLink(), "the fixture symlink was not created");
		QCOMPARE(browserCanonicalKey(link), browserCanonicalKey(m_wave));

		// And a tag added through the link is the tag of the target.
		BrowserTagStore& store = BrowserTagStore::instance();
		QVERIFY(store.add(link, QStringLiteral("via-link")));
		QVERIFY(store.has(m_wave, QStringLiteral("via-link")));
		QCOMPARE(store.entryCount(), 1);
#endif
	}

//! `scanned` is documented as the files examined after the KIND filter and
	//! before text filtering, so a file a TAG filter refuses was still examined.
	void scanCountCoversWhatTheTagFilterRejected()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString kick = writeFixture(dir.path(), QStringLiteral("kick_48k.wav"), WaveSpec());
		const QString snare = writeFixture(dir.path(), QStringLiteral("snare_48k.wav"), WaveSpec());
		QVERIFY(BrowserTagStore::instance().add(kick, QStringLiteral("favourite")));

		BrowserQuery query;
		query.path = dir.path();
		query.kind = QStringLiteral("audio");
		query.tags = QStringList{QStringLiteral("favourite")};
		const BrowserQueryResult result = runBrowserQuery(query);
		QCOMPARE(result.matched, 1);
		QCOMPARE(result.items.first().path, QFileInfo(kick).absoluteFilePath());
		// Both audio files were examined; only one carries the tag.
		QCOMPARE(result.scanned, 2);
		QVERIFY(result.scanned > result.matched);

		// A kind filter is the only filter `scanned` is documented to precede, so
		// a file of another kind is not examined at all.
		BrowserQuery audioOnly = query;
		audioOnly.tags.clear();
		writeText(QDir(dir.path()).filePath(QStringLiteral("notes.txt")), QStringLiteral("x"));
		const BrowserQueryResult byKind = runBrowserQuery(audioOnly);
		QCOMPARE(byKind.scanned, 2);
		QVERIFY(snare.endsWith(QStringLiteral("snare_48k.wav")));
	}

//! A probe budget spent on a file libsndfile cannot read is a probe wasted:
	//! presets, projects and MIDI carry no audio metadata, so they must not
	//! consume the limit the audio files need.
	void probesAreSpentOnAudioCandidatesOnly()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString wave = writeFixture(dir.path(), QStringLiteral("kick_48k.wav"), WaveSpec());
		writeText(QDir(dir.path()).filePath(QStringLiteral("notes.txt")), QStringLiteral("not audio"));

		BrowserQuery query;
		query.path = dir.path();
		query.text = QStringLiteral("drum");
		query.probe = true;
		const BrowserQueryResult result = runBrowserQuery(query);
		QCOMPARE(result.matched, 1);
		QCOMPARE(result.items.first().path, QFileInfo(wave).absoluteFilePath());
		QVERIFY(result.items.first().hasMetadata);
		// The directory holds an audio file AND notes.txt; only the audio file is
		// opened for metadata, so a preset- and project-heavy library cannot
		// spend the probe limit before the audio candidates are reached.
		QCOMPARE(result.probed, 1);
	}

//! An exclusion must see the metadata too. `kick -drum` cannot be settled by
	//! the name alone: kick_48k.wav says nothing about "drum", so a cheap match
	//! accepts it, while its embedded title "Kick Drum" is exactly what the
	//! exclusion names. Accepting the item before the file is read returns a row
	//! the query's own text excludes.
	//! A preset or a project has no metadata a probe reads, so an unfielded exclusion
	//! cannot overturn its cheap match: with the probe on, the matching ones are still
	//! listed, the excluded one is not, and none of them is probed.
	void probeKeepsCheapMatchesOfPresetsAndProjects()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		for (const QString& name : {QStringLiteral("kick_bass.xpf"), QStringLiteral("kick_song.mmp"),
			QStringLiteral("kick_drum.xpf")})
		{
			QFile file(QDir(dir.path()).filePath(name));
			QVERIFY(file.open(QIODevice::WriteOnly));
			QCOMPARE(file.write("placeholder"), qint64(11));
			file.close();
		}

		BrowserQuery query;
		query.path = dir.path();
		query.text = QStringLiteral("kick -drum");
		query.probe = true;
		const BrowserQueryResult result = runBrowserQuery(query);
		QCOMPARE(result.probed, 0);
		QCOMPARE(result.matched, 2);
		QVERIFY(!result.items.isEmpty());
		for (const BrowserItem& item : result.items)
		{
			QVERIFY(!item.path.endsWith(QStringLiteral("kick_drum.xpf")));
		}
	}

	void metadataCanStillExcludeACheapMatch()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString wave = writeFixture(dir.path(), QStringLiteral("kick_48k.wav"), WaveSpec());

		BrowserQuery query;
		query.path = dir.path();
		query.text = QStringLiteral("kick -drum");
		// Without the probe the exclusion is all the name and path can answer,
		// and the item is found.
		QCOMPARE(runBrowserQuery(query).matched, 1);

		query.probe = true;
		const BrowserQueryResult result = runBrowserQuery(query);
		QCOMPARE(result.probed, 1);
		QCOMPARE(result.matched, 0);
		QVERIFY(result.items.isEmpty());
		QVERIFY(wave.endsWith(QStringLiteral("kick_48k.wav")));

		// The same expression WITHOUT the exclusion still finds it: the probe
		// did not stop matching, it stopped excluding.
		BrowserQuery keep = query;
		keep.text = QStringLiteral("kick");
		const BrowserQueryResult kept = runBrowserQuery(keep);
		QCOMPARE(kept.matched, 1);
		QCOMPARE(kept.probed, 0);
	}

//! BrowserMaxScanEntries bounds the ENTRIES the walk visits - directories
	//! included. A tree of directories that holds almost no files used to walk
	//! all of them and still answer `truncated: false`, so the reported bound
	//! was not the bound the walk obeyed.
	void scanLimitBoundsTheWalkOverDirectories()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QDir root(dir.path());
		// One file, more directories than the limit has entries: the file
		// counter alone never reaches the bound.
		for (int i = 0; i <= BrowserMaxScanEntries; ++i)
		{
			QVERIFY2(root.mkdir(QStringLiteral("d%1").arg(i)), "a fixture directory was not created");
		}
		QVERIFY(writeWave(root.filePath(QStringLiteral("kick_48k.wav")), WaveSpec()));

		BrowserQuery query;
		query.path = dir.path();
		query.text = QStringLiteral("kick");
		const BrowserQueryResult result = runBrowserQuery(query);
		QVERIFY2(result.truncated, "the walk visited more entries than the limit and did not say so");
		QVERIFY(result.scanned <= BrowserMaxScanEntries);
	}

	//! An LV2 preset is the bundle directory itself: the walk returns it as one
	//! preset and does not descend into it. Without a kind filter the bundle must
	//! be the only item - a walk that entered it would return manifest.ttl instead.
	void lv2BundleIsOnePresetAndItsContentsAreNotWalked()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString bundle = writeLv2Bundle(dir.path());
		QVERIFY2(QFileInfo(bundle).isDir(), "the LV2 fixture bundle was not written");

		BrowserQuery everything;
		everything.path = dir.path();
		const BrowserQueryResult all = runBrowserQuery(everything);
		QCOMPARE(all.matched, 1);
		QCOMPARE(all.items.first().path, QFileInfo(bundle).absoluteFilePath());
		QCOMPARE(all.items.first().kind, BrowserKind::Preset);

		BrowserQuery presets = everything;
		presets.kind = QStringLiteral("preset");
		const BrowserQueryResult preset = runBrowserQuery(presets);
		QCOMPARE(preset.matched, 1);
		QCOMPARE(preset.items.first().path, QFileInfo(bundle).absoluteFilePath());
	}

	//! The persistent index records a bundle as one preset and does not index the
	//! files inside it. The store is a file of its own, outside the scanned tree.
	void indexRecordsLv2BundleAsOnePreset()
	{
		QTemporaryDir dir;
		QTemporaryDir store;
		QVERIFY(dir.isValid());
		QVERIFY(store.isValid());
		const QString bundle = writeLv2Bundle(dir.path());
		const QString manifest = QDir(bundle).filePath(QStringLiteral("manifest.ttl"));
		QVERIFY(QFileInfo(manifest).isFile());
		qputenv("ZENE_BROWSER_INDEX", QDir(store.path()).filePath(QStringLiteral("index.json")).toUtf8());

		BrowserIndex& index = BrowserIndex::instance();
		index.reload();
		QVERIFY(index.scanNow(QStringList{dir.path()}, false, 0));
		bool bundleIndexed = false;
		bool manifestIndexed = false;
		for (const BrowserIndexEntry& entry : index.entries())
		{
			bundleIndexed = bundleIndexed || entry.path == QFileInfo(bundle).canonicalFilePath();
			manifestIndexed = manifestIndexed || entry.path == QFileInfo(manifest).canonicalFilePath();
			if (entry.path == QFileInfo(bundle).canonicalFilePath())
			{
				QCOMPARE(entry.kind, BrowserKind::Preset);
			}
		}
		qunsetenv("ZENE_BROWSER_INDEX");
		QVERIFY2(bundleIndexed, "the LV2 bundle was not indexed");
		QVERIFY2(!manifestIndexed, "the bundle contents were indexed separately");
	}

	//! A rescan whose index file cannot be written does not publish its entries:
	//! queries keep the index the store still holds, and a restart reads that same index.
	void failedIndexSaveKeepsThePreviousIndex()
	{
		QTemporaryDir dir;
		QTemporaryDir store;
		QVERIFY(dir.isValid());
		QVERIFY(store.isValid());
		writeFixture(dir.path(), QStringLiteral("first.wav"), WaveSpec());
		const QString good = QDir(store.path()).filePath(QStringLiteral("index.json"));
		// A regular file where the store's folder would be: the save cannot create the store, so it fails.
		const QString blocker = QDir(store.path()).filePath(QStringLiteral("blocker"));
		QVERIFY(writeText(blocker, QStringLiteral("not a folder")));
		const QString unwritable = QDir(blocker).filePath(QStringLiteral("index.json"));

		BrowserIndex& index = BrowserIndex::instance();
		const auto names = [&index] {
			QStringList out;
			for (const BrowserIndexEntry& entry : index.entries()) { out << entry.name; }
			out.sort();
			return out;
		};
		qputenv("ZENE_BROWSER_INDEX", good.toUtf8());
		index.reload();
		QVERIFY(index.scanNow(QStringList{dir.path()}, false, 0));
		const QStringList published = names();
		QCOMPARE(published, QStringList{QStringLiteral("first.wav")});

		writeFixture(dir.path(), QStringLiteral("second.wav"), WaveSpec());
		qputenv("ZENE_BROWSER_INDEX", unwritable.toUtf8());
		QVERIFY(index.scanNow(QStringList{dir.path()}, false, 0));
		QVERIFY2(!index.status().error.isEmpty(), "the failed save was not reported");
		QVERIFY(!index.status().running);
		QCOMPARE(names(), published);

		// After a restart the store is read again: it holds the index the failed scan did not publish.
		qputenv("ZENE_BROWSER_INDEX", good.toUtf8());
		index.reload();
		QCOMPARE(names(), published);
		qunsetenv("ZENE_BROWSER_INDEX");
	}

	//! Exhausting the metadata probe budget while another candidate needs a
	//! probe marks the result partial, even when that candidate would match.
	void probeLimitReportsTruncation()
	{
		const QString directory = QDir(m_dir.path()).filePath(QStringLiteral("probe-limit"));
		QVERIFY(QDir().mkpath(directory));
		for (int i = 0; i <= BrowserMaxProbes; ++i)
		{
			WaveSpec spec;
			spec.title = QStringLiteral("needle");
			const QString name = QStringLiteral("candidate_%1.wav").arg(i, 3, 10, QLatin1Char('0'));
			QVERIFY(writeWave(QDir(directory).filePath(name), spec));
		}

		BrowserQuery query;
		query.path = directory;
		query.kind = QStringLiteral("audio");
		query.text = QStringLiteral("needle");
		query.probe = true;
		const BrowserQueryResult result = runBrowserQuery(query);
		QCOMPARE(result.scanned, BrowserMaxProbes + 1);
		QCOMPARE(result.probed, BrowserMaxProbes);
		QCOMPARE(result.matched, BrowserMaxProbes);
		QVERIFY(result.truncated);
	}

private:
	QTemporaryDir m_dir;
	QString m_wave;
};

QTEST_GUILESS_MAIN(BrowserCatalogQueryTest)
#include "BrowserCatalogQueryTest.moc"
