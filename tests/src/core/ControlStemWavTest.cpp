/*
 * ControlStemWavTest.cpp - the stem WAV writer's header and its refusals.
 *
 * stemWriteWavFile writes the float32 RIFF/WAVE a stem is delivered as. Its
 * byte rate is a 32-bit header field computed from the sample rate, and the
 * rate used to reach that arithmetic unchecked, as an int: a zero or negative
 * rate wrote a header that describes no real stream, and a rate whose byte rate
 * does not fit 32 bits wrapped silently. These tests pin the header of a valid
 * rate, the refusal of an unusable one (nothing is written and an existing file
 * is left as it was), and the exact boundary of what is accepted.
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

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <limits>
#include <vector>

#include "ControlStemSupport.h"
#include "SampleBuffer.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

//! The little-endian 32-bit field at \a offset of \a bytes.
quint32 fieldAt(const QByteArray& bytes, int offset)
{
	const auto byteAt = [&](int i) { return static_cast<quint32>(static_cast<quint8>(bytes.at(offset + i))); };
	return byteAt(0) | (byteAt(1) << 8) | (byteAt(2) << 16) | (byteAt(3) << 24);
}

//! Two frames of audio: the writer's payload is 2 * 8 bytes.
SampleBuffer twoFrames()
{
	static const std::vector<SampleFrame> data(2, SampleFrame(0.25f, -0.25f));
	return SampleBuffer(data.data(), data.size(), 44100);
}

} // namespace

class ControlStemWavTest : public QObject
{
	Q_OBJECT

private slots:
	//! 44100 Hz: the 44-byte header the reference CLI writes, field by field.
	void aValidRateWritesTheReferenceHeader();
	//! Zero and negative rates are not rates: refused with a message, and an existing
	//! stem at the path is left as it was.
	void aZeroOrNegativeRateIsRefusedBeforeAnyWrite();
	//! A byte rate past 32 bits cannot be described: refused, and no file is created.
	void aByteRatePastThirtyTwoBitsIsRefusedBeforeAnyWrite();
	//! The largest rate whose byte rate still fits 32 bits is written, with that byte rate.
	void theLargestRepresentableRateIsWritten();
};

void ControlStemWavTest::aValidRateWritesTheReferenceHeader()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.filePath(QStringLiteral("valid.wav"));

	QString error;
	QVERIFY2(control::stemWriteWavFile(path, twoFrames(), 44100, &error), qPrintable(error));

	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	const QByteArray bytes = file.readAll();
	QCOMPARE(bytes.size(), qsizetype(44 + 2 * 8));

	QCOMPARE(bytes.mid(0, 4), QByteArray("RIFF"));
	QCOMPARE(fieldAt(bytes, 4), quint32(36 + 2 * 8));
	QCOMPARE(bytes.mid(8, 8), QByteArray("WAVEfmt "));
	QCOMPARE(fieldAt(bytes, 16), quint32(16));
	QCOMPARE(static_cast<quint8>(bytes.at(20)), quint8(3)); // IEEE float
	QCOMPARE(static_cast<quint8>(bytes.at(22)), quint8(2)); // stereo
	QCOMPARE(fieldAt(bytes, 24), quint32(44100));
	QCOMPARE(fieldAt(bytes, 28), quint32(44100 * 2 * 4));
	QCOMPARE(static_cast<quint8>(bytes.at(32)), quint8(8)); // block align
	QCOMPARE(static_cast<quint8>(bytes.at(34)), quint8(32)); // bits per sample
	QCOMPARE(bytes.mid(36, 4), QByteArray("data"));
	QCOMPARE(fieldAt(bytes, 40), quint32(2 * 8));
}

void ControlStemWavTest::aZeroOrNegativeRateIsRefusedBeforeAnyWrite()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.filePath(QStringLiteral("existing.wav"));
	const QByteArray previous("the stem from before");
	{
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QCOMPARE(file.write(previous), qint64(previous.size()));
	}

	const SampleBuffer buffer = twoFrames();
	for (const int rate : { 0, -44100, std::numeric_limits<int>::min() })
	{
		QString error;
		QVERIFY(!control::stemWriteWavFile(path, buffer, rate, &error));
		QVERIFY(!error.isEmpty());
	}

	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	QCOMPARE(file.readAll(), previous);
}

void ControlStemWavTest::aByteRatePastThirtyTwoBitsIsRefusedBeforeAnyWrite()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());

	// 536870912 * 8 = 2^32: one past the largest byte rate a 32-bit field holds.
	for (const int rate : { 536870912, std::numeric_limits<int>::max() })
	{
		const QString path = dir.filePath(QStringLiteral("too-fast-%1.wav").arg(rate));
		QString error;
		QVERIFY(!control::stemWriteWavFile(path, twoFrames(), rate, &error));
		QVERIFY(!error.isEmpty());
		QVERIFY2(!QFileInfo::exists(path), qPrintable(path));
	}
}

void ControlStemWavTest::theLargestRepresentableRateIsWritten()
{
	QTemporaryDir dir;
	QVERIFY(dir.isValid());
	const QString path = dir.filePath(QStringLiteral("largest.wav"));

	// 536870911 * 8 = 2^32 - 8, the largest byte rate the field holds.
	const int rate = 536870911;
	QString error;
	QVERIFY2(control::stemWriteWavFile(path, twoFrames(), rate, &error), qPrintable(error));

	QFile file(path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	const QByteArray bytes = file.readAll();
	QCOMPARE(fieldAt(bytes, 24), quint32(rate));
	QCOMPARE(fieldAt(bytes, 28), quint32(4294967288u));
}

QTEST_GUILESS_MAIN(ControlStemWavTest)
#include "ControlStemWavTest.moc"
