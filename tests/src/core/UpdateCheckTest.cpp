/*
 * UpdateCheckTest.cpp - the update check against a loopback feed: what it asks, believes and never does
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

/*! Q050-501. Nothing here leaves 127.0.0.1. Held: SemVer precedence; the off and unanswered states make no request at all;
 *  newer / equal / older / pre-release / malformed / rate-limited / slow / oversized answers; one quiet log line for a
 *  failure and none otherwise; check() returns before the feed answers; once a day; exactly what a request says; the
 *  commands and their persistence; a sandboxed copy has no commands. With -DZENE_UPDATE_CHECK=OFF only the absence remains. */

#include <QtTest>

#include "ControlRegistry.h"
#include "lmmsconfig.h"

using namespace lmms;

namespace
{
const char* const CommandIds[] = {"app.update_check_get", "app.update_check_set", "app.update_check_run",
	"window.update_check", "window.update_download_page"};
}

#ifdef ZENE_UPDATE_CHECK_ENABLED

#include <QJsonObject>
#include <QTemporaryDir>

#include "ConfigManager.h"
#include "UpdateCheck.h"
#include "UpdateFeedFixture.h"
#include "lmmsversion.h"

using namespace lmms::updatecheck;
using updatetest::FeedServer;
using updatetest::release;

namespace
{

QStringList g_quietLines;
QtMessageHandler g_previousHandler = nullptr;

void captureLog(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
	if (message.startsWith(QStringLiteral("Update check:"))) { g_quietLines.append(message); }
	else if (g_previousHandler != nullptr) { g_previousHandler(type, context, message); }
}

ControlResult run(const char* id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(QLatin1String(id), args);
}

} // namespace

class UpdateCheckTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		qputenv("HOME", m_home.path().toLocal8Bit());
		qunsetenv("FLATPAK_ID");
		qunsetenv("SNAP");
		m_config = m_home.filePath(QStringLiteral("zene.xml"));
		ConfigManager::inst()->loadConfigFile(m_config);
		g_previousHandler = qInstallMessageHandler(captureLog);
		UpdateChecker::instance()->setCurrentVersion(QStringLiteral("1.2.0"));
	}

	void cleanupTestCase()
	{
		qInstallMessageHandler(g_previousHandler);
	}

	void init()
	{
		g_quietLines.clear();
		ConfigManager::inst()->setValue(QStringLiteral("updatecheck"), QStringLiteral("feed"), m_feed.url());
		ConfigManager::inst()->setValue(QStringLiteral("updatecheck"), QStringLiteral("lastcheck"), QString());
		savePreferences({true, true, false});
		UpdateChecker::instance()->setTimeoutMs(UpdateChecker::DefaultTimeoutMs);
		m_feed.requests.clear();
		m_feed.silent = false;
	}

	void versionsCompareBySemVerPrecedence()
	{
		// SemVer 2.0 section 11's own chain, and what a lexical comparison gets wrong.
		const QStringList ascending{"1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta", "1.0.0-beta.2",
			"1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1", "1.9.0", "1.10.0", "2.0.0", "10.0.0"};
		for (int i = 0; i + 1 < ascending.size(); ++i)
		{
			QVERIFY2(compareVersions(ascending[i], ascending[i + 1]) < 0, qPrintable(ascending[i]));
			QVERIFY2(compareVersions(ascending[i + 1], ascending[i]) > 0, qPrintable(ascending[i]));
		}
		QVERIFY(compareVersions(QStringLiteral("10000000000.0.0"), QStringLiteral("9999999999.0.0")) > 0);
		QVERIFY(compareVersions(QStringLiteral("1.0.0-10000000000"), QStringLiteral("1.0.0-9999999999")) > 0);
		QCOMPARE(compareVersions(QStringLiteral("v1.2.3"), QStringLiteral("1.2.3")), 0);
		QCOMPARE(compareVersions(QStringLiteral("1.2.3+build5"), QStringLiteral("1.2.3")), 0);
		for (const char* good : {"1.2.3", "v1.2.3", "0.5.0-test1", "1.2.3-rc.1+g42"}) { QVERIFY2(isValidVersion(QLatin1String(good)), good); }
		for (const char* bad : {"", "1.2", "1.2.3.4", "latest", "01.2.3", "1.2.3-", "1.2.x", " 1.2.3", "1.2.3-01"}) { QVERIFY2(!isValidVersion(QLatin1String(bad)), bad); }
		QVERIFY(isValidVersion(QString::fromUtf8(LMMS_VERSION)));
	}

	void theFeedAnswerDecidesTheOutcome_data()
	{
		QTest::addColumn<QByteArray>("feed");
		QTest::addColumn<bool>("prereleases");
		QTest::addColumn<int>("state");
		QTest::addColumn<QString>("version");
		const auto Newer = static_cast<int>(Outcome::State::Newer);
		const auto Same = static_cast<int>(Outcome::State::UpToDate);
		const auto Failed = static_cast<int>(Outcome::State::Failed);
		QTest::newRow("newer") << release("v1.10.0") << false << Newer << "1.10.0";
		QTest::newRow("equal") << release("v1.2.0") << false << Same << "";
		QTest::newRow("older") << release("v1.1.9") << false << Same << "";
		QTest::newRow("a list: the highest, not the first") << "[" + release("v1.3.0") + "," + release("v1.10.0") + "," + release("v1.9.0") + "]" << false << Newer << "1.10.0";
		QTest::newRow("pre-release flag, not opted in") << release("v1.3.0", true) << false << Same << "";
		QTest::newRow("pre-release version, not opted in") << release("v1.3.0-rc.1") << false << Same << "";
		QTest::newRow("pre-release, opted in") << release("v1.3.0-rc.1", true) << true << Newer << "1.3.0-rc.1";
		QTest::newRow("stable beside a higher pre-release") << "[" + release("v1.4.0-beta", true) + "," + release("v1.3.0") + "]" << false << Newer << "1.3.0";
		QTest::newRow("no releases yet") << QByteArray("[]") << false << Same << "";
		QTest::newRow("a draft does not count") << QByteArray("{\"tag_name\":\"v9.0.0\",\"html_url\":\"https://example.invalid/\",\"draft\":true}") << false << Same << "";
		QTest::newRow("a page that is not https does not count") << release("v9.0.0", false, "file:///etc/passwd") << false << Same << "";
		QTest::newRow("a tag that is no version does not count") << release("nightly") << false << Same << "";
		QTest::newRow("malformed: not JSON") << QByteArray("<html>oops") << false << Failed << "";
		QTest::newRow("malformed: another object") << QByteArray("{\"message\":\"Not Found\"}") << false << Failed << "";
		QTest::newRow("malformed: a list of numbers") << QByteArray("[1,2]") << false << Failed << "";
		QTest::newRow("malformed: empty body") << QByteArray() << false << Failed << "";
	}

	void theFeedAnswerDecidesTheOutcome()
	{
		QFETCH(QByteArray, feed);
		QFETCH(bool, prereleases);
		QFETCH(int, state);
		QFETCH(QString, version);
		savePreferences({true, true, prereleases});
		m_feed.answer(feed);
		const Outcome outcome = checkAndWait();
		QCOMPARE(static_cast<int>(outcome.state), state);
		QCOMPARE(outcome.release.version, version);
		QCOMPARE(m_feed.requests.size(), 1);
		QCOMPARE(g_quietLines.size(), state == static_cast<int>(Outcome::State::Failed) ? 1 : 0);
		if (!version.isEmpty())
		{
			QCOMPARE(outcome.release.url, QStringLiteral("https://example.invalid/page"));
			QCOMPARE(outcome.release.notes, QStringLiteral("Notes of v") + version);
		}
	}

	void aRefusalOrAnErrorIsOneQuietLine_data()
	{
		QTest::addColumn<int>("status");
		QTest::newRow("rate-limited 403") << 403;
		QTest::newRow("rate-limited 429") << 429;
		QTest::newRow("not found") << 404;
		QTest::newRow("server error") << 500;
	}

	void aRefusalOrAnErrorIsOneQuietLine()
	{
		QFETCH(int, status);
		m_feed.answer(release("v9.0.0"), status);  // a body that would be believed if the status were not read
		const Outcome outcome = checkAndWait();
		QCOMPARE(outcome.state, Outcome::State::Failed);
		QVERIFY2(outcome.reason.contains(QString::number(status)), qPrintable(outcome.reason));
		QCOMPARE(g_quietLines.size(), 1);
	}

	void aSlowFeedBlocksNothingAndIsGivenUpOn()
	{
		m_feed.silent = true;
		UpdateChecker::instance()->setTimeoutMs(300);
		bool heard = false;
		Outcome outcome;
		UpdateChecker::instance()->setListener([&](const Outcome& o) { heard = true; outcome = o; });
		// check() returning with the request still open is the proof it does not wait: the server has not answered and never will.
		QCOMPARE(UpdateChecker::instance()->check(false), UpdateChecker::Start::Started);
		QVERIFY(UpdateChecker::instance()->inProgress());
		QVERIFY(!heard);
		QCOMPARE(UpdateChecker::instance()->check(false), UpdateChecker::Start::AlreadyRunning);
		QTRY_VERIFY_WITH_TIMEOUT(heard, 20000);
		UpdateChecker::instance()->setListener({});
		QCOMPARE(outcome.state, Outcome::State::Failed);
		QVERIFY2(outcome.reason.contains(QStringLiteral("no answer")), qPrintable(outcome.reason));
		QVERIFY(!UpdateChecker::instance()->inProgress());
		QCOMPARE(g_quietLines.size(), 1);
	}

	void nobodyListeningOnThePortIsOneQuietLine()
	{
		QTcpServer closed;
		QVERIFY(closed.listen(QHostAddress::LocalHost, 0));
		const quint16 port = closed.serverPort();
		closed.close();
		ConfigManager::inst()->setValue(QStringLiteral("updatecheck"), QStringLiteral("feed"), QStringLiteral("http://127.0.0.1:%1/").arg(port));
		QCOMPARE(checkAndWait().state, Outcome::State::Failed);
		QCOMPARE(g_quietLines.size(), 1);
	}

	void anOversizedAnswerIsAbandoned()
	{
		m_feed.answer(QByteArray(UpdateChecker::MaxBodyBytes + 1024, ' '));
		const Outcome outcome = checkAndWait();
		QCOMPARE(outcome.state, Outcome::State::Failed);
		QVERIFY2(outcome.reason.contains(QStringLiteral("too large")), qPrintable(outcome.reason));
	}

	void offAndUnansweredMakeNoRequest()
	{
		m_feed.answer(release("v9.0.0"));
		const Preferences off[] = {{false, false, false}, {false, true, false}};
		for (const Preferences& preferences : off)
		{
			savePreferences(preferences);
			QCOMPARE(UpdateChecker::instance()->check(true), UpdateChecker::Start::Off);
			QVERIFY(!UpdateChecker::instance()->checkIfDue());
			const ControlResult refused = run("app.update_check_run");
			QVERIFY(!refused.ok);
			QCOMPARE(refused.errorKind, ControlErrorKind::Refused);
			QVERIFY(!UpdateChecker::instance()->inProgress());
		}
		// A hand-edited "enabled" with no answer on record is not consent.
		savePreferences({false, false, false});
		ConfigManager::inst()->setValue(QStringLiteral("updatecheck"), QStringLiteral("enabled"), QStringLiteral("1"));
		QVERIFY(!preferences().enabled);
		QCOMPARE(UpdateChecker::instance()->check(true), UpdateChecker::Start::Off);
		QTest::qWait(300);  // long enough for a loopback request to have arrived, had one been made
		QCOMPARE(m_feed.requests.size(), 0);
		QVERIFY(lastCheck().isNull());
	}

	void anAutomaticCheckRunsAtMostOnceADay()
	{
		m_feed.answer(release("v1.2.0"));
		const QDateTime now = QDateTime::currentDateTimeUtc();
		bool heard = false;
		UpdateChecker::instance()->setListener([&](const Outcome&) { heard = true; });
		QVERIFY(UpdateChecker::instance()->checkIfDue(now));
		QTRY_VERIFY_WITH_TIMEOUT(heard, 20000);
		heard = false;
		QCOMPARE(m_feed.requests.size(), 1);
		QVERIFY(!UpdateChecker::instance()->checkIfDue(now.addSecs(1)));
		QVERIFY(!UpdateChecker::instance()->checkIfDue(now.addSecs(UpdateChecker::CheckIntervalSeconds - 60)));
		QCOMPARE(m_feed.requests.size(), 1);
		// The stamp is saved: a restart does not reset the day.
		ConfigManager::inst()->saveConfigFile();
		ConfigManager::inst()->loadConfigFile(m_config);
		QVERIFY(!UpdateChecker::instance()->checkIfDue(now.addSecs(60)));
		QVERIFY(UpdateChecker::instance()->checkIfDue(now.addSecs(UpdateChecker::CheckIntervalSeconds + 60)));
		QTRY_VERIFY_WITH_TIMEOUT(heard, 20000);
		QCOMPARE(m_feed.requests.size(), 2);
		// A clock put back does not silence checking for good.
		heard = false;
		QVERIFY(UpdateChecker::instance()->checkIfDue(now.addDays(-30)));
		QTRY_VERIFY_WITH_TIMEOUT(heard, 20000);
		UpdateChecker::instance()->setListener({});
	}

	void aRequestSaysOnlyTheVersionAndPlatform()
	{
		m_feed.answer(release("v1.2.0"));
		checkAndWait();
		QCOMPARE(m_feed.requests.size(), 1);
		const QList<QByteArray> lines = m_feed.requests.first().split('\n');
		QCOMPARE(lines.first().trimmed(), QByteArray("GET /releases HTTP/1.1"));
		const QByteArray agent = "ZeneStudio/" + QByteArray(LMMS_VERSION) + " (" + QSysInfo::kernelType().toUtf8() + "; "
			+ QSysInfo::currentCpuArchitecture().toUtf8() + ")";
		// Every header there is, by name; anything Qt or a later change adds fails here until it is looked at.
		const QSet<QByteArray> allowed{"host", "user-agent", "accept", "accept-language", "accept-encoding", "connection"};
		for (int i = 1; i < lines.size(); ++i)
		{
			const auto colon = lines[i].indexOf(':');
			const QByteArray name = lines[i].left(colon).trimmed().toLower();
			const QByteArray value = lines[i].mid(colon + 1).trimmed();
			QVERIFY2(allowed.contains(name), name.constData());
			if (name == "user-agent") { QCOMPARE(value, agent); }
			if (name == "accept-language") { QCOMPARE(value, QByteArray("en")); }  // not the system's locale
		}
		QVERIFY(m_feed.requests.first().contains("User-Agent: " + agent));
	}

	void onlyHttpsOrLoopbackIsEverAsked()
	{
		QVERIFY(isAllowedUrl(QUrl(QStringLiteral("https://api.github.com/x"))));
		QVERIFY(isAllowedUrl(QUrl(QStringLiteral("http://127.0.0.1:9/x"))));
		QVERIFY(!isAllowedUrl(QUrl(QStringLiteral("http://example.invalid/x"))));
		QVERIFY(!isAllowedUrl(QUrl(QStringLiteral("file:///etc/passwd"))));
		QVERIFY(!isAllowedUrl(QUrl(QStringLiteral("ftp://example.invalid/x"))));
		ConfigManager::inst()->setValue(QStringLiteral("updatecheck"), QStringLiteral("feed"), QStringLiteral("http://example.invalid/x"));
		const Outcome outcome = checkAndWait();
		QCOMPARE(outcome.state, Outcome::State::Failed);
		QCOMPARE(g_quietLines.size(), 1);
		ConfigManager::inst()->deleteValue(QStringLiteral("updatecheck"), QStringLiteral("feed"));
		QCOMPARE(feedUrl(), QStringLiteral("https://api.github.com/repos/KRUZZZZY/zene-studio/releases?per_page=10"));
	}

	void theCommandsReadAndSaveThePreference()
	{
		savePreferences({false, false, false});
		QJsonObject state = run("app.update_check_get").result;
		QCOMPARE(state.value(QStringLiteral("enabled")).toBool(true), false);
		QCOMPARE(state.value(QStringLiteral("answered")).toBool(true), false);
		QCOMPARE(state.value(QStringLiteral("user_agent")).toString(), QString::fromUtf8(userAgent()));
		QVERIFY(!run("app.update_check_set").ok);  // nothing to set
		QVERIFY(!preferences().answered);
		const ControlResult set = run("app.update_check_set", {{QStringLiteral("enabled"), true}, {QStringLiteral("prereleases"), true}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		// Survives a reload of the file, as it survives a restart.
		ConfigManager::inst()->loadConfigFile(m_config);
		QVERIFY(preferences().enabled && preferences().answered && preferences().prereleases);
		state = run("app.update_check_get").result;
		QVERIFY(state.value(QStringLiteral("enabled")).toBool() && state.value(QStringLiteral("prereleases")).toBool());
		// One setting left out stays as it was.
		QVERIFY(run("app.update_check_set", {{QStringLiteral("prereleases"), false}}).ok);
		QVERIFY(preferences().enabled && !preferences().prereleases);
		// Saying no is an answer.
		QVERIFY(run("app.update_check_set", {{QStringLiteral("enabled"), false}}).ok);
		QVERIFY(!preferences().enabled && preferences().answered);
	}

	void theResultIsReadableThroughTheCommand()
	{
		m_feed.answer(release("v1.3.0"));
		bool heard = false;
		UpdateChecker::instance()->setListener([&](const Outcome&) { heard = true; });
		const ControlResult started = run("app.update_check_run");
		QVERIFY2(started.ok, qPrintable(started.errorMessage));
		QVERIFY(started.result.value(QStringLiteral("started")).toBool());
		QTRY_VERIFY_WITH_TIMEOUT(heard, 20000);
		UpdateChecker::instance()->setListener({});
		const QJsonObject state = run("app.update_check_get").result;
		const QJsonObject result = state.value(QStringLiteral("result")).toObject();
		QCOMPARE(result.value(QStringLiteral("state")).toString(), QStringLiteral("newer"));
		QCOMPARE(result.value(QStringLiteral("version")).toString(), QStringLiteral("1.3.0"));
		QCOMPARE(result.value(QStringLiteral("url")).toString(), QStringLiteral("https://example.invalid/page"));
		QVERIFY(!state.value(QStringLiteral("last_check")).toString().isEmpty());
	}

	void aSandboxedCopyHasNoFeature_data()
	{
		QTest::addColumn<QByteArray>("variable");
		QTest::addColumn<QString>("manager");
		QTest::newRow("Flatpak") << QByteArray("FLATPAK_ID") << QStringLiteral("flatpak");
		QTest::newRow("Snap") << QByteArray("SNAP") << QStringLiteral("snap");
	}

	void aSandboxedCopyHasNoFeature()
	{
		QFETCH(QByteArray, variable);
		QFETCH(QString, manager);
		m_feed.answer(release("v9.0.0"));
		QCOMPARE(managedBy(), QString());
		qputenv(variable.constData(), "1");
		QCOMPARE(managedBy(), manager);
		QCOMPARE(UpdateChecker::instance()->check(true), UpdateChecker::Start::Off);
		QTest::qWait(200);
		qunsetenv(variable.constData());
		QCOMPARE(m_feed.requests.size(), 0);
		// Started outside a sandbox, so they are here; UpdateCheckHiddenTest and the socket transcript start inside one.
		for (const char* id : CommandIds) { QVERIFY2(ControlRegistry::instance()->hasCommand(QLatin1String(id)), id); }
	}

private:
	Outcome checkAndWait()
	{
		bool heard = false;
		Outcome outcome;
		UpdateChecker::instance()->setListener([&](const Outcome& o) { heard = true; outcome = o; });
		if (UpdateChecker::instance()->check(false) != UpdateChecker::Start::Off) { QTest::qWaitFor([&] { return heard; }, 30000); }
		UpdateChecker::instance()->setListener({});
		return outcome;
	}

	QTemporaryDir m_home;
	QString m_config;
	FeedServer m_feed;
};

#else // !ZENE_UPDATE_CHECK_ENABLED

//! -DZENE_UPDATE_CHECK=OFF: no client exists, and the registry says so.
class UpdateCheckTest : public QObject
{
	Q_OBJECT

private slots:
	void noCommandExists()
	{
		for (const char* id : CommandIds) { QVERIFY2(!ControlRegistry::instance()->hasCommand(QLatin1String(id)), id); }
	}
};

#endif // ZENE_UPDATE_CHECK_ENABLED

QTEST_MAIN(UpdateCheckTest)
#include "UpdateCheckTest.moc"
