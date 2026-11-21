/*
 * SfzFilePoolTest.cpp - regressions for sfizz's sample file pool.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include <atomic>
#include <cmath>
#include <future>
#include <initializer_list>

#include "sfizz/FilePool.h"

namespace sfz
{

struct FilePoolTestAccess
{
	static FileData* addMappedFile(FilePool& pool)
	{
		auto [it, inserted] = pool.preloadedFiles.try_emplace(FileId("queued.wav"));
		return inserted ? &it->second : nullptr;
	}

	static bool queueMappedFile(FilePool& pool, FileData* data)
	{
		return pool.filesToLoad->try_push(FilePool::QueuedFileData{{}, data});
	}

	static void addDeferredLoad(FilePool& pool, FileData* data, std::atomic<bool>& completed,
		std::atomic<bool>& sawLiveData)
	{
		pool.loadingJobs.push_back(std::async(std::launch::deferred, [data, &completed, &sawLiveData]
		{
			sawLiveData.store(data->status.load() == FileData::Status::Invalid);
			completed.store(true);
		}));
	}

	static bool hasQueuedFile(FilePool& pool)
	{
		FilePool::QueuedFileData queued;
		return pool.filesToLoad->try_pop(queued);
	}
};

} // namespace sfz

namespace
{

void appendLittleEndian(QByteArray& bytes, quint64 value, int count)
{
	for (int shift = 0; shift < count * 8; shift += 8)
	{
		bytes.append(static_cast<char>((value >> shift) & 0xff));
	}
}

bool writeMonoWave(const QString& path, std::initializer_list<qint16> samples)
{
	QByteArray audio;
	for (const qint16 sample : samples)
	{
		appendLittleEndian(audio, static_cast<quint16>(sample), 2);
	}

	QByteArray wave("RIFF", 4);
	appendLittleEndian(wave, 36 + static_cast<quint32>(audio.size()), 4);
	wave.append("WAVEfmt ", 8);
	appendLittleEndian(wave, 16, 4);
	appendLittleEndian(wave, 1, 2); // PCM
	appendLittleEndian(wave, 1, 2); // mono
	appendLittleEndian(wave, 44100, 4);
	appendLittleEndian(wave, 88200, 4);
	appendLittleEndian(wave, 2, 2); // block alignment
	appendLittleEndian(wave, 16, 2);
	wave.append("data", 4);
	appendLittleEndian(wave, static_cast<quint32>(audio.size()), 4);
	wave.append(audio);

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	const bool written = file.write(wave) == wave.size();
	file.close();
	return written;
}

} // namespace

class SfzFilePoolTest : public QObject
{
	Q_OBJECT

private slots:
	void ramLoadingReadsInclusiveEndFrame()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		QVERIFY(writeMonoWave(directory.filePath(QStringLiteral("tone.wav")), {0, 8192, 16384}));

		sfz::FilePool pool;
		pool.setRootDirectory(fs::path(directory.path().toStdString()));
		pool.setRamLoading(false);
		const sfz::FileId fileId("tone.wav");
		QVERIFY(pool.preloadFile(fileId, 0));

		pool.setRamLoading(true);
		auto data = pool.getFilePromise(std::make_shared<sfz::FileId>(fileId));
		QVERIFY(data);
		QCOMPARE(data->preloadedData.getNumFrames(), size_t(3));
		QVERIFY(std::abs(data->preloadedData.getSpan(0)[2] - 0.5f) < 0.001f);
	}

	void emptySampleEndIsNotUnderflowed()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		QVERIFY(writeMonoWave(directory.filePath(QStringLiteral("empty.wav")), {}));

		sfz::FilePool pool;
		pool.setRootDirectory(fs::path(directory.path().toStdString()));
		const auto information = pool.getFileInformation(sfz::FileId("empty.wav"));
		QVERIFY(information);
		QCOMPARE(information->end, int64_t(0));
		QVERIFY(!pool.getFileInformation(sfz::FileId("missing.wav")));
	}

	void clearDrainsQueuedFilePointers()
	{
		sfz::FilePool pool;
		sfz::FileData* queuedData = sfz::FilePoolTestAccess::addMappedFile(pool);
		QVERIFY(queuedData);
		QVERIFY(sfz::FilePoolTestAccess::queueMappedFile(pool, queuedData));

		pool.clear();

		const bool staleQueueEntryRemained = sfz::FilePoolTestAccess::hasQueuedFile(pool);
		QVERIFY(!staleQueueEntryRemained);
		QCOMPARE(pool.getNumPreloadedSamples(), size_t(0));
	}

	void clearWaitsForLoadingJobsBeforeFreeingFileData()
	{
		sfz::FilePool pool;
		sfz::FileData* loadingData = sfz::FilePoolTestAccess::addMappedFile(pool);
		QVERIFY(loadingData);
		std::atomic<bool> deferredLoadCompleted { false };
		std::atomic<bool> deferredLoadSawLiveData { false };
		sfz::FilePoolTestAccess::addDeferredLoad(
			pool, loadingData, deferredLoadCompleted, deferredLoadSawLiveData);

		pool.clear();

		QVERIFY(deferredLoadCompleted.load());
		QVERIFY(deferredLoadSawLiveData.load());
		QCOMPARE(pool.getNumPreloadedSamples(), size_t(0));
	}

};

QTEST_GUILESS_MAIN(SfzFilePoolTest)
#include "SfzFilePoolTest.moc"
