/*
 * ZynNoteOffTest.cpp - unmatched note-offs leave Zyn's running-note count alone, and a
 * non-positive buffer size is refused.
 *
 * Copyright (c) 2026 Zene Studio contributors
 *
 * This file is part of LMMS - https://lmms.io
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 */

#include <QtTest>

#include "LocalZynAddSubFx.h"
#include "MidiEvent.h"

namespace
{

class ObservableLocalZynAddSubFx final : public lmms::LocalZynAddSubFx
{
public:
	int runningNotesFor(int key, int channel = 0) const
	{
		return m_runningNotes[static_cast<std::size_t>(channel)][static_cast<std::size_t>(key)];
	}
	int transportSize() const { return m_transportSize; }
};

} // namespace

class ZynNoteOffTest : public QObject
{
	Q_OBJECT

private slots:
	void unmatchedNoteOffDoesNotCorruptFollowingNotes()
	{
		ObservableLocalZynAddSubFx synth;
		synth.processMidiEvent(lmms::MidiEvent(lmms::MidiNoteOff, 0, 60, 0));
		QCOMPARE(synth.runningNotesFor(60), 0);

		synth.processMidiEvent(lmms::MidiEvent(lmms::MidiNoteOn, 0, 60, 100));
		QCOMPARE(synth.runningNotesFor(60), 1);
		synth.processMidiEvent(lmms::MidiEvent(lmms::MidiNoteOn, 0, 60, 100));
		QCOMPARE(synth.runningNotesFor(60), 2);
		synth.processMidiEvent(lmms::MidiEvent(lmms::MidiNoteOff, 0, 60, 0));
		QCOMPARE(synth.runningNotesFor(60), 1);
		synth.processMidiEvent(lmms::MidiEvent(lmms::MidiNoteOff, 0, 60, 0));
		QCOMPARE(synth.runningNotesFor(60), 0);
	}

	//! Q050-313: the handoff queue's recovery cut names one channel. A note running on another channel
	//! keeps its count, or its note-off would be swallowed and the note would stick.
	void allNotesOffLeavesOtherChannelsRunningNoteCountsAlone()
	{
		ObservableLocalZynAddSubFx synth;
		synth.processMidiEvent(lmms::MidiEvent(lmms::MidiNoteOn, 1, 61, 100));
		synth.processMidiEvent(lmms::MidiEvent(lmms::MidiNoteOn, 0, 61, 100));
		QCOMPARE(synth.runningNotesFor(61, 1), 1);

		synth.processMidiEvent(lmms::MidiEvent(lmms::MidiControlChange, 0, 123, 0));
		QCOMPARE(synth.runningNotesFor(61, 0), 0);
		QCOMPARE(synth.runningNotesFor(61, 1), 1);

		synth.processMidiEvent(lmms::MidiEvent(lmms::MidiNoteOff, 1, 61, 0));
		QCOMPARE(synth.runningNotesFor(61, 1), 0);
	}

	//! A period has at least one frame. A non-positive size must not replace the last
	//! valid one: it would make the transport size non-positive and the synth's buffer
	//! size invalid, so the refused call leaves the valid size in force.
	void nonPositiveBufferSizeIsRefusedAndTheLastValidSizeStays()
	{
		ObservableLocalZynAddSubFx synth;
		synth.setBufferSize(128);
		QCOMPARE(synth.transportSize(), 128);

		synth.setBufferSize(0);
		QCOMPARE(synth.transportSize(), 128);

		synth.setBufferSize(-4);
		QCOMPARE(synth.transportSize(), 128);
	}
};

QTEST_MAIN(ZynNoteOffTest)

#include "ZynNoteOffTest.moc"
