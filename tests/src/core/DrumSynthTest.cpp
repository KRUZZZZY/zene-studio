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
#include <new>

#include "DrumSynth.h"

using namespace lmms;

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
};

QTEST_APPLESS_MAIN(DrumSynthTest)

#include "DrumSynthTest.moc"
