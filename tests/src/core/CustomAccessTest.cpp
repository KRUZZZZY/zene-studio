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
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		// A throwaway config: app.whats_new's marker and the settings presets are written to it.
		qputenv("HOME", m_dir.path().toLocal8Bit());
		ConfigManager::inst()->loadConfigFile(m_dir.filePath(QStringLiteral("zene.xml")));
		Engine::init(true);
		ControlRegistry::setReady(true);
		m_track = Track::create(Track::Type::Sample, Engine::getSong());
		QVERIFY(m_track != nullptr);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theOverviewListsEveryTrack()
	{
		const ControlResult listed = run(QStringLiteral("track.overview"));
		QVERIFY2(listed.ok, qPrintable(listed.errorMessage));
		QCOMPARE(listed.result.value(QStringLiteral("count")).toInt(), static_cast<int>(Engine::getSong()->tracks().size()));
		const QJsonObject entry = overviewOf(m_track);
		QCOMPARE(entry.value(QStringLiteral("type")).toString(), QStringLiteral("sample"));
		QVERIFY(entry.contains(QStringLiteral("cpu_percent")));
		QVERIFY(entry.value(QStringLiteral("visible")).toBool());
	}

	void aColourIsOneUndoStep()
	{
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

	void hidingIsOneUndoStep()
	{
		const QString id = control::trackIdOf(m_track);
		const ControlResult hidden = run(QStringLiteral("track.set_visible"), {{QStringLiteral("track"), id}, {QStringLiteral("visible"), false}});
		QVERIFY2(hidden.ok, qPrintable(hidden.errorMessage));
		QVERIFY(!m_track->isVisible());
		QVERIFY(!m_track->isMuted());
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QVERIFY(m_track->isVisible());
	}

	void anIconIsSavedOnlyWhenSet()
	{
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

	void everyChannelReportsItsLoad()
	{
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

	void aQuestionFindsTheManualAndTheCommands()
	{
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

	void theReleaseNotesAreShownOncePerVersion()
	{
		const ControlResult notes = run(QStringLiteral("app.whats_new"));
		QVERIFY2(notes.ok, qPrintable(notes.errorMessage));
		QVERIFY(!notes.result.value(QStringLiteral("notes")).toObject().value(QStringLiteral("items")).toArray().isEmpty());
		QVERIFY(!notes.result.value(QStringLiteral("update_check")).toBool());
		QVERIFY(!notes.result.value(QStringLiteral("seen")).toBool());
		markWhatsNewSeen();
		QVERIFY(run(QStringLiteral("app.whats_new")).result.value(QStringLiteral("seen")).toBool());
	}

	void aMemoryLocationComesBack()
	{
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

	void aSavedSelectionComesBack()
	{
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

	//! Design theme 22: a save says what it kept and what a memory of its kind never keeps; asked not to replace, a
	//! name in use is refused (the Window menu's Save); a recall says what came back.
	void aViewMemorySaysWhatItKeepsAndAsksBeforeReplacing()
	{
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

	//! Luna cluster 4915: a clip id that names no clip is refused, and the empty string is one of
	//! them - it is not the "nothing unknown" answer the validator used to return.
	void aClipIdThatNamesNoClipIsRefusedByTheValidator()
	{
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

	//! Luna cluster 4917: a view-memory save or remove claims to be undoable and records an undo
	//! step, so its recorded transaction must say what it took back and how - not fall back on the
	//! registry's "this command recorded no inverse or snapshot" record.
	void aViewMemoryChangeReportsTheUndoItRecorded()
	{
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

	void theHistoryIsWalkedInOneGo()
	{
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

	void theSettingsAreFoundBackedUpAndKept()
	{
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

	//! Luna cluster 4941: audio.device_set on a config with no audioengine/audiodev key records an undo that writes
	//! the empty previous value back, so undoing leaves a key that was never there - and the inverse it advertises
	//! sets an empty device name, which the command itself refuses.
	void theDeviceUndoOfAnAbsentKeyDeletesItAgain()
	{
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

	//! Luna cluster 4940: settings.set on a key that did not exist advertised settings.set with the EMPTY previous
	//! value as its inverse, which writes an empty key rather than deleting it - so a client that follows the
	//! transaction ends with a key the command was told never existed.
	void theSettingUndoOfAnAbsentKeyDeletesItAgain()
	{
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

	//! Luna cluster 4937: the scale is read at startup from a file BESIDE the config (main() -> applyStoredScale),
	//! so a settings.set of ui/scale that only writes ConfigManager leaves the config and the scale file
	//! disagreeing - and the new scale is not applied at the next start.
	void theSettingScaleReachesTheFileStartupReads()
	{
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

	void theTunerReadsANote()
	{
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

	void workflowsAreKeptWithTheProjectWhenChosen()
	{
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

	//! Design theme 19: a run that stops says which step failed and why, what applied and what did not run; it
	//! resumes with the step retried, or skipped and the rest run; with nothing stopped there is nothing to resume.
	void aStoppedWorkflowSaysWhereAndResumes()
	{
		const auto step = [](const char* command, const QJsonObject& args) {
			return QJsonObject{{QStringLiteral("command"), QLatin1String(command)}, {QStringLiteral("args"), args}};
		};
		const QJsonArray steps{step("track.list", {}), step("track.remove", {{QStringLiteral("track"), QStringLiteral("no-such-track")}}),
			step("track.list", {})};
		QVERIFY(run(QStringLiteral("workflow.save"), {{QStringLiteral("name"), QStringLiteral("stops")}, {QStringLiteral("steps"), steps}}).ok);
		const auto done = [] { return run(QStringLiteral("workflow.status")).result.value(QStringLiteral("state")).toString() == QStringLiteral("done"); };
		QVERIFY(run(QStringLiteral("workflow.run"), {{QStringLiteral("name"), QStringLiteral("stops")}}).ok);
		QTRY_VERIFY(done());
		QJsonObject status = run(QStringLiteral("workflow.status")).result;
		QCOMPARE(status.value(QStringLiteral("stopped_at")).toInt(), 1);
		QCOMPARE(status.value(QStringLiteral("failed")).toObject().value(QStringLiteral("step")).toInt(), 2);
		QVERIFY(!status.value(QStringLiteral("failed")).toObject().value(QStringLiteral("error")).toString().isEmpty());
		QCOMPARE(status.value(QStringLiteral("applied_steps")).toArray(), QJsonArray{1});
		QCOMPARE(status.value(QStringLiteral("not_run")).toArray(), QJsonArray{3});
		QVERIFY(status.value(QStringLiteral("message")).toString().contains(QStringLiteral("stopped at step 2 of 3")));

		QCOMPARE(run(QStringLiteral("workflow.resume")).result.value(QStringLiteral("retried")).toInt(), 2);
		QTRY_VERIFY(done());
		QCOMPARE(run(QStringLiteral("workflow.status")).result.value(QStringLiteral("stopped_at")).toInt(), 1);  // it fails again
		QCOMPARE(run(QStringLiteral("workflow.resume"), {{QStringLiteral("skip"), true}}).result.value(QStringLiteral("skipped")).toInt(), 2);
		QTRY_VERIFY(done());
		status = run(QStringLiteral("workflow.status")).result;
		QCOMPARE(status.value(QStringLiteral("stopped_at")).toInt(), -1);
		QCOMPARE(status.value(QStringLiteral("applied_steps")).toArray(), (QJsonArray{1, 3}));
		QCOMPARE(status.value(QStringLiteral("skipped_steps")).toArray(), QJsonArray{2});
		QCOMPARE(run(QStringLiteral("workflow.resume")).errorKind, ControlErrorKind::Refused);
		QVERIFY(run(QStringLiteral("workflow.remove"), {{QStringLiteral("name"), QStringLiteral("stops")}}).ok);
	}

private:
	QTemporaryDir m_dir;
	Track* m_track = nullptr;
};

QTEST_MAIN(CustomAccessTest)
#include "CustomAccessTest.moc"
