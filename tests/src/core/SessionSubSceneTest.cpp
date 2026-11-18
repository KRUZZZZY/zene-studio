/*
 * SessionSubSceneTest.cpp - sub-scenes: the model, the commands and the project file (WF-064)
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

/*! 0.5.0 L02 (WF-064): a sub-scene is a named subset of one scene's cells. Making, renaming and deleting one is one
 *  undo step each; launching one starts only its columns, each as the scene launch would (the scene's quantisation),
 *  and records nothing; a project file keeps them in their own <z:subscenes> section - written only when there is
 *  one, so an old project re-saves byte for byte, and beside a <z:scenes> section of the unchanged shape older
 *  builds claim; a section of a newer version is kept as it came. */

#include <QtTest>

#include <QDomDocument>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QTextStream>

#include "ControlRegistry.h"
#include "Engine.h"
#include "SessionModel.h"
#include "SessionScheduler.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonArray subScenes()
{
	return run(QStringLiteral("session.get_state")).result.value(QStringLiteral("subscenes")).toArray();
}

QJsonObject audioCell(int track, int scene)
{
	return {{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene}, {QStringLiteral("type"), QStringLiteral("audio")},
		{QStringLiteral("source"), QStringLiteral("loop%1-%2.wav").arg(track).arg(scene)}};
}

SessionClockContext clockAt(tick_t ticks)
{
	SessionClockContext ctx;
	ctx.positionTicks = ticks;
	ctx.ticksPerBar = 192;
	ctx.framesPerTick = 4.0f;
	ctx.transportRunning = true;
	return ctx;
}

QByteArray readFileBytes(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

//! The text of the first <@a tag> element in the file at @a path, or empty.
QString sectionText(const QString& path, const QString& tag)
{
	QDomDocument document;
	if (!document.setContent(readFileBytes(path))) { return {}; }
	const QDomNodeList found = document.elementsByTagName(tag);
	if (found.isEmpty()) { return {}; }
	QString out;
	QTextStream stream(&out);
	found.item(0).save(stream, 1);
	return out;
}

} // namespace

class SessionSubSceneTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		Engine::init(true);
		ControlRegistry::setReady(true);
		buildGrid();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Create, rename and delete are one undo step each, and each refuses what it cannot name.
	void makingRenamingAndDeletingAreOneStepEach()
	{
		const ControlResult made = run(QStringLiteral("session.subscene_create"),
			{{QStringLiteral("scene"), 1}, {QStringLiteral("tracks"), QJsonArray{2, 0, 0}}});
		QVERIFY2(made.ok, qPrintable(made.errorMessage));
		QCOMPARE(made.result.value(QStringLiteral("subscene")).toInt(), 0);
		QCOMPARE(made.result.value(QStringLiteral("name")).toString(), QStringLiteral("Sub-scene 1"));
		QCOMPARE(made.result.value(QStringLiteral("tracks")).toArray(), (QJsonArray{0, 2}));  // sorted, each once
		QCOMPARE(subScenes().size(), 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(subScenes().isEmpty());
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCOMPARE(subScenes().size(), 1);

		QVERIFY(run(QStringLiteral("session.subscene_rename"), {{QStringLiteral("subscene"), 0}, {QStringLiteral("name"), QStringLiteral("Drums")}}).ok);
		QCOMPARE(subScenes().first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Drums"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(subScenes().first().toObject().value(QStringLiteral("name")).toString(), QStringLiteral("Sub-scene 1"));

		QVERIFY(run(QStringLiteral("session.subscene_delete"), {{QStringLiteral("subscene"), 0}}).ok);
		QVERIFY(subScenes().isEmpty());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCOMPARE(subScenes().size(), 1);
		QVERIFY(run(QStringLiteral("session.subscene_delete"), {{QStringLiteral("subscene"), 0}}).ok);

		const auto refused = [](const QJsonObject& args) { return run(QStringLiteral("session.subscene_create"), args).errorKind; };
		QCOMPARE(refused({{QStringLiteral("scene"), 9}, {QStringLiteral("tracks"), QJsonArray{0}}}), ControlErrorKind::InvalidArgs);
		QCOMPARE(refused({{QStringLiteral("scene"), 0}, {QStringLiteral("tracks"), QJsonArray{}}}), ControlErrorKind::InvalidArgs);
		QCOMPARE(refused({{QStringLiteral("scene"), 0}, {QStringLiteral("tracks"), QJsonArray{7}}}), ControlErrorKind::InvalidArgs);
		QCOMPARE(run(QStringLiteral("session.subscene_rename"), {{QStringLiteral("subscene"), 4}, {QStringLiteral("name"), QStringLiteral("x")}}).errorKind,
			ControlErrorKind::InvalidArgs);
		QVERIFY(subScenes().isEmpty());
	}

	//! A launch starts its columns only, each with the quantisation the scene launch gives it, and records nothing.
	void aLaunchStartsOnlyItsColumnsWithTheScenesQuantisation()
	{
		QVERIFY(run(QStringLiteral("session.subscene_create"), {{QStringLiteral("scene"), 1}, {QStringLiteral("tracks"), QJsonArray{0, 2}},
			{QStringLiteral("name"), QStringLiteral("Outer")}}).ok);
		QVERIFY(run(QStringLiteral("session.set_slot"), {{QStringLiteral("track"), 2}, {QStringLiteral("scene"), 1},
			{QStringLiteral("quantisation"), QStringLiteral("two_bars")}}).ok);
		const int depth = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		SessionScheduler& scheduler = Engine::getSong()->sessionScheduler();
		const ControlResult scene = run(QStringLiteral("session.launch_scene"), {{QStringLiteral("scene"), 1}});
		settle(scheduler);
		const ControlResult launched = run(QStringLiteral("session.subscene_launch"), {{QStringLiteral("subscene"), 0}});
		QVERIFY2(launched.ok, qPrintable(launched.errorMessage));
		const QJsonArray cells = launched.result.value(QStringLiteral("launched")).toArray();
		QCOMPARE(cells.size(), 2);
		QCOMPARE(cells.at(0).toObject().value(QStringLiteral("track")).toInt(), 0);
		QCOMPARE(cells.at(1).toObject().value(QStringLiteral("track")).toInt(), 2);
		// The scene's quantisation: what launch_scene gave the same cells (column 0 the session's bar, 2 its own).
		const QJsonArray whole = scene.result.value(QStringLiteral("launched")).toArray();
		QCOMPARE(cells.at(0).toObject().value(QStringLiteral("quantisation")), whole.at(0).toObject().value(QStringLiteral("quantisation")));
		QCOMPARE(cells.at(1).toObject().value(QStringLiteral("quantisation")).toString(), QStringLiteral("two_bars"));
		QCOMPARE(cells.at(1).toObject().value(QStringLiteral("quantisation")), whole.at(2).toObject().value(QStringLiteral("quantisation")));
		QVERIFY(!launched.result.value(QStringLiteral("in_sync")).toBool());  // a bar and two bars from mid-bar are two lines
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), depth);  // not a project edit
		settle(scheduler);

		// And the engine: with no quantisation, columns 0 and 2 play and column 1 stays idle.
		QVERIFY(run(QStringLiteral("session.subscene_launch"), {{QStringLiteral("subscene"), 0}, {QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		scheduler.processAudio(clockAt(0), 16);
		scheduler.processAudio(clockAt(4), 16);
		QCOMPARE(scheduler.columnState(0).phase, SlotPhase::Playing);
		QCOMPARE(scheduler.columnState(0).scene, 1);
		QCOMPARE(scheduler.columnState(1).phase, SlotPhase::Idle);
		QCOMPARE(scheduler.columnState(2).phase, SlotPhase::Playing);
		scheduler.reset();

		// A column with an empty cell is skipped and said; a sub-scene that is not there is refused.
		QVERIFY(run(QStringLiteral("session.subscene_create"), {{QStringLiteral("scene"), 0}, {QStringLiteral("tracks"), QJsonArray{0, 1}}}).ok);
		const ControlResult partial = run(QStringLiteral("session.subscene_launch"), {{QStringLiteral("subscene"), 1}});
		QVERIFY(partial.ok);
		QCOMPARE(partial.result.value(QStringLiteral("skipped")).toArray(), (QJsonArray{1}));
		QCOMPARE(run(QStringLiteral("session.subscene_launch"), {{QStringLiteral("subscene"), 5}}).errorKind, ControlErrorKind::InvalidArgs);
		scheduler.reset();
		QVERIFY(run(QStringLiteral("session.subscene_delete"), {{QStringLiteral("subscene"), 1}}).ok);
	}

	//! The project file: <z:subscenes v="1"> beside an unchanged <z:scenes>, read back as it was, re-saved byte for byte.
	void theProjectFileKeepsThemInTheirOwnSection()
	{
		Song* song = Engine::getSong();
		const QString first = m_dir.filePath(QStringLiteral("subscenes-1.mmp"));
		QVERIFY(song->saveProjectFile(first, false));
		const QString section = sectionText(first, QStringLiteral("z:subscenes"));
		QVERIFY2(section.contains(QStringLiteral("v=\"1\"")), qPrintable(section));
		QVERIFY2(section.contains(QStringLiteral("<z:subscene ")) && section.contains(QStringLiteral("name=\"Outer\""))
			&& section.contains(QStringLiteral("tracks=\"0,2\"")) && section.contains(QStringLiteral("scene=\"1\"")), qPrintable(section));
		// <z:scenes> keeps the shape a build before this one claims: rows and cells only.
		QVERIFY(!sectionText(first, QStringLiteral("z:scenes")).contains(QStringLiteral("subscene")));
		QDomDocument document;
		QVERIFY(document.setContent(readFileBytes(first)));
		SessionModel olderClaim;
		QVERIFY(olderClaim.restoreScenesSection(document.elementsByTagName(QStringLiteral("z:scenes")).item(0).toElement()));

		song->loadProject(first);
		QVERIFY2(song->loadRefusal().isEmpty(), qPrintable(song->loadRefusal()));
		const SessionModel& model = song->sessionModel();
		QCOMPARE(model.subScenes().size(), std::size_t(1));
		QCOMPARE(model.subScenes().front().name, QStringLiteral("Outer"));
		QCOMPARE(model.subScenes().front().scene, 1);
		QCOMPARE(model.subScenes().front().tracks, (std::vector<int>{0, 2}));
		QCOMPARE(model.trackCount(), 3);  // the grid, read after the sub-scenes, did not drop them
		const QString second = m_dir.filePath(QStringLiteral("subscenes-2.mmp"));
		QVERIFY(song->saveProjectFile(second, false));
		QCOMPARE(readFileBytes(second), readFileBytes(first));
	}

	//! A project that never made a sub-scene writes no section and re-saves byte for byte, as before this feature.
	void aProjectWithoutSubScenesIsUnchanged()
	{
		Song* song = Engine::getSong();
		std::vector<SubScene> kept = song->sessionModel().subScenes();
		song->sessionModel().setSubScenes({});
		const QString first = m_dir.filePath(QStringLiteral("plain-1.mmp"));
		QVERIFY(song->saveProjectFile(first, false));
		QVERIFY(!readFileBytes(first).contains("<z:subscenes"));  // (the provenance journal names the commands run)
		QVERIFY(!readFileBytes(first).contains("<z:subscene "));
		song->loadProject(first);
		QVERIFY(song->sessionModel().subScenes().empty());
		const QString second = m_dir.filePath(QStringLiteral("plain-2.mmp"));
		QVERIFY(song->saveProjectFile(second, false));
		QCOMPARE(readFileBytes(second), readFileBytes(first));
		song->sessionModel().setSubScenes(std::move(kept));
	}

	//! A section of a version this build does not know is not read, and goes back into the file as it came.
	void aNewerSectionIsKeptAsItCame()
	{
		Song* song = Engine::getSong();
		const QString base = m_dir.filePath(QStringLiteral("newer-base.mmp"));
		QVERIFY(song->saveProjectFile(base, false));
		QString text = QString::fromUtf8(readFileBytes(base));
		const int from = text.indexOf(QStringLiteral("<z:subscenes"));
		const int to = text.indexOf(QStringLiteral("</z:subscenes>"));
		QVERIFY(from > 0 && to > from);
		const QString newer = QStringLiteral("<z:subscenes v=\"2\"><z:subscene name=\"Future\" scene=\"0\" tracks=\"1\" mode=\"x\"/><z:later/></z:subscenes>");
		text.replace(from, to + int(qstrlen("</z:subscenes>")) - from, newer);
		const QString path = m_dir.filePath(QStringLiteral("newer.mmp"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(text.toUtf8());
		file.close();
		song->loadProject(path);
		QVERIFY2(song->loadRefusal().isEmpty(), qPrintable(song->loadRefusal()));
		QVERIFY(song->sessionModel().subScenes().empty());  // not read with guessed meaning
		QCOMPARE(song->sessionModel().trackCount(), 3);     // and the grid loads as ever
		const QString resaved = m_dir.filePath(QStringLiteral("newer-resaved.mmp"));
		QVERIFY(song->saveProjectFile(resaved, false));
		const QString kept = sectionText(resaved, QStringLiteral("z:subscenes"));
		QVERIFY2(kept.contains(QStringLiteral("v=\"2\"")) && kept.contains(QStringLiteral("Future")) && kept.contains(QStringLiteral("z:later")), qPrintable(kept));
	}

	//! QString::toInt() returns 0 for text, so a row or column that is not a number must be refused by its ok flag;
	//! read as 0 it would attach the sub-scene to scene 0 (or column 0) instead of leaving it out.
	void aNonNumericSubSceneRowIsRefusedNotSceneZero()
	{
		QDomDocument doc;
		QVERIFY(doc.setContent(QStringLiteral("<z:subscene name=\"Text\" scene=\"abc\" tracks=\"0,2\"/>")));
		SubScene text;
		text.name = QStringLiteral("Keep");
		text.scene = 3;
		QVERIFY(!text.restoreState(doc.documentElement()));
		QCOMPARE(text.scene, 3);  // *this unchanged, as the header promises
		QCOMPARE(text.name, QStringLiteral("Keep"));

		QVERIFY(doc.setContent(QStringLiteral("<z:subscene name=\"Gap\" scene=\"0\" tracks=\"0,x\"/>")));
		SubScene column;
		QVERIFY(!column.restoreState(doc.documentElement()));

		// A real row 0 still restores as row 0.
		QVERIFY(doc.setContent(QStringLiteral("<z:subscene name=\"Zero\" scene=\"0\" tracks=\"0,2\"/>")));
		SubScene zero;
		QVERIFY(zero.restoreState(doc.documentElement()));
		QCOMPARE(zero.scene, 0);

		// Through a project file: one malformed row makes the whole section unclaimed (SessionSubScenes::load), so it is
		// kept verbatim and nothing is read from it - the "abc" row is not attached to scene 0.
		Song* song = Engine::getSong();
		std::vector<SubScene> kept = song->sessionModel().subScenes();
		SubScene seed;
		seed.name = QStringLiteral("Seed");
		seed.tracks = {0};
		song->sessionModel().setSubScenes({seed});
		const QString base = m_dir.filePath(QStringLiteral("nonnumeric-base.mmp"));
		const bool saved = song->saveProjectFile(base, false);
		song->sessionModel().setSubScenes(std::move(kept));
		QVERIFY(saved);
		QString text_file = QString::fromUtf8(readFileBytes(base));
		const int from = text_file.indexOf(QStringLiteral("<z:subscenes"));
		const int to = text_file.indexOf(QStringLiteral("</z:subscenes>"));
		QVERIFY(from > 0 && to > from);
		const QString edited = QStringLiteral("<z:subscenes v=\"1\"><z:subscene name=\"Bad\" scene=\"abc\" tracks=\"0,2\"/>"
			"<z:subscene name=\"Good\" scene=\"1\" tracks=\"0\"/></z:subscenes>");
		text_file.replace(from, to + int(qstrlen("</z:subscenes>")) - from, edited);
		const QString path = m_dir.filePath(QStringLiteral("nonnumeric.mmp"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(text_file.toUtf8());
		file.close();
		song->loadProject(path);
		QVERIFY2(song->loadRefusal().isEmpty(), qPrintable(song->loadRefusal()));
		QVERIFY(song->sessionModel().subScenes().empty());  // not read with a guessed row 0
		const QString resaved = m_dir.filePath(QStringLiteral("nonnumeric-resaved.mmp"));
		QVERIFY(song->saveProjectFile(resaved, false));
		const QString section = sectionText(resaved, QStringLiteral("z:subscenes"));
		QVERIFY2(section.contains(QStringLiteral("scene=\"abc\"")) && section.contains(QStringLiteral("name=\"Bad\"")), qPrintable(section));
	}

	//! Text, CDATA or a processing instruction beside the rows is content this section does not model: the section is not
	//! claimed (no row is read from it) and goes back into the file as it came, so the next save does not drop it.
	//! Whitespace-only text and CDATA, the layout of a project file, is not content and still claims the section.
	void aSectionWithOtherContentIsKeptNotDropped()
	{
		const QString open = QStringLiteral("<z:subscenes v=\"1\">");
		const QString row = QStringLiteral("<z:subscene name=\"Outer\" scene=\"1\" tracks=\"0,2\"/>");
		const QString close = QStringLiteral("</z:subscenes>");
		std::size_t rows = 0;
		QString resaved;

		loadWithSection(QStringLiteral("text"), open + row + QStringLiteral("KEEP-TEXT") + close, &rows, &resaved);
		QCOMPARE(rows, std::size_t(0));
		QVERIFY2(resaved.contains(QStringLiteral("KEEP-TEXT")) && resaved.contains(QStringLiteral("name=\"Outer\"")), qPrintable(resaved));

		loadWithSection(QStringLiteral("cdata"), open + row + QStringLiteral("<![CDATA[KEEP-CDATA]]>") + close, &rows, &resaved);
		QCOMPARE(rows, std::size_t(0));
		QVERIFY2(resaved.contains(QStringLiteral("KEEP-CDATA")), qPrintable(resaved));

		loadWithSection(QStringLiteral("instruction"), open + row + QStringLiteral("<?keep KEEP-PI?>") + close, &rows, &resaved);
		QCOMPARE(rows, std::size_t(0));
		QVERIFY2(resaved.contains(QStringLiteral("KEEP-PI")), qPrintable(resaved));

		loadWithSection(QStringLiteral("layout"), open + QStringLiteral("\n  ") + row + QStringLiteral("\n  <![CDATA[ \n ]]>\n") + close,
			&rows, &resaved);
		QCOMPARE(rows, std::size_t(1));
		QVERIFY2(resaved.contains(QStringLiteral("name=\"Outer\"")), qPrintable(resaved));
	}

private:
	//! Saves a project with a sub-scene, replaces its <z:subscenes> section with @a section and loads the file: how many rows
	//! were read, and the section the next save writes.
	void loadWithSection(const QString& name, const QString& section, std::size_t* rows, QString* resaved)
	{
		Song* song = Engine::getSong();
		std::vector<SubScene> kept = song->sessionModel().subScenes();
		SubScene seed;
		seed.name = QStringLiteral("Seed");
		seed.tracks = {0};
		song->sessionModel().setSubScenes({seed});
		const QString base = m_dir.filePath(name + QStringLiteral("-base.mmp"));
		const bool saved = song->saveProjectFile(base, false);
		song->sessionModel().setSubScenes(std::move(kept));
		QVERIFY(saved);
		QString text = QString::fromUtf8(readFileBytes(base));
		const int from = text.indexOf(QStringLiteral("<z:subscenes"));
		const int to = text.indexOf(QStringLiteral("</z:subscenes>"));
		QVERIFY(from > 0 && to > from);
		text.replace(from, to + int(qstrlen("</z:subscenes>")) - from, section);
		const QString path = m_dir.filePath(name + QStringLiteral(".mmp"));
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		file.write(text.toUtf8());
		file.close();
		song->loadProject(path);
		QVERIFY2(song->loadRefusal().isEmpty(), qPrintable(song->loadRefusal()));
		*rows = song->sessionModel().subScenes().size();
		const QString out = m_dir.filePath(name + QStringLiteral("-resaved.mmp"));
		QVERIFY(song->saveProjectFile(out, false));
		*resaved = sectionText(out, QStringLiteral("z:subscenes"));
	}

	//! Drop what is queued or playing: the reset is taken on the scheduler's next period, with what was queued before it.
	static void settle(SessionScheduler& scheduler)
	{
		scheduler.reset();
		scheduler.processAudio(clockAt(0), 16);
		scheduler.reset();
		scheduler.processAudio(clockAt(4), 16);
	}

	void buildGrid()
	{
		Song* song = Engine::getSong();
		while (song->tracks().size() < 3) { QVERIFY(Track::create(Track::Type::Sample, song) != nullptr); }
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 3}, {QStringLiteral("scenes"), 2}}).ok);
		for (const QJsonObject& cell : {audioCell(0, 0), audioCell(0, 1), audioCell(1, 1), audioCell(2, 1)})
		{
			QVERIFY(run(QStringLiteral("session.set_slot"), cell).ok);
		}
		// Mid-bar, so a bar and two bars resolve to different lines.
		Engine::getSong()->setPlayPos(96, Song::PlayMode::Song);
	}

	QTemporaryDir m_dir;
};

QTEST_MAIN(SessionSubSceneTest)
#include "SessionSubSceneTest.moc"
