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

#include <limits>
#include <vector>

#include "Engine.h"
#include "EnvelopeAndLfoParameters.h"
#include "SampleBuffer.h"

using namespace lmms;

namespace
{

bool writeWaveFile(const QString& path, bool rising = true)
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
	for (quint32 i = 0; i < sampleCount; ++i)
	{
		const qint16 value = static_cast<qint16>(rising ? i * 1000 : (sampleCount - i) * 1000);
		out << value;
	}
	return out.status() == QDataStream::Ok;
}

void loadSettings(EnvelopeAndLfoParameters& parameters, bool hasWaveAttribute, const QString& path = {},
			  int lfoWaveShape = -1)
{
	QDomDocument document;
	QDomElement settings = document.createElement(QStringLiteral("elvol"));
	if (hasWaveAttribute) { settings.setAttribute(QStringLiteral("userwavefile"), path); }
	if (lfoWaveShape >= 0) { settings.setAttribute(QStringLiteral("lshp"), QString::number(lfoWaveShape)); }
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
		m_otherWavePath = m_tempDir.filePath(QStringLiteral("custom-lfo-2.wav"));
		QVERIFY(writeWaveFile(m_otherWavePath, false));
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

	/*! The wave the audio path samples must not be freed by a wave that
	 *  replaces it: the audio thread may be sampling it right now. The test
	 *  stands in for the audio thread by holding the published snapshot (the
	 *  test seam holdAudioState()), which is what the audio path holds while
	 *  it fills a level. */
	void replacedUserWaveIsNotFreedWhileTheAudioPathHoldsIt()
	{
		EnvelopeAndLfoParameters parameters(0.0f, nullptr);
		loadSettings(parameters, true, m_wavePath);
		const std::weak_ptr<const SampleBuffer> weakWave = parameters.getLfoUserWave();
		QVERIFY(!weakWave.expired());

		// the audio path holds the published snapshot
		QVERIFY(parameters.holdAudioState());

		// a wave that replaces it must not free the wave that is in use
		loadSettings(parameters, true, m_otherWavePath);

		QVERIFY(!weakWave.expired());
		QVERIFY(parameters.getLfoUserWave() != weakWave.lock());
		parameters.releaseHeldAudioState();
	}

	/*! The audio path samples the wave that was published with the
	 *  parameters, so replacing the wave changes what it renders. */
	void theAudioPathSamplesThePublishedUserWave()
	{
		EnvelopeAndLfoParameters parameters(0.0f, nullptr);
		const int userWave =
			static_cast<int>(EnvelopeAndLfoParameters::LfoShape::UserDefinedWave);
		const auto frames = Engine::audioEngine()->framesPerPeriod();

		loadSettings(parameters, true, m_wavePath, userWave);
		parameters.getLfoAmountModel().setValue(1.0f);
		std::vector<float> first(frames);
		parameters.fillLevel(first.data(), 0, std::numeric_limits<f_cnt_t>::max(), frames);

		loadSettings(parameters, true, m_otherWavePath, userWave);
		parameters.getLfoAmountModel().setValue(1.0f);
		std::vector<float> second(frames);
		parameters.fillLevel(second.data(), 0, std::numeric_limits<f_cnt_t>::max(), frames);

		QVERIFY(first != second);
	}

private:
	QTemporaryDir m_tempDir;
	QString m_wavePath;
	QString m_otherWavePath;
};

QTEST_GUILESS_MAIN(EnvelopeAndLfoParametersTest)
#include "EnvelopeAndLfoParametersTest.moc"
