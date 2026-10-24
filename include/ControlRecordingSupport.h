/*
 * ControlRecordingSupport.h - the record.* group's shared helpers: the take
 *                             journal's read-back, its schemas and the
 *                             command-shaped transaction the journal verbs
 *                             record (0.3.0).
 *
 * Split out of the group's two translation units for the same reason the warp
 * and automation groups split (this fork's file-length ratchet measures a FILE,
 * not a group, and the group's boilerplate alone does not fit in one): the
 * journal verbs and the recovery verbs are separate halves of one group, and
 * both need the same read-back and the same transaction shape.
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

#ifndef LMMS_CONTROL_RECORDING_SUPPORT_H
#define LMMS_CONTROL_RECORDING_SUPPORT_H

#include <cstdint>

#include <QJsonObject>
#include <QString>

#include "ControlVocabulary.h" // the shared schema vocabulary
#include "RecordingJournal.h"

namespace lmms
{

struct ControlResult;

class TrackRecorder;

class MultiTrackRecorder;

namespace control
{

//! One take as every verb of the group reports it. \a framesInFile is passed in
//! rather than measured, so one result cannot report two different counts.
QJsonObject journalStateJson(const TakeJournal& journal, std::uint64_t framesInFile);

//! The same, measuring the take's own bytes. This is the read-back every verb
//! returns, and it carries THE BOUND as numbers: frames_journalled,
//! frames_in_file, frames_recoverable (their minimum), the journal's lag bound
//! and the ring frames a crash loses for good.
QJsonObject takeJson(const TakeJournal& journal);

//! The address every verb of this group takes: an absolute take path.
QString takeArg(const QJsonObject& args);

//! Reads the journal for the `take` argument; false with \p error filled
//! (invalid_args for a missing argument, not_found for no journal) when it
//! cannot be read.
bool readJournal(const QJsonObject& args, TakeJournal* journal, ControlResult* error);

/*! A transaction whose inverse is a COMMAND. control.undo dispatches the named
 *  command only when the inverse says `applies: command`; transactionPayload's
 *  default is the project journal, which holds nothing for a side file in a
 *  take directory (SPEC A16). */
QJsonObject commandTransaction(const QJsonObject& before, const QString& inverseOp,
	const QJsonObject& inverseArgs);

//! \a left minus \a right, never negative: a report names a surplus only.
qint64 surplus(std::uint64_t left, std::uint64_t right);

//! The directory recovery scans when the caller names none: this instance's own
//! working directory (where its recovery file lives, and where a take recorded
//! against this session is written).
QString defaultRecoveryDir();

//! The args schema of the journal verbs (\a requireTake adds `take` to it).
QJsonObject journalArgsSchema(bool requireTake);

//! The result schema of a take read-back.
QJsonObject recoveryResultSchema();

// ---------------------------------------------------------------------------
// The recording engine surface's shared helpers (0.3.0, feature rows 14/16/64).
// They live here for the reason this unit exists at all: the group's two new
// halves (the input-path verbs and the route verbs) are separate translation
// units for the file-length ratchet, and both of them report the same facts in
// the same shape. One definition, so a route's state cannot be reported two ways.
// ---------------------------------------------------------------------------

//! One record route as every verb of the group reports it.
QJsonObject routeJson(const TrackRecorder& route, int index);

//! The whole recorder: the route count, the selectable input width, every route.
QJsonObject recorderJson(const MultiTrackRecorder& recorder);

//! The input path: the configured plan, the published device state and the
//! engine's two input stages. THE MEASUREMENT "a record route can take more than
//! zero inputs" is read from this: `bus_frames` and `wide_frames` are 0 forever
//! under a backend with no capture path.
QJsonObject inputPathJson();

//! The engine's own sample rate - the rate a take must be written at.
int engineSampleRate();

//! The `route` argument, already range-checked by the schema; -1 when absent.
int routeArg(const QJsonObject& args);

//! The take file: the caller's absolute path, or one this instance derives from
//! the directory its own recovery file lives in.
QString takePathArg(const QJsonObject& args, int route, ControlResult* error);

// ---------------------------------------------------------------------------
// The recording budget's filesystem identity (record.remaining).
//
// The budget of free bytes belongs to the FILESYSTEM, not to a directory: two
// armed directories on one filesystem draw on one pool. What names a filesystem
// is the platform's business, so it is asked for here - once - instead of in
// the command's handler, where no test could reach it.
// ---------------------------------------------------------------------------

//! How a platform names the filesystem a place sits on.
enum class VolumeNaming
{
	//! POSIX: the device the kernel mounts the filesystem from (st_dev). The device
	//! LABEL (QStorageInfo::device() there - every tmpfs mount answers "tmpfs") is
	//! not an identity: two independent filesystems answer the same one.
	KernelDevice,
	//! Windows: the volume's GUID (QStorageInfo::device()), which one volume keeps
	//! under every path it is mounted at.
	VolumeId,
};

//! How this platform names the filesystem a place sits on.
VolumeNaming platformVolumeNaming();

/*! The key that pools the free bytes of the filesystem @a path sits on.
 *
 * @a naming says how the platform names it, @a deviceId is the storage info's
 * device id and @a mountPoint its mount point. The key names the FILESYSTEM, so
 * every path onto one filesystem - a directory, a link to it, a second mount of
 * it - shares one budget of free bytes.
 */
QString filesystemIdentityKey(const QString& path, VolumeNaming naming, const QString& deviceId,
	const QString& mountPoint);
/*! TEST SEAM. While set, record.recovery_place's placement of a recovered take reports that no sample track could be
 *  created - the failure Track::create never gives for a Sample track, so no loadable build reaches it on its own. A
 *  production path never sets it. */
LMMS_EXPORT void setRecoveredTakeFailsForTest(bool fails);

} // namespace control

} // namespace lmms

#endif // LMMS_CONTROL_RECORDING_SUPPORT_H
