/*! Help > Online Help must not fail silently (cluster new-p11-58).
 *
 *  The menu entry declares app.online_help, and that command declares `requires: human`,
 *  so the registry refuses it for EVERY caller (ControlRegistry::checkRequires returns
 *  "requires a human" unconditionally) - the action therefore has to reach the shared
 *  implementation itself, exactly as the Telemetry action above it does (MainWindow.cpp,
 *  the openTelemetryConsentScreen comment).  Once it does, the desktop can refuse the URL
 *  (a system with no URL handler), and the command's own `opened: false` is DISCARDED by
 *  the menu dispatch - nothing opened and the user was told nothing.
 *
 *  What is held here: the real action reaches the implementation and, when the desktop
 *  takes the URL, the window says so in the status bar; when the desktop DOES take it, the
 *  window says nothing (no nagging on the path that works).
 */

#include <QtTest>

#include <QAction>
#include <QDesktopServices>
#include <QMenu>
#include <QMenuBar>
#include <QStatusBar>
#include <QTemporaryDir>
#include <QUrl>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! The desktop that takes every https URL: QDesktopServices asks the handler registered for a
//! scheme BEFORE the platform, and answers true when the slot says it took it - which is how
//! the test drives the path where a browser really opens.
class DesktopThatOpens : public QObject
{
	Q_OBJECT

public:
	QString seen;

public slots:
	bool takeTheUrl(QUrl url)
	{
		seen = url.toString();
		return true;
	}
};

} // namespace

class OnlineHelpNoticeTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		guitest::startGui(m_dir);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Nothing opens under the offscreen platform (QPlatformIntegration::openUrl), which
	//! is the finding's case: the click has to leave something the user can read.
	void nothingOpeningIsSaidOutLoud()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->show();
		window->statusBar()->clearMessage();
		QVERIFY(!QDesktopServices::openUrl(QUrl(QStringLiteral("https://example.invalid/"))));

		onlineHelpAction()->trigger();

		const QString message = window->statusBar()->currentMessage();
		QVERIFY2(message.contains(QStringLiteral("https://")), qPrintable(message));
	}

	//! The path that works stays quiet: the desktop takes the URL and nothing is claimed.
	void anOpenedUrlIsNotReportedAsAFailure()
	{
		MainWindow* window = getGUI()->mainWindow();
		window->show();
		window->statusBar()->clearMessage();
		DesktopThatOpens desktop;
		QDesktopServices::setUrlHandler(QStringLiteral("https"), &desktop, "takeTheUrl");

		onlineHelpAction()->trigger();

		QDesktopServices::unsetUrlHandler(QStringLiteral("https"));
		QVERIFY2(desktop.seen.contains(QStringLiteral("zene-studio")), qPrintable(desktop.seen));
		QVERIFY2(window->statusBar()->currentMessage().isEmpty(),
			qPrintable(window->statusBar()->currentMessage()));
	}

private:
	//! Help > Online Help itself - found through the command it declares, not by its label.
	QAction* onlineHelpAction() const
	{
		const QList<QAction*> menus = getGUI()->mainWindow()->menuBar()->actions();
		for (QAction* menuAction : menus)
		{
			QMenu* menu = menuAction->menu();
			if (menu == nullptr) { continue; }
			const QList<QAction*> entries = menu->actions();
			for (QAction* entry : entries)
			{
				if (entry->property("controlCommand").toString() == QLatin1String("app.online_help"))
				{
					return entry;
				}
			}
		}
		return nullptr;
	}

	QTemporaryDir m_dir;
};

QTEST_MAIN(OnlineHelpNoticeTest)
#include "OnlineHelpNoticeTest.moc"