/*
 * BrowserCatalogTest.cpp - the browser's engine half: the metadata probe, the
 *                          persisted tag store, the query and the waveform peak
 *                          cache.
 *
 * This is the ctest the release contract asks for on the ENGINE side. The
 * command surface has its own suite (ControlBrowserCommandsTest) and the A16
 * undo proof lives there, because a tag's inverse is a command.
 *
 * The load-bearing cases, in the order the class defines them:
 *   - metadata comes from the file: a byte-written RIFF/WAVE whose rate,
 *     channel count, frame count and embedded INAM title are asserted exactly;
 *   - the tag store round-trips through the FILE, proved by clearing the
 *     in-memory store and reloading (a cache that never persisted would pass a
 *     same-process read but fail this);
 *   - the peak cache answers the burst at the right bucket, and a second call
 *     is a HIT while a changed file is a MISS - the cache is observable, not
 *     merely assumed;
 *   - a query matches on name, on tag and (with 'probe') on metadata.
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

#include "BrowserCatalog.h"
#include "BrowserPeakCache.h"
#include "BrowserTestSupport.h"

using namespace lmms;
using namespace browserfixture;

namespace
{

//! 20000/32768, the amplitude the fixture's burst carries in the float domain.
constexpr double kBurstAmplitude = 20000.0 / 32768.0;

/*! The bucket the fixture's burst lands in at the resolutions the tests read.
 *  With the default 2048-frame fixture and 32 buckets (64 frames each) the burst
 *  is exactly bucket 16; at 16 buckets (128 frames each) it is exactly bucket 8. */
constexpr int kBurstBucket32 = 16;
constexpr int kBurstBucket16 = 8;

QString writeFixture(const QString& directory, const QString& name, const WaveSpec& spec)
{
	const QString path = QDir(directory).filePath(name);
	writeWave(path, spec);
	return path;
}

QStringList tagListOf(const QString& path)
{
	return BrowserTagStore::instance().tagsOf(path);
}

} // namespace

