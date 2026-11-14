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

// Complete fixture method definitions; included once after the fixture declaration.

void CustomAccessTest::initTestCase() {
	QVERIFY(m_dir.isValid());
	// A throwaway config: app.whats_new's marker and the settings presets are written to it.
	qputenv("HOME", m_dir.path().toLocal8Bit());
	ConfigManager::inst()->loadConfigFile(m_dir.filePath(QStringLiteral("zene.xml")));
	Engine::init(true);
	ControlRegistry::setReady(true);
	m_track = Track::create(Track::Type::Sample, Engine::getSong());
	QVERIFY(m_track != nullptr);
}
void CustomAccessTest::cleanupTestCase() {
	ControlRegistry::setReady(false);
	Engine::destroy();
}
void CustomAccessTest::theOverviewListsEveryTrack() {
	const ControlResult listed = run(QStringLiteral("track.overview"));
	QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
	QCOMPARE(listed.result.value(QStringLiteral("count")).toInt(), static_cast<int>(Engine::getSong()->tracks().size()));
	const QJsonObject entry = overviewOf(m_track);
	QCOMPARE(entry.value(QStringLiteral("type")).toString(), QStringLiteral("sample"));
	QVERIFY(entry.contains(QStringLiteral("cpu_percent")));
	QVERIFY(entry.value(QStringLiteral("visible")).toBool());
}
void CustomAccessTest::aColourIsOneUndoStep() {
	const QString id = control::trackIdOf(m_track);
	QVERIFY(run(QStringLiteral("track.set_color"), {{QStringLiteral("track"), id}, {QStringLiteral("color"), QStringLiteral("#3366cc")}}).ok);
	QVERIFY(m_track->color().has_value());
	QCOMPARE(m_track->color()->name(), QStringLiteral("#3366cc"));
	QCOMPARE(overviewOf(m_track).value(QStringLiteral("color")).toString(), QStringLiteral("#3366cc"));
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QVERIFY(!m_track->color().has_value());
	const ControlResult bad = run(QStringLiteral("track.set_color"), {{QStringLiteral("track"), id}, {QStringLiteral("color"), QStringLiteral("blurple?")}});
	QCOMPARE(bad.errorKind, ControlErrorKind::InvalidArgs);
	QCOMPARE(run(QStringLiteral("track.set_color"), {{QStringLiteral("track"), QStringLiteral("trk-99999")},
		{QStringLiteral("color"), QStringLiteral("#000000")}}).errorKind, ControlErrorKind::NotFound);
}
void CustomAccessTest::hidingIsOneUndoStep() {
	const QString id = control::trackIdOf(m_track);
	const ControlResult hidden = run(QStringLiteral("track.set_visible"), {{QStringLiteral("track"), id}, {QStringLiteral("visible"), false}});
	QVERIFY2(hidden.ok, qPrintable(hidden.errorMessage));
	QVERIFY(!m_track->isVisible());
	QVERIFY(!m_track->isMuted());
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QVERIFY(m_track->isVisible());
}
void CustomAccessTest::anIconIsSavedOnlyWhenSet() {
	const QString bare = m_dir.filePath(QStringLiteral("bare.mmp"));
	QVERIFY(Engine::getSong()->saveProjectFile(bare));
	QVERIFY2(!fileText(bare).contains(QStringLiteral("<track-looks")), "a project with no icons wrote <track-looks>");
	const QString id = control::trackIdOf(m_track);
	QVERIFY(run(QStringLiteral("track.set_icon"), {{QStringLiteral("track"), id}, {QStringLiteral("icon"), QStringLiteral("drums")}}).ok);
	QCOMPARE(TrackLooks::instance().icon(m_track->id()), QStringLiteral("drums"));
	QCOMPARE(overviewOf(m_track).value(QStringLiteral("icon")).toString(), QStringLiteral("drums"));
	const QString named = m_dir.filePath(QStringLiteral("named.mmp"));
	QVERIFY(Engine::getSong()->saveProjectFile(named));
	QVERIFY(fileText(named).contains(QStringLiteral("<track-looks")));
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QVERIFY(TrackLooks::instance().icon(m_track->id()).isEmpty());
	QVERIFY(!run(QStringLiteral("track.set_icon"), {{QStringLiteral("track"), id}, {QStringLiteral("icon"), QString(41, QLatin1Char('x'))}}).ok);
	// The file brings its icons back, and a file without the element clears them.
	Engine::getSong()->loadProject(named);
	const Track* loaded = Engine::getSong()->tracks().back();
	QCOMPARE(TrackLooks::instance().icon(loaded->id()), QStringLiteral("drums"));
	Engine::getSong()->loadProject(bare);
	QVERIFY(TrackLooks::instance().icon(Engine::getSong()->tracks().back()->id()).isEmpty());
	m_track = Engine::getSong()->tracks().back();
}
void CustomAccessTest::everyChannelReportsItsLoad() {
	const ControlResult load = run(QStringLiteral("mixer.cpu_load"));
	QVERIFY2(load.ok, qPrintable(load.errorMessage));
	const QJsonArray channels = load.result.value(QStringLiteral("channels")).toArray();
	QCOMPARE(static_cast<int>(channels.size()), static_cast<int>(Engine::mixer()->numChannels()));
	for (const QJsonValue& channel : channels)
	{
		QVERIFY(channel.toObject().value(QStringLiteral("cpu_percent")).toDouble() >= 0.0);
		QVERIFY(channel.toObject().value(QStringLiteral("devices")).isArray());
	}
	QVERIFY(load.result.value(QStringLiteral("total_percent")).toDouble() >= 0.0);
}
void CustomAccessTest::aQuestionFindsTheManualAndTheCommands() {
	const ControlResult tempo = run(QStringLiteral("app.ask"), {{QStringLiteral("question"), QStringLiteral("How do I change the tempo?")},
		{QStringLiteral("limit"), 10}});
	QVERIFY2(tempo.ok, qPrintable(tempo.errorMessage));
	QVERIFY(tempo.result.value(QStringLiteral("offline")).toBool());
	QStringList ids;
	for (const QJsonValue& answer : tempo.result.value(QStringLiteral("answers")).toArray())
	{
		ids.append(answer.toObject().value(QStringLiteral("id")).toString());
	}
	QVERIFY2(ids.contains(QStringLiteral("transport.set_tempo")), qPrintable(ids.join(QStringLiteral(", "))));
	const ControlResult record = run(QStringLiteral("app.ask"), {{QStringLiteral("question"), QStringLiteral("record audio takes")}});
	bool manual = false;
	for (const QJsonValue& answer : record.result.value(QStringLiteral("answers")).toArray())
	{
		manual = manual || answer.toObject().value(QStringLiteral("kind")).toString() == QLatin1String("manual");
	}
	QVERIFY2(manual, "the manual's sections were not searched (data/manual/index.html)");
	QCOMPARE(run(QStringLiteral("app.ask"), {{QStringLiteral("question"), QStringLiteral("a an")}}).errorKind, ControlErrorKind::InvalidArgs);
	QVERIFY(run(QStringLiteral("app.ask"), {{QStringLiteral("question"), QStringLiteral("qqqzzz xxyyq")}}).result
		.value(QStringLiteral("answers")).toArray().isEmpty());
}
void CustomAccessTest::manualMarkupPreservesWordBoundaries() {
	const QStringList originalPaths = QDir::searchPaths(QStringLiteral("data"));
	const QString manualRoot = m_dir.filePath(QStringLiteral("assistant-manual"));
	QVERIFY(QDir().mkpath(manualRoot + QStringLiteral("/manual")));
	QFile file(manualRoot + QStringLiteral("/manual/index.html"));
	QVERIFY(file.open(QIODevice::WriteOnly));
	QVERIFY(file.write("<html><body><h2 id=\"tag-boundary\">Manual formatting</h2><p>drum</p><p>loop</p></body></html>") > 0);
	file.close();
	QDir::setSearchPaths(QStringLiteral("data"), QStringList{manualRoot});
	const ControlResult result = run(QStringLiteral("app.ask"), {{QStringLiteral("question"), QStringLiteral("drum loop")},
		{QStringLiteral("limit"), 20}});
	QDir::setSearchPaths(QStringLiteral("data"), originalPaths);

	QVERIFY2(result.ok, qPrintable(result.errorMessage));
	bool found = false;
	for (const QJsonValue& value : result.result.value(QStringLiteral("answers")).toArray())
	{
		const QJsonObject answer = value.toObject();
		if (answer.value(QStringLiteral("id")).toString() == QLatin1String("tag-boundary"))
		{
			found = true;
			QCOMPARE(answer.value(QStringLiteral("text")).toString(), QStringLiteral("drum loop"));
		}
	}
	QVERIFY(found);
}
void CustomAccessTest::theReleaseNotesAreShownOncePerVersion() {
	const ControlResult notes = run(QStringLiteral("app.whats_new"));
	QVERIFY2(notes.ok, qPrintable(notes.errorMessage));
	QVERIFY(!notes.result.value(QStringLiteral("notes")).toObject().value(QStringLiteral("items")).toArray().isEmpty());
	QVERIFY(!notes.result.value(QStringLiteral("update_check")).toBool());
	QVERIFY(!notes.result.value(QStringLiteral("seen")).toBool());
	markWhatsNewSeen();
	QVERIFY(run(QStringLiteral("app.whats_new")).result.value(QStringLiteral("seen")).toBool());
}
void CustomAccessTest::aMemoryLocationComesBack() {
	const QString bare = m_dir.filePath(QStringLiteral("nomemory.mmp"));
	QVERIFY(Engine::getSong()->saveProjectFile(bare));
	QVERIFY2(!fileText(bare).contains(QStringLiteral("<view-memory")), "a project with no locations wrote <view-memory>");
	QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 1920}}).ok);
	QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 960}, {QStringLiteral("end"), 2880}}).ok);
	const ControlResult saved = run(QStringLiteral("location.save"), {{QStringLiteral("name"), QStringLiteral("verse")}});
	QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
	QCOMPARE(saved.result.value(QStringLiteral("tick")).toInt(), 1920);
	QCOMPARE(run(QStringLiteral("location.list")).result.value(QStringLiteral("count")).toInt(), 1);
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(run(QStringLiteral("location.list")).result.value(QStringLiteral("count")).toInt(), 0);
	QVERIFY(run(QStringLiteral("location.save"), {{QStringLiteral("name"), QStringLiteral("verse")}}).ok);
	QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 0}}).ok);
	QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}}).ok);
	const ControlResult recalled = run(QStringLiteral("location.recall"), {{QStringLiteral("name"), QStringLiteral("verse")}});
	QVERIFY2(recalled.ok, qPrintable(recalled.errorMessage));
	QVERIFY(recalled.result.value(QStringLiteral("range_restored")).toBool());
	QCOMPARE(run(QStringLiteral("transport.get_state")).result.value(QStringLiteral("position_ticks")).toInt(), 1920);
	QCOMPARE(run(QStringLiteral("range.get")).result.value(QStringLiteral("end")).toInt(), 2880);
	QCOMPARE(run(QStringLiteral("location.recall"), {{QStringLiteral("name"), QStringLiteral("chorus")}}).errorKind, ControlErrorKind::NotFound);
	const QString named = m_dir.filePath(QStringLiteral("memory.mmp"));
	QVERIFY(Engine::getSong()->saveProjectFile(named));
	QVERIFY(fileText(named).contains(QStringLiteral("<view-memory")));
	QVERIFY(run(QStringLiteral("location.remove"), {{QStringLiteral("name"), QStringLiteral("verse")}}).ok);
	QCOMPARE(run(QStringLiteral("location.remove"), {{QStringLiteral("name"), QStringLiteral("verse")}}).errorKind, ControlErrorKind::NotFound);
}
void CustomAccessTest::aSavedSelectionComesBack() {
	QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}}).ok);
	QCOMPARE(run(QStringLiteral("selection.save"), {{QStringLiteral("name"), QStringLiteral("intro")}}).errorKind, ControlErrorKind::Refused);
	QCOMPARE(run(QStringLiteral("selection.save"), {{QStringLiteral("name"), QStringLiteral("  ")}}).errorKind, ControlErrorKind::InvalidArgs);
	QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 3840}}).ok);
	const ControlResult saved = run(QStringLiteral("selection.save"), {{QStringLiteral("name"), QStringLiteral("intro")}});
	QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
	QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 100}, {QStringLiteral("end"), 200}}).ok);
	const ControlResult applied = run(QStringLiteral("selection.apply"), {{QStringLiteral("name"), QStringLiteral("intro")}});
	QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
	QCOMPARE(run(QStringLiteral("range.get")).result.value(QStringLiteral("end")).toInt(), 3840);
	QCOMPARE(run(QStringLiteral("selection.list")).result.value(QStringLiteral("count")).toInt(), 1);
	// range.set and clip.select are interface state: the newest undo step is still the save.
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(run(QStringLiteral("selection.list")).result.value(QStringLiteral("count")).toInt(), 0);
	// selection.remove forgets it, one undo step of its own.
	QVERIFY(run(QStringLiteral("selection.save"), {{QStringLiteral("name"), QStringLiteral("intro")}}).ok);
	const ControlResult removed = run(QStringLiteral("selection.remove"), {{QStringLiteral("name"), QStringLiteral("intro")}});
	QVERIFY2(removed.ok, qPrintable(removed.errorMessage));
	QCOMPARE(removed.result.value(QStringLiteral("count")).toInt(), 0);
	QCOMPARE(run(QStringLiteral("selection.list")).result.value(QStringLiteral("count")).toInt(), 0);
	QCOMPARE(run(QStringLiteral("selection.remove"), {{QStringLiteral("name"), QStringLiteral("intro")}}).errorKind, ControlErrorKind::NotFound);
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(run(QStringLiteral("selection.list")).result.value(QStringLiteral("count")).toInt(), 1);
}
void CustomAccessTest::aViewMemorySaysWhatItKeepsAndAsksBeforeReplacing() {
	const QString name = QStringLiteral("bridge");
	QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 1920}}).ok);
	const ControlResult saved = run(QStringLiteral("location.save"), {{QStringLiteral("name"), name}});
	QVERIFY2(saved.ok, qPrintable(saved.errorMessage));
	const QJsonArray captures = saved.result.value(QStringLiteral("captures")).toArray();
	QVERIFY(captures.contains(QStringLiteral("position")) && captures.contains(QStringLiteral("range")));
	QVERIFY(saved.result.value(QStringLiteral("not_kept")).toArray().contains(QStringLiteral("scroll")));
	QCOMPARE(saved.result.value(QStringLiteral("max_entries")).toInt(), 99);
	const ControlResult kept = run(QStringLiteral("location.save"), {{QStringLiteral("name"), name}, {QStringLiteral("replace"), false}});
	QCOMPARE(kept.errorKind, ControlErrorKind::Refused);
	QVERIFY(kept.errorMessage.contains(name));
	QVERIFY(run(QStringLiteral("location.save"), {{QStringLiteral("name"), name}}).result.value(QStringLiteral("replaced")).toBool());
	const ControlResult recalled = run(QStringLiteral("location.recall"), {{QStringLiteral("name"), name}});
	QVERIFY2(recalled.ok, qPrintable(recalled.errorMessage));
	const QJsonArray restored = recalled.result.value(QStringLiteral("restored")).toArray();
	QVERIFY(restored.contains(QStringLiteral("position")) && restored.contains(QStringLiteral("range")));
	QVERIFY(!recalled.result.value(QStringLiteral("skipped")).toArray().contains(QStringLiteral("position")));
	QVERIFY(run(QStringLiteral("location.list")).result.value(QStringLiteral("not_kept")).toArray().contains(QStringLiteral("layout")));
	QVERIFY(run(QStringLiteral("location.remove"), {{QStringLiteral("name"), name}}).ok);
	// A saved selection keeps the same contract.
	QVERIFY(run(QStringLiteral("selection.save"), {{QStringLiteral("name"), name}}).ok);
	QCOMPARE(run(QStringLiteral("selection.save"), {{QStringLiteral("name"), name}, {QStringLiteral("replace"), false}}).errorKind,
		ControlErrorKind::Refused);
	const ControlResult applied = run(QStringLiteral("selection.apply"), {{QStringLiteral("name"), name}});
	QVERIFY2(applied.ok, qPrintable(applied.errorMessage));
	QVERIFY(applied.result.value(QStringLiteral("restored")).toArray().contains(QStringLiteral("range")));
	QVERIFY(run(QStringLiteral("selection.remove"), {{QStringLiteral("name"), name}}).ok);
	QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}}).ok);
	// Headless there is no shell, so no project's layout is on screen.
	QVERIFY(!run(QStringLiteral("window.layout_list")).result.value(QStringLiteral("project_layout_shown")).toBool());
}
void CustomAccessTest::aClipIdThatNamesNoClipIsRefusedByTheValidator() {
	QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 0}}).ok);
	const int kept = run(QStringLiteral("selection.list")).result.value(QStringLiteral("count")).toInt();
	const ControlResult saved = run(QStringLiteral("selection.save"),
		{{QStringLiteral("name"), QStringLiteral("blank")}, {QStringLiteral("clips"), QJsonArray{QString()}}});
	QVERIFY2(!saved.ok, "an empty clip id was stored as a clip");
	QCOMPARE(saved.errorKind, ControlErrorKind::NotFound);
	QCOMPARE(run(QStringLiteral("selection.list")).result.value(QStringLiteral("count")).toInt(), kept);
	// A well-formed id naming no live clip is still refused the same way.
	const ControlResult ghost = run(QStringLiteral("selection.save"),
		{{QStringLiteral("name"), QStringLiteral("ghost")}, {QStringLiteral("clips"), QJsonArray{QStringLiteral("clip-99999")}}});
	QCOMPARE(ghost.errorKind, ControlErrorKind::NotFound);
	QCOMPARE(run(QStringLiteral("selection.list")).result.value(QStringLiteral("count")).toInt(), kept);
}
void CustomAccessTest::aViewMemoryChangeReportsTheUndoItRecorded() {
	QVERIFY(run(QStringLiteral("range.set"), {{QStringLiteral("start"), 0}, {QStringLiteral("end"), 1920}}).ok);
	QVERIFY(run(QStringLiteral("location.save"), {{QStringLiteral("name"), QStringLiteral("paid")}}).ok);
	const QJsonObject saved = transactionOf(QStringLiteral("location.save"));
	QVERIFY2(!saved.isEmpty(), "location.save recorded no transaction at all");
	QVERIFY2(saved.value(QStringLiteral("before")).toObject().contains(QStringLiteral("locations")),
		"location.save reported no previous list of locations to take back");
	QVERIFY(saved.value(QStringLiteral("reversible")).toBool());
	QVERIFY(!saved.value(QStringLiteral("mechanism")).toString()
		.contains(QStringLiteral("recorded no inverse or snapshot")));
	QVERIFY(run(QStringLiteral("location.remove"), {{QStringLiteral("name"), QStringLiteral("paid")}}).ok);
	const QJsonObject removed = transactionOf(QStringLiteral("location.remove"));
	QVERIFY2(!removed.isEmpty(), "location.remove recorded no transaction at all");
	QVERIFY(!removed.value(QStringLiteral("mechanism")).toString()
		.contains(QStringLiteral("recorded no inverse or snapshot")));
	// The same contract for a saved selection.
	QVERIFY(run(QStringLiteral("selection.save"), {{QStringLiteral("name"), QStringLiteral("verse")}}).ok);
	const QJsonObject selection = transactionOf(QStringLiteral("selection.save"));
	QVERIFY2(selection.value(QStringLiteral("before")).toObject().contains(QStringLiteral("selections")),
		"selection.save reported no previous list of selections to take back");
	QVERIFY(run(QStringLiteral("selection.remove"), {{QStringLiteral("name"), QStringLiteral("verse")}}).ok);
	QVERIFY(!transactionOf(QStringLiteral("selection.remove")).value(QStringLiteral("mechanism")).toString()
		.contains(QStringLiteral("recorded no inverse or snapshot")));
}
void CustomAccessTest::theHistoryIsWalkedInOneGo() {
	const QString id = control::trackIdOf(m_track);
	QVERIFY(run(QStringLiteral("track.set_color"), {{QStringLiteral("track"), id}, {QStringLiteral("color"), QStringLiteral("#111111")}}).ok);
	QVERIFY(run(QStringLiteral("track.set_color"), {{QStringLiteral("track"), id}, {QStringLiteral("color"), QStringLiteral("#222222")}}).ok);
	const ControlResult back = run(QStringLiteral("control.history_jump"), {{QStringLiteral("steps"), -2}});
	QVERIFY2(back.ok, qPrintable(back.errorMessage));
	QCOMPARE(back.result.value(QStringLiteral("steps")).toInt(), -2);
	QVERIFY(!m_track->color().has_value());
	const ControlResult forward = run(QStringLiteral("control.history_jump"), {{QStringLiteral("steps"), 2}});
	QVERIFY2(forward.ok, qPrintable(forward.errorMessage));
	QCOMPARE(m_track->color()->name(), QStringLiteral("#222222"));
	QCOMPARE(run(QStringLiteral("control.history_jump"), {{QStringLiteral("steps"), 0}}).errorKind, ControlErrorKind::InvalidArgs);
}
void CustomAccessTest::theSettingsAreFoundBackedUpAndKept() {
	const auto value = [] { return ConfigManager::inst()->value(QStringLiteral("ui"), QStringLiteral("printnotelabels")); };
	const ControlResult found = run(QStringLiteral("settings.search"), {{QStringLiteral("text"), QStringLiteral("note names")}});
	QVERIFY2(found.ok, qPrintable(found.errorMessage));
	QCOMPARE(found.result.value(QStringLiteral("settings")).toArray().first().toObject().value(QStringLiteral("key")).toString(),
		QStringLiteral("ui/printnotelabels"));
	QVERIFY(run(QStringLiteral("settings.search")).result.value(QStringLiteral("count")).toInt() > 30);
	QVERIFY(run(QStringLiteral("settings.set"), {{QStringLiteral("key"), QStringLiteral("ui/printnotelabels")}, {QStringLiteral("value"), QStringLiteral("1")}}).ok);
	ConfigManager::inst()->setValue(QStringLiteral("paths"), QStringLiteral("vstdir"), m_dir.path());
	const QString backup = m_dir.filePath(QStringLiteral("settings.json"));
	QVERIFY(run(QStringLiteral("settings.export"), {{QStringLiteral("file"), backup}}).ok);
	QVERIFY2(!fileText(backup).contains(QStringLiteral("vstdir")), "a folder path travelled in a settings backup");
	QVERIFY(run(QStringLiteral("settings.set"), {{QStringLiteral("key"), QStringLiteral("ui/printnotelabels")}, {QStringLiteral("value"), QStringLiteral("0")}}).ok);
	const ControlResult restored = run(QStringLiteral("settings.import"), {{QStringLiteral("file"), backup}});
	QVERIFY2(restored.ok, qPrintable(restored.errorMessage));
	QVERIFY(restored.result.value(QStringLiteral("applied")).toInt() >= 1);
	QCOMPARE(value(), QStringLiteral("1"));
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(value(), QStringLiteral("0"));
	QCOMPARE(run(QStringLiteral("settings.import"), {{QStringLiteral("file"), m_dir.filePath(QStringLiteral("none.json"))}}).errorKind,
		ControlErrorKind::InvalidArgs);

	QVERIFY(run(QStringLiteral("settings.preset_save"), {{QStringLiteral("name"), QStringLiteral("studio")}}).ok);
	QCOMPARE(static_cast<int>(run(QStringLiteral("settings.preset_list")).result.value(QStringLiteral("presets")).toArray().size()), 1);
	QVERIFY(run(QStringLiteral("settings.set"), {{QStringLiteral("key"), QStringLiteral("ui/printnotelabels")}, {QStringLiteral("value"), QStringLiteral("1")}}).ok);
	QVERIFY(run(QStringLiteral("settings.preset_apply"), {{QStringLiteral("name"), QStringLiteral("studio")}}).ok);
	QCOMPARE(value(), QStringLiteral("0"));
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(value(), QStringLiteral("1"));
	QVERIFY(run(QStringLiteral("settings.preset_remove"), {{QStringLiteral("name"), QStringLiteral("studio")}}).ok);
	QCOMPARE(run(QStringLiteral("settings.preset_apply"), {{QStringLiteral("name"), QStringLiteral("studio")}}).errorKind,
		ControlErrorKind::NotFound);
}
void CustomAccessTest::theDeviceUndoOfAnAbsentKeyDeletesItAgain() {
	ConfigManager::inst()->deleteValue(QStringLiteral("audioengine"), QStringLiteral("audiodev"));
	QVERIFY(!deviceKeyThere());
	QVERIFY(run(QStringLiteral("audio.device_set"), {{QStringLiteral("device"), someDevice()}}).ok);
	QVERIFY(deviceKeyThere());

	const QJsonObject inverse = revtest::stateOf(QStringLiteral("audio.device_set"))
		.value(QStringLiteral("inverse")).toObject();
	QVERIFY2(!inverse.isEmpty(), "audio.device_set advertised no inverse at all");
	QVERIFY2(run(inverse.value(QStringLiteral("op")).toString(),
		inverse.value(QStringLiteral("args")).toObject()).ok,
		"the advertised inverse of audio.device_set was refused when replayed");
	QVERIFY2(!deviceKeyThere(),
		"reversing audio.device_set left an audioengine/audiodev key that was not there before");
}
void CustomAccessTest::theSettingUndoOfAnAbsentKeyDeletesItAgain() {
	ConfigManager::inst()->deleteValue(QStringLiteral("ui"), AbsentKey);
	QVERIFY(!settingThere(AbsentKey));
	QVERIFY(run(QStringLiteral("settings.set"), {{QStringLiteral("key"), AbsentKey}, {QStringLiteral("value"), QStringLiteral("1")}}).ok);
	QVERIFY(settingThere(AbsentKey));

	const QJsonObject inverse = revtest::stateOf(QStringLiteral("settings.set"))
		.value(QStringLiteral("inverse")).toObject();
	QVERIFY2(!inverse.isEmpty(), "settings.set advertised no inverse at all");
	QVERIFY2(run(inverse.value(QStringLiteral("op")).toString(),
		inverse.value(QStringLiteral("args")).toObject()).ok,
		"the advertised inverse of settings.set was refused when replayed");
	QVERIFY2(!settingThere(AbsentKey),
		"reversing settings.set left a key that was not there before");
}
void CustomAccessTest::theSettingScaleReachesTheFileStartupReads() {
	const QString scaled = fileText(appearance::scaleFilePath()).trimmed();
	QVERIFY(run(QStringLiteral("settings.set"), {{QStringLiteral("key"), QStringLiteral("ui/scale")},
		{QStringLiteral("value"), QStringLiteral("2")}}).ok);
	QCOMPARE(fileText(appearance::scaleFilePath()).trimmed(), QStringLiteral("2"));

	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(fileText(appearance::scaleFilePath()).trimmed(), scaled);
	QVERIFY(run(QStringLiteral("control.redo")).ok);
	QCOMPARE(fileText(appearance::scaleFilePath()).trimmed(), QStringLiteral("2"));
	QVERIFY(run(QStringLiteral("control.undo")).ok);  // the config this test borrowed is as it was
}
void CustomAccessTest::theTunerReadsANote() {
	// The input: the Dummy device captures nothing, so only this test's frames reach the tap (the master
	// would also take the engine's own silent periods while the test runs).
	QVERIFY(run(QStringLiteral("tuner.set"), {{QStringLiteral("source"), QStringLiteral("input")}, {QStringLiteral("reference_hz"), 440.0}}).ok);
	QVERIFY(!run(QStringLiteral("tuner.get_state")).result.value(QStringLiteral("clear")).toBool());
	const double rate = Engine::audioEngine()->baseSampleRate();
	std::vector<SampleFrame> tone(TunerTap::Capacity);
	for (std::size_t i = 0; i < tone.size(); ++i)
	{
		const auto v = static_cast<float>(0.5 * std::sin(2.0 * M_PI * 440.0 * static_cast<double>(i) / rate));
		tone[i] = SampleFrame(v, v);
	}
	TunerTap::instance().push(TunerTap::Source::Master, tone.data(), tone.size());  // not the chosen source: ignored
	QVERIFY(!run(QStringLiteral("tuner.get_state")).result.value(QStringLiteral("clear")).toBool());
	TunerTap::instance().push(TunerTap::Source::Input, tone.data(), tone.size());
	const QJsonObject a4 = run(QStringLiteral("tuner.get_state")).result;
	QVERIFY2(a4.value(QStringLiteral("clear")).toBool(), "no pitch in a 440 Hz tone");
	QCOMPARE(a4.value(QStringLiteral("note")).toString(), QStringLiteral("A4"));
	QVERIFY2(std::abs(a4.value(QStringLiteral("cents")).toDouble()) < 15.0, qPrintable(QString::number(a4.value(QStringLiteral("cents")).toDouble())));
	QVERIFY(run(QStringLiteral("tuner.set"), {{QStringLiteral("reference_hz"), 432.0}}).ok);
	QVERIFY(run(QStringLiteral("tuner.get_state")).result.value(QStringLiteral("cents")).toDouble() > 20.0);
	QCOMPARE(run(QStringLiteral("tuner.set"), {{QStringLiteral("reference_hz"), 300.0}}).errorKind, ControlErrorKind::InvalidArgs);
	QVERIFY(run(QStringLiteral("tuner.set"), {{QStringLiteral("source"), QStringLiteral("off")}, {QStringLiteral("reference_hz"), 440.0}}).ok);
	QVERIFY(!run(QStringLiteral("tuner.get_state")).result.value(QStringLiteral("clear")).toBool());
}
void CustomAccessTest::workflowsAreKeptWithTheProjectWhenChosen() {
	const auto count = [] { return run(QStringLiteral("workflow.list")).result.value(QStringLiteral("count")).toInt(); };
	const QString bare = m_dir.filePath(QStringLiteral("noworkspace.mmp"));
	QVERIFY(Engine::getSong()->saveProjectFile(bare));
	QVERIFY2(!fileText(bare).contains(QStringLiteral("<workspace")), "a project that keeps nothing wrote <workspace>");
	const QJsonArray steps{QJsonObject{{QStringLiteral("command"), QStringLiteral("track.add")},
		{QStringLiteral("args"), QJsonObject{{QStringLiteral("type"), QStringLiteral("sample")}}}}};
	QVERIFY(run(QStringLiteral("workflow.save"), {{QStringLiteral("name"), QStringLiteral("shared one")}, {QStringLiteral("steps"), steps}}).ok);
	QCOMPARE(run(QStringLiteral("workflow.list")).result.value(QStringLiteral("scope")).toString(), QStringLiteral("shared"));
	QVERIFY(run(QStringLiteral("workflow.set_scope"), {{QStringLiteral("scope"), QStringLiteral("project")}}).result
		.value(QStringLiteral("changed")).toBool());
	QCOMPARE(count(), 0);  // the project's own library: the shared one is not in it
	const ControlResult kept = run(QStringLiteral("workflow.save"), {{QStringLiteral("name"), QStringLiteral("song one")}, {QStringLiteral("steps"), steps}});
	QVERIFY2(kept.ok, qPrintable(kept.errorMessage));
	QCOMPARE(kept.result.value(QStringLiteral("scope")).toString(), QStringLiteral("project"));
	QVERIFY(run(QStringLiteral("control.undo")).ok);
	QCOMPARE(count(), 0);
	QVERIFY(run(QStringLiteral("control.redo")).ok);
	QCOMPARE(count(), 1);
	const QString with = m_dir.filePath(QStringLiteral("workspace.mmp"));
	QVERIFY(Engine::getSong()->saveProjectFile(with));
	const QString text = fileText(with);
	QVERIFY2(text.contains(QStringLiteral("<workspace")) && text.contains(QStringLiteral("song one")), "the project's workflows were not saved");
	QVERIFY2(!text.contains(QStringLiteral("shared one")), "a shared workflow travelled in the project");
	// Read back from the file: the element is the project's library again.
	ProjectWorkspace::instance().clear();
	QCOMPARE(count(), 0);
	QDomDocument doc;
	QVERIFY(doc.setContent(text));
	ProjectWorkspace::instance().load(doc.elementsByTagName(QStringLiteral("workspace")).at(0).toElement());
	QCOMPARE(run(QStringLiteral("workflow.list")).result.value(QStringLiteral("workflows")).toArray().first().toObject()
		.value(QStringLiteral("name")).toString(), QStringLiteral("song one"));
	// Shared again: the shared library comes back as it was; the project keeps its own.
	QVERIFY(run(QStringLiteral("workflow.set_scope"), {{QStringLiteral("scope"), QStringLiteral("shared")}}).ok);
	QCOMPARE(run(QStringLiteral("workflow.list")).result.value(QStringLiteral("workflows")).toArray().first().toObject()
		.value(QStringLiteral("name")).toString(), QStringLiteral("shared one"));
	QVERIFY(run(QStringLiteral("workflow.remove"), {{QStringLiteral("name"), QStringLiteral("shared one")}}).ok);
	// The project's own layout without an interface: forgetting none is NotFound, remembering needs the shell.
	ProjectWorkspace::instance().clear();
	QCOMPARE(run(QStringLiteral("window.layout_project_clear")).errorKind, ControlErrorKind::NotFound);
	QCOMPARE(run(QStringLiteral("window.layout_project_save")).errorKind, ControlErrorKind::Requires);
	QVERIFY(!run(QStringLiteral("window.layout_list")).result.value(QStringLiteral("project_layout")).toBool());
}
