/*
 * ControlAutomationScriptTest.cpp - unit tests for the automation.* and
 *                                   script.* command groups
 *                                   (SPEC-zene-studio.md A11-A16).
 *
 * The end-to-end behaviour (rendered audio, control.undo, the wire) lives in
 * tests/control-socket-integration.py; this file pins the registry surface those
 * flows rely on: the ids, the schemas, the typed refusal each error path must
 * produce, and the fact that script.run drives the embedded ScriptEngine rather
 * than a child process.
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

// Complete fixture method definitions; included once after the fixture declaration.

void ControlAutomationScriptTest::scriptListHashesScriptsLargerThanControlDataLimit() {
	constexpr qint64 largeScriptSize = 64LL * 1024 * 1024 + 1;
	QTemporaryDir temporaryDir;
	QVERIFY(temporaryDir.isValid());
	const QString scriptsDir = QDir(temporaryDir.path()).filePath(QStringLiteral("scripts"));
	QVERIFY(QDir().mkpath(scriptsDir));
	const QString scriptPath = QDir(scriptsDir).filePath(QStringLiteral("large.lua"));
	QFile scriptFile(scriptPath);
	QVERIFY(scriptFile.open(QIODevice::WriteOnly));
	QVERIFY(scriptFile.resize(largeScriptSize));
	scriptFile.close();

	QCryptographicHash expectedHash(QCryptographicHash::Sha256);
	const QByteArray zeroes(64 * 1024, '\0');
	for (qint64 remaining = largeScriptSize; remaining > 0;)
	{
		const qsizetype chunkSize = static_cast<qsizetype>(qMin<qint64>(zeroes.size(), remaining));
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
		expectedHash.addData(QByteArrayView(zeroes.constData(), chunkSize));
#else
		// Qt 5 (hosted CI) has no QByteArrayView; its (const char*, int) overload hashes the same bytes.
		expectedHash.addData(zeroes.constData(), static_cast<int>(chunkSize));
#endif
		remaining -= chunkSize;
	}

	const QStringList originalDataSearchPaths = QDir::searchPaths(QStringLiteral("data"));
	QDir::setSearchPaths(QStringLiteral("data"), {temporaryDir.path()});
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("script.list"));
	QDir::setSearchPaths(QStringLiteral("data"), originalDataSearchPaths);

	QVERIFY2(result.ok, qPrintable(result.errorMessage));
	bool foundLargeScript = false;
	for (const QJsonValue& value : result.result.value(QStringLiteral("scripts")).toArray())
	{
		const QJsonObject entry = value.toObject();
		if (entry.value(QStringLiteral("name")).toString() != QStringLiteral("large.lua")) { continue; }
		foundLargeScript = true;
		QCOMPARE(entry.value(QStringLiteral("bytes")).toDouble(), static_cast<double>(largeScriptSize));
		QCOMPARE(entry.value(QStringLiteral("sha256")).toString(),
			QString::fromLatin1(expectedHash.result().toHex()));
	}
	QVERIFY(foundLargeScript);
}
void ControlAutomationScriptTest::scriptListHashesTheChunkBoundaries() {
	QTemporaryDir temporaryDir;
	QVERIFY(temporaryDir.isValid());
	const QString scriptsDir = QDir(temporaryDir.path()).filePath(QStringLiteral("scripts"));
	QVERIFY(QDir().mkpath(scriptsDir));

	const QList<QPair<QString, qint64>> cases = {
		{qstr("empty.lua"), 0},
		{qstr("one-chunk.lua"), 64 * 1024},
		{qstr("one-chunk-plus-one.lua"), 64 * 1024 + 1},
	};
	for (const QPair<QString, qint64>& entry : cases)
	{
		QFile file(QDir(scriptsDir).filePath(entry.first));
		QVERIFY(file.open(QIODevice::WriteOnly));
		if (entry.second > 0) { QVERIFY(file.resize(entry.second)); }
		file.close();
	}

	const QStringList originalDataSearchPaths = QDir::searchPaths(QStringLiteral("data"));
	QDir::setSearchPaths(QStringLiteral("data"), {temporaryDir.path()});
	const ControlResult result = ControlRegistry::instance()->invoke(QStringLiteral("script.list"));
	QDir::setSearchPaths(QStringLiteral("data"), originalDataSearchPaths);

	QVERIFY2(result.ok, qPrintable(result.errorMessage));
	const QJsonArray scripts = result.result.value(QStringLiteral("scripts")).toArray();
	QVERIFY(scripts.size() == cases.size());

	// SHA-256 of 0, 64 KiB and 64 KiB+1 zero bytes (sha256sum, not the product hasher).
	const QHash<qint64, QString> expected = {
		{0, qstr("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855")},
		{64 * 1024, qstr("de2f256064a0af797747c2b97505dc0b9f3df0de4f489eac731c23ae9ca9cc31")},
		{64 * 1024 + 1, qstr("3266304f31be278d06c3bd3eb9aa3e00c59bedec0a890de466568b0b90b0e01f")},
	};
	for (const QJsonValue& value : scripts)
	{
		const QJsonObject entry = value.toObject();
		const qint64 bytes = static_cast<qint64>(entry.value(QStringLiteral("bytes")).toDouble());
		QVERIFY2(expected.contains(bytes),
			qPrintable(QStringLiteral("an unexpected %1-byte script was listed").arg(bytes)));
		QCOMPARE(entry.value(QStringLiteral("sha256")).toString(), expected.value(bytes));
	}
}
void ControlAutomationScriptTest::scriptRunsInProcessAndReturnsItsLog() {
	ControlRegistry* registry = ControlRegistry::instance();
	const QString path = QStringLiteral(LUA_SCRIPT_DIR) + QStringLiteral("/hello.lua");
	QVERIFY2(QFileInfo::exists(path), qPrintable(path));

	const ControlResult result = registry->invoke(QStringLiteral("script.run"),
		QJsonObject{{QStringLiteral("path"), path}});
	QVERIFY2(result.ok, qPrintable(result.errorMessage));
	QCOMPARE(result.result.value(QStringLiteral("ran")).toBool(), true);
	QCOMPARE(result.result.value(QStringLiteral("path")).toString(), path);

	const QJsonArray log = result.result.value(QStringLiteral("log")).toArray();
	QVERIFY2(log.size() >= 3, qPrintable(QStringLiteral("log has %1 line(s)").arg(log.size())));
	QCOMPARE(result.result.value(QStringLiteral("log_lines")).toInt(), static_cast<int>(log.size()));
	QString joined;
	for (const QJsonValue& line : log) { joined += line.toString() + QLatin1Char('\n'); }
	QVERIFY2(joined.contains(QStringLiteral("Hello from Lua")), qPrintable(joined));
	QVERIFY2(joined.contains(QStringLiteral("hello.lua finished")), qPrintable(joined));

	// The engine ran the script on its dedicated worker, not on this thread
	// and not in a child process.
	ScriptEngine* engine = ScriptEngine::instance();
	QVERIFY(engine->workerThread() != nullptr);
	QVERIFY(engine->workerThread() != QThread::currentThread());
	QVERIFY(!engine->lastRunFailed());
}
void ControlAutomationScriptTest::scriptRefusesAMissingFileAndTheBudgetExhaustion() {
	ControlRegistry* registry = ControlRegistry::instance();

	const ControlResult missing = registry->invoke(QStringLiteral("script.run"),
		QJsonObject{{QStringLiteral("path"), QStringLiteral("/nonexistent/no-such.lua")}});
	QVERIFY(!missing.ok);
	QCOMPARE(missing.errorKind, ControlErrorKind::NotFound);

	const ControlResult both = registry->invoke(QStringLiteral("script.run"),
		QJsonObject{{QStringLiteral("path"), QStringLiteral("a.lua")},
			{QStringLiteral("source"), QStringLiteral("return 1")}});
	QVERIFY(!both.ok);
	QCOMPARE(both.errorKind, ControlErrorKind::InvalidArgs);

	const ControlResult neither = registry->invoke(QStringLiteral("script.run"));
	QVERIFY(!neither.ok);
	QCOMPARE(neither.errorKind, ControlErrorKind::InvalidArgs);

	// A runaway script is stopped by the engine's own instruction hook, and
	// the refusal names the budget rather than swallowing it.
	const ControlResult runaway = registry->invoke(QStringLiteral("script.run"),
		QJsonObject{{QStringLiteral("source"),
				QStringLiteral("lmms.log():info('runaway')\nwhile true do end\n")},
			{QStringLiteral("budget"), 20000}});
	QVERIFY2(!runaway.ok, "the runaway script was not stopped");
	QCOMPARE(runaway.errorKind, ControlErrorKind::Refused);
	QVERIFY2(runaway.errorMessage.contains(QStringLiteral("instruction budget exceeded")),
		qPrintable(runaway.errorMessage));
	// The log line the script did write travels with the refusal.
	QVERIFY2(runaway.errorMessage.contains(QStringLiteral("runaway")),
		qPrintable(runaway.errorMessage));
}
void ControlAutomationScriptTest::scriptRunRecordsAnHonestTransaction() {
	ControlRegistry* registry = ControlRegistry::instance();
	const ControlResult result = registry->invoke(QStringLiteral("script.run"),
		QJsonObject{{QStringLiteral("source"), QStringLiteral("lmms.log():info('tx')\n")}});
	QVERIFY2(result.ok, qPrintable(result.errorMessage));
	QCOMPARE(result.result.value(QStringLiteral("instruction_budget")).toInt(),
		static_cast<int>(ScriptEngine::instance()->instructionBudget()));

	bool found = false;
	for (const QJsonValue& value : registry->transactions())
	{
		const QJsonObject transaction = value.toObject();
		if (transaction.value(QStringLiteral("command")).toString() != QStringLiteral("script.run"))
		{
			continue;
		}
		found = true;
		QCOMPARE(transaction.value(QStringLiteral("reversible")).toBool(), false);
		QVERIFY(transaction.value(QStringLiteral("mechanism")).toString()
			.contains(QStringLiteral("script")));
	}
	QVERIFY2(found, "script.run recorded no transaction");
}
