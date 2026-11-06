/*
 * UpdateCheckWindowsTest.cpp - Help > Check for Updates, the first-run question, the notice and Settings, driven by hand
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

/*! Q050-501, over the real main window, with the feed on loopback. Every path here is a click or a trigger: the question
 *  is asked once, closing it says no and sends nothing, Yes saves and checks; Help > Check for Updates asks first when
 *  off, and otherwise shows a non-modal notice with the version, the notes and a button opening the page; an automatic
 *  check announces a release once; Settings changes the saved choice through the command, one undo step. */

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QDesktopServices>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QTextBrowser>

#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "SetupDialog.h"
#include "UpdateCheck.h"
#include "UpdateCheckWindows.h"
#include "../core/UpdateFeedFixture.h"

using namespace lmms;
using namespace lmms::gui;
using namespace lmms::updatecheck;
using updatetest::FeedServer;
using updatetest::release;

namespace
{

QDialog* openWindow(const char* name)
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		if (widget->objectName() == QLatin1String(name) && widget->isVisible()) { return qobject_cast<QDialog*>(widget); }
	}
	return nullptr;
}

QAction* checkForUpdatesAction()
{
	for (QAction* top : getGUI()->mainWindow()->menuBar()->actions())
	{
		if (top->menu() == nullptr) { continue; }
		for (QAction* action : top->menu()->actions())
		{
			if (action->property("controlCommand").toString() == QLatin1String("window.update_check")) { return action; }
		}
	}
	return nullptr;
}

void closeAll()
{
	for (const char* name : {"updateConsentDialog", "updateNoticeDialog"})
	{
		// Without a rejected() from the question: closing it is an answer, tested on its own.
		if (QDialog* window = openWindow(name)) { window->blockSignals(true); window->close(); delete window; }
	}
}

} // namespace

//! Records the page the button opens, instead of a browser.
class Desktop : public QObject
{
	Q_OBJECT
public:
	QString seen;
public slots:
	void take(const QUrl& url) { seen = url.toString(); }
};

class UpdateCheckWindowsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		qunsetenv("FLATPAK_ID");
		qunsetenv("SNAP");
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		ControlRegistry::setReady(true);
		UpdateChecker::instance()->setCurrentVersion(QStringLiteral("1.2.0"));
		getGUI()->mainWindow()->show();
		QTRY_VERIFY_WITH_TIMEOUT(checkForUpdatesAction() != nullptr, 20000);  // the shell fills the menus just after start
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		closeAll();
		QTRY_VERIFY_WITH_TIMEOUT(!UpdateChecker::instance()->inProgress(), 20000);  // the last test's request is counted against it
		ConfigManager* config = ConfigManager::inst();
		for (const char* attribute : {"asked", "noticed", "lastcheck"}) { config->setValue(QStringLiteral("updatecheck"), QLatin1String(attribute), QString()); }
		config->setValue(QStringLiteral("updatecheck"), QStringLiteral("feed"), m_feed.url());
		savePreferences({false, false, false});
		m_feed.requests.clear();
		m_feed.answer(release("v1.3.0"));
	}

	void theMenuEntryRunsTheCommand()
	{
		QAction* action = checkForUpdatesAction();
		QVERIFY(action != nullptr);
		QVERIFY(action->isEnabled());
	}

	void theQuestionIsAskedOnceAndClosingItIsNo()
	{
		runUpdateCheckStartup(getGUI()->mainWindow());
		QDialog* question = openWindow("updateConsentDialog");
		QVERIFY(question != nullptr);
		QVERIFY(!question->isModal());
		question->close();
		QVERIFY(openWindow("updateConsentDialog") == nullptr);
		QVERIFY(preferences().answered && !preferences().enabled);
		runUpdateCheckStartup(getGUI()->mainWindow());
		QVERIFY(openWindow("updateConsentDialog") == nullptr);
		QTest::qWait(300);
		QCOMPARE(m_feed.requests.size(), 0);
	}

	void anUnansweredQuestionIsNotAskedTwice()
	{
		runUpdateCheckStartup(getGUI()->mainWindow());
		QVERIFY(openWindow("updateConsentDialog") != nullptr);
		closeAll();  // the program was quit with the question open: no answer, but it has been asked
		QVERIFY(!preferences().answered);
		runUpdateCheckStartup(getGUI()->mainWindow());
		QVERIFY(openWindow("updateConsentDialog") == nullptr);
		QTest::qWait(300);
		QCOMPARE(m_feed.requests.size(), 0);
	}

	void yesSavesAndChecksAndTheNoticeOpensThePage()
	{
		runUpdateCheckStartup(getGUI()->mainWindow());
		QDialog* question = openWindow("updateConsentDialog");
		QVERIFY(question != nullptr);
		QTRY_COMPARE(m_feed.requests.size(), 0);  // nothing before the answer
		question->findChild<QPushButton*>(QStringLiteral("updateConsentYes"))->click();
		QVERIFY(preferences().enabled && preferences().answered && !preferences().prereleases);
		QTRY_VERIFY_WITH_TIMEOUT(openWindow("updateNoticeDialog") != nullptr, 20000);
		QDialog* notice = openWindow("updateNoticeDialog");
		QVERIFY(!notice->isModal());
		QVERIFY(QApplication::activeModalWidget() == nullptr);
		const QString heading = notice->findChild<QLabel*>(QStringLiteral("updateNoticeHeading"))->text();
		QVERIFY2(heading.contains(QStringLiteral("1.3.0")) && heading.contains(QStringLiteral("1.2.0")), qPrintable(heading));
		QCOMPARE(notice->findChild<QTextBrowser*>(QStringLiteral("updateNoticeNotes"))->toPlainText(), QStringLiteral("Notes of v1.3.0"));

		Desktop desktop;
		QDesktopServices::setUrlHandler(QStringLiteral("https"), &desktop, "take");
		auto* download = notice->findChild<QPushButton*>(QStringLiteral("updateNoticeDownload"));
		QVERIFY(download->isVisible());
		download->click();
		QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
		QCOMPARE(desktop.seen, QStringLiteral("https://example.invalid/page"));
	}

	void aPageNothingCanOpenIsSaidOutLoud()
	{
		savePreferences({true, true, false});
		checkForUpdatesAction()->trigger();
		QTRY_VERIFY_WITH_TIMEOUT(!UpdateChecker::instance()->inProgress(), 20000);
		const ControlResult opened = ControlRegistry::instance()->invoke(QStringLiteral("window.update_download_page"));
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		if (!opened.result.value(QStringLiteral("opened")).toBool())
		{
			QVERIFY(getGUI()->mainWindow()->statusBar()->currentMessage().contains(QStringLiteral("example.invalid")));
		}
	}

	void checkingByHandWhileOffAsksFirst()
	{
		checkForUpdatesAction()->trigger();
		QDialog* question = openWindow("updateConsentDialog");
		QVERIFY(question != nullptr);
		QTest::qWait(300);
		QCOMPARE(m_feed.requests.size(), 0);
		question->findChild<QPushButton*>(QStringLiteral("updateConsentNo"))->click();
		QVERIFY(preferences().answered && !preferences().enabled);
		QTest::qWait(300);
		QCOMPARE(m_feed.requests.size(), 0);
		QVERIFY(openWindow("updateNoticeDialog") == nullptr);
	}

	void checkingByHandSaysUpToDateAndWhyItFailed()
	{
		savePreferences({true, true, false});
		m_feed.answer(release("v1.2.0"));
		checkForUpdatesAction()->trigger();
		QDialog* notice = openWindow("updateNoticeDialog");
		QVERIFY(notice != nullptr);
		auto* heading = notice->findChild<QLabel*>(QStringLiteral("updateNoticeHeading"));
		QTRY_VERIFY_WITH_TIMEOUT(heading->text().contains(QStringLiteral("up to date")), 20000);
		QVERIFY(!notice->findChild<QPushButton*>(QStringLiteral("updateNoticeDownload"))->isVisible());

		m_feed.answer("{}", 429);
		checkForUpdatesAction()->trigger();
		QTRY_VERIFY_WITH_TIMEOUT(heading->text().contains(QStringLiteral("429")), 20000);
	}

	void aSecondPreReleaseChoiceTakesEffect()
	{
		m_feed.answer(release("v1.4.0-rc.1", true));
		savePreferences({true, true, true});
		checkForUpdatesAction()->trigger();
		QDialog* notice = openWindow("updateNoticeDialog");
		auto* heading = notice->findChild<QLabel*>(QStringLiteral("updateNoticeHeading"));
		QTRY_VERIFY_WITH_TIMEOUT(heading->text().contains(QStringLiteral("1.4.0-rc.1")), 20000);
		savePreferences({true, true, false});
		checkForUpdatesAction()->trigger();
		QTRY_VERIFY_WITH_TIMEOUT(heading->text().contains(QStringLiteral("up to date")), 20000);
	}

	void anAutomaticCheckAnnouncesAReleaseOnce()
	{
		savePreferences({true, true, false});
		runUpdateCheckStartup(getGUI()->mainWindow());
		QTRY_VERIFY_WITH_TIMEOUT(openWindow("updateNoticeDialog") != nullptr, 20000);
		closeAll();
		ConfigManager::inst()->setValue(QStringLiteral("updatecheck"), QStringLiteral("lastcheck"), QString());  // a day later
		runUpdateCheckStartup(getGUI()->mainWindow());
		QTRY_COMPARE_WITH_TIMEOUT(m_feed.requests.size(), 2, 20000);
		QTRY_VERIFY_WITH_TIMEOUT(!UpdateChecker::instance()->inProgress(), 20000);
		QVERIFY(openWindow("updateNoticeDialog") == nullptr);
		// A failed automatic check shows nothing at all.
		ConfigManager::inst()->setValue(QStringLiteral("updatecheck"), QStringLiteral("lastcheck"), QString());
		m_feed.answer("{}", 500);
		runUpdateCheckStartup(getGUI()->mainWindow());
		QTRY_COMPARE_WITH_TIMEOUT(m_feed.requests.size(), 3, 20000);
		QTRY_VERIFY_WITH_TIMEOUT(!UpdateChecker::instance()->inProgress(), 20000);
		QVERIFY(openWindow("updateNoticeDialog") == nullptr);
	}

	void settingsSaveThroughTheCommandOneUndoStep()
	{
		// The earlier tests' answers are the same command moments ago, which the registry would group with this one.
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.set_undo_coalescing"), {{QStringLiteral("window_ms"), 0}}).ok);
		const int depth = ControlRegistry::instance()->invoke(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		{
			SetupDialog untouched;
			static_cast<QDialog&>(untouched).accept();
			QVERIFY2(!preferences().answered, "saving Settings with the box untouched answered the question");
		}
		SetupDialog settings;
		auto* enabled = settings.findChild<QCheckBox*>(QStringLiteral("updateCheckEnabled"));
		auto* prereleases = settings.findChild<QCheckBox*>(QStringLiteral("updateCheckPrereleases"));
		QVERIFY(enabled != nullptr && prereleases != nullptr);
		QVERIFY(!enabled->isChecked());
		enabled->click();
		prereleases->click();
		static_cast<QDialog&>(settings).accept();
		QVERIFY(preferences().enabled && preferences().answered && preferences().prereleases);
		const ControlResult after = ControlRegistry::instance()->invoke(QStringLiteral("control.undo_depth"));
		QCOMPARE(after.result.value(QStringLiteral("depth")).toInt(), depth + 1);
		const ControlResult undone = ControlRegistry::instance()->invoke(QStringLiteral("control.undo"));
		QVERIFY2(undone.ok, qPrintable(undone.errorMessage));
		QVERIFY(!preferences().enabled && !preferences().answered && !preferences().prereleases);
		QTest::qWait(300);
		QCOMPARE(m_feed.requests.size(), 0);  // choosing is not checking
	}

private:
	QTemporaryDir m_home;
	FeedServer m_feed;
};

QTEST_MAIN(UpdateCheckWindowsTest)
#include "UpdateCheckWindowsTest.moc"