class BrowserCatalogTest : public QObject
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
	}

	void cleanupTestCase()
	{
		// Leave the store pointed at a file inside the temporary tree, so nothing
		// outside it is read or written by the rest of this process.
		BrowserTagStore::instance().openAt(QDir(m_dir.path()).filePath(QStringLiteral("done.json")));
	}

	//! The probe reads the FILE. Every number here is in the fixture's own
	//! header, and the title is the embedded `LIST`/`INFO` tag.
	void metadataComesFromTheFile()
	{
		QString why;
		const BrowserMetadata probe = probeAudioMetadata(m_wave, &why);
		QVERIFY2(probe.valid, qPrintable(why));
		QCOMPARE(probe.sampleRate, 48000);
		QCOMPARE(probe.channels, 2);
		QCOMPARE(probe.frames, qint64(2048));
		QCOMPARE(probe.format, QStringLiteral("wav"));
		QCOMPARE(probe.encoding, QStringLiteral("pcm_16"));
		QCOMPARE(probe.durationSeconds, 2048.0 / 48000.0);
		QCOMPARE(probe.title, QStringLiteral("Kick Drum"));
		// The numeric facts are part of what a query may match on.
		QVERIFY(probe.searchableText().contains(QStringLiteral("48000")));
		QVERIFY(probe.searchableText().contains(QStringLiteral("kick drum")));
	}

	//! A file libsndfile cannot read is a typed result, not an empty success.
	void metadataRefusesWhatItCannotRead()
	{
		QString why;
		const BrowserMetadata probe = probeAudioMetadata(m_dir.path() + "/notes.txt", &why);
		QCOMPARE(probe.valid, false);
		QVERIFY2(!probe.error.isEmpty(), "an unreadable file reported no reason");
		QCOMPARE(probe.error, why);
		QVERIFY(probeAudioMetadata(m_dir.path() + "/absent.wav").valid == false);
	}

	//! The tag store persists to its file: clearing the in-memory store and
	//! reloading proves the write reached the disk, which a same-process read
	//! could not.
	void tagStoreRoundTripsThroughItsFile()
	{
		BrowserTagStore& store = BrowserTagStore::instance();
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("browser-tags.json"));
		QVERIFY(store.openAt(path));
		store.clear();
		QVERIFY(store.save());
		QVERIFY2(QFileInfo::exists(path), "the store was not written");

		QString why;
		QVERIFY2(store.add(m_wave, QStringLiteral("Bass"), &why), qPrintable(why));
		QVERIFY2(store.add(m_wave, QStringLiteral("loop")), "the second tag was refused");
		// Case-insensitively unique: "bass" is already there.
		QCOMPARE(store.add(m_wave, QStringLiteral("bass")), false);
		QCOMPARE(store.entryCount(), 1);
		QCOMPARE(tagListOf(m_wave).size(), 2);
		QCOMPARE(store.allTags().size(), 2);
		QCOMPARE(store.filesWithTag(QStringLiteral("BASS")), 1);

		store.clear();
		QCOMPARE(store.entryCount(), 0);
		QVERIFY2(store.load(), qPrintable(store.lastError()));
		QCOMPARE(tagListOf(m_wave), QStringList({QStringLiteral("Bass"), QStringLiteral("loop")}));

		// Removal drops the entry once its last tag is gone, and removing a tag
		// that is not there is a refusal rather than a no-op success.
		QVERIFY(store.remove(m_wave, QStringLiteral("loop"), &why));
		QCOMPARE(tagListOf(m_wave).size(), 1);
		QVERIFY(store.remove(m_wave, QStringLiteral("Bass"), &why));
		QCOMPARE(store.entryCount(), 0);
		QCOMPARE(store.remove(m_wave, QStringLiteral("Bass"), &why), false);
		QVERIFY(!why.isEmpty());
	}

	//! A store that cannot be parsed is refused loudly: silently starting from an
	//! empty library would destroy the one on disk at the next save.
	void unreadableStoreIsReported()
	{
		BrowserTagStore& store = BrowserTagStore::instance();
		const QString path = QDir(m_dir.path()).filePath(QStringLiteral("broken.json"));
		writeText(path, QStringLiteral("this is not JSON"));
		QCOMPARE(store.openAt(path), false);
		QVERIFY2(!store.lastError().isEmpty(), "a broken store reported no reason");
		QCOMPARE(store.entryCount(), 0);
		QVERIFY(store.openAt(QDir(m_dir.path()).filePath(QStringLiteral("browser-tags.json"))));
	}

	//! The peaks of the fixture: silence everywhere except bucket 10, which
	//! carries both a negative minimum and a positive maximum.
	void peaksLandInTheRightBucket()
	{
		BrowserPeakCache& cache = BrowserPeakCache::instance();
		cache.clear();
		BrowserPeakCache::Answer answer;
		QString why;
		QVERIFY2(cache.peaksFor(m_wave, 32, &answer, &why), qPrintable(why));

		QCOMPARE(answer.cached, false);
		QCOMPARE(answer.buckets, 32);
		QCOMPARE(answer.bucketFrames, qint64(64));
		QCOMPARE(answer.frames, qint64(2048));
		QCOMPARE(answer.channels, 2);
		QCOMPARE(answer.sampleRate, 48000);
		QCOMPARE(answer.peaks.size(), 32);

		const BrowserPeak burst = answer.peaks.at(kBurstBucket32);
		QVERIFY2(qAbs(burst.max - kBurstAmplitude) < 0.001, qPrintable(QString::number(burst.max)));
		QVERIFY2(qAbs(burst.min + kBurstAmplitude) < 0.001, qPrintable(QString::number(burst.min)));
		// The neighbours are the silence the fixture puts around the burst: 64
		// frames before it and 960 after it.
		QCOMPARE(answer.peaks.at(kBurstBucket32 - 1).max, 0.0f);
		QCOMPARE(answer.peaks.at(kBurstBucket32 - 1).min, 0.0f);
		QCOMPARE(answer.peaks.at(kBurstBucket32 + 1).max, 0.0f);
		QCOMPARE(answer.peaks.at(kBurstBucket32 + 1).min, 0.0f);
		QCOMPARE(answer.peaks.at(0).max, 0.0f);
		QCOMPARE(answer.peaks.at(answer.buckets - 1).min, 0.0f);
	}

	//! The cache is observable: the second call is a hit, and a file whose size
	//! changed is a miss whose answer is the new audio. A cache that always
	//! recomputed, or always answered from a stale entry, fails here.
	void theCacheHitsAndIsInvalidated()
	{
		BrowserPeakCache& cache = BrowserPeakCache::instance();
		cache.clear();
		// The counters are cumulative for the process, and an earlier slot has
		// already read this file, so the assertions are on the DELTAS.
		const int missesBefore = cache.missCount();
		const int hitsBefore = cache.hitCount();
		BrowserPeakCache::Answer answer;
		QString why;
		QVERIFY2(cache.peaksFor(m_wave, 32, &answer, &why), qPrintable(why));
		QCOMPARE(answer.cached, false);
		QCOMPARE(cache.missCount() - missesBefore, 1);
		QCOMPARE(cache.hitCount() - hitsBefore, 0);

		QVERIFY2(cache.peaksFor(m_wave, 32, &answer, &why), qPrintable(why));
		QCOMPARE(answer.cached, true);
		QCOMPARE(cache.hitCount() - hitsBefore, 1);
		QCOMPARE(cache.entryCount(), 1);

		// A coarser request is answered from the same entry, by aggregation.
		QVERIFY2(cache.peaksFor(m_wave, 16, &answer, &why), qPrintable(why));
		QCOMPARE(answer.cached, true);
		QCOMPARE(answer.buckets, 16);
		QCOMPARE(answer.bucketFrames, qint64(128));
		QVERIFY(qAbs(answer.peaks.at(kBurstBucket16).max - kBurstAmplitude) < 0.001);
		QCOMPARE(answer.peaks.at(kBurstBucket16 - 1).max, 0.0f);
		QCOMPARE(answer.peaks.at(kBurstBucket16 + 1).max, 0.0f);

		// The file changes underneath the cache (a different length, so the
		// invalidation does not depend on timestamp granularity).
		WaveSpec longer;
		longer.frames = 4096;
		QVERIFY(writeWave(m_wave, longer));
		QVERIFY2(cache.peaksFor(m_wave, 32, &answer, &why), qPrintable(why));
		QCOMPARE(answer.cached, false);
		QCOMPARE(answer.frames, qint64(4096));
		QCOMPARE(answer.bucketFrames, qint64(128));
		QCOMPARE(answer.peaks.at(kBurstBucket32 - 1).max, 0.0f);

		// With twice the frames the same 64-frame burst is exactly bucket 8.
		QVERIFY(qAbs(answer.peaks.at(kBurstBucket32 / 2).max - kBurstAmplitude) < 0.001);
		QCOMPARE(answer.peaks.at(kBurstBucket32 / 2 + 1).max, 0.0f);
		QVERIFY(writeWave(m_wave, WaveSpec()));
	}

	//! A file with no audio in it is a refusal with a reason, never a flat line
	//! that claims to be the audio.
	void peaksAreRefusedForNonAudio()
	{
		BrowserPeakCache::Answer answer;
		QString why;
		QCOMPARE(BrowserPeakCache::instance().peaksFor(m_dir.path() + "/notes.txt", 30, &answer, &why),
			false);
		QVERIFY2(!why.isEmpty(), "a refused peak map carried no reason");
		QCOMPARE(BrowserPeakCache::instance().peaksFor(m_dir.path() + "/absent.wav", 30, &answer, &why),
			false);
		QVERIFY(!why.isEmpty());
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

	//! The paths the browser groups, from the browser's own extension lists.
	void kindsAndRootsAreTheBrowsersOwn()
	{
		QCOMPARE(browserKindForPath(QStringLiteral("/tmp/a.wav")), BrowserKind::Audio);
		QCOMPARE(browserKindForPath(QStringLiteral("/tmp/a.FLAC")), BrowserKind::Audio);
		QCOMPARE(browserKindForPath(QStringLiteral("/tmp/a.xpf")), BrowserKind::Preset);
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

private:
	QTemporaryDir m_dir;
	QString m_wave;
};

QTEST_GUILESS_MAIN(BrowserCatalogTest)
#include "BrowserCatalogTest.moc"
