/*
 * CustomAccessTest.cpp - the 0.5.0 W3 L13 / L14 project and engine commands
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
/*! 0.5.0 W3 L13 / L14, the commands that need no interface (and design theme 19's workflow step recovery): the track manager's list and its colour,
 *  visibility and icon edits, each one undo step, the icon saved only when set (WF-097 / WF-120); every
 *  mixer channel's processing load (WF-104); a question answered from the manual and the commands, offline
 *  (WF-142); the shipped release notes and their once-per-version marker (WF-141); memory locations and saved
 *  selections, saved with the project only when there are any (WF-098 / WF-106); the undo history walked
 *  several steps at once (WF-102); the settings found by words, backed up, restored and kept as presets, each
 *  write one undo step, no folder path travelling (WF-122); the tuner reads A4 off a 440 Hz tone on its tap,
 *  against the reference chosen (WF-147); workflows kept with the project when the user chooses so, saved as
 *  <workspace> only when there is something to keep, and the project's own layout commands without an
 *  interface (design themes 19 and 22). */

#include <QtTest>

#include <cmath>
#include <vector>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include "Appearance.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectWorkspace.h"
#include "ReversibilityTestSupport.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TrackLooks.h"
#include "TunerTap.h"
#include "AudioEngine.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

//! A ui/ key no other case in this file writes, so "it was absent" is true by construction.
const QString AbsentKey = QStringLiteral("ui/bunnysettingsmarker");

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! Whether settings.get reports @a key as set at all (rather than defaulted).
bool settingThere(const QString& key)
{
	return run(QStringLiteral("settings.get"), {{QStringLiteral("key"), key}})
		.result.value(QStringLiteral("present")).toBool();
}

bool deviceKeyThere() { return settingThere(QStringLiteral("audioengine/audiodev")); }

//! The name of the first backend audio.device_list says this build can open.
QString someDevice()
{
	const QJsonArray devices = run(QStringLiteral("audio.device_list"))
		.result.value(QStringLiteral("devices")).toArray();
	return devices.isEmpty() ? QString() : devices.first().toObject().value(QStringLiteral("name")).toString();
	}

//! The last transaction the engine recorded for @a command, or an empty object when it recorded none.
QJsonObject transactionOf(const QString& command)
{
	QJsonObject found;
	for (const QJsonValue& value : run(QStringLiteral("control.transactions")).result
		.value(QStringLiteral("transactions")).toArray())
	{
		if (value.toObject().value(QStringLiteral("command")).toString() == command) { found = value.toObject(); }
	}
	return found;
}

QString fileText(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

//! The track.overview entry of @a track.
QJsonObject overviewOf(const Track* track)
{
	for (const QJsonValue& entry : run(QStringLiteral("track.overview")).result.value(QStringLiteral("tracks")).toArray())
	{
		if (entry.toObject().value(QStringLiteral("id")).toString() == control::trackIdOf(track)) { return entry.toObject(); }
	}
	return {};
}

} // namespace

class CustomAccessTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase();

	void cleanupTestCase();

	void theOverviewListsEveryTrack();

	void aColourIsOneUndoStep();

	void hidingIsOneUndoStep();

	void anIconIsSavedOnlyWhenSet();

	void everyChannelReportsItsLoad();

	void aQuestionFindsTheManualAndTheCommands();

	void manualMarkupPreservesWordBoundaries();

	void theReleaseNotesAreShownOncePerVersion();

	void aMemoryLocationComesBack();

	void aSavedSelectionComesBack();

	//! Design theme 22: a save says what it kept and what a memory of its kind never keeps; asked not to replace, a
	//! name in use is refused (the Window menu's Save); a recall says what came back.
	void aViewMemorySaysWhatItKeepsAndAsksBeforeReplacing();

	//! Luna cluster 4915: a clip id that names no clip is refused, and the empty string is one of
	//! them - it is not the "nothing unknown" answer the validator used to return.
	void aClipIdThatNamesNoClipIsRefusedByTheValidator();

	//! Luna cluster 4917: a view-memory save or remove claims to be undoable and records an undo
	//! step, so its recorded transaction must say what it took back and how - not fall back on the
	//! registry's "this command recorded no inverse or snapshot" record.
	void aViewMemoryChangeReportsTheUndoItRecorded();

	void theHistoryIsWalkedInOneGo();

	void theSettingsAreFoundBackedUpAndKept();

	//! Luna cluster 4941: audio.device_set on a config with no audioengine/audiodev key records an undo that writes
	//! the empty previous value back, so undoing leaves a key that was never there - and the inverse it advertises
	//! sets an empty device name, which the command itself refuses.
	void theDeviceUndoOfAnAbsentKeyDeletesItAgain();

	//! Luna cluster 4940: settings.set on a key that did not exist advertised settings.set with the EMPTY previous
	//! value as its inverse, which writes an empty key rather than deleting it - so a client that follows the
	//! transaction ends with a key the command was told never existed.
	void theSettingUndoOfAnAbsentKeyDeletesItAgain();

	//! Luna cluster 4937: the scale is read at startup from a file BESIDE the config (main() -> applyStoredScale),
	//! so a settings.set of ui/scale that only writes ConfigManager leaves the config and the scale file
	//! disagreeing - and the new scale is not applied at the next start.
	void theSettingScaleReachesTheFileStartupReads();

	void theTunerReadsANote();

	void workflowsAreKeptWithTheProjectWhenChosen();

	//! Design theme 19: a run that stops says which step failed and why, what applied and what did not run; it
	//! resumes with the step retried, or skipped and the rest run; with nothing stopped there is nothing to resume.
	void aStoppedWorkflowSaysWhereAndResumes();

	//! Retrying a failed final step that was removed from the saved workflow must not finish an empty run.
	void retryingADeletedFinalWorkflowStepIsRefused();

private:
	QTemporaryDir m_dir;
	Track* m_track = nullptr;
};

#include "CustomAccessTestParts1.h"
#include "CustomAccessTestParts2.h"

QTEST_MAIN(CustomAccessTest)
#include "CustomAccessTest.moc"
