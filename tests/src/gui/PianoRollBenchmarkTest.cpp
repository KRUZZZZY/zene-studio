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
#include <QTimer>

#include <algorithm>
#include <cstdlib>
#include <new>
#include <vector>

#include "../core/BenchmarkBaseline.h"
#include "ComboBox.h"
#include "ControlRegistry.h"
#include "NoteCanvas.h"
#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "NoteEditor.h"
#include "Piano.h"
#include "Song.h"
#include "StepRecorderWidget.h"

// StepRecorder's pending notes are private; the lifetime regression below reads them directly.
#define private public
#include "StepRecorder.h"
#include "PianoRoll.h"
#undef private

// ---- pending-step-note lifetime probe -------------------------------------
// The regression needs to know whether one specific heap block (a pending StepNote) is freed,
// so the global operator delete records when the tracked pointer is returned to the heap.
namespace
{

void* s_trackedPendingStepNote = nullptr;
bool s_trackedPendingStepNoteDeleted = false;

void trackedDelete(void* ptr) noexcept
{
	if (ptr != nullptr && ptr == s_trackedPendingStepNote)
	{
		s_trackedPendingStepNoteDeleted = true;
	}
	std::free(ptr);
}

} // namespace

void* operator new(std::size_t size)
{
	if (void* ptr = std::malloc(size != 0 ? size : 1)) { return ptr; }
	throw std::bad_alloc();
}

void* operator new[](std::size_t size)
{
	return ::operator new(size);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept
{
	try { return ::operator new(size); }
	catch (...) { return nullptr; }
}

void* operator new[](std::size_t size, const std::nothrow_t& tag) noexcept
{
	return ::operator new(size, tag);
}

void operator delete(void* ptr) noexcept { trackedDelete(ptr); }
void operator delete[](void* ptr) noexcept { trackedDelete(ptr); }
void operator delete(void* ptr, std::size_t) noexcept { trackedDelete(ptr); }
void operator delete[](void* ptr, std::size_t) noexcept { trackedDelete(ptr); }
void operator delete(void* ptr, const std::nothrow_t&) noexcept { trackedDelete(ptr); }
void operator delete[](void* ptr, const std::nothrow_t&) noexcept { trackedDelete(ptr); }

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

template <typename Window>
double medianFrameMs(Window& window)
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
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);  // the note editor reads the scale context through scale.get_state
	}
	void cleanupTestCase() { Engine::destroy(); }

	// Closing the piano roll while a step note is still pending must free that note.
	void destructionReleasesPendingStepNotes()
	{
		s_trackedPendingStepNote = nullptr;
		s_trackedPendingStepNoteDeleted = false;
		auto* pianoRoll = new PianoRoll();
		pianoRoll->m_stepRecorder.notePressed(Note(TimePos(0), TimePos(0), 60));
		QCOMPARE(pianoRoll->m_stepRecorder.m_curStepNotes.size(), std::size_t{1});
		s_trackedPendingStepNote = pianoRoll->m_stepRecorder.m_curStepNotes.front();

		delete pianoRoll;

		QVERIFY(s_trackedPendingStepNoteDeleted);
		s_trackedPendingStepNote = nullptr;
	}

	void invalidKeysAreSafe()
	{
		Piano piano(nullptr);
		QVERIFY(!Piano::isBlackKey(NumKeys));
		for (const int key : {-1, NumKeys})
		{
			QVERIFY(!piano.isKeyPressed(key));
			QVERIFY(!Piano::isBlackKey(key));
			QVERIFY(!Piano::isWhiteKey(key));
		}
	}

	void invalidKeysAreNotPressed()
	{
		struct GuardedPiano
		{
			Piano piano{nullptr};
			bool guard = true;
		};
		GuardedPiano guarded;

		QVERIFY(!guarded.piano.isKeyPressed(-1));
		QVERIFY(!guarded.piano.isKeyPressed(NumKeys));
	}

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

			// The 0.5.0 note editor (docs/NOTE-EDITOR.md) over the same clip, at its default zoom
			// and at its widest - which is wide enough that a long clip puts thousands of notes on
			// screen at once, the case the classic roll's 12.5% floor could not show.
			NoteEditor editor;
			editor.resize(1600, 900);
			editor.setClip(clip);
			editor.show();
			QVERIFY(QTest::qWaitForWindowExposed(&editor));
			editor.setZoom(0.25);
			const double neMs = medianFrameMs(editor);
			editor.setZoom(NoteGrid::minPixelsPerTick);
			const auto [first, last] = editor.canvas()->visibleRange();
			const double neOutMs = medianFrameMs(editor);
			measured.insert(QStringLiteral("ne:%1").arg(notes), neMs);
			measured.insert(QStringLiteral("ne:%1@widest").arg(notes), neOutMs);
			std::printf("NOTEEDITOR_BENCH notes=%d frame_ms=%.2f widest_ms=%.2f on_screen_widest=%d\n",
				notes, neMs, neOutMs, last - first);
			// The successor's own bar, and the one comparison that holds on any machine: with the
			// same ~1 000 notes on screen (the classic roll's widest view of this fill), the note
			// editor's frame is no slower than the classic roll's. Measured on the development
			// box: 4.0 ms against 13.1 ms (docs/PIANO-ROLL-BENCHMARK.md).
			if (notes == 1000)
			{
				QVERIFY2(neOutMs <= outMs, qPrintable(QStringLiteral("the note editor drew 1000 on-screen notes in "
					"%1 ms, the classic roll in %2 ms").arg(neOutMs).arg(outMs)));
			}
			QVERIFY2(neMs < 5000.0 && neOutMs < 5000.0,
				qPrintable(QStringLiteral("a %1-note note-editor frame took %2 ms (%3 widest)").arg(notes).arg(neMs).arg(neOutMs)));
			editor.hide();
			editor.setClip(nullptr);
			delete track;
		}
		benchtest::compareOrWriteBaseline(measured);
	}

};

QTEST_MAIN(PianoRollBenchmarkTest)
#include "PianoRollBenchmarkTest.moc"
