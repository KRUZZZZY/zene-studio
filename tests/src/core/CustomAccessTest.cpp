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
/*! 0.5.0 W3 L13 / L14, the commands that need no interface: the track manager's list and its colour,
 *  visibility and icon edits, each one undo step, the icon saved only when set (WF-097 / WF-120); every
 *  mixer channel's processing load (WF-104); a question answered from the manual and the commands, offline
 *  (WF-142); the shipped release notes and their once-per-version marker (WF-141); memory locations and saved
 *  selections, saved with the project only when there are any (WF-098 / WF-106); the undo history walked
 *  several steps at once (WF-102); the settings found by words, backed up, restored and kept as presets, each
 *  write one undo step, no folder path travelling (WF-122); the tuner reads A4 off a 440 Hz tone on its tap,
 *  against the reference chosen (WF-147). */

#include <QtTest>

#include <cmath>
#include <vector>

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "ControlRegistryGroups.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TrackLooks.h"
#include "TunerTap.h"
#include "AudioEngine.h"
#include "SampleFrame.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
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

private:
	QTemporaryDir m_dir;
	Track* m_track = nullptr;
};

QTEST_MAIN(CustomAccessTest)
#include "CustomAccessTest.moc"
