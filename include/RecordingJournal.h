/*
 * RecordingJournal.h - the take journal: what makes a recording that is still
 *                      in progress recoverable after an abnormal exit
 *                      (0.3.0, the "recording crash recovery" gap of the
 *                      charter's In-list).
 *
 * What this is. A capture that is running writes a SMALL side file beside its
 * take - `<take>.rec-journal` - and removes it when the capture stops cleanly.
 * The side file never becomes part of a project (the same design the autosave
 * recovery sidecar uses, include/ProjectRecovery.h): it says which take is
 * being written, at what sample rate, how many frames the disk-writer had
 * flushed at the last update, and when. If the process dies mid-capture the
 * journal is still there, so the NEXT start can find the take, say what is in
 * it and hand the material back, instead of a user discovering a half-written
 * WAV with no idea what it was.
 *
 * THE BOUND, stated the way docs/UNDO-BOUNDS.md states the undo caps - because
 * a recovery promise that overstates itself is worse than none:
 *
 *   GUARANTEED recoverable: the take's file exists and holds at least the
 *   frames the journal recorded at its last update. `recoverableFrames()` is
 *   `min(journal.framesOnDisk, frames the file actually holds)`, so the number
 *   reported is never larger than what is on disk. The journal is updated at
 *   most once per SECOND of audio (RecordingJournal::UpdateIntervalFrames) and
 *   at arm time, so at most one second of material is missing from the count.
 *
 *   NOT guaranteed: (a) the audio written after the last update - the file may
 *   hold more than the journal promised, which is why the report carries both
 *   numbers and lets the caller see the difference; and (b) up to
 *   `capFrames` frames that were still in the recorder's ring buffer when the
 *   process died. That is TrackRecorder::RingCapacityFrames (65536 frames,
 *   ~1.37 s at 48 kHz) and it is IRRECOVERABLE: it never reached a file. In
 *   0.3.0 nothing recovers audio that was never written.
 *
 * There is no recording-recovery UI in 0.3.0 (docs/KNOWN-LIMITATIONS.md).
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

#ifndef LMMS_RECORDING_JOURNAL_H
#define LMMS_RECORDING_JOURNAL_H

#include <QList>
#include <QString>
#include <QStringList>

#include <cstdint>

#include "LmmsTypes.h"

namespace lmms
{

//! One journalled capture: the side file's contents, as data.
struct TakeJournal
{
	//! Absolute path of the take being recorded.
	QString takePath;
	//! "in_progress" (offered for recovery), "finished" (a clean stop named it
	//! this and it is NOT offered) or "restored" (a caller took it).
	QString state;
	int sampleRate = 0;
	int channels = 1;
	//! Frames the disk-writer had flushed when the journal was last written.
	std::uint64_t framesOnDisk = 0;
	//! The project the capture was made in, the track it was armed on and the
	//! transport position it started at. All optional: the recorder's own arm
	//! path does not know them, and an absent field is written as nothing.
	QString projectPath;
	QString track;
	qint64 startTicks = -1;
	QString updatedUtc;
};

//! The take journal (0.3.0). Pure data + file I/O: no GUI, no audio thread, no
//! project membership. Everything here is off the audio thread by construction.
namespace recordingjournal
{

//! The journal side file's path for a take: `<takePath>.rec-journal`.
QString journalPathFor(const QString& takePath);
//! The suffix `journalPathFor` appends; the scanner lists by it.
QString journalSuffix();
//! Writes \a journal atomically (QSaveFile: a crash mid-write leaves the
//! previous contents, never a torn file). False when it cannot be written.
bool write(const QString& journalPath, const TakeJournal& journal);
//! Reads a journal. False when it is absent, unreadable or has no `take`.
bool read(const QString& journalPath, TakeJournal* journal);
//! Removes a journal. True when it is gone afterwards (absent counts).
bool remove(const QString& journalPath);
/*! Every journal in \a directory whose state is `in_progress` and whose take
 *  file exists - i.e. every capture an abnormal exit left behind. Sorted by
 *  take path so two runs report them in the same order. */
QList<TakeJournal> scan(const QString& directory);
//! True when \a journal is one an abnormal exit left: in progress, a real take.
bool isRecoverable(const TakeJournal& journal);
//! The frames a take's own file holds, read from its RIFF/WAVE header. 0 when
//! the file is absent or is not a PCM WAV this reader understands.
std::uint64_t framesInFile(const QString& takePath);
/*! THE REPORTED BOUND: `min(journal.framesOnDisk, framesInFile())`. Never more
 *  than the file holds, so the number cannot promise material that is not on
 *  disk. */
std::uint64_t recoverableFrames(const TakeJournal& journal);
/*! The frames the journal is allowed to be behind by: the journal's update
 *  interval (one second of audio at the take's rate) - the most the count can
 *  understate what is already in the file. */
std::uint64_t updateIntervalFrames(int sampleRate);
/*! The frames a crash can lose for good: what was still in the recorder's ring
 *  buffer. Named as a constant so the limit is a number in the report, not a
 *  sentence in a comment (TrackRecorder::RingCapacityFrames). */
constexpr std::uint64_t RingFramesNotRecoverable = 65536u;
//! One sentence naming the bound, for a caller that wants to print it.
QString boundText(int sampleRate);

} // namespace recordingjournal

} // namespace lmms

#endif // LMMS_RECORDING_JOURNAL_H
