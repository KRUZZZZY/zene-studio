/*
 * HybridTrackInstrumentRoleTest.cpp - a neutral row given an instrument, and back (Q050-283.2)
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

/*! 0.5.0 L04, Q050-283.2 (R-workflow-286), stage 2 of 3: the model half of assigning an instrument.
 *
 *  - track.set_role role=instrument on a neutral row makes it an instrument track playing the named
 *    plug-in, with the SAME trk-<n> id, the same position, name, mute and folder, and every other
 *    track (and its clips) exactly as it was;
 *  - role=neutral switches it back and the row KEEPS the instrument role - plug-in, track settings,
 *    clips and notes - readable as `kept_roles`, saved and loaded with the project, and given back by
 *    the next role=instrument;
 *  - each switch is one undo step whose undo and redo keep the id and position (true_inverse);
 *  - bad requests are typed refusals that change nothing and record no step. */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "AutomatableModel.h"
#include "AutomationClip.h"
#include "AutomationTrack.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "MidiClip.h"
#include "NeutralTrack.h"
#include "Song.h"
#include "Track.h"
#include "TrackFolder.h"

using namespace lmms;

namespace
{

const QString Synth = QStringLiteral("tripleoscillator");

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QString add(const QString& type, const QString& name)
{
	return run(QStringLiteral("track.add"), {{QStringLiteral("type"), type}, {QStringLiteral("name"), name}})
		.result.value(QStringLiteral("track")).toString();
}

ControlResult setRole(const QString& track, const QString& role, const QString& instrument = QString())
{
	QJsonObject args{{QStringLiteral("track"), track}, {QStringLiteral("role"), role}};
	if (!instrument.isEmpty()) { args.insert(QStringLiteral("instrument"), instrument); }
	return run(QStringLiteral("track.set_role"), args);
}

QJsonObject stateOf(const QString& track)
{
	return run(QStringLiteral("track.get_state"), {{QStringLiteral("track"), track}}).result;
}

QJsonArray listed()
{
	return run(QStringLiteral("track.list")).result.value(QStringLiteral("tracks")).toArray();
}

//! Every row except @a skip, as id|type|name|clips: what "the rest of the project" looks like.
QStringList othersThan(const QString& skip)
{
	QStringList rows;
	for (const QJsonValue& value : listed())
	{
		const QJsonObject row = value.toObject();
		const QString id = row.value(QStringLiteral("id")).toString();
		if (id == skip) { continue; }
		rows << QStringLiteral("%1|%2|%3|%4").arg(id, row.value(QStringLiteral("type")).toString(),
			row.value(QStringLiteral("name")).toString())
			.arg(Engine::getSong()->findTrackById(id.mid(4).toInt())->numOfClips());
	}
	return rows;
}

Track* trackOf(const QString& id) { return Engine::getSong()->findTrackById(id.mid(4).toInt()); }

int indexOf(const QString& id)
{
	const auto& tracks = Engine::getSong()->tracks();
	for (int i = 0; i < static_cast<int>(tracks.size()); ++i)
	{
		if (control::trackIdOf(tracks[i]) == id) { return i; }
	}
	return -1;
}

int undoDepth()
{
	return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
}

QString pluginOf(const QString& id)
{
	auto* track = dynamic_cast<InstrumentTrack*>(trackOf(id));
	return track != nullptr && track->instrument() != nullptr
		? QString::fromUtf8(track->instrument()->descriptor()->name) : QString();
}

//! The notes on the row's clips, summed.
int noteCount(const QString& id)
{
	int notes = 0;
	for (Clip* clip : trackOf(id)->getClips())
	{
		if (auto* midi = dynamic_cast<MidiClip*>(clip)) { notes += static_cast<int>(midi->notes().size()); }
	}
	return notes;
}

//! Gives @a id (an instrument row) a clip with two notes and a non-default volume: its role content.
void giveContent(const QString& id)
{
	const ControlResult clip = run(QStringLiteral("clip.add"),
		{{QStringLiteral("track"), id}, {QStringLiteral("position"), 0}, {QStringLiteral("length"), 192}});
	QVERIFY2(clip.ok, qPrintable(clip.errorMessage));
	const QString clipId = clip.result.value(QStringLiteral("clip")).toString();
	for (int key : {60, 64})
	{
		QVERIFY(run(QStringLiteral("note.add"), {{QStringLiteral("clip"), clipId}, {QStringLiteral("key"), key},
			{QStringLiteral("position"), 0}, {QStringLiteral("length"), 48}}).ok);
	}
	static_cast<InstrumentTrack*>(trackOf(id))->volumeModel()->setValue(42.0f);
}

void verifyRoleContent(const QString& id)
{
	QCOMPARE(pluginOf(id), Synth);
	QCOMPARE(trackOf(id)->numOfClips(), 1);
	QCOMPARE(noteCount(id), 2);
	QCOMPARE(static_cast<InstrumentTrack*>(trackOf(id))->volumeModel()->value(), 42.0f);
}

//! The automation clip at index 0 of the automation track @a id.
AutomationClip* firstAutomationClip(const QString& id)
{
	Track* track = Engine::getSong()->findTrackById(id.mid(4).toInt());
	return track != nullptr && !track->getClips().empty() ? dynamic_cast<AutomationClip*>(track->getClips().front()) : nullptr;
}

//! The clip's targets, as raw pointers.
QList<AutomatableModel*> targetsOf(AutomationClip* clip)
{
	QList<AutomatableModel*> targets;
	for (const auto& target : clip->objectsSnapshot())
	{
		if (target) { targets << target.data(); }
	}
	return targets;
}

//! The first control of the row's instrument plug-in itself (not the track's own knobs).
AutomatableModel* pluginControlOf(const QString& id)
{
	auto* track = dynamic_cast<InstrumentTrack*>(trackOf(id));
	if (track == nullptr || track->instrument() == nullptr) { return nullptr; }
	const auto controls = track->instrument()->findChildren<AutomatableModel*>();
	return controls.isEmpty() ? nullptr : controls.first();
}

} // namespace

class HybridTrackInstrumentRoleTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init() { Engine::getSong()->clearProject(); }

	//! AC1: assigning an instrument keeps the row's id, position and row state, and the rest of the project.
	void assigningAnInstrumentKeepsIdPositionAndProject()
	{
		const QString lead = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")},
			{QStringLiteral("name"), QStringLiteral("Lead")}, {QStringLiteral("instrument"), Synth}})
			.result.value(QStringLiteral("track")).toString();
		giveContent(lead);
		const QString folder = add(QStringLiteral("folder"), QStringLiteral("Group"));
		const QString row = add(QStringLiteral("neutral"), QStringLiteral("Idea"));
		add(QStringLiteral("sample"), QStringLiteral("Loops"));
		QVERIFY(run(QStringLiteral("track.set_folder"), {{QStringLiteral("track"), row}, {QStringLiteral("folder"), folder}}).ok);
		QVERIFY(run(QStringLiteral("track.set_mute"), {{QStringLiteral("track"), row}, {QStringLiteral("muted"), true}}).ok);
		const int index = indexOf(row);
		QCOMPARE(index, 2);
		const QStringList others = othersThan(row);
		QCOMPARE(stateOf(row).value(QStringLiteral("role")).toString(), QStringLiteral("neutral"));

		const ControlResult switched = setRole(row, QStringLiteral("instrument"), Synth);
		QVERIFY2(switched.ok, qPrintable(switched.errorMessage));
		QCOMPARE(switched.result.value(QStringLiteral("track")).toString(), row);
		QCOMPARE(switched.result.value(QStringLiteral("index")).toInt(), index);
		QCOMPARE(switched.result.value(QStringLiteral("type")).toString(), QStringLiteral("instrument"));
		QCOMPARE(switched.result.value(QStringLiteral("role")).toString(), QStringLiteral("instrument"));

		QVERIFY2(dynamic_cast<InstrumentTrack*>(trackOf(row)) != nullptr, "the row is not an instrument track");
		QCOMPARE(pluginOf(row), Synth);
		QCOMPARE(indexOf(row), index);
		QCOMPARE(trackOf(row)->name(), QStringLiteral("Idea"));
		QCOMPARE(trackOf(row)->isMuted(), true);
		QCOMPARE(control::trackIdOf(trackOf(row)->parentFolder()), folder);
		QCOMPARE(othersThan(row), others);
		verifyRoleContent(lead);
		const QJsonObject state = stateOf(row);
		QCOMPARE(state.value(QStringLiteral("type")).toString(), QStringLiteral("instrument"));
		QCOMPARE(state.value(QStringLiteral("role")).toString(), QStringLiteral("instrument"));
		QCOMPARE(listed().at(index).toObject().value(QStringLiteral("id")).toString(), row);
	}

	//! One undo step per switch, and undo / redo keep the id and the position.
	void aSwitchIsOneUndoStepThatKeepsTheRow()
	{
		add(QStringLiteral("sample"), QStringLiteral("Loops"));
		const QString row = add(QStringLiteral("neutral"), QStringLiteral("Idea"));
		add(QStringLiteral("sample"), QStringLiteral("Tail"));
		const int depth = undoDepth();
		QVERIFY(setRole(row, QStringLiteral("instrument"), Synth).ok);
		QCOMPARE(undoDepth(), depth + 1);

		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(dynamic_cast<NeutralTrack*>(trackOf(row)) != nullptr);
		QCOMPARE(indexOf(row), 1);
		QCOMPARE(trackOf(row)->name(), QStringLiteral("Idea"));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), 3);

		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(pluginOf(row), Synth);
		QCOMPARE(indexOf(row), 1);
		QCOMPARE(trackOf(row)->name(), QStringLiteral("Idea"));
		QCOMPARE(static_cast<int>(Engine::getSong()->tracks().size()), 3);

		const QJsonArray records = ControlRegistry::instance()->transactions();
		QJsonObject transaction;
		for (const QJsonValue& value : records)
		{
			if (value.toObject().value(QStringLiteral("command")).toString() == QStringLiteral("track.set_role"))
			{
				transaction = value.toObject();
			}
		}
		QCOMPARE(transaction.value(QStringLiteral("reversible")).toBool(), true);
	}

	//! AC2: switching back to neutral keeps the instrument role, and the next switch gives it all back.
	void switchingBackToNeutralKeepsTheInstrumentRole()
	{
		add(QStringLiteral("sample"), QStringLiteral("Loops"));
		const QString row = add(QStringLiteral("neutral"), QStringLiteral("Idea"));
		QVERIFY(setRole(row, QStringLiteral("instrument"), Synth).ok);
		giveContent(row);

		const ControlResult neutral = setRole(row, QStringLiteral("neutral"));
		QVERIFY2(neutral.ok, qPrintable(neutral.errorMessage));
		QVERIFY(dynamic_cast<NeutralTrack*>(trackOf(row)) != nullptr);
		QCOMPARE(indexOf(row), 1);
		QCOMPARE(trackOf(row)->numOfClips(), 0);
		const QJsonArray kept = stateOf(row).value(QStringLiteral("kept_roles")).toArray();
		QCOMPARE(kept.size(), 1);
		QCOMPARE(kept.first().toObject().value(QStringLiteral("role")).toString(), QStringLiteral("instrument"));
		QCOMPARE(kept.first().toObject().value(QStringLiteral("instrument")).toString(), Synth);

		// Renamed while neutral: the row's name is the row's, and it carries into the instrument role.
		QVERIFY(run(QStringLiteral("track.rename"), {{QStringLiteral("track"), row}, {QStringLiteral("name"), QStringLiteral("Hook")}}).ok);
		const ControlResult back = setRole(row, QStringLiteral("instrument"));
		QVERIFY2(back.ok, qPrintable(back.errorMessage));
		QCOMPARE(indexOf(row), 1);
		QCOMPARE(trackOf(row)->name(), QStringLiteral("Hook"));
		verifyRoleContent(row);

		// And the switch back to neutral is undone with its content too.
		QVERIFY(setRole(row, QStringLiteral("neutral")).ok);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		verifyRoleContent(row);
		QCOMPARE(indexOf(row), 1);
	}

	//! AC2: the kept instrument role survives save/load, and so does the instrument role itself.
	void theKeptRoleRoundTripsThroughSaveAndLoad()
	{
		add(QStringLiteral("instrument"), QStringLiteral("Lead"));
		const QString row = add(QStringLiteral("neutral"), QStringLiteral("Idea"));
		add(QStringLiteral("automation"), QStringLiteral("Auto"));
		QVERIFY(setRole(row, QStringLiteral("instrument"), Synth).ok);
		giveContent(row);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());

		// The active instrument role: a plain instrument track in the file, same id and place.
		const QString active = QDir(dir.path()).filePath(QStringLiteral("active.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(active));
		Engine::getSong()->clearProject();
		Engine::getSong()->loadProject(active);
		QCOMPARE(indexOf(row), 1);
		verifyRoleContent(row);

		// The kept role: neutral in the file, its instrument kept with it, and given back after the load.
		QVERIFY(setRole(row, QStringLiteral("neutral")).ok);
		const QStringList before = othersThan(QString());
		const QString path = QDir(dir.path()).filePath(QStringLiteral("kept.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path));
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		const QString text = QString::fromUtf8(file.readAll());
		QVERIFY2(text.contains(QStringLiteral("<keptrole")), "the kept role was not written");
		Engine::getSong()->clearProject();
		Engine::getSong()->loadProject(path);
		QCOMPARE(othersThan(QString()), before);
		QVERIFY(dynamic_cast<NeutralTrack*>(trackOf(row)) != nullptr);
		QCOMPARE(stateOf(row).value(QStringLiteral("kept_roles")).toArray().size(), 1);
		QVERIFY(setRole(row, QStringLiteral("instrument")).ok);
		QCOMPARE(indexOf(row), 1);
		verifyRoleContent(row);
	}

	//! A neutral row that never had an instrument saves what stage 1 saved: no kept-role mark.
	void aRowWithoutAKeptRoleSavesNoMark()
	{
		const QString row = add(QStringLiteral("neutral"), QStringLiteral("Idea"));
		QCOMPARE(stateOf(row).value(QStringLiteral("kept_roles")).toArray().size(), 0);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = QDir(dir.path()).filePath(QStringLiteral("plain.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path));
		QFile file(path);
		QVERIFY(file.open(QIODevice::ReadOnly));
		QVERIFY(!QString::fromUtf8(file.readAll()).contains(QStringLiteral("keptrole")));
	}

	//! Requests that cannot be honoured are typed refusals: nothing changes and no undo step is recorded.
	void badRequestsAreRefusedWithoutAStep()
	{
		const QString row = add(QStringLiteral("neutral"), QStringLiteral("Idea"));
		const QString loops = add(QStringLiteral("sample"), QStringLiteral("Loops"));
		const int depth = undoDepth();
		QCOMPARE(setRole(row, QStringLiteral("instrument")).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(setRole(row, QStringLiteral("instrument"), QStringLiteral("nosuchsynth")).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(setRole(row, QStringLiteral("neutral")).errorKind, ControlErrorKind::Refused);
		QCOMPARE(setRole(loops, QStringLiteral("instrument"), Synth).errorKind, ControlErrorKind::Refused);
		QVERIFY(dynamic_cast<NeutralTrack*>(trackOf(row)) != nullptr);
		QCOMPARE(undoDepth(), depth);

		// A row that keeps an instrument gives that one back; naming another one is refused.
		QVERIFY(setRole(row, QStringLiteral("instrument"), Synth).ok);
		QVERIFY(setRole(row, QStringLiteral("neutral")).ok);
		const int kept = undoDepth();
		QCOMPARE(setRole(row, QStringLiteral("instrument"), QStringLiteral("kicker")).errorKind, ControlErrorKind::InvalidArgs);
		QCOMPARE(undoDepth(), kept);
	}

	/*! Review r1: the automation links to the kept instrument's controls go with the kept role. An
	 *  automation clip drives the row's volume and one control of its plug-in; the row goes neutral (the
	 *  controls are destroyed), the project is saved and opened again, and the restored instrument is driven
	 *  by the same clip again, its points unchanged. The undo of a switch to neutral gives the links back too. */
	void automationLinksSurviveNeutralSaveLoadAndRestore()
	{
		const QString row = add(QStringLiteral("neutral"), QStringLiteral("Idea"));
		const QString lane = add(QStringLiteral("automation"), QStringLiteral("Auto"));
		QVERIFY(setRole(row, QStringLiteral("instrument"), Synth).ok);
		auto* lanes = dynamic_cast<AutomationTrack*>(trackOf(lane));
		QVERIFY(lanes != nullptr);
		auto* clip = dynamic_cast<AutomationClip*>(lanes->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		AutomatableModel* plugin = pluginControlOf(row);
		QVERIFY2(plugin != nullptr, "the instrument has no control of its own to automate");
		QVERIFY(clip->addObject(static_cast<InstrumentTrack*>(trackOf(row))->volumeModel()));
		QVERIFY(clip->addObject(plugin));
		clip->putValue(TimePos(0), 20.0f, false);
		clip->putValue(TimePos(96), 80.0f, false);
		const int points = clip->getTimeMap().size();
		const QString pluginName = plugin->objectName() + plugin->displayName();

		// Undo of a switch to neutral gives the links back.
		QVERIFY(setRole(row, QStringLiteral("neutral")).ok);
		QCOMPARE(targetsOf(firstAutomationClip(lane)).size(), 0);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(targetsOf(firstAutomationClip(lane)).size(), 2);

		const ControlResult neutral = setRole(row, QStringLiteral("neutral"));
		QVERIFY(neutral.ok);
		QCOMPARE(stateOf(row).value(QStringLiteral("kept_roles")).toArray().first().toObject()
			.value(QStringLiteral("automation_links")).toInt(), 2);
		QCOMPARE(firstAutomationClip(lane)->getTimeMap().size(), points);

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = QDir(dir.path()).filePath(QStringLiteral("links.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path));
		Engine::getSong()->clearProject();
		Engine::getSong()->loadProject(path);
		QVERIFY(dynamic_cast<NeutralTrack*>(trackOf(row)) != nullptr);

		QVERIFY(setRole(row, QStringLiteral("instrument")).ok);
		AutomationClip* restored = firstAutomationClip(lane);
		QVERIFY(restored != nullptr);
		const QList<AutomatableModel*> targets = targetsOf(restored);
		QCOMPARE(targets.size(), 2);
		QVERIFY2(targets.contains(static_cast<InstrumentTrack*>(trackOf(row))->volumeModel()),
			"the clip no longer drives the restored row's volume");
		AutomatableModel* restoredPlugin = pluginControlOf(row);
		QVERIFY(targets.contains(restoredPlugin));
		QCOMPARE(restoredPlugin->objectName() + restoredPlugin->displayName(), pluginName);
		QCOMPARE(restored->getTimeMap().size(), points);
		QCOMPARE(restored->valueAt(TimePos(96)), 80.0f);

		// Undo of the restore takes the links back into the kept role, redo puts them on the instrument again.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(targetsOf(firstAutomationClip(lane)).size(), 0);
		QCOMPARE(stateOf(row).value(QStringLiteral("kept_roles")).toArray().first().toObject()
			.value(QStringLiteral("automation_links")).toInt(), 2);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(targetsOf(firstAutomationClip(lane)).size(), 2);
		QVERIFY(targetsOf(firstAutomationClip(lane)).contains(static_cast<InstrumentTrack*>(trackOf(row))->volumeModel()));
	}

	//! Row mute automation stays bound to the same row control across both role swaps and a save/load.
	void rowMuteAutomationSurvivesRoleSwitch()
	{
		const QString row = add(QStringLiteral("neutral"), QStringLiteral("Idea"));
		const QString lane = add(QStringLiteral("automation"), QStringLiteral("Auto"));
		auto* lanes = dynamic_cast<AutomationTrack*>(trackOf(lane));
		QVERIFY(lanes != nullptr);
		auto* clip = dynamic_cast<AutomationClip*>(lanes->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		QVERIFY(clip->addObject(trackOf(row)->getMutedModel()));
		clip->putValue(TimePos(0), 1.0f, false);
		const int points = clip->getTimeMap().size();
		const int depth = undoDepth();
		QVERIFY(setRole(row, QStringLiteral("instrument"), Synth).ok);
		QCOMPARE(undoDepth(), depth + 1);
		auto* instrument = dynamic_cast<InstrumentTrack*>(trackOf(row));
		QVERIFY(instrument != nullptr);
		QVERIFY(targetsOf(firstAutomationClip(lane)).contains(instrument->getMutedModel()));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(targetsOf(firstAutomationClip(lane)).contains(trackOf(row)->getMutedModel()));
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		instrument = dynamic_cast<InstrumentTrack*>(trackOf(row));
		QVERIFY(instrument != nullptr);
		QVERIFY(targetsOf(firstAutomationClip(lane)).contains(instrument->getMutedModel()));

		QVERIFY(setRole(row, QStringLiteral("neutral")).ok);
		QCOMPARE(stateOf(row).value(QStringLiteral("kept_roles")).toArray().first().toObject()
			.value(QStringLiteral("automation_links")).toInt(), 0);
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = QDir(dir.path()).filePath(QStringLiteral("row-mute.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path));
		Engine::getSong()->clearProject();
		Engine::getSong()->loadProject(path);
		QVERIFY(dynamic_cast<NeutralTrack*>(trackOf(row)) != nullptr);
		QVERIFY(targetsOf(firstAutomationClip(lane)).contains(trackOf(row)->getMutedModel()));
		QCOMPARE(firstAutomationClip(lane)->getTimeMap().size(), points);

		QVERIFY(setRole(row, QStringLiteral("instrument")).ok);
		instrument = dynamic_cast<InstrumentTrack*>(trackOf(row));
		QVERIFY(instrument != nullptr);
		QVERIFY(targetsOf(firstAutomationClip(lane)).contains(instrument->getMutedModel()));
		QCOMPARE(firstAutomationClip(lane)->getTimeMap().size(), points);
	}
};

QTEST_MAIN(HybridTrackInstrumentRoleTest)
#include "HybridTrackInstrumentRoleTest.moc"
