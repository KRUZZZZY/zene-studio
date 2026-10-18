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
#include <memory>
#include <vector>

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

} // namespace

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
};

QTEST_APPLESS_MAIN(DrumSynthTest)

#include "DrumSynthTest.moc"
