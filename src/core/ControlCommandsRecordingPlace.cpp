/*
 * ControlCommandsRecordingPlace.cpp - record.recovery_place: bring an interrupted
 *                                     take into the project as one undo step
 *
 * record.recovery_restore only resolves the offer; this command does the half the
 * menu used to do on its own - a new sample track named after the take, holding
 * the recovered file in a clip at bar 1 - and marks the journal restored in the
 * same step. The File > Recover Recordings menu and the startup offer invoke it,
 * so the GUI and the agent surface share one implementation and one undo step.
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <atomic>
#include <memory>

#include <QFileInfo>
#include <QJsonObject>
#include <QObject>
#include <QString>

#include "ControlEdit.h"
#include "ControlRecordingSupport.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "RecordingJournal.h"
#include "SampleClip.h"
#include "Song.h"
#include "Track.h"

namespace lmms
{

using namespace control;
using namespace recordingjournal;

namespace
{

std::atomic<bool> g_recoveredTakeFailsForTest{false};  // the test seam: setRecoveredTakeFailsForTest()

//! A new sample track named after the take, holding \a audio in a clip at bar 1; its stable id, or empty.
QString landRecoveredTake(Song* song, const QString& audio)
{
	if (g_recoveredTakeFailsForTest.load()) { return QString(); }
	Track* track = Track::create(Track::Type::Sample, song);
	if (track == nullptr) { return QString(); }
	track->setName(QObject::tr("Recovered: %1").arg(QFileInfo(audio).completeBaseName()));
	if (auto* clip = dynamic_cast<SampleClip*>(track->createClip(TimePos(0)))) { clip->setSampleFile(audio); }
	return trackIdOf(track);
}

ControlResult recoveryPlace(const QJsonObject& args)
{
	TakeJournal journal;
	ControlResult error;
	if (!readJournal(args, &journal, &error)) { return error; }
	if (!isRecoverable(journal))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("the journal for '%1' is '%2', not an interrupted capture")
				.arg(journal.takePath, journal.state));
	}
	const QJsonObject before = takeJson(journal);
	if (before.value(QStringLiteral("frames_recoverable")).toDouble() <= 0)
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("'%1' holds no recoverable audio, so there is nothing to place").arg(journal.takePath));
	}

	const TakeJournal offered = journal;
	TakeJournal restored = journal;
	restored.state = QStringLiteral("restored");
	const QString journalPath = journalPathFor(journal.takePath);
	if (!write(journalPath, restored))
	{
		return ControlResult::failure(ControlErrorKind::Refused,
			QStringLiteral("could not write the recording journal %1").arg(journalPath));
	}

	// ONE action step: undo removes the placed track and puts the offer back (the journal returns to
	// in_progress); redo places the same file on a new track and resolves the offer again. The track
	// is found by its stable id when the step runs - a later track.remove and its undo re-create it
	// as a new object under the same id.
	Song* song = Engine::getSong();
	const QString audio = journal.takePath;
	auto placed = std::make_shared<QString>();
	addUndoStep(
		[placed, offered, journalPath]() {
			ControlResult ignored;
			if (Track* track = resolveTrack(*placed, &ignored)) { removeTrack(track); }
			placed->clear();
			write(journalPath, offered);
		},
		[placed, offered, restored, journalPath, song, audio]() {
			*placed = landRecoveredTake(song, audio);
			// A redo that places nothing leaves the take offered, as the first placement's failure does below, and
			// declines the redo: the step goes back on the redo stack instead of being recorded as an applied undo step.
			if (placed->isEmpty())
			{
				write(journalPath, offered);
				if (ProjectJournal* journal = Engine::projectJournal()) {
					journal->declineRedo(QStringLiteral("no sample track could be created for the recovered take"));
				}
				return;
			}
			write(journalPath, restored);
		});
	*placed = landRecoveredTake(song, audio);
	if (placed->isEmpty())
	{
		write(journalPath, offered);
		return ControlResult::failure(ControlErrorKind::Refused, QStringLiteral("could not create a sample track"));
	}

	QJsonObject result = takeJson(restored);
	result.insert(QStringLiteral("restored"), true);
	result.insert(QStringLiteral("placed"), true);
	result.insert(QStringLiteral("audio"), audio);
	result.insert(QStringLiteral("audio_untouched"), true);
	result.insert(QStringLiteral("placed_track"), *placed);
	result.insert(QStringLiteral("__transaction"),
		transactionPayload(before, QStringLiteral("remove the placed track and put the offer back"), QJsonObject(),
			true, QStringLiteral("action checkpoint: the recorded undo step removes the placed track through "
				"control::removeTrack (as track.add's does) and rewrites the take's journal as in_progress; "
				"redo places the file again and marks it restored. The take's WAV is never moved or deleted")));
	return ControlResult::success(result);
}

} // namespace

namespace control
{

void setRecoveredTakeFailsForTest(bool fails) { g_recoveredTakeFailsForTest.store(fails); }

} // namespace control

void registerRecordingPlaceCommand(ControlRegistry& registry)
{
	ControlCommand cmd;
	cmd.id = QStringLiteral("record.recovery_place");
	cmd.group = QStringLiteral("record");
	cmd.verb = QStringLiteral("recovery_place");
	cmd.description = QStringLiteral("Bring an interrupted capture into the project: a new sample track "
		"named 'Recovered: <take>' holding the recovered file in a clip at bar 1, and the take's journal "
		"marked restored so the next start stops offering it. One undo step: control.undo removes the track "
		"and puts the offer back, control.redo places it again. The take's WAV is never moved, rewritten or "
		"deleted. record.recovery_restore stays the resolve-only verb; File > Recover Recordings and the "
		"startup offer run this command.");
	cmd.argsSchema = objectSchema({{QStringLiteral("take"), stringProperty()}}, {QStringLiteral("take")});
	cmd.resultSchema = objectSchema({
		{QStringLiteral("take"), stringProperty()},
		{QStringLiteral("journal"), stringProperty()},
		{QStringLiteral("state"), stringProperty()},
		{QStringLiteral("restored"), booleanProperty()},
		{QStringLiteral("placed"), booleanProperty()},
		{QStringLiteral("placed_track"), stringProperty()},
		{QStringLiteral("audio"), stringProperty()},
		{QStringLiteral("audio_untouched"), booleanProperty()},
		{QStringLiteral("frames_journalled"), integerProperty()},
		{QStringLiteral("frames_in_file"), integerProperty()},
		{QStringLiteral("frames_recoverable"), integerProperty()},
		{QStringLiteral("channels"), numberProperty()},
		{QStringLiteral("frames_beyond_the_journal"), numberProperty()},
		{QStringLiteral("journal_lag_bound_frames"), numberProperty()},
		{QStringLiteral("ring_frames_not_recoverable"), numberProperty()},
		{QStringLiteral("sample_rate"), numberProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("updated_utc"), stringProperty()},
	});
	cmd.mutating = true;
	cmd.handler = recoveryPlace;
	registry.registerCommand(cmd);
}

} // namespace lmms
