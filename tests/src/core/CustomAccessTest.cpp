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
 *  mixer channel's processing load (WF-104). */

#include <QtTest>

#include <QFile>
#include <QJsonArray>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "Mixer.h"
#include "SampleTrack.h"
#include "Song.h"
#include "TrackLooks.h"

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
		for (const QJsonValue& channel : channels) { QVERIFY(channel.toObject().value(QStringLiteral("cpu_percent")).toDouble() >= 0.0); }
		QVERIFY(load.result.value(QStringLiteral("total_percent")).toDouble() >= 0.0);
	}

private:
	QTemporaryDir m_dir;
	Track* m_track = nullptr;
};

QTEST_MAIN(CustomAccessTest)
#include "CustomAccessTest.moc"
