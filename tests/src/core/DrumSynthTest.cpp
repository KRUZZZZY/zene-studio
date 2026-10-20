/*
 * DrumSynthTest.cpp - malformed .ds envelope parsing regression coverage.
 *
 * This file is part of Zene Studio (an LMMS-derived product).
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
#include <QTemporaryDir>
#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>
#include <limits>
#include <new>

#include "DrumSynth.h"

using namespace lmms;

namespace
{

// "0,9 4,9 8,9 ..." with the given number of points.
QString envelopeText(int points)
{
	QString result;
	for (int point = 0; point < points; ++point)
	{
		if (point > 0) { result += QLatin1Char(' '); }
		result += QStringLiteral("%1,9").arg(point * 4);
	}
	return result;
}

bool writeToneFixture(const QString& path, const QString& toneEnvelope)
{
	const auto contents = QStringLiteral(
		"[General]\n"
		"Version=DrumSynth v2.0\n"
		"[Tone]\n"
		"On=1\n"
		"Level=100\n"
		"Envelope=%1\n"
		"F1=440\n"
		"F2=220\n").arg(toneEnvelope).toUtf8();
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

std::vector<int16_t> render(const QString& path)
{
	DrumSynth synth;
	int16_t* rawSamples = nullptr;
	const int frames = synth.GetDSFileSamples(path, rawSamples, 1, 44100);
	const auto samples = std::unique_ptr<int16_t[]>{rawSamples};
	if (frames <= 0 || !samples) { return {}; }
	return {samples.get(), samples.get() + frames};
}

// A tone whose envelope ends at time zero: the tone has no positive duration to sweep over.
bool writeZeroDurationToneFixture(const QString& path, int droop)
{
	const auto contents = QStringLiteral(
		"[General]\n"
		"Version=DrumSynth v2.0\n"
		"Level=0\n"
		"[Tone]\n"
		"On=1\n"
		"Level=100\n"
		"F1=440\n"
		"F2=880\n"
		"Phase=0\n"
		"Droop=%1\n"
		"Envelope=0,100\n").arg(droop).toUtf8();
	QFile file(path);
	return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

} // namespace

namespace lmms {
extern float envpts[8][3][32];
extern float timestretch;
extern int chkOn[8];
}

class DrumSynthTest : public QObject
{
	Q_OBJECT

private slots:
	void rejectsOverlongEnvelopeLevel()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());

		const QString path = directory.filePath(QStringLiteral("overlong-envelope.ds"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QVERIFY(file.write(R"([General]
Version=DrumSynth v1.0

[Tone]
On=1
Level=128
F1=440
F2=440
Phase=90
Envelope=0,00000001 100,0
)") > 0);
		file.close();

		DrumSynth synth;
		int16_t* samples = nullptr;
		const int sampleCount = synth.GetDSFileSamples(path, samples, 1, 44100);
		const bool rendered = sampleCount > 0 && samples != nullptr;
		const bool silent = rendered
			&& std::all_of(samples, samples + sampleCount,
				[](int16_t sample) { return sample == 0; });
		delete[] samples;

		QVERIFY2(rendered, "the valid file around the malformed envelope must still render");
		QVERIFY2(silent, "an eight-character envelope level must be rejected safely");
	}

	void ignoresEnvelopePointsBeyondSupportedCapacity()
	{
		// envpts holds 32 points per envelope and needs one slot for the end sentinel, so the 32nd
		// point must be dropped rather than written past the array and into the first level.
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString prefixPath = directory.filePath(QStringLiteral("prefix.ds"));
		const QString oversizedPath = directory.filePath(QStringLiteral("oversized.ds"));
		QVERIFY(writeToneFixture(prefixPath, envelopeText(31)));
		QVERIFY(writeToneFixture(oversizedPath, envelopeText(32)));

		const auto prefixSamples = render(prefixPath);
		const auto oversizedSamples = render(oversizedPath);
		QVERIFY(!prefixSamples.empty());
		QCOMPARE(oversizedSamples.size(), prefixSamples.size());
		QVERIFY2(oversizedSamples == prefixSamples,
			"an envelope point beyond the supported capacity changed the rendered prefix");
	}

	void zeroDurationToneUsesItsStartingFrequency_data()
	{
		QTest::addColumn<int>("droop");
		QTest::newRow("static") << 0;
		QTest::newRow("with-droop") << 40;
	}

	void zeroDurationToneUsesItsStartingFrequency()
	{
		QFETCH(int, droop);
		QTemporaryDir directory;
		QVERIFY(directory.isValid());
		const QString path = directory.filePath(QStringLiteral("zero-duration.ds"));
		QVERIFY(writeZeroDurationToneFixture(path, droop));

		const auto samples = render(path);
		QCOMPARE(samples.size(), std::size_t{2400});

		// A tone without a positive envelope duration has no sweep to run, so it stays at F1.
		const float phaseIncrement = 2.f * std::numbers::pi_v<float> * 440.f / 44100.f;
		const auto expected = static_cast<int16_t>(10000.f * std::sin(phaseIncrement));
		QCOMPARE(samples.front(), expected);
	}

	void refusesAnEnvelopeWhoseRoundedLengthOverflows()
	{
		QTemporaryDir tempDir;
		QVERIFY(tempDir.isValid());
		QFile file(tempDir.filePath("oversized.ds"));
		QVERIFY(file.open(QIODevice::WriteOnly));
		const QByteArray content = "[General]\nVersion=DrumSynth v2.0\nStretch=100\n\n"
			"[Tone]\nOn=1\nEnvelope=0,0 3e9,0\n";
		QCOMPARE(file.write(content), static_cast<qint64>(content.size()));
		file.close();

		int16_t* samples = nullptr;
		int frames = -1;
		bool allocationFailed = false;
		try
		{
			DrumSynth synth;
			frames = synth.GetDSFileSamples(file.fileName(), samples, 2, 44100);
		}
		catch (const std::bad_alloc&)
		{
			allocationFailed = true;
		}
		delete[] samples;

		QVERIFY2(!allocationFailed, "An overflowing duration must be rejected before allocation");
		QCOMPARE(frames, 0);
		QVERIFY(samples == nullptr);
	}

	void mapsAFractionalStepAboveMaxToTheSafeMaximumLength()
	{
		// globals declared at file scope below

		// Reset envelope state, then install a duration whose step count is
		// fractional just above maxSteps; truncation must yield maxSteps.
		for (int e = 0; e < 8; e++)
		{
			chkOn[e] = 0;
			for (int t = 0; t < 3; t++)
			{
				for (int p = 0; p < 32; p++)
				{
					envpts[e][t][p] = -1.f;
				}
			}
		}
		constexpr int blockLength = 1200;
		constexpr int baseLength = 2 * blockLength;
		constexpr int maxSteps = (std::numeric_limits<int>::max() - baseLength) / blockLength;
		const float boundarySteps = static_cast<float>(maxSteps) + 0.5f;
		envpts[1][0][0] = boundarySteps * blockLength;
		envpts[1][0][1] = -1.f;
		chkOn[0] = 1;
		timestretch = 1.f;

		DrumSynth synth;
		const int length = synth.LongestEnv();
		const int expectedLength = baseLength + (blockLength * maxSteps);
		QCOMPARE(length, expectedLength);
	}
};

QTEST_APPLESS_MAIN(DrumSynthTest)

#include "DrumSynthTest.moc"
