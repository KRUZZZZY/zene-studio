/*
 * UndoBoundsTest.cpp - the acceptance tests of the BOUNDED, COALESCING undo
 *                     (Zene Studio; SPEC A16's "undo depth and drag coalescing"
 *                     obligation, task #623; docs/UNDO-BOUNDS.md).
 *
 * What each test is for, in the order the file declares them:
 *
 *   1. undoDepthIsDeclaredAndBounded
 *        the depth is READABLE and the count cap is SETTABLE through the socket,
 *        and past the cap the oldest steps are evicted rather than kept.
 *   2. aTwoHundredCallDragIsOneUndoStep
 *        the headline defect: 200 clip.move calls used to cost 200 undo steps.
 *        Measured on the journal AND on the transaction record, then proved by
 *        undoing once and reading the clip's position back.
 *   3. coalescingOffReproducesTheOldBehaviour
 *        the NEGATIVE CONTROL, in the same process: with the window at 0 the same
 *        20-move run costs 20 steps, which is exactly what the tree did before
 *        this change. A rule that cannot be switched off cannot be shown to be
 *        the thing that changed the number.
 *   4. aPauseADifferentTargetAndAnotherCommandAllBreakTheRun
 *        the window is a window, not "merge everything forever": a pause longer
 *        than the window, a different target and an unrelated command each open a
 *        new step.
 *   5. theByteBudgetEvictsAndIsReported
 *        the MEMORY bound, measured rather than asserted: the retained bytes
 *        stay under a budget set from a step's own measured size, and what was
 *        evicted is reported.
 *   6. anEvictedStepIsNeverSilentlyUndone
 *        the honesty half: once a bound has evicted the step a record describes,
 *        control.undo REFUSES (typed) instead of unwinding an older edit, and the
 *        stack is left exactly as it was.
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

#include <QDomDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QThread>

#include "AudioEngine.h"
#include "ControlRegistry.h"
#include "ControlReversibility.h"
#include "ControlUndoCoalescing.h"
#include "Engine.h"
#include "MidiPort.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "ReversibilityTestSupport.h"

using namespace lmms;
using namespace revtest;

class UndoBoundsTest : public QObject
{
	Q_OBJECT

private:
	//! The journal the assertions are made on.
	static ProjectJournal* journal() { return Engine::projectJournal(); }

	static int depth() { return journal() == nullptr ? -1 : journal()->undoDepth(); }

	//! How many records control.transactions retains - one per undo step, so a
	//! coalesced run must NOT add one per call.
	static int recordCount()
	{
		return run(QStringLiteral("control.transactions"))
			.result.value(QStringLiteral("transactions")).toArray().size();
	}

	static QJsonObject depthReport() { return run(QStringLiteral("control.undo_depth")).result; }

	static int capSteps() { return depthReport().value(QStringLiteral("cap_steps")).toInt(); }

	static qint64 capBytes() { return depthReport().value(QStringLiteral("cap_bytes")).toDouble(); }

	static qint64 retainedBytes()
	{
		return depthReport().value(QStringLiteral("retained_bytes")).toDouble();
	}

	static bool bounded() { return depthReport().value(QStringLiteral("bounded")).toBool(); }

	//! Sets one cap through the CONTROL SURFACE (never by calling the engine
	//! directly): the whole point of the slice is that a client can do this.
	static void setCap(const QString& key, double value)
	{
		const ControlResult set = run(QStringLiteral("control.set_undo_depth"),
			QJsonObject{{key, value}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
	}

	static void setWindow(int ms)
	{
		const ControlResult set = run(QStringLiteral("control.set_undo_coalescing"),
			QJsonObject{{QStringLiteral("window_ms"), ms}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
	}

	//! The caps the release ships with, restored between tests so one test's
	//! bound cannot explain the next test's number.
	static void shipCaps()
	{
		setCap(QStringLiteral("steps"), ProjectJournal::MAX_UNDO_STATES);
		setCap(QStringLiteral("bytes"), static_cast<double>(ProjectJournal::DefaultMaxUndoBytes));
		setWindow(control::UndoCoalesceWindowMs);
	}

	//! A clip on a fresh track, at a known start position.
	static QString clipAt(int position)
	{
		const QString track = addTrack();
		const ControlResult clip = run(QStringLiteral("clip.add"),
			QJsonObject{{QStringLiteral("track"), track}, {QStringLiteral("position"), position},
				{QStringLiteral("length"), 192}});
		return clip.ok ? clip.result.value(QStringLiteral("clip")).toString() : QString();
	}

	static void tempo(int bpm)
	{
		const ControlResult set = run(QStringLiteral("transport.set_tempo"),
			QJsonObject{{QStringLiteral("bpm"), bpm}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
	}

	//! \a count steps that CANNOT coalesce: a command with no coalescing
	//! declaration, each call on a different value.
	static void distinctSteps(int count, int from)
	{
		for (int i = 0; i < count; ++i) { tempo(from + i); }
	}

private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		shipCaps();
		ControlRegistry::setReady(false);
		Engine::destroy();
	}


	//! The depth is DECLARED (readable through the socket) and BOUNDED (a cap
	//! that is enforced, with eviction reported rather than hidden).
	void undoDepthIsDeclaredAndBounded()
	{
		shipCaps();

		const QJsonObject declared = depthReport();
		QVERIFY2(!declared.isEmpty(), "control.undo_depth answered nothing");
		for (const QString& key : {QStringLiteral("depth"), QStringLiteral("redo_depth"),
				 QStringLiteral("cap_steps"), QStringLiteral("cap_bytes"),
				 QStringLiteral("retained_bytes"), QStringLiteral("evicted"),
				 QStringLiteral("coalescing")})
		{
			QVERIFY2(declared.contains(key), qPrintable(key + " is missing from control.undo_depth"));
		}
		QCOMPARE(declared.value(QStringLiteral("cap_steps")).toInt(), ProjectJournal::MAX_UNDO_STATES);

		// The rule names the commands it applies to, read from the contract
		// table rather than restated by this command.
		const QJsonArray coalescing = declared.value(QStringLiteral("coalescing"))
			.toObject().value(QStringLiteral("commands")).toArray();
		QVERIFY2(coalescing.contains(QJsonValue(QStringLiteral("clip.move"))),
			"the coalescing declaration for clip.move is not reachable through the socket");

		// A five-step cap and twelve distinct steps: the oldest are evicted, and
		// the fact that they were is REPORTED.
		setCap(QStringLiteral("steps"), 5);
		QCOMPARE(capSteps(), 5);
		distinctSteps(12, 100);

		QVERIFY2(depth() <= 5, qPrintable(QStringLiteral("depth %1 exceeds the cap of 5").arg(depth())));
		QVERIFY2(depthReport().value(QStringLiteral("evicted")).toInt() > 0,
			"the count cap evicted nothing although twelve steps ran against a cap of five");
		QVERIFY(bounded());
	}


	//! THE DEFECT. 200 moves of one clip used to be 200 undo steps; under the
	//! coalescing rule they are ONE, on the journal and on the record, and one
	//! control.undo takes the whole drag back.
	void aTwoHundredCallDragIsOneUndoStep()
	{
		shipCaps();
		const QString clip = clipAt(0);
		QVERIFY2(!clip.isEmpty(), "clip.add failed, so the drag cannot be measured");
		QCOMPARE(clipPosition(clip), 0);

		const int depthBefore = depth();
		const int recordsBefore = recordCount();

		for (int i = 0; i < 200; ++i)
		{
			const ControlResult moved = run(QStringLiteral("clip.move"),
				QJsonObject{{QStringLiteral("clip"), clip},
					{QStringLiteral("position"), 10 + i}});
			QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		}
		QCOMPARE(clipPosition(clip), 209);

		// ONE step on the engine's own stack (the one Ctrl+Z unwinds) ...
		QCOMPARE(depth(), depthBefore + 1);
		// ... and one RECORD for it, covering every call of the run.
		QCOMPARE(recordCount(), recordsBefore + 1);
		const QJsonObject record = stateOf(QStringLiteral("clip.move"));
		QCOMPARE(record.value(QStringLiteral("commands")).toInt(), 200);
		QCOMPARE(record.value(QStringLiteral("reversible")).toBool(), true);

		// And one undo really does take the whole drag back, to the pre-drag
		// position - not to the second-to-last frame of it.
		REV_UNDO_OR_FAIL();
		QCOMPARE(clipPosition(clip), 0);
		QCOMPARE(depth(), depthBefore);
	}


	//! The NEGATIVE CONTROL: the same gesture with grouping switched off costs
	//! one step per call - the number this tree produced before the change.
	void coalescingOffReproducesTheOldBehaviour()
	{
		shipCaps();
		const QString clip = clipAt(0);
		QVERIFY2(!clip.isEmpty(), "clip.add failed");

		setWindow(0);
		const int depthBefore = depth();
		for (int i = 0; i < 20; ++i)
		{
			QVERIFY(run(QStringLiteral("clip.move"),
				QJsonObject{{QStringLiteral("clip"), clip},
					{QStringLiteral("position"), 5 + i}}).ok);
		}
		QCOMPARE(depth() - depthBefore, 20);

		// ... and back on, the same 20 moves are ONE step. The difference between
		// these two numbers IS the change this test exists to pin.
		setWindow(control::UndoCoalesceWindowMs);
		const int beforeOn = depth();
		for (int i = 0; i < 20; ++i)
		{
			QVERIFY(run(QStringLiteral("clip.move"),
				QJsonObject{{QStringLiteral("clip"), clip},
					{QStringLiteral("position"), 100 + i}}).ok);
		}
		QCOMPARE(depth() - beforeOn, 1);
	}


	//! The run is broken by all three things that should break it: a pause longer
	//! than the window, a different target, and another command in between.
	void aPauseADifferentTargetAndAnotherCommandAllBreakTheRun()
	{
		shipCaps();
		const QString first = clipAt(0);
		const QString second = clipAt(300);
		QVERIFY2(!first.isEmpty() && !second.isEmpty(), "clip.add failed");

		// (a) three moves, a pause longer than the window, three more: two steps.
		const int start = depth();
		for (int i = 0; i < 3; ++i)
		{
			QVERIFY(run(QStringLiteral("clip.move"),
				QJsonObject{{QStringLiteral("clip"), first},
					{QStringLiteral("position"), i}}).ok);
		}
		QThread::msleep(static_cast<unsigned long>(control::UndoCoalesceWindowMs + 100));
		for (int i = 0; i < 3; ++i)
		{
			QVERIFY(run(QStringLiteral("clip.move"),
				QJsonObject{{QStringLiteral("clip"), first},
					{QStringLiteral("position"), 20 + i}}).ok);
		}
		QCOMPARE(depth() - start, 2);

		// (b) a DIFFERENT clip is a different target, even back to back.
		const int afterPause = depth();
		for (int i = 0; i < 3; ++i)
		{
			QVERIFY(run(QStringLiteral("clip.move"),
				QJsonObject{{QStringLiteral("clip"), second},
					{QStringLiteral("position"), i}}).ok);
		}
		QCOMPARE(depth() - afterPause, 1);

		// (c) another command between two runs of the same target ends the run:
		// the third move is NOT merged into the first two.
		const int afterSecond = depth();
		QVERIFY(run(QStringLiteral("clip.move"),
			QJsonObject{{QStringLiteral("clip"), first},
				{QStringLiteral("position"), 100}}).ok);
		QVERIFY(run(QStringLiteral("clip.resize"),
			QJsonObject{{QStringLiteral("clip"), first}, {QStringLiteral("length"), 96}}).ok);
		QVERIFY(run(QStringLiteral("clip.move"),
			QJsonObject{{QStringLiteral("clip"), first},
				{QStringLiteral("position"), 200}}).ok);
		QCOMPARE(depth() - afterSecond, 3);
	}


	//! The MEMORY bound, measured: the budget is set from a step's own measured
	//! size, the retained bytes stay under it, and the evictions are reported.
	void theByteBudgetEvictsAndIsReported()
	{
		shipCaps();

		// ONE step, measured rather than assumed: the delta the step added to the
		// stack's retained bytes. Steps are not all the same size (a command may
		// checkpoint one object or several), so a budget derived from a step this
		// engine actually took is the only honest one.
		const int before = depth();
		const qint64 bytesBefore = retainedBytes();
		tempo(90);
		QCOMPARE(depth(), before + 1);
		const qint64 oneStep = retainedBytes() - bytesBefore;
		QVERIFY2(oneStep > 0, "a Song checkpoint measured as zero bytes");

		// A budget that holds about three of them.
		setCap(QStringLiteral("bytes"), static_cast<double>(oneStep * 3));
		const qint64 budget = capBytes();
		QVERIFY2(budget > 0, "control.set_undo_depth refused a budget of three measured steps");
		const int evictedBefore = depthReport().value(QStringLiteral("evicted")).toInt();

		distinctSteps(20, 120);

		// The bound is measured on the two quantities a memory bound is: the
		// retained bytes stay inside the budget, and the stack did not grow by the
		// twenty steps that ran against it.
		QVERIFY2(retainedBytes() <= budget,
			qPrintable(QStringLiteral("retained %1 bytes against a budget of %2")
				.arg(retainedBytes()).arg(budget)));
		QVERIFY2(depth() - before < 20,
			qPrintable(QStringLiteral("the byte budget did not bound the stack: depth %1 after 20 steps")
				.arg(depth())));
		QVERIFY2(depthReport().value(QStringLiteral("evicted")).toInt() - evictedBefore >= 12,
			qPrintable(QStringLiteral("only %1 of the 20 steps were evicted")
				.arg(depthReport().value(QStringLiteral("evicted")).toInt() - evictedBefore)));
		QVERIFY(bounded());
	}


	//! The honesty half: a record whose step a bound evicted must REFUSE, typed,
	//! and must leave the stack alone rather than unwinding an older edit.
	void anEvictedStepIsNeverSilentlyUndone()
	{
		shipCaps();
		// A budget of ONE byte: the newest step is always kept (that is the rule),
		// so every older step - including the one the record below describes -
		// falls off the stack.
		setCap(QStringLiteral("bytes"), 1);

		tempo(101);
		const QJsonObject record = stateOf(QStringLiteral("transport.set_tempo"));
		QCOMPARE(record.value(QStringLiteral("command")).toString(),
			QStringLiteral("transport.set_tempo"));

		// Steps pushed the way the GUI pushes them (a direct engine checkpoint,
		// which records no transaction): they are what evicts the step above.
		// The record NAMES the step it describes. Without that, an undo that has
		// lost its step cannot tell "mine is gone" from "mine is still there".
		const double recordedStep = record.value(QStringLiteral("step")).toDouble();
		QVERIFY2(recordedStep > 0, "the record for a journal-backed command names no step");
		QCOMPARE(static_cast<quint64>(recordedStep), journal()->topStepSerial());

		Song* song = Engine::getSong();
		QVERIFY(song != nullptr);
		for (int i = 0; i < 5; ++i) { journal()->addJournalCheckPoint(song); }
		QVERIFY2(journal()->oldestStepSerial() > static_cast<quint64>(recordedStep),
			"the five direct steps did not evict the record's step, so the refusal below "
			"would be proving nothing");

		const int depthBeforeUndo = depth();
		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(!undone.ok, "control.undo unwound an evicted step instead of refusing");
		QCOMPARE(undone.errorKind, ControlErrorKind::Irreversible);
		QVERIFY2(undone.errorMessage.contains(QStringLiteral("EVICTED")),
			qPrintable(undone.errorMessage));

		// NOTHING was unwound: the refusal is not a partial undo. The stack is
		// exactly as the refusal found it, and the tempo it would have taken back
		// is still 101.
		QCOMPARE(depth(), depthBeforeUndo);
		QCOMPARE(run(QStringLiteral("transport.get_state"))
			.result.value(QStringLiteral("tempo")).toInt(), 101);
	}


	//! A MIDI port's readable/writable flags are DEVICE state, not an edit: the
	//! assignment costs NO undo step, and nothing was dropped to get that zero.
	//!
	//! WHY AN UNDO-BOUNDS TEST. `InstrumentTrack::autoAssignMidiDevice()` writes
	//! these two models - off and on around every save
	//! (InstrumentTrack.cpp:1017/:1025), and on every track construction and
	//! destruction (:114/:213) - and `MidiPort::subscribeReadablePort()` writes
	//! one whenever it forces input on (MidiPort.cpp:302). Journalled, each flip
	//! reached `AutomatableModel::setValue` -> `addJournalCheckPoint()`
	//! (AutomatableModel.cpp:317) and pushed a step for an assignment the user
	//! never made. Measured cost: on a machine whose MIDI client is the RAW
	//! `MidiDummy` fallback (every Linux CI runner - no ALSA sequencer,
	//! AudioEngine.cpp:1036 with MidiClient.h:145, so `isRaw()` is true) the
	//! save-time flip put ONE step on the stack for `render.render`,
	//! `bounce.in_place` and `project.save` - the `not_mutating` commands - and
	//! the freeze release gate failed with two undos landing on the wrong steps
	//! (tests/control-freeze-commands-transcript.py; the mechanism is in
	//! docs/UNDO-BOUNDS.md). The flags are not journalled now, and the three
	//! assertions below are the contract that keeps them that way: the flip
	//! still HAPPENS, it costs NO step, and the flag is still saved and restored
	//! with the port - the zero is not bought by dropping the state.
	void aDeviceAssignmentIsNotAnUndoStep()
	{
		AudioEngine* engine = Engine::audioEngine();
		QVERIFY2(engine != nullptr && engine->midiClient() != nullptr,
			"no MIDI client to build a port over");

		// Construction assigns the flags from the mode (MidiPort.cpp:70-71):
		// the +1-per-track-start that the reproduced trace showed as its
		// startup offset.
		const int before = depth();
		MidiPort port(QStringLiteral("undo-bounds-probe"), engine->midiClient(),
			nullptr, nullptr, MidiPort::Mode::Input);
		QCOMPARE(port.isReadable(), true);
		QCOMPARE(depth(), before);

		// A REAL change of value in both directions, twice each way: the
		// guarded `if (fittedValue(value) == m_value) return;` at
		// AutomatableModel.cpp:310 must not be what makes this pass, so the
		// flag is read back after every write.
		port.setReadable(false);
		QCOMPARE(port.isReadable(), false);
		QCOMPARE(depth(), before);
		port.setReadable(true);
		QCOMPARE(port.isReadable(), true);
		QCOMPARE(depth(), before);
		port.setWritable(true);
		QCOMPARE(port.isWritable(), true);
		QCOMPARE(depth(), before);
		port.setWritable(false);
		QCOMPARE(port.isWritable(), false);
		QCOMPARE(depth(), before);

		// Nothing was dropped to get that: the flags are still written with the
		// port and still read back by loadSettings (MidiPort.cpp:197-198,
		// :251-252).
		port.setReadable(true);
		port.setWritable(true);
		QDomDocument doc;
		QDomElement root = doc.createElement(QStringLiteral("root"));
		port.saveState(doc, root);
		const QDomElement saved = root.firstChildElement();
		QCOMPARE(saved.tagName(), QStringLiteral("midiport"));
		QCOMPARE(saved.attribute(QStringLiteral("readable")), QStringLiteral("1"));
		QCOMPARE(saved.attribute(QStringLiteral("writable")), QStringLiteral("1"));

		// Mode::Disabled starts with both flags false, so a true here can only
		// have come from the saved element.
		MidiPort restored(QStringLiteral("undo-bounds-probe-restored"),
			engine->midiClient(), nullptr, nullptr, MidiPort::Mode::Disabled);
		QCOMPARE(restored.isReadable(), false);
		QCOMPARE(restored.isWritable(), false);
		restored.loadSettings(saved);
		QCOMPARE(restored.isReadable(), true);
		QCOMPARE(restored.isWritable(), true);
		QCOMPARE(depth(), before);
	}

};

QTEST_GUILESS_MAIN(UndoBoundsTest)
#include "UndoBoundsTest.moc"
