/*
 * ScriptCommandBridgeTest.cpp - zene.command() reaches the registry and keeps the sandbox
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

/*! The scripting spine (include/ScriptCommandBridge.h): a Lua script calls any registered command
 *  through zene.command and gets the reply as a table; zene.commands lists the registry; the
 *  sandbox holds - a refused command and a path outside the project directory come back as
 *  nil, message, "refused", and nothing runs. */

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QJsonObject>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "Engine.h"
#include "ScriptCommandBridge.h"
#include "ScriptEngine.h"
#include "Song.h"
#include "Track.h"

using namespace lmms;

namespace
{

QStringList run(const QString& source)
{
	ScriptEngine* engine = ScriptEngine::instance();
	engine->takeLogMessages();
	QString error;
	const auto result = engine->runString(source, &error, QStringLiteral("=(bridge test)"));
	if (result != ScriptEngine::RunResult::Ok) { return {QStringLiteral("ERROR: ") + error}; }
	return engine->takeLogMessages();
}

bool hasTrackNamed(const QString& name)
{
	for (const Track* track : Engine::getSong()->tracks())
	{
		if (track->name() == name) { return true; }
	}
	return false;
}

} // namespace

class ScriptCommandBridgeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY(m_projectDir.isValid());
		ScriptEngine::instance()->setProjectDir(m_projectDir.path());
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void commandsListsTheWholeRegistry()
	{
		const QStringList out = run(QStringLiteral(
			"local ids = zene.commands()\n"
			"local found = false\n"
			"for _, id in ipairs(ids) do if id == 'track.add' then found = true end end\n"
			"print(#ids .. ' ' .. tostring(found))\n"));
		QCOMPARE(out, QStringList{QStringLiteral("%1 true").arg(ControlRegistry::instance()->commandIds().size())});
	}

	void aCommandRunsThroughTheRegistryAndAnswersATable()
	{
		const QStringList out = run(QStringLiteral(
			"local reply, err = zene.command('track.add', {type = 'instrument', name = 'From Lua'})\n"
			"print(tostring(err == nil) .. ' ' .. string.sub(reply.track, 1, 4))\n"
			"local list = zene.command('track.list')\n"
			"print(tostring(#list.tracks > 0) .. ' ' .. type(list.tracks[1].id))\n"));
		QCOMPARE(out, (QStringList{QStringLiteral("true trk-"), QStringLiteral("true string")}));
		QVERIFY(hasTrackNamed(QStringLiteral("From Lua")));
	}

	void stringsWithEmbeddedNullBytesKeepTheirLengthAcrossCommands()
	{
		const QString seededName = QString::fromUtf8("A\0B", 3);
		const ControlResult seeded = ControlRegistry::instance()->invoke(QStringLiteral("track.add"),
			{{QStringLiteral("type"), QStringLiteral("instrument")}, {QStringLiteral("name"), seededName}});
		QVERIFY2(seeded.ok, qPrintable(seeded.errorMessage));

		const QStringList out = run(QStringLiteral(
			"local added, err = zene.command('track.add', {type = 'instrument', name = 'C\\0D'})\n"
			"local fromLua = zene.command('track.get_state', {track = added.track})\n"
			"local fromJson = zene.command('track.get_state', {track = '%1'})\n"
			"print(tostring(err == nil) .. ' ' .. #fromLua.name .. ' ' .. (string.byte(fromLua.name, 2) or -1))\n"
			"print(#fromJson.name .. ' ' .. (string.byte(fromJson.name, 2) or -1))\n").arg(seeded.result.value(QStringLiteral("track")).toString()));
		QCOMPARE(out, (QStringList{QStringLiteral("true 3 0"), QStringLiteral("3 0")}));
	}

	void aNonTableArgumentIsRejectedTyped()
	{
		// A scalar second argument used to be silently swapped for {}, so the registry
		// validated (and ran) defaults the caller never asked for (review L17 #17).
		const QStringList out = run(QStringLiteral(
			"local reply, err, kind = zene.command('track.list', 'oops')\n"
			"print(tostring(reply) .. ' ' .. kind)\n"
			"local reply2, err2, kind2 = zene.command('track.list', 42)\n"
			"print(tostring(reply2) .. ' ' .. kind2)\n"
			"local reply3, err3 = zene.command('track.list', nil)\n"
			"print(tostring(reply3 ~= nil) .. ' ' .. tostring(err3 == nil))\n"));
		QCOMPARE(out, (QStringList{QStringLiteral("nil invalid_args"), QStringLiteral("nil invalid_args"),
			QStringLiteral("true true")}));
	}

	void aRegistryRefusalComesBackTyped()
	{
		const QStringList out = run(QStringLiteral(
			"local reply, err, kind = zene.command('no.such_command')\n"
			"print(tostring(reply) .. ' ' .. kind)\n"));
		QCOMPARE(out, QStringList{QStringLiteral("nil not_found")});
	}

	void commandsThatLeaveTheSandboxAreRefused()
	{
		for (const QString& id : {QStringLiteral("control.quit"), QStringLiteral("script.run"),
			QStringLiteral("settings.set"), QStringLiteral("telemetry.consent")})
		{
			QVERIFY2(!ScriptCommandBridge::refusalFor(id).isEmpty(), qPrintable(id));
		}
		QVERIFY(ScriptCommandBridge::refusalFor(QStringLiteral("track.add")).isEmpty());
		const QStringList out = run(QStringLiteral(
			"local reply, err, kind = zene.command('control.quit')\n"
			"print(tostring(reply) .. ' ' .. kind)\n"));
		QCOMPARE(out, QStringList{QStringLiteral("nil refused")});
	}

	void aPathOutsideTheProjectDirectoryIsRefusedAndNothingIsWritten()
	{
		QTemporaryDir outside;
		const QString escape = QDir(outside.path()).filePath(QStringLiteral("escape.mmp"));
		const QStringList out = run(QStringLiteral(
			"local reply, err, kind = zene.command('project.save', {path = '%1'})\n"
			"print(tostring(reply) .. ' ' .. kind)\n").arg(escape));
		QCOMPARE(out, QStringList{QStringLiteral("nil refused")});
		QVERIFY(!QFile::exists(escape));

		QJsonObject nested{{QStringLiteral("options"), QJsonObject{{QStringLiteral("out_dir"), outside.path()}}}};
		QString error;
		QVERIFY(!ScriptCommandBridge::sandboxPaths(QStringLiteral("render.render"), nested, &error));
		// Paths under names the key rule does not know (review L01-1 #1): browser.query's root,
		// project.diff's a/b, stem.job_start's source.
		QJsonObject root{{QStringLiteral("root"), QStringLiteral("/")}};
		QVERIFY(!ScriptCommandBridge::sandboxPaths(QStringLiteral("browser.query"), root, &error));
		QJsonObject diff{{QStringLiteral("a"), QStringLiteral("/etc/hosts")}, {QStringLiteral("b"), QStringLiteral("x.mmp")}};
		QVERIFY(!ScriptCommandBridge::sandboxPaths(QStringLiteral("project.diff"), diff, &error));
		QJsonObject stem{{QStringLiteral("source"), outside.path()}};
		QVERIFY(!ScriptCommandBridge::sandboxPaths(QStringLiteral("stem.job_start"), stem, &error));
		// ...while `a` is not a path for a command that does not take one there.
		QJsonObject other{{QStringLiteral("a"), QStringLiteral("/not-a-path")}};
		QVERIFY(ScriptCommandBridge::sandboxPaths(QStringLiteral("revisions.compare"), other, &error));
		QVERIFY(!ScriptCommandBridge::refusalFor(QStringLiteral("stem.model_download")).isEmpty());
		QJsonObject inside{{QStringLiteral("path"), QStringLiteral("song.mmp")}};
		QVERIFY2(ScriptCommandBridge::sandboxPaths(QStringLiteral("project.save"), inside, &error), qPrintable(error));
		QVERIFY(inside.value(QStringLiteral("path")).toString().startsWith(QDir(m_projectDir.path()).canonicalPath()));
	}

private:
	QTemporaryDir m_projectDir;
};

QTEST_MAIN(ScriptCommandBridgeTest)
#include "ScriptCommandBridgeTest.moc"
