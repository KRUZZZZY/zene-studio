/*
 * SmfNoteReadTest.cpp - the SMF note reader's own regression file
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
/*! readSmfNotes() (SmfNoteRead.h) is pure - bytes in, notes or a refusal out - so each cluster that found a
 *  reader bug gets its own slot here without an engine, a file on disk or a clock. This is the reader's OWN
 *  file because ClipImportFileTest (the import path the reader serves) is at the 500-line cap every test file
 *  is held to:
 *    - new-p14-314: closing the oldest open note of a pitch must not shuffle the notes queued behind it.
 */

#include <QtTest>

#include "SmfNoteRead.h"

using namespace lmms;

namespace
{

void be16(QByteArray& out, int value)
{
	out.append(static_cast<char>((value >> 8) & 0xFF));
	out.append(static_cast<char>(value & 0xFF));
}

void be32(QByteArray& out, quint32 value)
{
	be16(out, static_cast<int>(value >> 16));
	be16(out, static_cast<int>(value & 0xFFFF));
}

QByteArray smf(const QByteArray& trackBody, int division = 96, int format = 0, int tracks = 1)
{
	QByteArray out("MThd", 4);
	be32(out, 6);
	be16(out, format);
	be16(out, tracks);
	be16(out, division);
	out.append("MTrk", 4);
	be32(out, static_cast<quint32>(trackBody.size()));
	out.append(trackBody);
	return out;
}

} // namespace

class SmfNoteReadTest : public QObject
{
	Q_OBJECT

private slots:
	void closingOverlappingNotesOfOnePitchDoesNotShiftTheQueue()
	{
		// FIFO pairing first: the second note opens while the first still sounds, so the first note-off
		// must close the OLDEST of the two - whatever the close itself costs the queue.
		QByteArray body;
		body.append("\x00\x90\x3C\x64", 4);  // C4 on, velocity 100, at file tick 0
		body.append("\x0A\x90\x3C\x40", 4);   // +10 ticks: C4 on, velocity 64, overlapping the first
		body.append("\x0A\x80\x3C\x00", 4);   // +10: C4 off -> closes the first note
		body.append("\x0A\x90\x3C\x50", 4);   // +10: C4 on, velocity 80
		body.append("\x0A\x80\x3C\x00", 4);   // +10: C4 off -> closes the second note
		body.append("\x00\xFF\x2F\x00", 4);   // end of track
		const interchange::SmfNoteRead read = interchange::readSmfNotes(smf(body));
		QVERIFY2(read.ok, qPrintable(read.error));
		QCOMPARE(static_cast<int>(read.notes.size()), 3);
		// division 96 -> 48 ticks a quarter, so 10 file ticks are 5 LMMS ticks; the first note closes at
		// file tick 20 (10 LMMS ticks), the second and last are ended by the track's end at 40.
		QCOMPARE(read.notes[0].velocity, 100);
		QCOMPARE(read.notes[0].start, 0LL);
		QCOMPARE(read.notes[0].length, 10LL);
		QCOMPARE(read.notes[1].velocity, 64);
		QCOMPARE(read.notes[1].start, 5LL);
		QCOMPARE(read.notes[1].length, 15LL);
		QCOMPARE(read.notes[2].velocity, 80);
		QCOMPARE(read.notes[2].start, 15LL);
		QCOMPARE(read.notes[2].length, 5LL);
		QCOMPARE(read.endTick, 20LL);

		// The shape behind new-p14-314: 4000 notes of one pitch open at once, then every one of them
		// closed, each close at the FRONT of the queue its siblings are queued behind. What must stay
		// small is the count of queue element moves that costs (smfNoteQueueShiftCount(), a count and
		// never a clock): a queue that shuffles its whole tail per close moves every note behind the
		// closed one - millions of moves here - and reading such a track becomes quadratic.
		const int overlap = 4000;
		QByteArray deep;
		for (int i = 0; i < overlap; ++i) { deep.append("\x00\x90\x3C\x40", 4); }
		for (int i = 0; i < overlap; ++i) { deep.append("\x00\x80\x3C\x00", 4); }
		deep.append("\x00\xFF\x2F\x00", 4);
		const long long before = interchange::smfNoteQueueShiftCount();
		const interchange::SmfNoteRead many = interchange::readSmfNotes(smf(deep));
		const long long shifts = interchange::smfNoteQueueShiftCount() - before;
		QVERIFY2(many.ok, qPrintable(many.error));
		QCOMPARE(static_cast<int>(many.notes.size()), overlap);
		QVERIFY2(shifts <= 2LL * overlap, qPrintable(QStringLiteral(
			"closing %1 overlapping notes of one pitch moved %2 queue elements; the linear bound is %3")
			.arg(overlap).arg(shifts).arg(2LL * overlap)));
	}
};

QTEST_GUILESS_MAIN(SmfNoteReadTest)
#include "SmfNoteReadTest.moc"
