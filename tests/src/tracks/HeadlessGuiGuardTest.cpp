/*
 * HeadlessGuiGuardTest.cpp - inherited clip paths that assumed a GUI, run headless
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

//! Two inherited paths dereferenced gui::getGUI() unguarded, so they crashed in
//! any GUI-less process (the agent surface, the CLI renderer, a test binary):
//!
//!  * MidiClip::addNote with its DEFAULT quant_pos=true asked the piano roll
//!    for a quantisation - reachable headless from MidiImport's addNote(n);
//!  * Clip::copyStateTo refreshed the automation editor after the copy.
//!
//! Headless, the note keeps its own position (there is no piano-roll
//! quantisation to apply) and the state copy completes. Both cases segfault
//! against the unguarded code, so the test process itself is the assertion.

#include <QtTest>

#include "Engine.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "Note.h"
#include "Song.h"

class HeadlessGuiGuardTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase() { lmms::Engine::init(true); }
	void cleanupTestCase() { lmms::Engine::destroy(); }

	void addNoteWithTheDefaultQuantisationKeepsItsPosition()
	{
		using namespace lmms;
		InstrumentTrack track(Engine::getSong());
		MidiClip clip(&track);
		clip.changeLength(TimePos(4, 0));

		// An off-grid position: seven ticks is on no piano-roll quantisation.
		Note* note = clip.addNote(Note(TimePos(48), TimePos(7)));
		QVERIFY(note != nullptr);
		QCOMPARE(clip.notes().size(), std::size_t{1});
		QCOMPARE(note->pos().getTicks(), 7);
	}

	void copyStateToCompletesHeadless()
	{
		using namespace lmms;
		InstrumentTrack track(Engine::getSong());
		MidiClip source(&track);
		source.changeLength(TimePos(4, 0));
		source.addNote(Note(TimePos(48), TimePos(96)), false);
		source.addNote(Note(TimePos(24), TimePos(1, 0)), false);
		MidiClip target(&track);

		Clip::copyStateTo(&source, &target);
		QCOMPARE(target.notes().size(), std::size_t{2});
	}
};

QTEST_GUILESS_MAIN(HeadlessGuiGuardTest)
#include "HeadlessGuiGuardTest.moc"
