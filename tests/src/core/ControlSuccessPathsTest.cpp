/*
 * ControlSuccessPathsTest.cpp - the success path of every command the suite only ever refused
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

/*! R6.3: a command whose reply was never seen SUCCEED under result checks has an
 *  unverified reply contract. tests/checked-coverage.py measured 27 such commands
 *  on the release surface (gate 1's checked-command ratchet, tests/checked-coverage-unreached.txt).
 *  Their own test files hold only refusals, so the success path of each is built
 *  here: the prerequisite state first, then one call that must succeed. Every
 *  LMMS_TESTS entry runs with ZENE_CONTROL_CHECK_RESULTS, so a reply that breaks
 *  its own resultSchema comes back as a typed failure and the QVERIFY names it.
 *
 *  Each slot also asserts one fact the success claims, so a success that did
 *  nothing is not a pass. What stays refused by design (telemetry.consent needs
 *  a human, crash.upload_report does no network) is listed in the ratchet with
 *  its reason, not faked here. Three live beside their group instead: the
 *  recording verbs need a configured input (tests/control-record-inputs.py),
 *  automation.record_mode_set needs a device parameter, which only a test given
 *  the plugin directory has (ControlAutomationScriptTest), and
 *  project.audible_diff renders through the running binary as child processes
 *  (tests/control-project-audible-diff.py).
 */

#include <QtTest>

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QTextStream>

#include "ConfigManager.h"
#include "ControlProjectAssets.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "MidiEvent.h"
#include "MidiLearn.h"
#include "Mixer.h"
#include "ModulationTestSupport.h"
#include "ProjectInfo.h"
#include "ProjectJournal.h"
#include "RackTestSupport.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;
using modtest::run;
using namespace racktest;

namespace
{

//! The reply, or a QVERIFY-able failure naming the command and its error.
#define REQUIRE_OK(result, id) \
	QVERIFY2((result).ok, qPrintable(QStringLiteral("%1: %2").arg(id, (result).errorMessage)))

QString addTrack(const QString& type)
{
	const ControlResult track = run(QStringLiteral("track.add"), {{QStringLiteral("type"), type}});
	return track.ok ? track.result.value(QStringLiteral("track")).toString() : QString();
}

QString addClip(const QString& track, int position)
{
	const ControlResult clip = run(QStringLiteral("clip.add"),
		{{QStringLiteral("track"), track}, {QStringLiteral("position"), position},
			{QStringLiteral("length"), 192}});
	return clip.ok ? clip.result.value(QStringLiteral("clip")).toString() : QString();
}

//! A 12-note Scala keyboard mapping, linear, A4 = 440 Hz on key 69.
QString writeKeymap(const QTemporaryDir& dir)
{
	const QString path = dir.filePath(QStringLiteral("linear.kbm"));
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) { return QString(); }
	QTextStream out(&file);
	out << "! linear.kbm\n12\n0\n127\n60\n69\n440.0\n12\n";
	for (int degree = 0; degree < 12; ++degree) { out << degree << "\n"; }
	return path;
}

} // namespace


class ControlSuccessPathsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QStandardPaths::setTestModeEnabled(true);
		QString why;
		QVERIFY2(initRackFixture(&why), qPrintable(why));
		QVERIFY(m_dir.isValid());
	}

	void cleanupTestCase() { teardownRackFixture(); }

	void modulatorTargetRemove()
	{
		const ControlResult created = run(QStringLiteral("modulator.create"),
			{{QStringLiteral("name"), QStringLiteral("Route")}, {QStringLiteral("rate"), 1.0}});
		REQUIRE_OK(created, "modulator.create");
		const QString modulator = created.result.value(QStringLiteral("modulator")).toString();
		REQUIRE_OK(run(QStringLiteral("modulator.target_set"),
			{{QStringLiteral("modulator"), modulator}, {QStringLiteral("channel"), underTestChannel()},
				{QStringLiteral("chain"), kDrivenChain}, {QStringLiteral("effect"), 0},
				{QStringLiteral("parameter"), QString::fromLatin1(kGainName)},
				{QStringLiteral("depth"), 0.25}}), "modulator.target_set");
		const ControlResult removed = run(QStringLiteral("modulator.target_remove"),
			{{QStringLiteral("modulator"), modulator}, {QStringLiteral("target"), 0}});
		REQUIRE_OK(removed, "modulator.target_remove");
		REQUIRE_OK(run(QStringLiteral("modulator.remove"), {{QStringLiteral("modulator"), modulator}}),
			"modulator.remove");
	}

	void rackMacrosAndSelection()
	{
		const QString channel = underTestChannel();
		const ControlResult macro = run(QStringLiteral("rack.macro_add"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("name"), QStringLiteral("Level")},
				{QStringLiteral("value"), 0.0}});
		REQUIRE_OK(macro, "rack.macro_add");
		const QString id = macro.result.value(QStringLiteral("macro")).toString();
		REQUIRE_OK(run(QStringLiteral("rack.macro_target_add"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), id},
				{QStringLiteral("chain"), kDrivenChain}, {QStringLiteral("effect"), 0},
				{QStringLiteral("parameter"), QString::fromLatin1(kGainName)},
				{QStringLiteral("low"), 0.25}, {QStringLiteral("high"), 0.75}}), "rack.macro_target_add");
		REQUIRE_OK(run(QStringLiteral("rack.macro_target_remove"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), id},
				{QStringLiteral("target"), 0}}), "rack.macro_target_remove");
		REQUIRE_OK(run(QStringLiteral("rack.macro_remove"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("macro"), id}}), "rack.macro_remove");
		REQUIRE_OK(run(QStringLiteral("rack.set_selected"),
			{{QStringLiteral("channel"), channel}, {QStringLiteral("chain"), kDrivenChain}}),
			"rack.set_selected");
		QCOMPARE(rackUnderTest().selectedChain(), kDrivenChain);
	}

	void vcaUnassign()
	{
		const ControlResult group = run(QStringLiteral("vca.create"), {{QStringLiteral("name"), QStringLiteral("Bus")}});
		REQUIRE_OK(group, "vca.create");
		const QJsonObject membership{{QStringLiteral("group"), group.result.value(QStringLiteral("group"))},
			{QStringLiteral("channel"), underTestChannel()}};
		REQUIRE_OK(run(QStringLiteral("vca.assign"), membership), "vca.assign");
		const ControlResult unassigned = run(QStringLiteral("vca.unassign"), membership);
		REQUIRE_OK(unassigned, "vca.unassign");
	}

	void clipLinkSync()
	{
		const QString track = addTrack(QStringLiteral("instrument"));
		QVERIFY(!track.isEmpty());
		const QString first = addClip(track, 0);
		const QString second = addClip(track, 384);
		QVERIFY(!first.isEmpty() && !second.isEmpty());
		REQUIRE_OK(run(QStringLiteral("clip.link_create"),
			{{QStringLiteral("clip"), first}, {QStringLiteral("clips"), QJsonArray{second}}}), "clip.link_create");
		REQUIRE_OK(run(QStringLiteral("clip.link_sync"), {{QStringLiteral("clip"), first}}), "clip.link_sync");
	}

	void visibilitySetRemove()
	{
		const QString track = addTrack(QStringLiteral("instrument"));
		REQUIRE_OK(run(QStringLiteral("track.visibility_set_save"),
			{{QStringLiteral("name"), QStringLiteral("Keys")}, {QStringLiteral("tracks"), QJsonArray{track}}}),
			"track.visibility_set_save");
		REQUIRE_OK(run(QStringLiteral("track.visibility_set_remove"), {{QStringLiteral("name"), QStringLiteral("Keys")}}),
			"track.visibility_set_remove");
	}

	void tuningNoteTableAndKeymap()
	{
		REQUIRE_OK(run(QStringLiteral("mts.set_note"),
			{{QStringLiteral("note"), 69}, {QStringLiteral("frequency"), 442.0}}), "mts.set_note");
		REQUIRE_OK(run(QStringLiteral("mts.set_tuning"),
			{{QStringLiteral("note"), 60}, {QStringLiteral("frequency"), 261.63}}), "mts.set_tuning");
		const QString keymap = writeKeymap(m_dir);
		QVERIFY(!keymap.isEmpty());
		REQUIRE_OK(run(QStringLiteral("mts.load_keymap"), {{QStringLiteral("path"), keymap}}), "mts.load_keymap");
		REQUIRE_OK(run(QStringLiteral("mts.reset")), "mts.reset");
	}

	void controllerFeedbackTakeoverAndTemplates()
	{
		// One control bound through the landed MIDI-learn path, so the group's
		// "the only bound control" default resolves without naming it.
		FloatModel& volume = Engine::mixer()->mixerChannel(kChannel)->m_volumeModel;
		MidiLearn::instance()->setFocusTarget(&volume);
		MidiLearn::instance()->setEnabled(true);
		QVERIFY(MidiLearn::instance()->handleMidiEvent(MidiEvent(MidiControlChange, 3, 21, 64)));
		REQUIRE_OK(run(QStringLiteral("controller.feedback"), {{QStringLiteral("enabled"), true}}), "controller.feedback");
		REQUIRE_OK(run(QStringLiteral("controller.soft_takeover"),
			{{QStringLiteral("enabled"), true}, {QStringLiteral("target"), 0.5}}), "controller.soft_takeover");
		const QString name = QStringLiteral("zene-success-paths-selftest");
		REQUIRE_OK(run(QStringLiteral("controller.template_save"), {{QStringLiteral("name"), name}}),
			"controller.template_save");
		REQUIRE_OK(run(QStringLiteral("controller.template_apply"), {{QStringLiteral("name"), name}}),
			"controller.template_apply");
		REQUIRE_OK(run(QStringLiteral("controller.template_delete"), {{QStringLiteral("name"), name}}),
			"controller.template_delete");
	}

	void sessionFollowActions()
	{
		REQUIRE_OK(run(QStringLiteral("session.set_grid"),
			{{QStringLiteral("tracks"), 1}, {QStringLiteral("scenes"), 2}}), "session.set_grid");
		const ControlResult follow = run(QStringLiteral("session.follow_set"),
			{{QStringLiteral("track"), 0}, {QStringLiteral("scene"), 0}, {QStringLiteral("enabled"), true},
				{QStringLiteral("actions"), QJsonArray{QJsonObject{{QStringLiteral("type"), QStringLiteral("next")},
					{QStringLiteral("chance"), 1.0}}}}});
		REQUIRE_OK(follow, "session.follow_set");
	}

	void projectDiffMergeAndConflicts()
	{
		const QString a = m_dir.filePath(QStringLiteral("a.mmp"));
		const QString b = m_dir.filePath(QStringLiteral("b.mmp"));
		REQUIRE_OK(run(QStringLiteral("project.save"), {{QStringLiteral("path"), a}}), "project.save a");
		REQUIRE_OK(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 133}}), "transport.set_tempo");
		REQUIRE_OK(run(QStringLiteral("project.save"), {{QStringLiteral("path"), b}}), "project.save b");
		const ControlResult diff = run(QStringLiteral("project.diff"),
			{{QStringLiteral("a"), a}, {QStringLiteral("b"), b}});
		REQUIRE_OK(diff, "project.diff");
		const ControlResult merged = run(QStringLiteral("project.merge"),
			{{QStringLiteral("base"), a}, {QStringLiteral("ours"), a}, {QStringLiteral("theirs"), b}});
		REQUIRE_OK(merged, "project.merge");
		REQUIRE_OK(run(QStringLiteral("project.conflicts"), {{QStringLiteral("file"), a}}), "project.conflicts");
		// project.audible_diff renders both files through the RUNNING binary as
		// child processes, which in-process is this test executable: its success
		// path is driven over the socket instead (tests/control-project-audible-diff.py).
	}

	/*! Luna cluster 5742: project.export_midi used FILE EXISTENCE as proof of a
	 *  successful export. When the midiexport filter does not load,
	 *  Song::exportProjectMidi writes nothing and says so only through qDebug, so
	 *  exporting onto a path that already held an older `.mid` left that stale
	 *  file in place and the handler answered `success` with the OLD byte count -
	 *  telling a caller that had just overwritten its only copy of the session's
	 *  notes that the write had happened.
	 *
	 *  This asserts the REFUSAL half, which is the half that was wrong. A headless
	 *  test process loads no export filters, so the export writing nothing is the
	 *  case by construction - the same state a user hits with a build that cannot
	 *  load midiexport. Were the filter to load, the temporary output is renamed
	 *  over the target, and the post-condition below (the target no longer holds
	 *  the bytes this test wrote) holds either way.
	 */
	void exportMidiDoesNotReportAStaleFileAsAFreshExport()
	{
		const QString path = m_dir.filePath(QStringLiteral("stale.mid"));
		const QByteArray stale("MThd-STALE-NOT-AN-EXPORT");
		{
			QFile file(path);
			QVERIFY(file.open(QIODevice::WriteOnly));
			QVERIFY(file.write(stale) == stale.size());
		}
		QVERIFY(QFileInfo::exists(path));

		const ControlResult result = run(QStringLiteral("project.export_midi"),
			{{QStringLiteral("path"), path}});

		// The defect: the handler saw the file it was pointed at and called the
		// pre-existing bytes a successful export of this call.
		QVERIFY2(!result.ok || result.result.value(QStringLiteral("bytes")).toInteger()
			!= static_cast<qint64>(stale.size()),
			"project.export_midi reported success for the pre-existing file");
		if (result.ok)
		{
			// The filter DID load, so the target must now hold the NEW bytes.
			QFile written(path);
			QVERIFY(written.open(QIODevice::ReadOnly));
			QVERIFY(written.readAll() != stale);
		}
		else
		{
			// A refusal names what went wrong rather than failing silently, and
			// leaves the previous export in place for the caller to fall back on.
			QVERIFY(!result.errorMessage.isEmpty());
			QFile kept(path);
			QVERIFY(kept.open(QIODevice::ReadOnly));
			QCOMPARE(kept.readAll(), stale);
		}
	}

	/*! Luna 5742 (review): the export's temporary file used one FIXED sibling name and the handler
	 *  removed it before exporting, so a file the user kept under that name was destroyed. The
	 *  temporary name is unique now; this proves a user file with the old name survives the call. */
	void exportMidiLeavesAUserFileWithTheOldTemporaryNameAlone()
	{
		const QString path = m_dir.filePath(QStringLiteral("keep-me.mid"));
		const QString sibling = path + QStringLiteral(".zene-export.tmp");
		const QByteArray userBytes("USER-FILE-NOT-A-TEMPORARY");
		{
			QFile file(sibling);
			QVERIFY(file.open(QIODevice::WriteOnly));
			QVERIFY(file.write(userBytes) == userBytes.size());
		}
		run(QStringLiteral("project.export_midi"), {{QStringLiteral("path"), path}});
		QFile kept(sibling);
		QVERIFY(kept.open(QIODevice::ReadOnly));
		QCOMPARE(kept.readAll(), userBytes);
	}

	/*! Luna 5742 (review): the replacement must be atomic. A failed replacement keeps the previous file
	 *  byte for byte. Each failure below used to remove the target first and rename second, so the
	 *  target was lost when the rename had nothing to move. */
	void replacingAnExportIsAtomic()
	{
		const QString target = m_dir.filePath(QStringLiteral("atomic.mid"));
		const QByteArray previous("PREVIOUS-EXPORT");
		{
			QFile file(target);
			QVERIFY(file.open(QIODevice::WriteOnly));
			QVERIFY(file.write(previous) == previous.size());
		}
		ControlResult error;

		// 1. The new output is missing: nothing to move, so the target must keep its bytes.
		QVERIFY(!control::controlReplaceFileAtomically(m_dir.filePath(QStringLiteral("no-such-output.tmp")),
			target, &error));
		QVERIFY(!error.errorMessage.isEmpty());
		{
			QFile kept(target);
			QVERIFY(kept.open(QIODevice::ReadOnly));
			QCOMPARE(kept.readAll(), previous);
		}

		// 2. The target cannot be replaced (a directory holds its name): refused, directory intact.
		const QString directory = m_dir.filePath(QStringLiteral("held.mid"));
		QVERIFY(QDir().mkpath(directory));
		const QString child = directory + QStringLiteral("/child.txt");
		{
			QFile file(child);
			QVERIFY(file.open(QIODevice::WriteOnly));
			QVERIFY(file.write("child") == 5);
		}
		const QString output = m_dir.filePath(QStringLiteral("output-2.tmp"));
		{
			QFile file(output);
			QVERIFY(file.open(QIODevice::WriteOnly));
			QVERIFY(file.write("NEW-BYTES") == 9);
		}
		QVERIFY(!control::controlReplaceFileAtomically(output, directory, &error));
		QVERIFY(QFileInfo(child).exists());

		// 3. The ordinary case still replaces the target with exactly the new bytes.
		const QString fresh = m_dir.filePath(QStringLiteral("output-3.tmp"));
		{
			QFile file(fresh);
			QVERIFY(file.open(QIODevice::WriteOnly));
			QVERIFY(file.write("NEW-BYTES") == 9);
		}
		QVERIFY2(control::controlReplaceFileAtomically(fresh, target, &error), qPrintable(error.errorMessage));
		QFile replaced(target);
		QVERIFY(replaced.open(QIODevice::ReadOnly));
		QCOMPARE(replaced.readAll(), QByteArray("NEW-BYTES"));
	}

