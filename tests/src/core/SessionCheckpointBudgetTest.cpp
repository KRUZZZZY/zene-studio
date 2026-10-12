/*
 * SessionCheckpointBudgetTest.cpp - Q050-044: charge both checkpoint snapshots
 * to the bounded undo journal.
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
 * License along with this program (see COPYING); if not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <cstdint>

#include <QDir>
#include <QJsonArray>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "ProjectJournal.h"
#include "Song.h"

using namespace lmms;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QStringList trackNames()
{
	QStringList names;
	for (const QJsonValue& track : run(QStringLiteral("track.list")).result.value(QStringLiteral("tracks")).toArray())
	{
		names.append(track.toObject().value(QStringLiteral("name")).toString());
	}
	return names;
}

int journalDepth()
{
	return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
}

QString longTrackName(int seed)
{
	QString name;
	name.reserve(512);
	uint32_t value = static_cast<uint32_t>(seed + 1);
	for (int i = 0; i < 512; ++i)
	{
		value = value * 1664525u + 1013904223u;
		name.append(QChar(static_cast<char16_t>('a' + value % 26)));
	}
	return name;
}

bool addNoisyTracks(int count)
{
	for (int i = 0; i < count; ++i)
	{
		if (!run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")},
			{QStringLiteral("name"), longTrackName(i)}}).ok) { return false; }
	}
	return true;
}

QString noisyTracksScript(int count)
{
	QString source;
	for (int i = 0; i < count; ++i)
	{
		source += QStringLiteral("zene.command('track.add', {type = 'instrument', name = '%1'})\n")
			.arg(longTrackName(i));
	}
	return source;
}

} // namespace

class SessionCheckpointBudgetTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_dir;

	QString path(const QString& name) const { return m_dir.filePath(name); }

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

	void aLargeOpenedPostStateIsIncludedInTheUndoBudget()
	{
		const QString target = path(QStringLiteral("large-open-target.mmp"));
		freshProject(QStringLiteral("target-base"), QStringLiteral("large-open-target.mmp"));
		QVERIFY(addNoisyTracks(24));
		QVERIFY(run(QStringLiteral("project.save"), {{QStringLiteral("path"), target}}).ok);
		const auto largeSnapshot = Engine::getSong()->captureUndoSnapshot();
		QVERIFY(largeSnapshot);
		QVERIFY(largeSnapshot->size() > 512);

		freshProject(QStringLiteral("displaced"), QStringLiteral("small-open-source.mmp"));
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")},
			{QStringLiteral("name"), QStringLiteral("unsaved-before-large-open")}}).ok);
		Engine::projectJournal()->clearJournal();
		const QStringList tracksBefore = trackNames();
		const QString beforeFile = Engine::getSong()->projectFileName();
		const auto beforeSnapshot = Engine::getSong()->captureUndoSnapshot();
		QVERIFY(beforeSnapshot);
		const qint64 limit = beforeSnapshot->size() + 512;
		QVERIFY(Engine::projectJournal()->setMaxUndoBytes(limit));

		const ControlResult opened = run(QStringLiteral("project.open"), {{QStringLiteral("path"), target}});
		QVERIFY2(!opened.ok, "project.open kept a pre-state checkpoint but exceeded its byte budget with the redo state");
		QCOMPARE(opened.errorKind, ControlErrorKind::Refused);
		QCOMPARE(trackNames(), tracksBefore);
		QCOMPARE(Engine::getSong()->projectFileName(), beforeFile);
		QCOMPARE(journalDepth(), 0);
		QVERIFY(Engine::projectJournal()->retainedBytes() <= limit);
	}

	void aLargeScriptPostStateIsIncludedInTheUndoBudget()
	{
		freshProject(QStringLiteral("script-base"), QStringLiteral("script-budget.mmp"));
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")},
			{QStringLiteral("name"), QStringLiteral("unsaved-before-large-script")}}).ok);
		Engine::projectJournal()->clearJournal();
		const QStringList tracksBefore = trackNames();
		const auto beforeSnapshot = Engine::getSong()->captureUndoSnapshot();
		QVERIFY(beforeSnapshot);
		const qint64 limit = beforeSnapshot->size() + 512;
		QVERIFY(Engine::projectJournal()->setMaxUndoBytes(limit));

		const ControlResult scripted = run(QStringLiteral("script.run"),
			{{QStringLiteral("source"), noisyTracksScript(24)}});
		QVERIFY2(!scripted.ok, "script.run kept a pre-state checkpoint but exceeded its byte budget with the redo state");
		QCOMPARE(scripted.errorKind, ControlErrorKind::Refused);
		QCOMPARE(trackNames(), tracksBefore);
		QCOMPARE(journalDepth(), 0);
		QVERIFY(Engine::projectJournal()->retainedBytes() <= limit);
	}
};

QTEST_GUILESS_MAIN(SessionCheckpointBudgetTest)
#include "SessionCheckpointBudgetTest.moc"
