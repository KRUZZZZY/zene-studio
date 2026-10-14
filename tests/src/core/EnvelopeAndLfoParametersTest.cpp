/*
 * EnvelopeAndLfoParametersTest.cpp - loading settings clears a prior user LFO waveform
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

#include <QDataStream>
#include <QDomDocument>
#include <QFile>
#include <QTemporaryDir>

#include "Engine.h"
#include "EnvelopeAndLfoParameters.h"
#include "SampleBuffer.h"

using namespace lmms;

namespace
{

bool writeWaveFile(const QString& path)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) { return false; }
	QDataStream out(&file);
	out.setByteOrder(QDataStream::LittleEndian);
	constexpr quint32 sampleCount = 8;
	constexpr quint32 dataBytes = sampleCount * sizeof(qint16);
	out.writeRawData("RIFF", 4);
	out << quint32(36 + dataBytes);
	out.writeRawData("WAVEfmt ", 8);
	out << quint32(16) << quint16(1) << quint16(1) << quint32(44100)
		<< quint32(88200) << quint16(2) << quint16(16);
	out.writeRawData("data", 4);
	out << dataBytes;
	for (quint32 i = 0; i < sampleCount; ++i) { out << static_cast<qint16>(i * 1000); }
	return out.status() == QDataStream::Ok;
}

void loadSettings(EnvelopeAndLfoParameters& parameters, bool hasWaveAttribute, const QString& path = {})
{
	QDomDocument document;
	QDomElement settings = document.createElement(QStringLiteral("elvol"));
	if (hasWaveAttribute) { settings.setAttribute(QStringLiteral("userwavefile"), path); }
	parameters.loadSettings(settings);
}

} // namespace

class EnvelopeAndLfoParametersTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		QVERIFY(m_tempDir.isValid());
		m_wavePath = m_tempDir.filePath(QStringLiteral("custom-lfo.wav"));
		QVERIFY(writeWaveFile(m_wavePath));
	}

	void cleanupTestCase() { Engine::destroy(); }

	void missingWaveAttributeClearsPreviousWave()
	{
		EnvelopeAndLfoParameters parameters(0.0f, nullptr);
		loadSettings(parameters, true, m_wavePath);
		QVERIFY(parameters.getLfoUserWave()->size() > 0);

		loadSettings(parameters, false);
		QVERIFY(parameters.getLfoUserWave() == SampleBuffer::emptyBuffer());
	}

	void emptyWaveAttributeClearsPreviousWave()
	{
		EnvelopeAndLfoParameters parameters(0.0f, nullptr);
		loadSettings(parameters, true, m_wavePath);
		QVERIFY(parameters.getLfoUserWave()->size() > 0);

		loadSettings(parameters, true);
		QVERIFY(parameters.getLfoUserWave() == SampleBuffer::emptyBuffer());
	}

private:
	QTemporaryDir m_tempDir;
	QString m_wavePath;
};

QTEST_GUILESS_MAIN(EnvelopeAndLfoParametersTest)
#include "EnvelopeAndLfoParametersTest.moc"
