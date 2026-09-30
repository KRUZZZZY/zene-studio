/*
 * PianoRollBenchmarkTest.cpp - R7.3: the piano roll's frame time at 1k, 5k, 10k and 50k notes
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

/*! The relief plan's R7.3: frame time, offscreen, for the real PianoRollWindow over clips of
 *  1 000, 5 000, 10 000 and 50 000 notes, so R8.8 (a successor editor) has a number to beat.
 *
 *  A frame is one grab() of the window at 1600 x 900: a full paint of the roll, its keyboard
 *  and its note-property area, which is what a scroll or a zoom costs. Each size is the MEDIAN
 *  of five frames after one warm-up, printed as a PIANOROLL_BENCH line.
 *
 *  THE GATE, and why it is opt-in. The plan asks for a regression gate at +20%, and a timing
 *  is only comparable on the machine that took it: the hosted runners vary by more than that
 *  between runs. So the test always MEASURES and always asserts a sanity bound (a frame under
 *  five seconds - a hang, not a slowdown), and it compares against a baseline only when
 *  ZENE_BENCH_BASELINE names a file of "notes<TAB>ms" lines taken on the same machine; with
 *  ZENE_BENCH_WRITE it writes that file instead. docs record this box's numbers.
 */

#include <QtTest>

#include <QElapsedTimer>
#include <QFile>
#include <QMap>
#include <QTextStream>

#include <algorithm>
#include <vector>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "PianoRoll.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! @a count notes: sixteenth notes walking up and down four octaves, bar after bar.
void fill(MidiClip* clip, int count)
{
	for (int i = 0; i < count; ++i)
	{
		const int key = 36 + (i * 7) % 48;
		clip->addNote(Note(TimePos(12), TimePos(i * 12), key), false);
	}
	clip->rearrangeAllNotes();
}

double medianFrameMs(PianoRollWindow& window)
{
	window.grab();  // warm-up: caches, first layout
	std::vector<double> frames;
	for (int i = 0; i < 5; ++i)
	{
		QElapsedTimer timer;
		timer.start();
		window.grab();
		frames.push_back(static_cast<double>(timer.nsecsElapsed()) / 1.0e6);
	}
	std::sort(frames.begin(), frames.end());
	return frames[2];
}

} // namespace

class PianoRollBenchmarkTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { Engine::init(true); }
	void cleanupTestCase() { Engine::destroy(); }

	void frameTimeAtFourSizes()
	{
		QMap<int, double> measured;
		for (const int notes : {1000, 5000, 10000, 50000})
		{
			auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
			QVERIFY(track != nullptr);
			auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
			QVERIFY(clip != nullptr);
			fill(clip, notes);
			QCOMPARE(static_cast<int>(clip->notes().size()), notes);

			PianoRollWindow window;
			window.resize(1600, 900);
			window.setCurrentMidiClip(clip);
			window.show();
			QVERIFY(QTest::qWaitForWindowExposed(&window));
			const double ms = medianFrameMs(window);
			measured.insert(notes, ms);
			std::printf("PIANOROLL_BENCH notes=%d frame_ms=%.2f\n", notes, ms);
			QVERIFY2(ms < 5000.0, qPrintable(QStringLiteral("a %1-note frame took %2 ms").arg(notes).arg(ms)));
			window.hide();
			delete track;
		}
		compareOrWriteBaseline(measured);
	}

private:
	static void compareOrWriteBaseline(const QMap<int, double>& measured)
	{
		const QString write = qEnvironmentVariable("ZENE_BENCH_WRITE");
		if (!write.isEmpty())
		{
			QFile file(write);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Text));
			QTextStream out(&file);
			for (auto it = measured.begin(); it != measured.end(); ++it) { out << it.key() << '\t' << it.value() << '\n'; }
			return;
		}
		const QString baseline = qEnvironmentVariable("ZENE_BENCH_BASELINE");
		if (baseline.isEmpty()) { return; }
		QFile file(baseline);
		QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text), qPrintable(baseline));
		QTextStream in(&file);
		while (!in.atEnd())
		{
			const QStringList fields = in.readLine().split(QLatin1Char('\t'));
			if (fields.size() != 2) { continue; }
			const int notes = fields[0].toInt();
			const double reference = fields[1].toDouble();
			if (!measured.contains(notes)) { continue; }
			QVERIFY2(measured[notes] <= reference * 1.2, qPrintable(QStringLiteral(
				"%1 notes: %2 ms against a %3 ms baseline - more than 20% slower")
				.arg(notes).arg(measured[notes]).arg(reference)));
		}
	}
};

QTEST_MAIN(PianoRollBenchmarkTest)
#include "PianoRollBenchmarkTest.moc"
