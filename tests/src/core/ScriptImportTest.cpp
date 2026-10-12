/*
 * ScriptImportTest.cpp - sandboxed Lua `require` (Q050-051)
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

#include <QtTest>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "Engine.h"
#include "ScriptApiVersion.h"
#include "ScriptConsole.h"
#include "ScriptEngine.h"
#include "ScriptImport.h"
#include "Song.h"

namespace
{

QString header()
{
	return QStringLiteral("--! lmms-api %1\n").arg(QStringLiteral(ZENE_LUA_API_MAJOR_MINOR_STRING));
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
	{
		return false;
	}
	file.write(bytes);
	return true;
}

//! Run \a entry (a script file with the version header added) in \a dir.
lmms::ScriptEngine::RunResult runEntry(const QString& dir, const QString& body, QString* error,
					const QString& name = QStringLiteral("main.lua"))
{
	const QString path = dir + QLatin1Char('/') + name;
	writeFile(path, (header() + body).toUtf8());
	return lmms::ScriptEngine::instance()->runFile(path, error);
}

} // namespace

class ScriptImportTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase() { lmms::Engine::init(true); }
	void cleanupTestCase() { lmms::Engine::destroy(); }

	void init()
	{
		using namespace lmms;
		Engine::getSong()->clearProject();
		ScriptEngine::instance()->takeLogMessages();
		ScriptEngine::instance()->setInstructionBudget(5000000);
		ScriptEngine::instance()->setAutoApply(true);
		ScriptConsole::setEnabled(true);
	}

	//! Assert a script run fails and the error contains \a needle.
	void expectError(const QString& dir, const QString& body, const QString& needle)
	{
		QString error;
		const auto result = runEntry(dir, body, &error);
		QCOMPARE(int(result), int(lmms::ScriptEngine::RunResult::ScriptError));
		QVERIFY2(error.contains(needle), qPrintable(error));
	}

	void siblingModuleLoads()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QVERIFY(writeFile(dir.path() + "/util.lua", "local M = {} function M.twice(x) return x*2 end return M"));
		QVERIFY(writeFile(dir.path() + "/lib/deep.lua", "return {name = ...}"));
		QString error;
		const auto result = runEntry(dir.path(), QStringLiteral(
			"local u = require('util')\n"
			"assert(u.twice(21) == 42)\n"
			"local d = require('lib.deep')\n"
			"assert(d.name == 'lib.deep', tostring(d.name))\n"), &error);
		QVERIFY2(result == lmms::ScriptEngine::RunResult::Ok, qPrintable(error));
	}

	void packageEntryImportsFromItsDirectory()
	{
		QTemporaryDir dir;
		QVERIFY(dir.isValid());
		QVERIFY(writeFile(dir.path() + "/arp/notes.lua", "return {root = 60}"));
		QString error;
		const auto result = runEntry(dir.path() + "/arp",
			QStringLiteral("assert(require('notes').root == 60)\n"), &error,
			QStringLiteral("package.lua"));
		QVERIFY2(result == lmms::ScriptEngine::RunResult::Ok, qPrintable(error));
		// A package cannot reach its sibling package.
		QVERIFY(writeFile(dir.path() + "/other/secret.lua", "return 1"));
		expectError(dir.path() + "/arp", "require('other.secret')", "not found");
	}

	void modulesAreCachedPerRunAndNotAcrossRuns()
	{
		QTemporaryDir dir;
		QVERIFY(writeFile(dir.path() + "/counter.lua", "return {}"));
		QString error;
		auto result = runEntry(dir.path(), QStringLiteral(
			"assert(require('counter') == require('counter'), 'not cached')\n"), &error);
		QVERIFY2(result == lmms::ScriptEngine::RunResult::Ok, qPrintable(error));
		// A module returning nothing caches `true`, like Lua's require.
		QVERIFY(writeFile(dir.path() + "/quiet.lua", "x_loaded = (x_loaded or 0) + 1"));
		result = runEntry(dir.path(), QStringLiteral(
			"assert(require('quiet') == true)\nrequire('quiet')\nassert(x_loaded == 1, 'ran twice')\n"),
			&error);
		QVERIFY2(result == lmms::ScriptEngine::RunResult::Ok, qPrintable(error));
		// A new run starts from a fresh state: the module runs again.
		result = runEntry(dir.path(), QStringLiteral("require('quiet') assert(x_loaded == 1)\n"), &error);
		QVERIFY2(result == lmms::ScriptEngine::RunResult::Ok, qPrintable(error));
	}

	void traversalAndBadNamesAreRefused()
	{
		QTemporaryDir dir;
		QTemporaryDir outside;
		QVERIFY(writeFile(outside.path() + "/evil.lua", "return 1"));
		QVERIFY(writeFile(dir.path() + "/ok.lua", "return 1"));
		const QStringList bad = {
			"'../evil'", "'..'", "'a..b'", "'sub/ok'", "'sub\\\\ok'", "'/etc/passwd'",
			"'\\\\\\\\server\\\\share\\\\x'", "'C:\\\\x'", "'C:/x'", "''", "'a b'", "'.ok'",
			"'ok.'", "'ok\\0'", "'1ok'",
		};
		for (const QString& name : bad)
		{
			QString error;
			const auto result = runEntry(dir.path(), QStringLiteral("require(%1)").arg(name), &error);
			QVERIFY2(result == lmms::ScriptEngine::RunResult::ScriptError, qPrintable(name));
			QVERIFY2(error.contains("require '") || error.contains("attempt"), qPrintable(name + ": " + error));
			QVERIFY2(!error.contains("module changed"), qPrintable(name));
		}
		// The specific reasons, for the shapes an author is likely to try.
		expectError(dir.path(), "require('../evil')", "'..' is not allowed");
		expectError(dir.path(), "require('/etc/passwd')", "absolute paths");
		expectError(dir.path(), "require('C:/x')", "absolute paths");
		expectError(dir.path(), "require('\\\\\\\\srv\\\\share')", "absolute paths");
		expectError(dir.path(), "require('sub/ok')", "path separators");
		expectError(dir.path(), "require('ok\\0')", "NUL");
		expectError(dir.path(), "require(42.5)", "identifiers joined");
	}

	void parseModuleNameIsPure()
	{
		QStringList parts;
		QString error;
		QVERIFY(lmms::ScriptImport::parseModuleName("lib.util_2", &parts, &error));
		QCOMPARE(parts, (QStringList{"lib", "util_2"}));
		QVERIFY(!lmms::ScriptImport::parseModuleName(QByteArray("a\0b", 3), &parts, &error));
		QVERIFY(!lmms::ScriptImport::parseModuleName(QByteArray(200, 'a'), &parts, &error));
		QVERIFY(!lmms::ScriptImport::parseModuleName("..", &parts, &error));
	}

	void symlinkEscapesAreRefused()
	{
#ifdef _WIN32
		QSKIP("symlink creation needs privileges on Windows");
#else
		QTemporaryDir dir;
		QTemporaryDir outside;
		QVERIFY(writeFile(outside.path() + "/stolen.lua", "return 'stolen'"));
		QVERIFY(writeFile(outside.path() + "/pkg/inner.lua", "return 'inner'"));
		// A file symlink inside the root pointing at a file outside it.
		QVERIFY(QFile::link(outside.path() + "/stolen.lua", dir.path() + "/link.lua"));
		// A directory symlink inside the root pointing at a directory outside it.
		QVERIFY(QFile::link(outside.path() + "/pkg", dir.path() + "/dirlink"));
		expectError(dir.path(), "require('link')", "outside the import root");
		expectError(dir.path(), "require('dirlink.inner')", "outside the import root");
		// A symlink that stays inside the root is fine.
		QVERIFY(writeFile(dir.path() + "/real.lua", "return 'real'"));
		QVERIFY(QFile::link(dir.path() + "/real.lua", dir.path() + "/alias.lua"));
		QString error;
		const auto result = runEntry(dir.path(), "assert(require('alias') == 'real')", &error);
		QVERIFY2(result == lmms::ScriptEngine::RunResult::Ok, qPrintable(error));
#endif
	}

	void readModuleRefusesAFileSwappedForAnEscape()
	{
#ifdef _WIN32
		QSKIP("symlink creation needs privileges on Windows");
#else
		// The window between resolve() and the open: swap the checked file for
		// a symlink to an outside file. readModule() must refuse the swap.
		QTemporaryDir dir;
		QTemporaryDir outside;
		QVERIFY(writeFile(outside.path() + "/stolen.lua", "return 'stolen'"));
		QVERIFY(writeFile(dir.path() + "/m.lua", "return 'm'"));
		const QString root = QFileInfo(dir.path()).canonicalFilePath();
		QString canonical;
		QString error;
		QVERIFY(lmms::ScriptImport::resolve(root, {"m"}, &canonical, &error));
		QVERIFY(QFile::remove(canonical));
		QVERIFY(QFile::link(outside.path() + "/stolen.lua", canonical));
		QByteArray source;
		QVERIFY(!lmms::ScriptImport::readModule(root, canonical, &source, &error));
		QVERIFY2(error.contains("changed"), qPrintable(error));
		QVERIFY(source.isEmpty());
#endif
	}

	void cyclesAreRefusedWithAClearError()
	{
		QTemporaryDir dir;
		QVERIFY(writeFile(dir.path() + "/a.lua", "require('b')"));
		QVERIFY(writeFile(dir.path() + "/b.lua", "require('a')"));
		expectError(dir.path(), "require('a')", "import cycle: a -> b -> a");
		QVERIFY(writeFile(dir.path() + "/self.lua", "require('self')"));
		expectError(dir.path(), "require('self')", "import cycle: self -> self");
	}

	void depthAndSizeAreBounded()
	{
		QTemporaryDir dir;
		const int limit = lmms::ScriptImport::MaxDepth;
		for (int i = 0; i < limit + 2; ++i)
		{
			QVERIFY(writeFile(dir.path() + QString("/m%1.lua").arg(i),
				QString("require('m%1')").arg(i + 1).toUtf8()));
		}
		QVERIFY(writeFile(dir.path() + QString("/m%1.lua").arg(limit + 2), "return 1"));
		expectError(dir.path(), "require('m0')", "nested deeper");

		const QByteArray big = "return '" + QByteArray(lmms::ScriptImport::MaxModuleBytes, 'x') + "'";
		QVERIFY(writeFile(dir.path() + "/big.lua", big));
		expectError(dir.path(), "require('big')", "too large");
		QVERIFY(writeFile(dir.path() + "/fits.lua", "return 'x'"));
		QString error;
		QVERIFY2(runEntry(dir.path(), "assert(require('fits') == 'x')", &error)
			== lmms::ScriptEngine::RunResult::Ok, qPrintable(error));
	}

	void sandboxStaysClosedInsideModules()
	{
		QTemporaryDir dir;
		QVERIFY(writeFile(dir.path() + "/probe.lua",
			"for _, n in ipairs({'os','io','package','dofile','loadfile','load','loadstring','module'}) do\n"
			"  assert(_G[n] == nil, n .. ' leaked into a module')\n"
			"end\n"
			"assert(string ~= nil and table ~= nil)\n"
			"return true"));
		QString error;
		const auto result = runEntry(dir.path(), QStringLiteral(
			"assert(os == nil and io == nil and package == nil and dofile == nil and loadfile == nil and load == nil)\n"
			"assert(require('probe') == true)\n"
			"assert(os == nil and io == nil and package == nil and dofile == nil and loadfile == nil)\n"), &error);
		QVERIFY2(result == lmms::ScriptEngine::RunResult::Ok, qPrintable(error));
		// require itself cannot be pointed at a file by a module: it takes names only.
		expectError(dir.path(), "require('probe') require('probe.lua')", "not found");
	}

	void bytecodeModulesAreRefused()
	{
		QTemporaryDir dir;
		QVERIFY(writeFile(dir.path() + "/bin.lua", QByteArray("\x1bLua\x54\x00garbage", 11)));
		expectError(dir.path(), "require('bin')", "binary chunk");
	}

	void moduleErrorsPropagateAndAreNotCached()
	{
		QTemporaryDir dir;
		QVERIFY(writeFile(dir.path() + "/boom.lua", "error('kaboom')"));
		expectError(dir.path(), "require('boom')", "kaboom");
		QVERIFY(writeFile(dir.path() + "/syn.lua", "this is not lua"));
		expectError(dir.path(), "require('syn')", "syn.lua");
		expectError(dir.path(), "require('missing')", "module 'missing' not found");
	}

	void runStringHasNoRequire()
	{
		QString error;
		const auto result = lmms::ScriptEngine::instance()->runString(
			QStringLiteral("assert(require == nil)"), &error);
		QVERIFY2(result == lmms::ScriptEngine::RunResult::Ok, qPrintable(error));
	}
};

QTEST_GUILESS_MAIN(ScriptImportTest)
#include "ScriptImportTest.moc"
