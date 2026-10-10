/*
 * NeuralAmpWavInputTest.cpp - verifies the NAM harness WAV reader's input contract
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
 */

#include <cstdint>
#include <vector>

#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#define main nam_harness_cli_main
#include "../../../plugins/NeuralAmp/tests/nam_harness.cpp"
#undef main

namespace
{

void appendLittleEndian(QByteArray& bytes, std::uint32_t value, int byteCount)
{
	for (int i = 0; i < byteCount; ++i)
	{
		bytes.append(static_cast<char>((value >> (i * 8)) & 0xff));
	}
}

QByteArray monoPcm16Wav(std::uint32_t sampleRate)
{
	QByteArray wav("RIFF", 4);
	appendLittleEndian(wav, 38, 4);
	wav.append("WAVEfmt ", 8);
	appendLittleEndian(wav, 16, 4);
	appendLittleEndian(wav, 1, 2);
	appendLittleEndian(wav, 1, 2);
	appendLittleEndian(wav, sampleRate, 4);
	appendLittleEndian(wav, sampleRate * 2u, 4);
	appendLittleEndian(wav, 2, 2);
	appendLittleEndian(wav, 16, 2);
	wav.append("data", 4);
	appendLittleEndian(wav, 2, 4);
	appendLittleEndian(wav, 0, 2);
	return wav;
}

} // namespace

class NeuralAmpWavInputTest : public QObject
{
	Q_OBJECT

private slots:
	void zeroSampleRateIsRejected();
	void positiveSampleRateIsAccepted();

private:
	QString writeFixture(std::uint32_t sampleRate);
	QTemporaryDir m_tempDir;
};

QString NeuralAmpWavInputTest::writeFixture(std::uint32_t sampleRate)
{
	const QString path = m_tempDir.filePath(QString::number(sampleRate) + ".wav");
	QFile file(path);
	const QByteArray wav = monoPcm16Wav(sampleRate);
	if (!file.open(QIODevice::WriteOnly) || file.write(wav) != wav.size()) { return {}; }
	return path;
}

void NeuralAmpWavInputTest::zeroSampleRateIsRejected()
{
	QVERIFY(m_tempDir.isValid());
	const QString path = writeFixture(0);
	QVERIFY(!path.isEmpty());
	std::vector<float> samples;
	double sampleRate = 0.0;
	QVERIFY(!readWav(path.toStdString(), samples, &sampleRate));
}

void NeuralAmpWavInputTest::positiveSampleRateIsAccepted()
{
	QVERIFY(m_tempDir.isValid());
	const QString path = writeFixture(44100);
	QVERIFY(!path.isEmpty());
	std::vector<float> samples;
	double sampleRate = 0.0;
	QVERIFY(readWav(path.toStdString(), samples, &sampleRate));
	QCOMPARE(sampleRate, 44100.0);
	QCOMPARE(static_cast<quint64>(samples.size()), quint64{1});
}

QTEST_GUILESS_MAIN(NeuralAmpWavInputTest)
#include "NeuralAmpWavInputTest.moc"
