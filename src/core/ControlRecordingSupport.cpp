/*
 * ControlRecordingSupport.cpp - the record.* group's shared helpers (see the
 *                               header for why they are in their own unit).
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

#include "ControlRecordingSupport.h"

#include <algorithm>

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>

#include "ConfigManager.h"
#include "AudioEngine.h"
#include "AudioInputPath.h"
#include "ControlEdit.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MultiTrackRecorder.h"
#include "TrackRecorder.h"

namespace lmms
{

using namespace recordingjournal;

namespace control
{

namespace
{

//! Why the journal's inverse is a command and not a checkpoint: the journal is a
//! file in a take directory, which the project journal holds nothing for.
const QString ClauseJournalFile = QStringLiteral("the journal is a side file beside the "
	"take, not project state, so the project journal holds nothing for it: the recorded "
	"inverse is the paired COMMAND (`applies: command`), which control.undo dispatches "
	"through the registry");

} // namespace

QJsonObject journalStateJson(const TakeJournal& journal, std::uint64_t framesInFile)
{
	QJsonObject result;
	result.insert(QStringLiteral("take"), journal.takePath);
	result.insert(QStringLiteral("journal"), journalPathFor(journal.takePath));
	result.insert(QStringLiteral("state"), journal.state);
	result.insert(QStringLiteral("sample_rate"), journal.sampleRate);
	result.insert(QStringLiteral("channels"), journal.channels);
	result.insert(QStringLiteral("frames_journalled"), static_cast<qint64>(journal.framesOnDisk));
	result.insert(QStringLiteral("frames_in_file"), static_cast<qint64>(framesInFile));
	// THE BOUND, as a number the caller can check: the guaranteed count is the
	// SMALLER of what the journal recorded and what the take really holds.
	result.insert(QStringLiteral("frames_recoverable"),
		static_cast<qint64>(std::min(journal.framesOnDisk, framesInFile)));
	result.insert(QStringLiteral("frames_beyond_the_journal"),
		surplus(framesInFile, journal.framesOnDisk));
	result.insert(QStringLiteral("journal_lag_bound_frames"),
		static_cast<qint64>(updateIntervalFrames(journal.sampleRate)));
	result.insert(QStringLiteral("ring_frames_not_recoverable"),
		static_cast<qint64>(RingFramesNotRecoverable));
	result.insert(QStringLiteral("updated_utc"), journal.updatedUtc);
	if (!journal.projectPath.isEmpty())
	{
		result.insert(QStringLiteral("project"), journal.projectPath);
	}
	if (!journal.track.isEmpty()) { result.insert(QStringLiteral("track"), journal.track); }
	if (journal.startTicks >= 0)
	{
		result.insert(QStringLiteral("start_ticks"), journal.startTicks);
	}
	return result;
}

QJsonObject takeJson(const TakeJournal& journal)
{
	return journalStateJson(journal, framesInFile(journal.takePath));
}

QString takeArg(const QJsonObject& args)
{
	return args.value(QStringLiteral("take")).toString();
}

bool readJournal(const QJsonObject& args, TakeJournal* journal, ControlResult* error)
{
	const QString take = takeArg(args);
	if (take.isEmpty())
	{
		*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
			QStringLiteral("'take' is required: the absolute path of the capture's WAV file"));
		return false;
	}
	if (!read(journalPathFor(take), journal))
	{
		*error = ControlResult::failure(ControlErrorKind::NotFound,
			QStringLiteral("no recording journal for '%1' (expected %2); a capture that was "
				"never journalled cannot be recovered from the journal")
				.arg(take, journalPathFor(take)));
		return false;
	}
	return true;
}

QJsonObject commandTransaction(const QJsonObject& before, const QString& inverseOp,
	const QJsonObject& inverseArgs)
{
	QJsonObject transaction = transactionPayload(before, inverseOp, inverseArgs, true,
		ClauseJournalFile);
	QJsonObject inverse = transaction.value(QStringLiteral("inverse")).toObject();
	inverse.insert(QStringLiteral("applies"), QStringLiteral("command"));
	transaction.insert(QStringLiteral("inverse"), inverse);
	return transaction;
}

qint64 surplus(std::uint64_t left, std::uint64_t right)
{
	return left > right ? static_cast<qint64>(left - right) : 0;
}

QString defaultRecoveryDir()
{
	return QFileInfo(ConfigManager::inst()->recoveryFile()).absoluteDir().absolutePath();
}

QJsonObject journalArgsSchema(bool requireTake)
{
	return objectSchema({
		{QStringLiteral("take"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("channels"), integerProperty()},
		{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("start_ticks"), integerProperty()},
	}, requireTake ? QJsonArray{QStringLiteral("take")} : QJsonArray{});
}

QJsonObject recoveryResultSchema()
{
	return objectSchema({
		{QStringLiteral("take"), stringProperty()},
		{QStringLiteral("journal"), stringProperty()},
		{QStringLiteral("state"), stringProperty()},
		{QStringLiteral("sample_rate"), integerProperty()},
		{QStringLiteral("channels"), integerProperty()},
		{QStringLiteral("frames_journalled"), integerProperty()},
		{QStringLiteral("frames_in_file"), integerProperty()},
		{QStringLiteral("frames_recoverable"), integerProperty()},
		{QStringLiteral("frames_beyond_the_journal"), integerProperty()},
		{QStringLiteral("journal_lag_bound_frames"), integerProperty()},
		{QStringLiteral("ring_frames_not_recoverable"), integerProperty()},
		{QStringLiteral("updated_utc"), stringProperty()},
		{QStringLiteral("project"), stringProperty()},
		{QStringLiteral("track"), stringProperty()},
		{QStringLiteral("start_ticks"), integerProperty()},
	});
}

// ---------------------------------------------------------------------------
// The recording engine surface's shared helpers (see the header).
// ---------------------------------------------------------------------------

QJsonObject routeJson(const TrackRecorder& route, int index)
{
	QJsonObject entry;
	entry.insert(QStringLiteral("route"), index);
	entry.insert(QStringLiteral("armed"), route.isArmed());
	entry.insert(QStringLiteral("input_channel"), route.inputChannel());
	entry.insert(QStringLiteral("input_channel_capacity"), route.inputChannelCapacity());
	entry.insert(QStringLiteral("file"), QString::fromStdString(route.filePath()));
	entry.insert(QStringLiteral("journal"), QString::fromStdString(route.journalPath()));
	entry.insert(QStringLiteral("frames_pushed"), static_cast<qint64>(route.framesPushed()));
	entry.insert(QStringLiteral("frames_recorded"), static_cast<qint64>(route.framesRecorded()));
	entry.insert(QStringLiteral("frames_journalled"), static_cast<qint64>(route.journalledFrames()));
	entry.insert(QStringLiteral("overflow_frames"), static_cast<qint64>(route.overflowCount()));
	entry.insert(QStringLiteral("write_errors"), static_cast<qint64>(route.writeErrorCount()));
	return entry;
}

QJsonObject recorderJson(const MultiTrackRecorder& recorder)
{
	QJsonArray routes;
	for (int i = 0; i < recorder.trackCount(); ++i)
	{
		routes.append(routeJson(recorder.track(i), i));
	}

	QJsonObject result;
	result.insert(QStringLiteral("route_count"), recorder.trackCount());
	result.insert(QStringLiteral("input_channel_capacity"), recorder.inputChannelCapacity());
	result.insert(QStringLiteral("routes"), routes);
	result.insert(QStringLiteral("total_overflow_frames"),
		static_cast<qint64>(recorder.totalOverflowCount()));
	return result;
}

QJsonObject inputPathJson()
{
	const AudioInputPath::Plan plan = AudioInputPath::configuredPlan();
	const AudioInputPath::Live live = AudioInputPath::live();

	QJsonObject configured;
	configured.insert(QStringLiteral("device"), plan.device);
	configured.insert(QStringLiteral("channels"), plan.channels);
	configured.insert(QStringLiteral("left"), plan.left);
	configured.insert(QStringLiteral("right"), plan.right);

	QJsonObject result;
	result.insert(QStringLiteral("configured"), configured);
	result.insert(QStringLiteral("capture_capable"), live.captureCapable);
	result.insert(QStringLiteral("capture_open"), live.open);
	result.insert(QStringLiteral("capture_device"), live.device);
	result.insert(QStringLiteral("capture_reason"), live.reason);
	result.insert(QStringLiteral("capture_channels"), live.channels);
	result.insert(QStringLiteral("capture_rate"), live.rate);
	result.insert(QStringLiteral("capture_left"), live.left);
	result.insert(QStringLiteral("capture_right"), live.right);
	result.insert(QStringLiteral("capture_format"), live.sampleFormat);
	result.insert(QStringLiteral("capture_frames"), static_cast<qint64>(live.framesCaptured));
	result.insert(QStringLiteral("capture_overruns"), static_cast<qint64>(live.overruns));
	result.insert(QStringLiteral("recordable_channels"), AudioInputPath::recordableChannelCount());
	result.insert(QStringLiteral("max_channels"), AudioInputPath::MaxChannels);
	result.insert(QStringLiteral("route_capacity"), AudioInputPath::recordRouteCapacity());

	// The engine's own view: the stereo bus and the wide stage the recorders
	// read. A null engine reports zeroes rather than pretending.
	const AudioEngine* engine = Engine::audioEngine();
	result.insert(QStringLiteral("input_frames_staged"),
		static_cast<qint64>(engine != nullptr ? engine->inputFramesStaged() : 0u));
	result.insert(QStringLiteral("input_frames_dropped"),
		static_cast<qint64>(engine != nullptr ? engine->inputFramesDropped() : 0u));
	result.insert(QStringLiteral("bus_frames"),
		static_cast<qint64>(engine != nullptr ? engine->inputBufferFrames() : 0));
	result.insert(QStringLiteral("wide_frames"),
		static_cast<qint64>(engine != nullptr ? engine->inputWideFrames() : 0));
	result.insert(QStringLiteral("wide_channels"),
		engine != nullptr ? engine->inputWideChannels() : 0);
	result.insert(QStringLiteral("wide_frames_staged"),
		static_cast<qint64>(engine != nullptr ? engine->inputWideFramesStaged() : 0u));
	result.insert(QStringLiteral("wide_frames_dropped"),
		static_cast<qint64>(engine != nullptr ? engine->inputWideFramesDropped() : 0u));
	return result;
}

int engineSampleRate()
{
	const AudioEngine* engine = Engine::audioEngine();
	return engine != nullptr ? static_cast<int>(engine->baseSampleRate()) : 44100;
}

int routeArg(const QJsonObject& args)
{
	return args.contains(QStringLiteral("route")) ? args.value(QStringLiteral("route")).toInt() : -1;
}

QString takePathArg(const QJsonObject& args, int route, ControlResult* error)
{
	const QString file = args.value(QStringLiteral("file")).toString();
	if (!file.isEmpty())
	{
		if (!QFileInfo(file).isAbsolute())
		{
			*error = ControlResult::failure(ControlErrorKind::InvalidArgs,
				QStringLiteral("'file' must be absolute: a relative path names a different file "
					"in the next process, and the take journal beside it does too"));
			return QString();
		}
		return file;
	}
	return QDir(defaultRecoveryDir()).absoluteFilePath(
		QStringLiteral("zene-take-route%1.wav").arg(route));
}

} // namespace control

} // namespace lmms
