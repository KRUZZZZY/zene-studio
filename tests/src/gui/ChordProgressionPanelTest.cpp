/*
 * ChordProgressionPanelTest.cpp - the progression panel: suggestions for the
 *                                  selected scale, and the write that is the
 *                                  registered command
 *
 * The panel is a view over two registered commands (chord.progression_list with
 * a scale for the suggestions it shows, chord.progression_generate for the
 * write). This test holds it to that: choosing a scale spells the catalogue in
 * that key with the engine's own chord names, the Write button declares the
 * generator command, and a write is ONE control.undo away from the clip it
 * changed - the same contract the socket proof exercises end to end.
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
 * You should have received a copy of the GNU General Public License along
 * with this program (see COPYING); if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QLabel>
#include <QPushButton>
#include <QString>
#include <QTableWidget>
#include <QVector>

#include "../core/ChordTestSupport.h"
#include "ChordProgressionPanel.h"
#include "ControlRegistry.h"
#include "Engine.h"

#include "ControlEdit.h"
#include "ShellSelection.h"

using namespace lmms;
using namespace lmms::gui;
using namespace chordtest;

class ChordProgressionPanelTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	/*! Choosing a scale fills the suggestion rows with the whole catalogue
	 *  SPELT OUT in that key, by the engine's own chord names. */
	void thePanelShowsSuggestionsForTheSelectedScale()
	{
		ChordProgressionPanel panel;
		panel.setScale(QStringLiteral("Major"), 0);
		QCOMPARE(panel.scaleName(), QStringLiteral("Major"));
		QCOMPARE(panel.rootPitchClass(), 0);
		QVERIFY2(panel.rowCount() >= 8, qPrintable(QString::number(panel.rowCount())));
		QVERIFY(panel.selectProgression(QStringLiteral("I-V-vi-IV")));
		QCOMPARE(panel.chordsFor(QStringLiteral("I-V-vi-IV")),
			QStringLiteral("Major, Major, minor, Major"));

		// Another key respells the same walk: the qualities hold, the key moves.
		panel.setScale(QStringLiteral("Major"), 2);
		QCOMPARE(panel.rootPitchClass(), 2);
		QCOMPARE(panel.chordsFor(QStringLiteral("I-V-vi-IV")),
			QStringLiteral("Major, Major, minor, Major"));
	}

	/*! The Write button IS the registered generator command - one action, one
	 *  implementation, as the acceptance asks. */
	void theWriteButtonRunsTheRegisteredCommand()
	{
		ChordProgressionPanel panel;
		auto* write = panel.findChild<QPushButton*>(QStringLiteral("progressionWrite"));
		QVERIFY(write != nullptr);
		QCOMPARE(write->property("controlCommand").toString(),
			QStringLiteral("chord.progression_generate"));
	}

	/*! Clicking the actual Write button, with a real fixture clip selected, writes
	 *  the progression through the registered command the button routes to - and
	 *  one control.undo puts the original clip back. This is the UI action the
	 *  same headless workflow performs over the socket. */
	void clickingTheWriteButtonWritesAndOneUndoRestores()
	{
		const QString clip = makeClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");
		QVERIFY(addNote(clip, 40, 0, 60));
		const QVector<NoteAt> before = notesOf(clip);
		QCOMPARE(before.size(), 1);

		// The way the panel learns its target: the shell selection holds the
		// selected clip (what window.select and a clip click both set).
		control::ClipRef ref;
		ControlResult error;
		QVERIFY2(control::resolveClip(clip, &ref, &error),
			qPrintable(error.errorMessage));
		ShellSelection::instance()->setClip(ref.clip);

		ChordProgressionPanel panel;
		panel.setScale(QStringLiteral("Major"), 0);
		QVERIFY(panel.selectProgression(QStringLiteral("I-V-vi-IV")));
		auto* write = panel.findChild<QPushButton*>(QStringLiteral("progressionWrite"));
		QVERIFY2(write != nullptr, "the Write button is not in the panel");
		write->click(); // the real activation path: clicked -> writeSelected -> registry

		const QVector<NoteAt> after = notesOf(clip);
		QCOMPARE(after.size(), 12);

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY2((notesOf(clip) == before),
			qPrintable(QStringLiteral("one undo did not restore the clip: %1 vs %2")
				.arg(describe(notesOf(clip)), describe(before))));

		ShellSelection::instance()->setClip(nullptr);
	}

	/*! A refused Write says why in the status line. After a successful write, a
	 *  Write with no clip selected must replace the earlier "Wrote ..." message
	 *  with the refusal, not leave the stale success standing, and it must not
	 *  touch the clip. */
	void aRefusedWriteSaysWhyInTheStatusLine()
	{
		const QString clip = makeClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");
		QVERIFY(addNote(clip, 40, 0, 60));

		control::ClipRef ref;
		ControlResult error;
		QVERIFY2(control::resolveClip(clip, &ref, &error),
			qPrintable(error.errorMessage));
		ShellSelection::instance()->setClip(ref.clip);

		ChordProgressionPanel panel;
		panel.setScale(QStringLiteral("Major"), 0);
		QVERIFY(panel.selectProgression(QStringLiteral("I-V-vi-IV")));
		auto* write = panel.findChild<QPushButton*>(QStringLiteral("progressionWrite"));
		auto* status = panel.findChild<QLabel*>(QStringLiteral("progressionStatus"));
		QVERIFY2(write != nullptr && status != nullptr,
			"the Write button or the status line is not in the panel");

		write->click();
		QCOMPARE(notesOf(clip).size(), 12);
		QVERIFY2(status->text().startsWith(QStringLiteral("Wrote ")),
			qPrintable(status->text()));

		ShellSelection::instance()->setClip(nullptr);
		write->click();
		QCOMPARE(status->text(), QStringLiteral("Select a MIDI clip in the arranger first."));
		QCOMPARE(notesOf(clip).size(), 12);
	}

	/*! A Write with no progression selected says so in the status line, rather
	 *  than leaving whatever the panel last showed. */
	void aWriteWithNoProgressionSelectedSaysWhyInTheStatusLine()
	{
		ChordProgressionPanel panel;
		auto* write = panel.findChild<QPushButton*>(QStringLiteral("progressionWrite"));
		auto* status = panel.findChild<QLabel*>(QStringLiteral("progressionStatus"));
		auto* table = panel.findChild<QTableWidget*>(QStringLiteral("progressionTable"));
		QVERIFY2(write != nullptr && status != nullptr && table != nullptr,
			"a panel control is missing");

		panel.setScale(QStringLiteral("Major"), 0);
		table->setCurrentIndex(QModelIndex());
		QCOMPARE(table->currentRow(), -1);
		write->click();
		QCOMPARE(status->text(), QStringLiteral("Select a progression first."));
	}

	/*! A write through the panel replaces the clip's notes (the generator's own
	 *  default) and comes back with ONE control.undo. */
	void writingThroughThePanelIsOneUndoStep()
	{
		const QString clip = makeClip();
		QVERIFY2(!clip.isEmpty(), "the fixture clip was not created");
		QVERIFY(addNote(clip, 40, 0, 60));
		const QVector<NoteAt> before = notesOf(clip);
		QCOMPARE(before.size(), 1);

		ChordProgressionPanel panel;
		panel.setScale(QStringLiteral("Major"), 0);
		QVERIFY(panel.selectProgression(QStringLiteral("I-V-vi-IV")));
		QCOMPARE(panel.writeTo(clip, QStringLiteral("I-V-vi-IV")), QString());
		QCOMPARE(notesOf(clip).size(), 12);

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY2((notesOf(clip) == before),
			qPrintable(QStringLiteral("one undo did not restore the clip: %1 vs %2")
				.arg(describe(notesOf(clip)), describe(before))));
	}
};

QTEST_MAIN(ChordProgressionPanelTest)
#include "ChordProgressionPanelTest.moc"
