/*
 * SfzFilePoolTest.cpp - regressions for sfizz's sample file pool.
 *
 * This file is part of Zene Studio, derived from LMMS - https://lmms.io
 * Licensed under the GNU General Public License version 2 or later.
 */

#include <QtTest>

#include <QFile>
#include <QTemporaryDir>

#include <cmath>
#include <initializer_list>

#include "sfizz/FilePool.h"

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

};

QTEST_GUILESS_MAIN(SfzFilePoolTest)
#include "SfzFilePoolTest.moc"
