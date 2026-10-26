/*
 * HelpAccessTest.cpp - the manual and command reference, hover help, guided tours, appearance, the library search view
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
/*! 0.5.0 L14 / L11: the command reference is the registry and filters as asked (WF-136); a control
 *  that runs a command shows that command's description as its help (WF-137); the tours are found
 *  and one steps through a window (WF-140); app.help and app.tour open the help centre and a tour
 *  over the REAL main window (GuiApplication: app.tour has no window to show a tour in without
 *  it); window.appearance stores the scale where main() reads it and one undo puts it back
 *  (WF-131); window.pin keeps a side panel open across views (MX-007); the library search view
 *  runs a query and lists its columns (WF-003 / WF-011). Design theme 21: the reference is grouped by
 *  the manual's own sections under their own titles and finds by task; the manual has a find field; and
 *  F1's in-context help (app.help 'context') names the focused control, its command by its friendly name
 *  and its manual section, needing no hover. */

#include <QtTest>

#include <QFile>
#include <QFrame>
#include <QJsonArray>
#include <QRegularExpression>
#include <QScrollArea>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QToolButton>
#include <QTreeWidget>

#include "Appearance.h"
#include "CommandMetadata.h"
#include "ConfigManager.h"
#include "BrowserSearchView.h"
#include "ContextHelp.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "HelpCenter.h"
#include "HelpTasks.h"
#include "MainWindow.h"
#include "Shell.h"

using namespace lmms;
using namespace lmms::gui;

class HelpAccessTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		qputenv("ZENE_UI_SCALE_FILE", m_dir.filePath(QStringLiteral("scale")).toLocal8Bit());
		guitest::startGui(m_dir);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theReferenceIsTheRegistry()
	{
		HelpCenter* help = HelpCenter::showHelp(QString(), true);
		QVERIFY(help != nullptr);
		QVERIFY(help->referenceText().contains(QStringLiteral("app.help")));
		help->filterReference(QStringLiteral("collection_save"));
		QVERIFY(help->referenceText().contains(QStringLiteral("browser.collection_save")));
		QVERIFY(!help->referenceText().contains(QStringLiteral("transport.play")));
		help->close();
	}

	//! Theme 21: the tasks are the manual's sections, under the manual's own headings, and the reference is
	//! grouped and found by them - "mixing" finds the mixer's commands, each by its friendly name.
	void theReferenceIsGroupedByTheManualsTasks()
	{
		QFile manual(ConfigManager::inst()->dataDir() + QStringLiteral("manual/index.html"));
		QVERIFY(manual.open(QIODevice::ReadOnly));
		const QString html = QString::fromUtf8(manual.readAll());
		for (const QString& topic : help::topics())
		{
			const QString heading = QStringLiteral("<h2 id=\"%1\">%2</h2>").arg(topic, help::topicTitle(topic));
			QVERIFY2(html.contains(heading), qPrintable(heading));
		}
		// ... and the other way: every top-level manual section is a task app.help can open,
		// in the manual's order (Q050-159 adds the patterns / clips / cells section).
		QStringList manualTopics;
		for (int at = html.indexOf(QStringLiteral("<h2 id=\"")); at >= 0;
			at = html.indexOf(QStringLiteral("<h2 id=\""), at + 1))
		{
			const int first = at + 8;  // just past <h2 id="
			manualTopics << html.mid(first, html.indexOf(QLatin1Char('"'), first) - first);
			at = first;
		}
		QCOMPARE(manualTopics, help::topics());
		HelpCenter* centre = HelpCenter::showHelp(QString(), true);
		centre->filterReference(QStringLiteral("mixing"));
		const QString text = centre->referenceText();
		QVERIFY(text.contains(help::topicTitle(QStringLiteral("mix"))));
		QVERIFY(text.contains(QStringLiteral("mixer.set_volume")));
		QVERIFY(text.contains(commandName(QStringLiteral("mixer.set_volume"))));
		QVERIFY(!text.contains(QStringLiteral("transport.play")));
		centre->findCommand(QStringLiteral("collection_save"));  // app.help's 'query'
		QVERIFY(centre->referenceText().contains(QStringLiteral("browser.collection_save")));
		QVERIFY(centre->findInManual(QStringLiteral("Mixing")));
		QVERIFY(!centre->findInManual(QStringLiteral("zzqqxxnothing")));
		centre->close();
	}

	//! Q050-099: the limitations page no longer says the waveform takes no keys or that an
	//! MPE lane edits only the cursor note, and the manual describes the keyboard route.
	void theWaveformAndMpeKeyboardLimitsAreCurrent()
	{
		QFile limitations(QStringLiteral("../../docs/KNOWN-LIMITATIONS.md"));
		QVERIFY(limitations.open(QIODevice::ReadOnly));
		const QString text = QString::fromUtf8(limitations.readAll());
		QVERIFY(!text.contains(QStringLiteral("The waveform itself takes no keys")));
		QVERIFY(!text.contains(QStringLiteral("An MPE lane key edits only the cursor note")));
		QVERIFY(text.contains(QStringLiteral("waveform takes keyboard focus")));
		QVERIFY(text.contains(QStringLiteral("MPE lane key edits every selected note")));
		QFile manual(ConfigManager::inst()->dataDir() + QStringLiteral("manual/index.html"));
		QVERIFY(manual.open(QIODevice::ReadOnly));
		const QString html = QString::fromUtf8(manual.readAll());
		QVERIFY(html.contains(QStringLiteral("Sample Editor: Tab to the waveform")));
		QVERIFY(html.contains(QStringLiteral("one key changes every selected note")));
	}

	//! Q050-325: the stale blanket "no interface" claims for Groove, MIDI Clock and
	//! freeze/bounce are gone, and the residual limits name both the GUI route and the command.
	void theLimitationsPageKeepsOnlyTrueInterfaceClaims()
	{
		QFile limitations(QStringLiteral("../../docs/KNOWN-LIMITATIONS.md"));
		QVERIFY(limitations.open(QIODevice::ReadOnly));
		const QString text = QString::fromUtf8(limitations.readAll());
		for (const QString& stale : {QStringLiteral("The groove pool and quantise have no interface"),
			QStringLiteral("Freeze and bounce-in-place have no interface"),
			QStringLiteral("MIDI clock is the engine and the socket, and there is no interface for it")})
		{
			QVERIFY2(!text.contains(stale), qPrintable(stale));
		}
		for (const QString& current : {QStringLiteral("The groove pool's rename, remove and humanise controls remain socket-only"),
			QStringLiteral("MIDI clock is in Edit ▸ MIDI Clock"), QStringLiteral("Clock Source"),
			QStringLiteral("Unfreeze track and Bounce in place"), QStringLiteral("freeze.region")})
		{
			QVERIFY2(text.contains(current), qPrintable(current));
		}
		QVERIFY(QFile::exists(QStringLiteral("../../docs/qwen-fixes-2026-10/Q050-325-audit.md")));
	}

	//! Q050-324: README, CONTRIBUTING and the manual's Access topic point at the same
	//! accessibility report form, and the form asks for the workflow, assistive technology and
	//! reproduction steps without requiring personal medical information.
	void theAccessibilityFeedbackRouteIsTheSameEverywhere()
	{
		const QString route = QStringLiteral("https://github.com/KRUZZZZY/zene-studio/issues/new?template=accessibility-report.yml");
		for (const QString& path : {QStringLiteral("../../README.md"), QStringLiteral("../../CONTRIBUTING.md"),
			ConfigManager::inst()->dataDir() + QStringLiteral("manual/index.html")})
		{
			QFile file(path);
			QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(path));
			QVERIFY2(QString::fromUtf8(file.readAll()).contains(route), qPrintable(path));
		}
		QFile form(QStringLiteral("../../.github/ISSUE_TEMPLATE/accessibility-report.yml"));
		QVERIFY(form.open(QIODevice::ReadOnly));
		const QString yaml = QString::fromUtf8(form.readAll());
		for (const QString& id : {QStringLiteral("id: workflow"), QStringLiteral("id: assistive_technology"),
			QStringLiteral("id: steps")})
		{
			QVERIFY2(yaml.contains(id), qPrintable(id));
		}
		QVERIFY(yaml.count(QStringLiteral("required: true")) >= 3);
		QVERIFY2(!yaml.contains(QStringLiteral("id: medical")) && !yaml.contains(QStringLiteral("id: diagnosis")),
			"the form asked for medical information");
		HelpCenter* centre = HelpCenter::showHelp(QStringLiteral("access"), false);
		QVERIFY(centre != nullptr);
		QVERIFY(centre->findInManual(QStringLiteral("accessibility blocker form")));
		centre->close();
	}

	//! Q050-323: the manual has a searchable task-indexed quick start, each task links to its
	//! area reference and to registered commands and installed tours, and Find reaches them.
	void theQuickStartIsSearchableAndReachesEveryArea()
	{
		QFile manual(ConfigManager::inst()->dataDir() + QStringLiteral("manual/index.html"));
		QVERIFY(manual.open(QIODevice::ReadOnly));
		const QString html = QString::fromUtf8(manual.readAll());
		QVERIFY(html.contains(QStringLiteral("<h2 id=\"quickstart\">Quick start</h2>")));
		QVERIFY(html.contains(QStringLiteral("<a href=\"#quickstart\">Quick start</a>")));
		const QList<QPair<QString, QString>> tasks{
			{QStringLiteral("task-first-beat"), QStringLiteral("Make a first beat")},
			{QStringLiteral("task-arrange-clips"), QStringLiteral("Arrange clips")},
			{QStringLiteral("task-edit-notes"), QStringLiteral("Edit notes")},
			{QStringLiteral("task-instrument-effect"), QStringLiteral("Add an instrument or effect")},
			{QStringLiteral("task-record-comp"), QStringLiteral("Record and comp takes")},
			{QStringLiteral("task-mix-export"), QStringLiteral("Mix, export and hand off")}};
		HelpCenter* centre = HelpCenter::showHelp(QString(), false);
		QVERIFY(centre != nullptr);
		for (const auto& [id, title] : tasks)
		{
			QVERIFY2(html.contains(QStringLiteral("<h3 id=\"%1\">%2</h3>").arg(id, title)), qPrintable(title));
			QVERIFY2(centre->findInManual(title), qPrintable(title));
		}
		for (const QString& command : {QStringLiteral("track.add"), QStringLiteral("app.tour"), QStringLiteral("note.edit"),
			QStringLiteral("plugin.load"), QStringLiteral("track.set_arm"), QStringLiteral("render.render")})
		{
			QVERIFY2(html.contains(command), qPrintable(command));
			QVERIFY2(ControlRegistry::instance()->hasCommand(command), qPrintable(command));
		}
		QVERIFY(html.contains(QStringLiteral("first-beat")));
		QVERIFY(html.contains(QStringLiteral("recording")));
		QVERIFY(html.contains(QStringLiteral("mixing")));
		// The area references remain the manual's own headings (HelpTasks owns the list).
		for (const QString& topic : help::topics())
		{
			QVERIFY2(html.contains(QStringLiteral("<a href=\"#%1\">").arg(topic)) || topic == QLatin1String("quickstart"),
				qPrintable(topic));
		}
		centre->close();
	}

	//! Q050-159: one manual section and one tour step compare patterns, clips and launcher
	//! cells with an example that traces a track to its mixer channel. (The train's own Q050-159
	//! section, "workflows", is the one section; this batch's duplicate was folded into it.)
	void theThreeArrangementMethodsAndTheirRoutingAreExplained()
	{
		QFile manual(ConfigManager::inst()->dataDir() + QStringLiteral("manual/index.html"));
		QVERIFY(manual.open(QIODevice::ReadOnly));
		const QString html = QString::fromUtf8(manual.readAll());
		QVERIFY(html.contains(QStringLiteral("<h2 id=\"workflows\">Patterns, clips and cells</h2>")));
		for (const QString& term : {QStringLiteral("PatternStore pattern"), QStringLiteral("PatternTrack"),
			QStringLiteral("clip"), QStringLiteral("launcher cell"), QStringLiteral("kick.wav"),
			QStringLiteral("track.overview"), QStringLiteral("mixer.get_state"), QStringLiteral("track.set_mixer_channel"),
			QStringLiteral("Inspector"), QStringLiteral("Routing")})
		{
			QVERIFY2(html.contains(term), qPrintable(term));
		}
		QFile tour(ConfigManager::inst()->dataDir() + QStringLiteral("tours/first-beat.json"));
		QVERIFY(tour.open(QIODevice::ReadOnly));
		const QString json = QString::fromUtf8(tour.readAll());
		QVERIFY(json.contains(QStringLiteral("Patterns, clips and cells")));
		QVERIFY(json.contains(QStringLiteral("PatternTrack placeholder")));
		QVERIFY(json.contains(QStringLiteral("PatternStore")));
	}

	//! Theme 21: in-context help for the focused control, no hovering - its name, its command by name, its
	//! section - from F1's command, over the help centre.
	void theFocusedControlHasHelpWithoutHovering()
	{
		QToolButton button;
		button.setText(QStringLiteral("&Record options"));
		button.setProperty("controlCommand", QStringLiteral("transport.record_options"));
		const QJsonObject context = help::contextHelp(&button);
		QCOMPARE(context.value(QStringLiteral("name")).toString(), QStringLiteral("Record options"));
		QCOMPARE(context.value(QStringLiteral("command_name")).toString(), commandName(QStringLiteral("transport.record_options")));
		QCOMPARE(context.value(QStringLiteral("topic")).toString(), help::commandTopic(QStringLiteral("transport.record_options")));
		QVERIFY(context.value(QStringLiteral("command_help")).toString().startsWith(QStringLiteral("How a recording pass starts")));
		button.setProperty("helpTopic", QStringLiteral("record"));
		QCOMPARE(help::widgetTopic(&button), QStringLiteral("record"));  // a declared topic wins

		ControlRegistry* registry = ControlRegistry::instance();
		const bool headless = registry->isHeadless();
		registry->setHeadless(false);
		const ControlResult opened = registry->invoke(QStringLiteral("app.help"), {{QStringLiteral("context"), true}});
		registry->setHeadless(headless);
		QVERIFY2(opened.ok, qPrintable(opened.errorMessage));
		QVERIFY(opened.result.value(QStringLiteral("context")).toObject().contains(QStringLiteral("topic")));
		HelpCenter* centre = getGUI()->mainWindow()->findChild<HelpCenter*>();
		QVERIFY(centre != nullptr && !centre->contextText().isEmpty());
		centre->close();
	}

	void aControlShowsItsCommandsHelp()
	{
		QToolButton button;
		button.setProperty("controlCommand", QStringLiteral("transport.record_options"));
		QVERIFY(ContextHelp::helpFor(&button, false).startsWith(QStringLiteral("How a recording pass starts")));
		QVERIFY(ContextHelp::helpFor(&button, true).contains(QStringLiteral("transport.record_options")));
		QToolButton plain;
		QVERIFY(ContextHelp::helpFor(&plain, false).isEmpty());
	}

	void aTourStepsThroughTheWindow()
	{
		const QJsonArray tours = GuidedTour::tours();
		QVERIFY2(tours.size() >= 3, "the shipped tours (data/tours) were not found");
		QWidget window;
		window.resize(800, 600);
		auto* target = new QWidget(&window);
		target->setObjectName(QStringLiteral("shellPanel_browser"));
		target->setGeometry(10, 10, 100, 100);
		window.show();
		QString error;
		GuidedTour* tour = GuidedTour::start(QStringLiteral("first-beat"), &window, &error);
		QVERIFY2(tour != nullptr, qPrintable(error));
		QVERIFY(tour->stepCount() >= 3);
		tour->showStep(1);
		QCOMPARE(tour->step(), 1);
		QVERIFY(GuidedTour::start(QStringLiteral("no-such-tour"), &window, &error) == nullptr);
	}

	void aMalformedTourIsNotListedOrStarted()
	{
		// The tours folder holds one usable tour and two that would show nothing: one whose step has no
		// title or text, and one that is not JSON. The temporary tree is put in front of the shipped tours
		// for this test only; the search path is put back before anything else runs.
		QTemporaryDir root;
		QVERIFY(root.isValid());
		QVERIFY(QDir().mkpath(root.filePath(QStringLiteral("tours"))));
		const auto write = [&](const QString& file, const QByteArray& body) {
			QFile out(root.filePath(QStringLiteral("tours/") + file));
			QVERIFY(out.open(QIODevice::WriteOnly));
			QCOMPARE(out.write(body), body.size());
		};
		write(QStringLiteral("good.json"), R"({"title":"Good","steps":[{"title":"Step","text":"Some text"}]})");
		write(QStringLiteral("blank.json"), R"({"title":"Blank","steps":[{}]})");
		write(QStringLiteral("broken.json"), "{ not json");

		QWidget window;
		window.resize(800, 600);
		const QStringList saved = QDir::searchPaths(QStringLiteral("data"));
		QDir::setSearchPaths(QStringLiteral("data"), {root.path()});
		QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("blank\\.json")));
		QTest::ignoreMessage(QtWarningMsg, QRegularExpression(QStringLiteral("broken\\.json")));
		const QJsonArray listed = GuidedTour::tours();
		QString blankError;
		QString brokenError;
		QString error;
		GuidedTour* blank = GuidedTour::start(QStringLiteral("blank"), &window, &blankError);
		GuidedTour* broken = GuidedTour::start(QStringLiteral("broken"), &window, &brokenError);
		GuidedTour* good = GuidedTour::start(QStringLiteral("good"), &window, &error);
		QDir::setSearchPaths(QStringLiteral("data"), saved);

		QCOMPARE(listed.size(), 1);
		QCOMPARE(listed.at(0).toObject().value(QStringLiteral("name")).toString(), QStringLiteral("good"));
		QVERIFY(blank == nullptr);
		QVERIFY2(blankError.contains(QStringLiteral("blank")), qPrintable(blankError));
		QVERIFY(broken == nullptr);
		QVERIFY2(brokenError.contains(QStringLiteral("broken")), qPrintable(brokenError));
		QVERIFY2(good != nullptr, qPrintable(error));
		QCOMPARE(good->stepCount(), 1);
		delete good;
	}

	void aTourTracksTargetMovementAndScrollingWithoutResizingWindow()
	{
		QWidget window;
		window.resize(800, 600);
		auto* scrollArea = new QScrollArea(&window);
		scrollArea->setGeometry(10, 10, 300, 200);
		auto* content = new QWidget;
		content->resize(360, 800);
		auto* target = new QWidget(content);
		target->setObjectName(QStringLiteral("shellPanel_browser"));
		target->setGeometry(20, 20, 100, 40);
		scrollArea->setWidget(content);
		window.show();
		QString error;
		GuidedTour* tour = GuidedTour::start(QStringLiteral("first-beat"), &window, &error);
		QVERIFY2(tour != nullptr, qPrintable(error));
		auto* bubble = tour->findChild<QFrame*>(QStringLiteral("guidedTourStep"));
		QVERIFY(bubble != nullptr);
		const auto expectedBubblePosition = [&] {
			const QPoint targetPosition = target->mapTo(&window, QPoint());
			return QPoint(targetPosition.x() + target->width() - 1 + 12, targetPosition.y());
		};
		QCOMPARE(bubble->pos(), expectedBubblePosition());

		target->move(40, 100);
		QCOMPARE(window.size(), QSize(800, 600));
		QCOMPARE(bubble->pos(), expectedBubblePosition());

		const int targetYBeforeScroll = target->mapTo(&window, QPoint()).y();
		QVERIFY(scrollArea->verticalScrollBar()->maximum() > 0);
		scrollArea->verticalScrollBar()->setValue(60);
		QVERIFY(target->mapTo(&window, QPoint()).y() < targetYBeforeScroll);
		QCOMPARE(bubble->pos(), expectedBubblePosition());
		QCOMPARE(window.size(), QSize(800, 600));
		delete tour;
	}

	void theHelpAndATourOpenFromTheirCommands()
	{
		MainWindow* shellWindow = getGUI()->mainWindow();
		shellWindow->show();  // a tour step toggles its panel when the target is not on screen
		ControlRegistry* registry = ControlRegistry::instance();
		const bool headless = registry->isHeadless();
		registry->setHeadless(false);  // the offscreen platform is a display (both declare requires: display)
		const ControlResult help = registry->invoke(QStringLiteral("app.help"), {{QStringLiteral("reference"), true}});
		const ControlResult tour = registry->invoke(QStringLiteral("app.tour"), {{QStringLiteral("name"), QStringLiteral("first-beat")}});
		const ControlResult missing = registry->invoke(QStringLiteral("app.tour"), {{QStringLiteral("name"), QStringLiteral("no-such-tour")}});
		registry->setHeadless(headless);
		QVERIFY2(help.ok, qPrintable(help.errorMessage));
		QVERIFY(help.result.value(QStringLiteral("open")).toBool());
		HelpCenter* centre = shellWindow->findChild<HelpCenter*>();
		QVERIFY(centre != nullptr && centre->isVisible());
		centre->close();
		QVERIFY2(tour.ok, qPrintable(tour.errorMessage));
		QCOMPARE(tour.result.value(QStringLiteral("name")).toString(), QStringLiteral("first-beat"));
		auto* shown = shellWindow->findChild<GuidedTour*>(QStringLiteral("guidedTour"));
		QVERIFY(shown != nullptr && shown->isVisible());
		QCOMPARE(tour.result.value(QStringLiteral("steps")).toInt(), shown->stepCount());
		QCOMPARE(shown->step(), 0);
		delete shown;
		QCOMPARE(missing.errorKind, ControlErrorKind::NotFound);
	}

	void appearanceIsStoredWhereMainReadsIt()
	{
		const ControlResult set = ControlRegistry::instance()->invoke(QStringLiteral("window.appearance"),
			{{QStringLiteral("scale"), 1.5}, {QStringLiteral("palette"), QStringLiteral("high_contrast")}});
		QVERIFY2(set.ok, qPrintable(set.errorMessage));
		QCOMPARE(appearance::storedScale(), 1.5);
		QCOMPARE(appearance::options().palette, QStringLiteral("high_contrast"));
		QVERIFY(ControlRegistry::instance()->invoke(QStringLiteral("control.undo")).ok);
		QCOMPARE(appearance::options().palette, QStringLiteral("dark"));
		QCOMPARE(appearance::storedScale(), 1.0);
	}

	void aPinnedPanelStaysOpenInEveryView()
	{
		Shell* shell = getGUI()->mainWindow()->shell();
		shell->setView(ShellView::Arrange);
		QVERIFY(!shell->model().isVisible(QStringLiteral("mixer")));  // Arrange shows the note editor below
		const ControlResult pinned = ControlRegistry::instance()->invoke(QStringLiteral("window.pin"), {{QStringLiteral("panel"), QStringLiteral("mixer")}});
		QVERIFY2(pinned.ok, qPrintable(pinned.errorMessage));
		QCOMPARE(pinned.result.value(QStringLiteral("pinned")).toBool(), true);
		QVERIFY(shell->model().isPinned(QStringLiteral("mixer")));
		QVERIFY(shell->model().isVisible(QStringLiteral("mixer")));
		shell->setView(ShellView::Edit);  // the Edit view opens its device chain below - the pin wins
		QVERIFY(shell->model().isVisible(QStringLiteral("mixer")));
		const ControlResult unpinned = ControlRegistry::instance()->invoke(QStringLiteral("window.pin"),
			{{QStringLiteral("panel"), QStringLiteral("mixer")}, {QStringLiteral("pinned"), false}});
		QVERIFY2(unpinned.ok, qPrintable(unpinned.errorMessage));
		QVERIFY(!shell->model().isPinned(QStringLiteral("mixer")));
		QCOMPARE(ControlRegistry::instance()->invoke(QStringLiteral("window.pin"), {{QStringLiteral("panel"), QStringLiteral("arranger")}}).errorKind,
			ControlErrorKind::InvalidArgs);  // a view's main panel cannot be pinned
		shell->setView(ShellView::Arrange);
	}

	void theSearchViewListsColumns()
	{
		BrowserSearchView view(nullptr);
		// Name, Kind, Size, Modified and Tags are the search's columns; Uses and State (Q050-121)
		// exist for Project Media only and stay hidden until it is shown.
		QCOMPARE(view.results()->columnCount(), 7);
		for (int column = 0; column < 5; ++column) { QVERIFY(!view.results()->isColumnHidden(column)); }
		QVERIFY(view.results()->isColumnHidden(5));
		QVERIFY(view.results()->isColumnHidden(6));
		QVERIFY(view.results()->isSortingEnabled());
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(HelpAccessTest)
#include "HelpAccessTest.moc"