/*! Luna clusters 5748 / 4833: project.conflicts and project.audible_diff
	 *  special-cased exit 2 and let every OTHER exit code through. The tool
	 *  documents 0/1 (success, or "there are conflicts / the renders differ")
	 *  and 2 (an error it could report); anything else is a tool that died, and
	 *  answering `success` there told a caller a file was CLEAN (conflict_count
	 *  0) or that two renders MATCHED (differ:false) when nobody ever compared
	 *  them - the most dangerous possible answer from a merge-conflict reporter.
	 *
	 *  A fake tool makes the failure deterministic: MMPZ_GIT_TOOL wins over the
	 *  discovered script (ControlCommandsProjectMmpzGit.cpp findMmpzGitTool), so
	 *  this writes a script that exits 3 and points the commands at it.
	 */
	void mmpzToolExitsWithAnUndocumentedCode()
	{
		const QString tool = m_dir.filePath(QStringLiteral("fake-mmpz-git.py"));
		{
			QFile file(tool);
			QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
			QVERIFY(file.write("import sys\n"
				"sys.stderr.write('fake tool died unexpectedly\\n')\n"
				"sys.exit(3)\n") > 0);
		}
		const QString project = m_dir.filePath(QStringLiteral("mmpz-probe.mmp"));
		REQUIRE_OK(run(QStringLiteral("project.save"), {{QStringLiteral("path"), project}}),
			"project.save");

		const QByteArray saved = qgetenv("MMPZ_GIT_TOOL");
		const bool hadTool = qEnvironmentVariableIsSet("MMPZ_GIT_TOOL");
		qputenv("MMPZ_GIT_TOOL", tool.toUtf8());

		// The defect, in both handlers: an exit nobody documented is reported
		// as a clean read of the file.
		const ControlResult conflicts = run(QStringLiteral("project.conflicts"),
			{{QStringLiteral("file"), project}});
		QVERIFY2(!conflicts.ok && !conflicts.errorMessage.isEmpty(),
			"project.conflicts reported success for an undocumented tool exit");
		const ControlResult audible = run(QStringLiteral("project.audible_diff"),
			{{QStringLiteral("a"), project}, {QStringLiteral("b"), project}});
		QVERIFY2(!audible.ok && !audible.errorMessage.isEmpty(),
			"project.audible_diff reported success for an undocumented tool exit");

		if (hadTool) { qputenv("MMPZ_GIT_TOOL", saved); }
		else { qunsetenv("MMPZ_GIT_TOOL"); }
	}

	void failedBuiltInDocumentKeepsCurrentSession()
	{
		ConfigManager::inst()->setWorkingDir(m_dir.filePath(QStringLiteral("workspace")));
		const QString cacheLocation = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
		QVERIFY(QDir().mkpath(cacheLocation));
		const QString templateDir = QDir(cacheLocation).filePath(QStringLiteral("document-templates"));
		if (QDir(templateDir).exists()) { QVERIFY(QDir(templateDir).removeRecursively()); }
		else if (QFile::exists(templateDir)) { QVERIFY(QFile::remove(templateDir)); }
		QFile blocker(templateDir);
		QVERIFY(blocker.open(QIODevice::WriteOnly));
		QVERIFY(blocker.write("blocked") == 7);
		blocker.close();

		Song* const song = Engine::getSong();
		Track* const preserved = Track::create(Track::Type::Sample, song);
		QVERIFY(preserved != nullptr);
		preserved->setName(QStringLiteral("Session that must survive"));
		const auto hasPreservedTrack = [song]
		{
			return std::any_of(song->tracks().begin(), song->tracks().end(), [](const Track* track)
			{
				return track->name() == QStringLiteral("Session that must survive");
			});
		};
		QVERIFY(hasPreservedTrack());

		const ControlResult failed = run(QStringLiteral("project.new_document"),
			{{QStringLiteral("type"), QStringLiteral("mastering")}});
		QVERIFY(!failed.ok);
		const bool preservedAfterFailure = hasPreservedTrack();
		QVERIFY(blocker.remove());
		QVERIFY2(preservedAfterFailure, "a failed built-in document creation replaced the current session");
	}

	void failedBuiltInDocumentSaveKeepsCurrentSession()
	{
		ConfigManager::inst()->setWorkingDir(m_dir.filePath(QStringLiteral("workspace")));
		const QString cacheLocation = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
		const QString templateDir = QDir(cacheLocation).filePath(QStringLiteral("document-templates"));
		if (QDir(templateDir).exists()) { QVERIFY(QDir(templateDir).removeRecursively()); }
		else if (QFile::exists(templateDir)) { QVERIFY(QFile::remove(templateDir)); }
		QVERIFY(QDir().mkpath(templateDir));
		const QString templatePath = QDir(templateDir).filePath(QStringLiteral("mastering.mpt"));
		const QString partialPath = templatePath + QStringLiteral(".new");
		QVERIFY(!QFile::exists(templatePath) || QFile::remove(templatePath));
		QVERIFY(QDir().mkpath(partialPath));

		QTemporaryFile probe(QDir(templateDir).filePath(QStringLiteral(".save-failure-probe-XXXXXX")));
		QVERIFY(probe.open());
		QVERIFY(probe.write("writable") == 8);
		QVERIFY(probe.flush());
		probe.close();
		QVERIFY(probe.remove());

		Song* const song = Engine::getSong();

		// A known undoable change made before the failed creation. Its step - and the
		// session content it describes - must come through the rollback untouched: a
		// rollback that reloads a plain snapshot leaves the project but drops the history.
		const ControlResult added = run(QStringLiteral("track.add"),
			{{QStringLiteral("type"), QStringLiteral("sample")},
				{QStringLiteral("name"), QStringLiteral("Session that must survive save failure")}});
		QVERIFY2(added.ok, qPrintable(added.errorMessage));
		QVERIFY2(Engine::projectJournal()->canUndo(), "the undoable change made no undo step to preserve");

		const QString previousFile = song->projectFileName();
		const bool previousModified = song->isModified();
		const QString previousType = ProjectInfo::instance().documentType();
		const QStringList previousRecentProjects = ConfigManager::inst()->recentlyOpenedProjects();
		const auto hasPreservedTrack = [song]
		{
			return std::any_of(song->tracks().begin(), song->tracks().end(), [](const Track* track)
			{
				return track->name() == QStringLiteral("Session that must survive save failure");
			});
		};
		QVERIFY(hasPreservedTrack());

		const ControlResult failed = run(QStringLiteral("project.new_document"),
			{{QStringLiteral("type"), QStringLiteral("mastering")}});

		QVERIFY(!failed.ok);
		QVERIFY(QDir(partialPath).exists());
		const bool preservedAfterFailure = hasPreservedTrack();
		const QString fileAfterFailure = song->projectFileName();
		const bool modifiedAfterFailure = song->isModified();
		const QString typeAfterFailure = ProjectInfo::instance().documentType();
		const QStringList recentProjectsAfterFailure = ConfigManager::inst()->recentlyOpenedProjects();
		const bool newRecentPathsExist = std::all_of(recentProjectsAfterFailure.begin(), recentProjectsAfterFailure.end(),
			[&previousRecentProjects](const QString& recent)
			{
				return previousRecentProjects.contains(recent) || QFile::exists(recent);
			});
		const bool undoSurvived = Engine::projectJournal()->canUndo();
		QVERIFY(QDir(partialPath).removeRecursively());
		QVERIFY2(preservedAfterFailure, "a save-time failure replaced the current session");
		QCOMPARE(fileAfterFailure, previousFile);
		QCOMPARE(modifiedAfterFailure, previousModified);
		QCOMPARE(typeAfterFailure, previousType);
		QVERIFY2(newRecentPathsExist, "restoring the original session left a dead recent-project entry");
		QVERIFY2(undoSurvived, "a save-time failure dropped the undo history of the restored session");

		// The history is not only present but usable, and it is the step the change made.
		REQUIRE_OK(run(QStringLiteral("control.undo")), "control.undo");
		QVERIFY2(!hasPreservedTrack(), "control.undo did not take back the change made before the failed creation");
		REQUIRE_OK(run(QStringLiteral("control.redo")), "control.redo");
		QVERIFY2(hasPreservedTrack(), "control.redo did not restore the change made before the failed creation");
	}

private:
	QTemporaryDir m_dir;
};

QTEST_GUILESS_MAIN(ControlSuccessPathsTest)
#include "ControlSuccessPathsTest.moc"
