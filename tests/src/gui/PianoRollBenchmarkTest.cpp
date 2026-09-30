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
 *  1 000, 5 000, 10 000 and 50 000 notes, at the default zoom and at 12.5%, so R8.8 (a successor editor) has a number to beat.
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
#include <QMap>

#include <algorithm>
#include <vector>

#include "../core/BenchmarkBaseline.h"
#include "ComboBox.h"
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

//! The window's horizontal zoom (the toolbar combo box its tooltip names); nullptr if not found.
ComboBoxModel* zoomOf(PianoRollWindow& window)
{
	for (ComboBox* box : window.findChildren<ComboBox*>())
	{
		if (box->toolTip() == PianoRollWindow::tr("Horizontal zooming")) { return box->model(); }
	}
	return nullptr;
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
		QMap<QString, double> measured;
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
			measured.insert(QString::number(notes), ms);
			// Zoomed all the way out (12.5%) every note of a long clip is on screen at once: the
			// case a successor editor has to survive, and the one the default zoom hides.
			ComboBoxModel* zoom = zoomOf(window);
			QVERIFY2(zoom != nullptr, "no horizontal zoom control on the piano-roll window");
			const int defaultZoom = zoom->value();
			zoom->setValue(0);
			const double outMs = medianFrameMs(window);
			zoom->setValue(defaultZoom);
			measured.insert(QStringLiteral("%1@12.5%").arg(notes), outMs);
			std::printf("PIANOROLL_BENCH notes=%d frame_ms=%.2f zoomed_out_ms=%.2f\n", notes, ms, outMs);
			QVERIFY2(ms < 5000.0 && outMs < 5000.0,
				qPrintable(QStringLiteral("a %1-note frame took %2 ms (%3 zoomed out)").arg(notes).arg(ms).arg(outMs)));
			window.hide();
			delete track;
		}
		benchtest::compareOrWriteBaseline(measured);
	}

};

QTEST_MAIN(PianoRollBenchmarkTest)
#include "PianoRollBenchmarkTest.moc"
