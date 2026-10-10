/*
 * SampleBufferResampleTest.cpp - a whole buffer at another rate keeps its length and pitch
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

#include <QFile>
#include <QProcess>
#include <QTemporaryDir>
#include <cmath>
#include <cstdint>
#include <numbers>
#include <vector>

#include "DrumSynth.h"
#include "SampleBufferResample.h"

using namespace lmms;

//! Engine audit E27: stem separation resamples a source to 44.1 kHz instead of refusing it, so
//! the resample must keep the audio's length in time and its pitch.
class SampleBufferResampleTest : public QObject
{
	Q_OBJECT

private slots:
	void aToneKeepsItsLengthAndPitch()
	{
		constexpr int InRate = 48000;
		constexpr int OutRate = 44100;
		constexpr double Hz = 1000.0;
		std::vector<SampleFrame> data(InRate / 2);  // half a second
		for (std::size_t i = 0; i < data.size(); ++i)
		{
			const auto v = static_cast<float>(std::sin(2.0 * std::numbers::pi * Hz * static_cast<double>(i) / InRate));
			data[i] = SampleFrame(v, v);
		}
		const auto input = std::make_shared<const SampleBuffer>(std::move(data), InRate);
		const auto output = resampleBuffer(input, OutRate);
		QCOMPARE(static_cast<int>(output->sampleRate()), OutRate);
		QVERIFY(std::abs(static_cast<int>(output->size()) - OutRate / 2) <= 1);
		// Pitch: count rising zero crossings over the settled middle - 1000 Hz for 0.25 s is 250.
		int crossings = 0;
		const std::size_t from = output->size() / 4;
		const std::size_t to = from + OutRate / 4;
		for (std::size_t i = from + 1; i < to; ++i)
		{
			if (output->data()[i - 1][0] < 0.0f && output->data()[i][0] >= 0.0f) { ++crossings; }
		}
		QVERIFY2(std::abs(crossings - 250) <= 1, qPrintable(QString::number(crossings)));
	}

	void aBufferAlreadyAtTheRateIsReturnedAsIs()
	{
		const auto input = std::make_shared<const SampleBuffer>(std::vector<SampleFrame>(100), 44100);
		QCOMPARE(resampleBuffer(input, 44100).get(), input.get());
	}

	void negativeDrumSynthDistortionRateDoesNotHang()
	{
		QTemporaryDir tempDir;
		QVERIFY(tempDir.isValid());
		QFile file(tempDir.filePath(QStringLiteral("invalid-rate.ds")));
		QVERIFY(file.open(QIODevice::WriteOnly));
		const QByteArray fixture = R"([General]
Version=DrumSynth v2.0
Stretch=100
[Distortion]
On=1
Bits=0
Rate=-1
)";
		QCOMPARE(file.write(fixture), static_cast<qint64>(fixture.size()));
		file.close();

		QProcess renderer;
		renderer.start(QCoreApplication::applicationFilePath(),
			{QStringLiteral("--render-drumsynth"), file.fileName()});
		QVERIFY(renderer.waitForStarted(5000));
		const bool completed = renderer.waitForFinished(3000);
		if (!completed)
		{
			renderer.kill();
			QVERIFY(renderer.waitForFinished(5000));
		}
		QVERIFY2(completed, "DrumSynth rendering stalled on Rate=-1");
		QCOMPARE(renderer.exitStatus(), QProcess::NormalExit);
		QCOMPARE(renderer.exitCode(), 0);
	}
};

int main(int argc, char* argv[])
{
	QCoreApplication app(argc, argv);
	if (app.arguments().size() == 3 && app.arguments().at(1) == QStringLiteral("--render-drumsynth"))
	{
		std::int16_t* wave = nullptr;
		DrumSynth drumSynth;
		const int samples = drumSynth.GetDSFileSamples(app.arguments().at(2), wave, 1, 44100);
		delete[] wave;
		return samples > 0 ? 0 : 1;
	}

	SampleBufferResampleTest test;
	return QTest::qExec(&test, argc, argv);
}
#include "SampleBufferResampleTest.moc"
