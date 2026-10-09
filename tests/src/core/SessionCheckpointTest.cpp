/*
 * SessionCheckpointTest.cpp - Q050-044: undo checkpoints for project.open and script.run
 *
 * The two operations that rewrite more of the session than the registry can describe
 * leave ONE undo step that restores the whole project, and refuse to run when that
 * step would not fit the undo stack's byte budget:
 *
 *   1. aScriptRunIsOneUndoStepAndRedoReappliesIt
 *        a script that adds a mixer channel, renames it and runs a registered command:
 *        one control.undo restores the project byte-for-byte (the whole-project
 *        snapshot a save would build, ids included), one control.redo reapplies the
 *        run, and the script cost exactly one journal step.
 *   2. aScriptThatFailsHalfWayStillLeavesOneUndoStep
 *        the edits made before the error stay, and one undo takes all of them back.
 *   3. aScriptThatChangesNothingLeavesNoStep
 *        no step, and the record says why it claims nothing.
 *   4. openingOverAnUnsavedSessionIsOneUndoStep
 *        project.open over a dirty session: one undo restores the displaced session
 *        (content and file name), one redo opens the project again.
 *   5. aProjectOpenedWithItsHistoryLeavesTheDisplacedSessionOneStepAway
 *        the opened project's saved history sits under the open's step; undoing the open leaves
 *        the displaced session and none of that history.
 *   6. openingOverASavedSessionRecordsNoStep
 *        a session with no unsaved changes is held by its file: no step, typed
 *        irreversible refusal on undo, as before.
 *   7. anOverLimitCheckpointRefusesBeforeAnythingChanges
 *        with the budget below the snapshot, project.open and script.run answer a
 *        typed refusal naming the budget, and neither project nor script ran.
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

#include <QtTest>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "DocumentIndex.h"
#include "Engine.h"
#include "Mixer.h"
#include "ProjectJournal.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString compact(const QJsonObject& object)
{
	return QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Compact));
}

int journalDepth()
{
	return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
}

//! The content of the project as one canonical digest per section over the file a save writes (the
//! UndoHistoryPersistenceTest precedent): <head> and every <song> section, without the revision
//! stamps (who saved, not what the project is) and without the append-only provenance log an undo
//! never rewinds. Readable lines, so a mismatch names the section.
void stripRevisionStamps(QDomElement element)
{
	element.removeAttribute(QStringLiteral("rev"));
	element.removeAttribute(QStringLiteral("writer"));
	for (QDomElement child = element.firstChildElement(); !child.isNull(); child = child.nextSiblingElement())
	{
		stripRevisionStamps(child);
	}
}

QString wholeProject()
{
	static int scratches = 0;
	const QString scratch = QDir(QDir::tempPath()).filePath(QStringLiteral("session-checkpoint-%1-%2.mmp")
		.arg(QCoreApplication::applicationPid()).arg(++scratches));
	if (!Engine::getSong()->saveProjectFile(scratch)) { return QStringLiteral("<save refused>"); }
	QDomDocument document;
	QFile file(scratch);
	if (!file.open(QIODevice::ReadOnly) || !document.setContent(&file)) { return QStringLiteral("<unreadable>"); }
	file.close();
	QFile::remove(scratch);
	QString out;
	const QDomElement root = document.documentElement();
	const auto add = [&out](QDomElement element) {
		stripRevisionStamps(element);
		out += element.tagName() + QLatin1Char(' ') + sectionDigest(element) + QLatin1Char('\n');
	};
	add(root.firstChildElement(QStringLiteral("head")));
	for (QDomElement child = root.firstChildElement(QStringLiteral("song")).firstChildElement(); !child.isNull();
		child = child.nextSiblingElement())
	{
		const QString name = child.tagName();
		if (name == QLatin1String("z:provenance") || name == QLatin1String("z:undohistory")
			|| name == QLatin1String("z:index")) { continue; }
		add(child);
	}
	return out;
}

//! Names the sections that differ.
QString firstDifference(const QString& was, const QString& now)
{
	return QStringLiteral("the project differs.\n--- expected\n%1--- now\n%2").arg(was, now);
}

#define SAME_PROJECT(now, expected) \
	QVERIFY2((now) == (expected), qPrintable(firstDifference((expected), (now))))

QStringList trackNames()
{
	QStringList names;
	for (const QJsonValue& track : run(QStringLiteral("track.list")).result.value(QStringLiteral("tracks")).toArray())
	{
		names.append(track.toObject().value(QStringLiteral("name")).toString());
	}
	return names;
}

//! The newest recorded transaction of @a command.
QJsonObject lastRecord(const QString& command)
{
	QJsonObject found;
	for (const QJsonValue& value : run(QStringLiteral("control.transactions")).result
			.value(QStringLiteral("transactions")).toArray())
	{
		if (value.toObject().value(QStringLiteral("command")).toString() == command) { found = value.toObject(); }
	}
	return found;
}

//! Adds a channel, renames it, and runs a registered command from the script: three kinds of write.
const char* const EditScript =
	"local channel = zene.mixer():addChannel()\n"
	"channel:setName('from-script')\n"
	"channel:setGain(0.25)\n"
	"zene.command('track.add', {type = 'instrument', name = 'script-track'})\n";

} // namespace

class SessionCheckpointTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;

	QString path(const QString& name) const { return m_dir.filePath(name); }

	QString makePartialTarget(const QString& suffix)
	{
		const QString name = QStringLiteral("session-checkpoint-%1-%2.mmp")
			.arg(suffix).arg(QCoreApplication::applicationPid());
		const QString targetPath = QDir::current().filePath(name);
		freshProject(QStringLiteral("partial-target"), QString());
		if (!Engine::getSong()->saveProjectFile(targetPath)) { return QString(); }
		QFile target(targetPath);
		if (!target.open(QIODevice::ReadOnly)) { return QString(); }
		QByteArray document = target.readAll();
		target.close();
		const qsizetype songEnd = document.indexOf("</song>");
		if (songEnd < 0) { return QString(); }
		document.insert(songEnd, "<newsection/>\n");
		if (!target.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return QString(); }
		const bool wrote = target.write(document) == static_cast<qint64>(document.size());
		target.close();
		return wrote ? name : QString();
	}

	//! A fresh project with one named instrument track, saved as @a file (or left unsaved when empty).
	void freshProject(const QString& trackName, const QString& file)
	{
		Engine::getSong()->clearProject();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")},
			{QStringLiteral("name"), trackName}}).ok);
		if (!file.isEmpty())
		{
			const ControlResult saved = run(QStringLiteral("project.save"), {{QStringLiteral("path"), path(file)}});
			QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		}
	}

	static void restoreBudget() { Engine::projectJournal()->setMaxUndoBytes(ProjectJournal::DefaultMaxUndoBytes); }

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		// A throwaway config: project.open records recent files.
		qputenv("HOME", m_dir.path().toLocal8Bit());
		ConfigManager::inst()->loadConfigFile(m_dir.filePath(QStringLiteral("zene.xml")));
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		restoreBudget();
		Engine::projectJournal()->clearJournal();
	}

	void aScriptRunIsOneUndoStepAndRedoReappliesIt()
	{
		freshProject(QStringLiteral("base"), QString());
		Engine::projectJournal()->clearJournal();
		const QString before = wholeProject();
		const int channelsBefore = Engine::mixer()->numChannels();
		const int depthBefore = journalDepth();

		const ControlResult ran = run(QStringLiteral("script.run"), {{QStringLiteral("source"), QString::fromUtf8(EditScript)}});
		QVERIFY2(ran.ok, qPrintable(ran.errorMessage + compact(ran.result)));
		const QString after = wholeProject();
		QVERIFY2(after != before, "the script changed nothing");
		QCOMPARE(int(Engine::mixer()->numChannels()), channelsBefore + 1);
		QVERIFY(trackNames().contains(QStringLiteral("script-track")));
		QVERIFY2(ran.result.value(QStringLiteral("changed_project")).toBool(), qPrintable(compact(ran.result)));
		// One run, one step - however many writes (and nested commands) it made.
		QCOMPARE(journalDepth(), depthBefore + 1);

		const QJsonObject record = lastRecord(QStringLiteral("script.run"));
		QCOMPARE(record.value(QStringLiteral("class")).toString(), QStringLiteral("snapshot"));
		QVERIFY2(record.value(QStringLiteral("reversible")).toBool(), qPrintable(compact(record)));

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok && undone.result.value(QStringLiteral("undone")).toBool(), qPrintable(undone.errorMessage));
		SAME_PROJECT(wholeProject(), before);
		QCOMPARE(int(Engine::mixer()->numChannels()), channelsBefore);
		QVERIFY(!trackNames().contains(QStringLiteral("script-track")));

		const ControlResult redone = run(QStringLiteral("control.redo"));
		QVERIFY2(redone.ok, qPrintable(redone.errorMessage + compact(redone.result)));
		SAME_PROJECT(wholeProject(), after);
		QCOMPARE(int(Engine::mixer()->numChannels()), channelsBefore + 1);
		QVERIFY(trackNames().contains(QStringLiteral("script-track")));

		// And back again: the step survives a full undo/redo cycle (the journal's own Undo, Ctrl+Z - a
		// redo issues the step a fresh serial, so the record of the first run no longer names it).
		Engine::projectJournal()->undo();
		SAME_PROJECT(wholeProject(), before);
		Engine::projectJournal()->redo();
		SAME_PROJECT(wholeProject(), after);
	}

	void aScriptThatFailsHalfWayStillLeavesOneUndoStep()
	{
		freshProject(QStringLiteral("base"), QString());
		Engine::projectJournal()->clearJournal();
		const QString before = wholeProject();
		const int depthBefore = journalDepth();

		const ControlResult ran = run(QStringLiteral("script.run"), {{QStringLiteral("source"),
			QStringLiteral("zene.mixer():addChannel()\nzene.command('track.add', {type = 'instrument', name = 'half'})\n"
				"error('boom')\n")}});
		QVERIFY(!ran.ok);
		QVERIFY2(ran.errorMessage.contains(QStringLiteral("ONE undo step")), qPrintable(ran.errorMessage));
		QVERIFY(trackNames().contains(QStringLiteral("half")));
		QCOMPARE(journalDepth(), depthBefore + 1);

		Engine::projectJournal()->undo();
		SAME_PROJECT(wholeProject(), before);
		QVERIFY(!trackNames().contains(QStringLiteral("half")));
	}

	void aFailedScriptStepIsRecordedAboveAnIrreversibleCommand()
	{
		freshProject(QStringLiteral("before"), QStringLiteral("before-error.mmp"));
		freshProject(QStringLiteral("opened"), QStringLiteral("opened-error.mmp"));
		Engine::projectJournal()->clearJournal();
		QVERIFY(!Engine::getSong()->isModified());

		// This successful open is intentionally not undoable: it displaced a clean session.
		const ControlResult opened = run(QStringLiteral("project.open"),
			{{QStringLiteral("path"), path(QStringLiteral("opened-error.mmp"))}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QVERIFY(!opened.result.value(QStringLiteral("checkpoint")).toObject()
			.value(QStringLiteral("undoable")).toBool());
		QCOMPARE(journalDepth(), 0);
		const QString before = wholeProject();

		const ControlResult failed = run(QStringLiteral("script.run"), {{QStringLiteral("source"),
			QStringLiteral("zene.command('track.add', {type = 'instrument', name = 'before-error'})\n"
				"error('intentional failure after the edit')\n")}});
		QVERIFY(!failed.ok);
		QVERIFY(trackNames().contains(QStringLiteral("before-error")));

		const QJsonObject record = lastRecord(QStringLiteral("script.run"));
		QVERIFY2(record.value(QStringLiteral("reversible")).toBool(), qPrintable(compact(record)));
		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok && undone.result.value(QStringLiteral("undone")).toBool(),
			qPrintable(undone.errorMessage + compact(undone.result)));
		SAME_PROJECT(wholeProject(), before);
		QVERIFY(!trackNames().contains(QStringLiteral("before-error")));
	}

	void aScriptThatLeavesAPartialLoadCanRedoIt()
	{
		const QString targetName = makePartialTarget(QStringLiteral("script-partial"));
		QVERIFY(!targetName.isEmpty());
		const QString partialPath = QDir::current().filePath(targetName);

		freshProject(QStringLiteral("displaced"), QStringLiteral("displaced-partial.mmp"));
		Engine::projectJournal()->clearJournal();
		const QString before = wholeProject();
		const QString beforeFile = Engine::getSong()->projectFileName();
		const QString source = QStringLiteral(
			"local opened, err = zene.command('project.open', {path = '%1', "
			"sections = {'newsection'}})\nif not opened then error(err) end\n").arg(targetName);
		const ControlResult ran = run(QStringLiteral("script.run"), {{QStringLiteral("source"), source}});
		QVERIFY2(ran.ok, qPrintable(ran.errorMessage));
		QVERIFY(Engine::getSong()->isPartialLoad());
		QCOMPARE(Engine::getSong()->notLoadedSections(), QStringList{QStringLiteral("newsection")});
		QCOMPARE(trackNames(), QStringList{QStringLiteral("partial-target")});

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		SAME_PROJECT(wholeProject(), before);
		QCOMPARE(Engine::getSong()->projectFileName(), beforeFile);
		QVERIFY(!Engine::getSong()->isPartialLoad());

		const ControlResult redone = run(QStringLiteral("control.redo"));
		QVERIFY2(redone.ok, qPrintable(redone.errorMessage));
		QVERIFY(Engine::getSong()->isPartialLoad());
		QCOMPARE(Engine::getSong()->notLoadedSections(), QStringList{QStringLiteral("newsection")});
		QCOMPARE(trackNames(), QStringList{QStringLiteral("partial-target")});
		QCOMPARE(Engine::getSong()->projectFileName(), partialPath);
		QVERIFY(QFile::remove(partialPath));
	}

	void openingAPartialProjectOverDirtyWorkCanRedoIt()
	{
		const QString targetName = makePartialTarget(QStringLiteral("open-partial"));
		QVERIFY(!targetName.isEmpty());
		const QString partialPath = QDir::current().filePath(targetName);
		freshProject(QStringLiteral("displaced"), QStringLiteral("displaced-open-partial.mmp"));
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")},
			{QStringLiteral("name"), QStringLiteral("unsaved-before-partial-open")}}).ok);
		Engine::projectJournal()->clearJournal();
		const QString before = wholeProject();
		const QString beforeFile = Engine::getSong()->projectFileName();

		const ControlResult opened = run(QStringLiteral("project.open"),
			{{QStringLiteral("path"), partialPath},
				{QStringLiteral("sections"), QJsonArray{QStringLiteral("newsection")}}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QVERIFY(opened.result.value(QStringLiteral("checkpoint")).toObject()
			.value(QStringLiteral("undoable")).toBool());
		QVERIFY(Engine::getSong()->isPartialLoad());
		QCOMPARE(Engine::getSong()->notLoadedSections(), QStringList{QStringLiteral("newsection")});

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		SAME_PROJECT(wholeProject(), before);
		QCOMPARE(Engine::getSong()->projectFileName(), beforeFile);
		QVERIFY(!Engine::getSong()->isPartialLoad());
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QVERIFY(Engine::getSong()->isPartialLoad());
		QCOMPARE(Engine::getSong()->notLoadedSections(), QStringList{QStringLiteral("newsection")});
		QCOMPARE(trackNames(), QStringList{QStringLiteral("partial-target")});
		QCOMPARE(Engine::getSong()->projectFileName(), partialPath);
		QVERIFY(QFile::remove(partialPath));
	}

	void aScriptThatChangesNothingLeavesNoStep()
	{
		freshProject(QStringLiteral("base"), QString());
		Engine::projectJournal()->clearJournal();
		const int depthBefore = journalDepth();
		const ControlResult ran = run(QStringLiteral("script.run"), {{QStringLiteral("source"), QStringLiteral("print('hello')\n")}});
		QVERIFY2(ran.ok, qPrintable(ran.errorMessage));
		QVERIFY(!ran.result.value(QStringLiteral("changed_project")).toBool());
		QCOMPARE(journalDepth(), depthBefore);
		const QJsonObject record = lastRecord(QStringLiteral("script.run"));
		QVERIFY2(!record.value(QStringLiteral("reversible")).toBool(true), qPrintable(compact(record)));
		QVERIFY2(record.value(QStringLiteral("mechanism")).toString().contains(QStringLiteral("nothing to undo")),
			qPrintable(compact(record)));
	}

	void openingOverAnUnsavedSessionIsOneUndoStep()
	{
		freshProject(QStringLiteral("bravo"), QStringLiteral("b.mmp"));
		freshProject(QStringLiteral("alpha"), QStringLiteral("a.mmp"));
		// Unsaved work on top of the saved file: a rename, a new track and a tempo change.
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")},
			{QStringLiteral("name"), QStringLiteral("unsaved-extra")}}).ok);
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 133}}).ok);
		QVERIFY(Engine::getSong()->isModified());
		Engine::projectJournal()->clearJournal();
		const QString displaced = wholeProject();
		const QStringList displacedTracks = trackNames();
		QVERIFY(displacedTracks.contains(QStringLiteral("unsaved-extra")));
		const QString displacedFile = Engine::getSong()->projectFileName();
		QVERIFY(displacedFile.endsWith(QStringLiteral("a.mmp")));

		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), path(QStringLiteral("b.mmp"))}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		const QJsonObject checkpoint = opened.result.value(QStringLiteral("checkpoint")).toObject();
		QVERIFY2(checkpoint.value(QStringLiteral("undoable")).toBool(), qPrintable(compact(checkpoint)));
		QVERIFY(checkpoint.value(QStringLiteral("bytes")).toDouble() > 0);
		QCOMPARE(trackNames(), QStringList{QStringLiteral("bravo")});
		QVERIFY(Engine::getSong()->projectFileName().endsWith(QStringLiteral("b.mmp")));
		const QString opened_b = wholeProject();
		QCOMPARE(journalDepth(), 1);

		const QJsonObject record = lastRecord(QStringLiteral("project.open"));
		QCOMPARE(record.value(QStringLiteral("class")).toString(), QStringLiteral("snapshot"));
		QVERIFY2(record.value(QStringLiteral("reversible")).toBool(), qPrintable(compact(record)));

		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok && undone.result.value(QStringLiteral("undone")).toBool(),
			qPrintable(undone.errorMessage + compact(undone.result)));
		SAME_PROJECT(wholeProject(), displaced);
		QCOMPARE(trackNames(), displacedTracks);
		QCOMPARE(Engine::getSong()->projectFileName(), displacedFile);
		QCOMPARE(int(Engine::getSong()->getTempo()), 133);

		const ControlResult redone = run(QStringLiteral("control.redo"));
		QVERIFY2(redone.ok, qPrintable(redone.errorMessage + compact(redone.result)));
		SAME_PROJECT(wholeProject(), opened_b);
		QCOMPARE(trackNames(), QStringList{QStringLiteral("bravo")});
		QVERIFY(Engine::getSong()->projectFileName().endsWith(QStringLiteral("b.mmp")));

		// The journal's own Undo (Ctrl+Z in the interface) takes the same step.
		Engine::projectJournal()->undo();
		SAME_PROJECT(wholeProject(), displaced);
	}

	void aProjectOpenedWithItsHistoryLeavesTheDisplacedSessionOneStepAway()
	{
		// A project that saved its undo history (WF-033): two renames of one track.
		freshProject(QStringLiteral("h1"), QString());
		QVERIFY(run(QStringLiteral("project.set_undo_history"), {{QStringLiteral("setting"), QStringLiteral("on")}}).ok);
		const QString track = run(QStringLiteral("track.list")).result.value(QStringLiteral("tracks")).toArray()
			.first().toObject().value(QStringLiteral("id")).toString();
		QVERIFY(!track.isEmpty());
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), track}, {QStringLiteral("name"), QStringLiteral("h2")}}).ok);
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), track}, {QStringLiteral("name"), QStringLiteral("h3")}}).ok);
		const ControlResult saved = run(QStringLiteral("project.save"), {{QStringLiteral("path"), path(QStringLiteral("h.mmp"))}});
		QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
		QVERIFY2(saved.result.value(QStringLiteral("undo_history")).toObject().value(QStringLiteral("written")).toBool(),
			qPrintable(compact(saved.result)));

		freshProject(QStringLiteral("displaced"), QString());
		Engine::projectJournal()->clearJournal();
		const QString displaced = wholeProject();

		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), path(QStringLiteral("h.mmp"))}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QVERIFY(opened.result.value(QStringLiteral("checkpoint")).toObject().value(QStringLiteral("undoable")).toBool());
		const QString openedContent = wholeProject();
		QVERIFY2(journalDepth() >= 3, "the file's two history steps and the checkpoint are on the stack");

		// The open is the newest action: one undo is the displaced session, and the opened project's own
		// history is not left beneath it (one more undo would replace the session with ITS earlier state).
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		SAME_PROJECT(wholeProject(), displaced);
		QCOMPARE(journalDepth(), 0);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		SAME_PROJECT(wholeProject(), openedContent);
		Engine::projectJournal()->undo();
		SAME_PROJECT(wholeProject(), displaced);
	}

	void openingOverASavedSessionRecordsNoStep()
	{
		freshProject(QStringLiteral("bravo"), QStringLiteral("b2.mmp"));
		freshProject(QStringLiteral("alpha"), QStringLiteral("a2.mmp"));
		QVERIFY(!Engine::getSong()->isModified());
		Engine::projectJournal()->clearJournal();

		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), path(QStringLiteral("b2.mmp"))}});
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QVERIFY(!opened.result.value(QStringLiteral("checkpoint")).toObject().value(QStringLiteral("undoable")).toBool());
		QCOMPARE(journalDepth(), 0);
		const QJsonObject record = lastRecord(QStringLiteral("project.open"));
		QVERIFY2(!record.value(QStringLiteral("reversible")).toBool(true), qPrintable(compact(record)));
		QCOMPARE(record.value(QStringLiteral("before")).toObject().value(QStringLiteral("previous_file")).toString(),
			path(QStringLiteral("a2.mmp")));
		const ControlResult undone = run(QStringLiteral("control.undo"));
		QVERIFY(!undone.ok);
		QCOMPARE(undone.errorKind, ControlErrorKind::Irreversible);
		QCOMPARE(trackNames(), QStringList{QStringLiteral("bravo")});
	}

	void anOverLimitCheckpointRefusesBeforeAnythingChanges()
	{
		freshProject(QStringLiteral("bravo"), QStringLiteral("b3.mmp"));
		freshProject(QStringLiteral("alpha"), QStringLiteral("a3.mmp"));
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")},
			{QStringLiteral("name"), QStringLiteral("dirty")}}).ok);
		Engine::projectJournal()->clearJournal();
		const QString before = wholeProject();
		const QStringList tracksBefore = trackNames();
		const QString fileBefore = Engine::getSong()->projectFileName();
		const int channelsBefore = Engine::mixer()->numChannels();

		// The budget is below any snapshot of this session.
		QVERIFY(Engine::projectJournal()->setMaxUndoBytes(64));

		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), path(QStringLiteral("b3.mmp"))}});
		QVERIFY2(!opened.ok, "an over-limit checkpoint did not refuse the open");
		QCOMPARE(opened.errorKind, ControlErrorKind::Refused);
		QVERIFY2(opened.errorMessage.contains(QStringLiteral("Nothing was changed"))
				&& opened.errorMessage.contains(QStringLiteral("control.set_undo_depth")),
			qPrintable(opened.errorMessage));
		SAME_PROJECT(wholeProject(), before);
		QCOMPARE(trackNames(), tracksBefore);
		QCOMPARE(Engine::getSong()->projectFileName(), fileBefore);

		const ControlResult scripted = run(QStringLiteral("script.run"), {{QStringLiteral("source"), QString::fromUtf8(EditScript)}});
		QVERIFY2(!scripted.ok, "an over-limit checkpoint did not refuse the script");
		QCOMPARE(scripted.errorKind, ControlErrorKind::Refused);
		QVERIFY2(scripted.errorMessage.contains(QStringLiteral("script.run refused")), qPrintable(scripted.errorMessage));
		SAME_PROJECT(wholeProject(), before);
		QCOMPARE(int(Engine::mixer()->numChannels()), channelsBefore);
		QCOMPARE(journalDepth(), 0);

		// Raising the budget (the message's own advice) lets the same open through, undoably.
		restoreBudget();
		const ControlResult again = run(QStringLiteral("project.open"), {{QStringLiteral("path"), path(QStringLiteral("b3.mmp"))}});
		QVERIFY2(again.ok, qPrintable(again.errorMessage));
		QVERIFY(again.result.value(QStringLiteral("checkpoint")).toObject().value(QStringLiteral("undoable")).toBool());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		SAME_PROJECT(wholeProject(), before);
	}
};

QTEST_GUILESS_MAIN(SessionCheckpointTest)
#include "SessionCheckpointTest.moc"
