/*
 * HybridTrackRoundTripTest.cpp - neutral arrangement rows: create, move, undo, save/load (Q050-283.1)
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



/*! 0.5.0 L04, Q050-283.1 (R-workflow-286), stage 1 of 3: the model half of the NEUTRAL track.
 *
 *  - track.add type=neutral makes a NeutralTrack with a stable trk-<n> id at the end of the
 *    arrangement; the reply, track.list and track.get_state all read it back as "neutral";
 *  - the row keeps its id when it is moved, and one control.undo removes it / one redo brings it back;
 *  - a neutral row survives a project save and load as a neutral row, with the id, name, position and
 *    mute it had, among every other track type, and a clone of it is neutral too;
 *  - a project that has no neutral row saves none of its marks and an old project file loads as before;
 *  - the row holds no clips: clip.add on it is a typed refusal that records no undo step. */

#include <QtTest>

#include <QDir>
#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "NeutralTrack.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonObject addNeutral(const QString& name = QString())
{
	QJsonObject args{{QStringLiteral("type"), QStringLiteral("neutral")}};
	if (!name.isEmpty()) { args.insert(QStringLiteral("name"), name); }
	const ControlResult added = run(QStringLiteral("track.add"), args);
	return added.ok ? added.result : QJsonObject();
}

QJsonArray listed()
{
	return run(QStringLiteral("track.list")).result.value(QStringLiteral("tracks")).toArray();
}

//! The comparable shape of the arrangement: id, type, name, muted per row, in order.
QStringList shape()
{
	QStringList rows;
	for (const QJsonValue& value : listed())
	{
		const QJsonObject row = value.toObject();
		rows << QStringLiteral("%1|%2|%3|%4").arg(row.value(QStringLiteral("id")).toString(),
			row.value(QStringLiteral("type")).toString(), row.value(QStringLiteral("name")).toString(),
			row.value(QStringLiteral("muted")).toBool() ? QStringLiteral("muted") : QStringLiteral("live"));
	}
	return rows;
}

QJsonObject lastTransaction(const QString& command)
{
	const QJsonArray records = ControlRegistry::instance()->transactions();
	for (int i = records.size() - 1; i >= 0; --i)
	{
		const QJsonObject record = records.at(i).toObject();
		if (record.value(QStringLiteral("command")).toString() == command) { return record; }
	}
	return {};
}

QString fileText(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

int undoDepth()
{
	return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
}

} // namespace

class HybridTrackRoundTripTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init() { Engine::getSong()->clearProject(); }

	//! AC1: the command creates a neutral row with a stable id at the end of the arrangement.
	void trackAddCreatesANeutralRowWithAnIdAndAPosition()
	{
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}}).ok);
		const QJsonObject added = addNeutral(QStringLiteral("Vocal"));
		QVERIFY2(!added.isEmpty(), "track.add type=neutral was refused");
		QCOMPARE(added.value(QStringLiteral("type")).toString(), QStringLiteral("neutral"));
		QCOMPARE(added.value(QStringLiteral("name")).toString(), QStringLiteral("Vocal"));
		QCOMPARE(added.value(QStringLiteral("index")).toInt(), 1);
		QCOMPARE(added.value(QStringLiteral("clip_count")).toInt(), 0);
		const QString id = added.value(QStringLiteral("track")).toString();
		QVERIFY(id.startsWith(QStringLiteral("trk-")));

		auto* neutral = dynamic_cast<NeutralTrack*>(Engine::getSong()->tracks()[1]);
		QVERIFY2(neutral != nullptr, "the second row is not a NeutralTrack");
		QCOMPARE(control::trackIdOf(neutral), id);
		QCOMPARE(neutral->type(), Track::Type::Neutral);

		const ControlResult state = run(QStringLiteral("track.get_state"), {{QStringLiteral("track"), id}});
		QVERIFY(state.ok);
		const QJsonObject read = state.result.value(QStringLiteral("tracks")).toArray().isEmpty()
			? state.result : state.result.value(QStringLiteral("tracks")).toArray().first().toObject();
		QCOMPARE(read.value(QStringLiteral("type")).toString(), QStringLiteral("neutral"));
		QCOMPARE(read.value(QStringLiteral("index")).toInt(), 1);
		QCOMPARE(listed().last().toObject().value(QStringLiteral("type")).toString(), QStringLiteral("neutral"));
	}

	//! The reversibility class is the honest one: a created row has no before-state, the inverse is the operation.
	void creatingARowIsATrueInverseStep()
	{
		QVERIFY(!addNeutral().isEmpty());
		const QJsonObject transaction = lastTransaction(QStringLiteral("track.add"));
		QVERIFY(!transaction.isEmpty());
		QCOMPARE(transaction.value(QStringLiteral("reversible")).toBool(), true);
		const QString text = QString::fromUtf8(QJsonDocument(transaction).toJson());
		QVERIFY2(text.contains(QStringLiteral("remove the created track")), qPrintable(text));
	}

	//! The id belongs to the row, not to its place: moving it keeps the id and changes the position.
	void aMovedRowKeepsItsId()
	{
		const QString id = addNeutral(QStringLiteral("Row")).value(QStringLiteral("track")).toString();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		QCOMPARE(listed().first().toObject().value(QStringLiteral("id")).toString(), id);
		const ControlResult moved = run(QStringLiteral("track.move"), {{QStringLiteral("track"), id}, {QStringLiteral("index"), 2}});
		QVERIFY2(moved.ok, qPrintable(moved.errorMessage));
		const QJsonObject row = listed().at(2).toObject();
		QCOMPARE(row.value(QStringLiteral("id")).toString(), id);
		QCOMPARE(row.value(QStringLiteral("type")).toString(), QStringLiteral("neutral"));
	}

	//! One undo step per creation: control.undo removes the row, control.redo brings a neutral row back.
	void oneUndoRemovesTheRowAndOneRedoRestoresIt()
	{
		const int depth = undoDepth();
		QVERIFY(!addNeutral(QStringLiteral("Undo me")).isEmpty());
		QCOMPARE(undoDepth(), depth + 1);
		QCOMPARE(listed().size(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(listed().size(), 0);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(listed().size(), 1);
		const QJsonObject row = listed().first().toObject();
		QCOMPARE(row.value(QStringLiteral("type")).toString(), QStringLiteral("neutral"));
		QCOMPARE(row.value(QStringLiteral("name")).toString(), QStringLiteral("Undo me"));
	}

	//! AC2: a neutral row round-trips through save/load among every other kind of track.
	void aNeutralRowRoundTripsThroughSaveAndLoad()
	{
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}, {QStringLiteral("name"), QStringLiteral("Synth")}}).ok);
		const QString first = addNeutral(QStringLiteral("Open row")).value(QStringLiteral("track")).toString();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}, {QStringLiteral("name"), QStringLiteral("Loops")}}).ok);
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("folder")}, {QStringLiteral("name"), QStringLiteral("Group")}}).ok);
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("automation")}, {QStringLiteral("name"), QStringLiteral("Auto")}}).ok);
		const QString second = addNeutral(QStringLiteral("Muted row")).value(QStringLiteral("track")).toString();
		QVERIFY(run(QStringLiteral("track.set_mute"), {{QStringLiteral("track"), second}, {QStringLiteral("muted"), true}}).ok);
		const QStringList before = shape();
		QCOMPARE(before.size(), 6);

		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = QDir(dir.path()).filePath(QStringLiteral("neutral.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path));
		const QString text = fileText(path);
		QVERIFY2(text.contains(QStringLiteral("type=\"8\"")), "the neutral row's type was not written");
		QVERIFY2(text.contains(QStringLiteral("<neutraltrack")), "the neutral row's element was not written");

		Engine::getSong()->clearProject();
		QCOMPARE(listed().size(), 0);
		Engine::getSong()->loadProject(path);
		QCOMPARE(shape(), before);
		QVERIFY(dynamic_cast<NeutralTrack*>(Engine::getSong()->findTrackById(first.mid(4).toInt())) != nullptr);
		QVERIFY(dynamic_cast<NeutralTrack*>(Engine::getSong()->findTrackById(second.mid(4).toInt())) != nullptr);
		QCOMPARE(Engine::getSong()->findTrackById(second.mid(4).toInt())->isMuted(), true);

		// And a second generation: saving what was loaded writes the same arrangement again.
		const QString again = QDir(dir.path()).filePath(QStringLiteral("neutral2.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(again));
		Engine::getSong()->clearProject();
		Engine::getSong()->loadProject(again);
		QCOMPARE(shape(), before);
	}

	//! A project that never used a neutral row keeps its bytes (no mark of the new type), and an old file loads.
	void anOldProjectLoadsAndSavesNoNeutralMark()
	{
		static const char* legacy =
			"<?xml version=\"1.0\"?>\n"
			"<zene-project creator=\"Zene Studio\" version=\"1.0\" type=\"song\" creatorversion=\"0.4.0\">\n"
			"  <head bpm=\"120\" timesig_numerator=\"4\" timesig_denominator=\"4\"/>\n"
			"  <song><trackcontainer type=\"song\">\n"
			"    <track name=\"Lead\" type=\"0\" id=\"0\"><instrumenttrack/></track>\n"
			"    <track name=\"Drums\" type=\"2\" id=\"1\"><sampletrack/></track>\n"
			"    <track name=\"Auto\" type=\"5\" id=\"2\"><automationtrack/></track>\n"
			"  </trackcontainer></song>\n"
			"</zene-project>\n";
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		const QString path = QDir(dir.path()).filePath(QStringLiteral("old.mmp"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(legacy);
		file.close();

		Engine::getSong()->loadProject(path);
		QCOMPARE(shape(), (QStringList{QStringLiteral("trk-0|instrument|Lead|live"),
			QStringLiteral("trk-1|sample|Drums|live"), QStringLiteral("trk-2|automation|Auto|live")}));
		const QString resaved = QDir(dir.path()).filePath(QStringLiteral("resaved.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(resaved));
		const QString text = fileText(resaved);
		QVERIFY2(!text.contains(QStringLiteral("neutraltrack")), "an old project grew a neutral element");
		QVERIFY2(!text.contains(QStringLiteral("type=\"8\"")), "an old project grew a neutral type");
	}

	//! Track::clone (the Clone Track action) of a neutral row is a neutral row, with the same name.
	void aClonedRowIsNeutral()
	{
		const QString id = addNeutral(QStringLiteral("Twin")).value(QStringLiteral("track")).toString();
		Track* source = Engine::getSong()->findTrackById(id.mid(4).toInt());
		QVERIFY(source != nullptr);
		Track* clone = source->clone();
		QVERIFY(clone != nullptr);
		QVERIFY(dynamic_cast<NeutralTrack*>(clone) != nullptr);
		QVERIFY(clone->id() != source->id());
		QCOMPARE(clone->name(), QStringLiteral("Twin"));
		QCOMPARE(Engine::getSong()->tracks().size(), std::size_t{2});
	}

	//! A neutral row holds no clips: clip.add is a typed refusal and records no undo step.
	void clipAddOnANeutralRowIsRefused()
	{
		const QString id = addNeutral().value(QStringLiteral("track")).toString();
		const int depth = undoDepth();
		const ControlResult refused = run(QStringLiteral("clip.add"), {{QStringLiteral("track"), id}, {QStringLiteral("position"), 0}, {QStringLiteral("length"), 192}});
		QVERIFY(!refused.ok);
		QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
		QCOMPARE(undoDepth(), depth);
		QCOMPARE(Engine::getSong()->findTrackById(id.mid(4).toInt())->numOfClips(), 0);
	}

	//! Integration with track.add's destination arguments (train 25): a neutral row has no mixer channel
	//! and no instrument yet, so `output` and `instrument` are typed refusals that leave no row behind,
	//! and the app/autotrackchannel preference gives it no channel of its own.
	void aNeutralRowTakesNoOutputInstrumentOrAutoChannel()
	{
		const std::size_t rows = Engine::getSong()->tracks().size();
		const int depth = undoDepth();
		for (const QString& key : {QStringLiteral("output"), QStringLiteral("instrument")})
		{
			const ControlResult refused = run(QStringLiteral("track.add"),
				{{QStringLiteral("type"), QStringLiteral("neutral")}, {key, QStringLiteral("ch-0")}});
			QVERIFY2(!refused.ok, qPrintable(key + QStringLiteral(" on a neutral row was accepted")));
			QCOMPARE(refused.errorKind, ControlErrorKind::InvalidArgs);
		}
		QCOMPARE(Engine::getSong()->tracks().size(), rows);
		QCOMPARE(undoDepth(), depth);

		const QString key = QStringLiteral("autotrackchannel");
		const QString app = QStringLiteral("app");
		const QString previous = ConfigManager::inst()->value(app, key, QStringLiteral("0"));
		ConfigManager::inst()->setValue(app, key, QStringLiteral("1"));
		const std::size_t channels = Engine::mixer()->numChannels();
		const QJsonObject added = addNeutral(QStringLiteral("Idea"));
		ConfigManager::inst()->setValue(app, key, previous);
		QVERIFY(!added.isEmpty());
		QVERIFY2(!added.contains(QStringLiteral("channel")), "a neutral row was given a mixer channel");
		QCOMPARE(Engine::mixer()->numChannels(), channels);
	}
};

QTEST_MAIN(HybridTrackRoundTripTest)
#include "HybridTrackRoundTripTest.moc"
