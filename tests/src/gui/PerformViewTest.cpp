/*
 * PerformViewTest.cpp - perform mode, the launcher's live show window (WF-071)
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

/*! 0.5.0 L02 (WF-071): perform mode over the REAL application (GuiApplication): View > Perform Mode and the
 *  Launcher menu open it through window.toggle 'perform'; its scene buttons, its keys and its Stop All produce
 *  the engine state session.get_state reports; it shows the playing scene, the next one, each column's cell, the
 *  tempo (whose buttons are undoable transport.set_tempo steps) and the setlist's song and the next; Learn MIDI
 *  binds the controller's next note to a scene through session.bind_learn; every button declares its command. */

#include <QtTest>

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>
#include <QToolButton>

#include <functional>
#include <limits>
#include <utility>

#include "ControlRegistry.h"
#include "DesignTokens.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "LaunchBindings.h"
#include "MainWindow.h"
#include "PerformView.h"
#include "SessionGridMenus.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = QJsonObject())
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A command that declares `requires: display`, run as the interface runs it (the offscreen platform is one).
ControlResult runShown(const QString& id, const QJsonObject& args)
{
	ControlRegistry* registry = ControlRegistry::instance();
	const bool headless = registry->isHeadless();
	registry->setHeadless(false);
	const ControlResult result = registry->invoke(id, args);
	registry->setHeadless(headless);
	return result;
}

//! The scene the engine reports column @a track playing, or -1 when it is idle.
int playingScene(int track)
{
	for (const QJsonValue& value : run(QStringLiteral("session.get_state")).result.value(QStringLiteral("columns")).toArray())
	{
		const QJsonObject column = value.toObject();
		if (column.value(QStringLiteral("track")).toInt() == track && column.value(QStringLiteral("phase")).toString() == QLatin1String("playing"))
		{
			return column.value(QStringLiteral("scene")).toInt();
		}
	}
	return -1;
}

bool transportPlaying()
{
	return run(QStringLiteral("transport.get_state")).result.value(QStringLiteral("playing")).toBool();
}

//! Open the setlist's song @a to (an index) at @a path, edit it (unsaved) and play it; false when that did not happen.
bool openUnsavedAndPlaying(int to, const QString& path)
{
	if (!run(QStringLiteral("session.setlist_go"), {{QStringLiteral("to"), QString::number(to)}}).ok) { return false; }
	if (Engine::getSong()->projectFileName() != path) { return false; }
	QCoreApplication::sendPostedEvents();  // deliver the track views the load queued, before the next load removes them
	Engine::getSong()->setModified();
	return run(QStringLiteral("transport.play")).ok;
}

//! Whether a song step at the end of the set did nothing: still playing, the same song open and unsaved, the same place.
bool nothingHappened(const QString& path, int index)
{
	return transportPlaying() && Engine::getSong()->projectFileName() == path && Engine::getSong()->isModified()
		&& run(QStringLiteral("session.setlist")).result.value(QStringLiteral("index")).toInt(-1) == index;
}

//! The action under @a menu (and its submenus) whose text is @a text, or nullptr.
QAction* findAction(QMenu* menu, const QString& text)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == text) { return action; }
		if (action->menu() != nullptr)
		{
			if (QAction* inner = findAction(action->menu(), text)) { return inner; }
		}
	}
	return nullptr;
}

QJsonObject cell(int track, int scene, const QString& name)
{
	return {{QStringLiteral("track"), track}, {QStringLiteral("scene"), scene}, {QStringLiteral("type"), QStringLiteral("audio")},
		{QStringLiteral("source"), name + QStringLiteral(".wav")}, {QStringLiteral("name"), name}};
}

//! Replaces one registered command handler while leaving the rest of the registry ready.
class CommandHandlerOverride
{
public:
	CommandHandlerOverride(const QString& id, std::function<ControlResult(const QJsonObject&)> handler) :
		m_command(const_cast<ControlCommand*>(ControlRegistry::instance()->command(id))),
		m_handler(m_command->handler)
	{
		m_command->handler = std::move(handler);
	}

	~CommandHandlerOverride() { m_command->handler = m_handler; }

private:
	ControlCommand* m_command;
	std::function<ControlResult(const QJsonObject&)> m_handler;
};

} // namespace

class PerformViewTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		guitest::startGui(m_dir);
		QVERIFY(getGUI() != nullptr && getGUI()->mainWindow() != nullptr);
		// Shown and settled as the application's own event loop would have it: the shell installs the Launcher menu
		// once it is shown, and each track's view is made from a queued call.
		getGUI()->mainWindow()->show();
		QVERIFY(QTest::qWaitForWindowExposed(getGUI()->mainWindow()));
		ControlRegistry::setReady(true);
		while (run(QStringLiteral("track.list")).result.value(QStringLiteral("count")).toInt() < 2)
		{
			QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		}
		QVERIFY(run(QStringLiteral("session.set_grid"), {{QStringLiteral("tracks"), 2}, {QStringLiteral("scenes"), 3}}).ok);
		QVERIFY(run(QStringLiteral("session.set_quantisation"), {{QStringLiteral("quantisation"), QStringLiteral("none")}}).ok);
		for (const QJsonObject& filled : {cell(0, 0, QStringLiteral("kick")), cell(1, 0, QStringLiteral("bass")), cell(0, 1, QStringLiteral("lead"))})
		{
			QVERIFY(run(QStringLiteral("session.set_slot"), filled).ok);
		}
		QVERIFY(run(QStringLiteral("session.set_scene"), {{QStringLiteral("scene"), 0}, {QStringLiteral("name"), QStringLiteral("Intro")}}).ok);
		QVERIFY(run(QStringLiteral("session.set_scene"), {{QStringLiteral("scene"), 1}, {QStringLiteral("name"), QStringLiteral("Drop")}}).ok);
		QTest::qWait(100);
	}

	void cleanupTestCase()
	{
		run(QStringLiteral("session.stop_all"));
		run(QStringLiteral("transport.stop"));
		LaunchBindings::disconnect();
		QTest::qWait(200);  // settle what the slots left queued, as the event loop would before a quit
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The shell reaches it: View > Perform Mode and Launcher > Perform Mode both declare window.toggle, and
	//! window.toggle 'perform' shows and hides it as window.get_state then reports.
	void theShellOpensPerformMode()
	{
		MainWindow* window = getGUI()->mainWindow();
		QAction* view = nullptr;
		for (QAction* action : window->actions())  // the View menu's window rows are the main window's own actions
		{
			if (action->property("floatingWindow").toString() == QLatin1String("perform")) { view = action; }
		}
		QVERIFY2(view != nullptr, "View > Perform Mode is missing");
		QCOMPARE(view->text(), QStringLiteral("Perform Mode"));
		QCOMPARE(view->property("controlCommand").toString(), QStringLiteral("window.toggle"));
		QTRY_VERIFY_WITH_TIMEOUT(window->findChild<QMenu*>(QStringLiteral("launcherMenu")) != nullptr, 5000);
		auto* launcher = window->findChild<QMenu*>(QStringLiteral("launcherMenu"));
		QAction* entry = findAction(launcher, QStringLiteral("Perform Mode"));
		QVERIFY2(entry != nullptr, "Launcher > Perform Mode is missing");
		QCOMPARE(entry->property("controlCommand").toString(), QStringLiteral("window.toggle"));

		const ControlResult shown = runShown(QStringLiteral("window.toggle"), {{QStringLiteral("editor"), QStringLiteral("perform")}});
		QVERIFY2(shown.ok, qPrintable(shown.errorMessage));
		QVERIFY(shown.result.value(QStringLiteral("visible")).toBool());
		QVERIFY(PerformView::instance(false) != nullptr && PerformView::instance(false)->isVisible());
		bool listed = false;
		for (const QJsonValue& value : runShown(QStringLiteral("window.get_state"), {}).result.value(QStringLiteral("editors")).toArray())
		{
			const QJsonObject editor = value.toObject();
			listed = listed || (editor.value(QStringLiteral("editor")).toString() == QLatin1String("perform") && editor.value(QStringLiteral("visible")).toBool());
		}
		QVERIFY(listed);
		const ControlResult hidden = runShown(QStringLiteral("window.toggle"), {{QStringLiteral("editor"), QStringLiteral("perform")}});
		QVERIFY(hidden.ok);
		QVERIFY(!hidden.result.value(QStringLiteral("visible")).toBool());
		QVERIFY(!PerformView::instance(false)->isVisible());
	}

	//! Big scene buttons launch the row; the window says what plays, what is next and what each column plays.
	void theSceneButtonsLaunchAndTheWindowSaysWhatPlays()
	{
		PerformView* view = PerformView::instance(true);
		view->refresh();
		QCOMPARE(view->sceneButtonCount(), 3);
		QVERIFY(view->sceneButton(0)->text().contains(QStringLiteral("Intro")));
		QVERIFY(view->sceneButton(2)->text().contains(QStringLiteral("Scene 3")));
		QVERIFY(view->sceneButton(0)->minimumHeight() >= 2 * tokens::targetSize());
		QVERIFY(view->stopAllButton()->minimumHeight() > view->sceneButton(0)->minimumHeight());
		QCOMPARE(view->playingText(), QStringLiteral("Playing: nothing"));
		QTest::mouseClick(view->sceneButton(0), Qt::LeftButton);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), 0, 3000);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(1), 0, 3000);
		view->refresh();
		QCOMPARE(view->playingText(), QStringLiteral("Playing: Intro"));
		QVERIFY2(view->columnText(0).contains(QStringLiteral("kick")), qPrintable(view->columnText(0)));
		QVERIFY2(view->columnText(1).contains(QStringLiteral("bass")), qPrintable(view->columnText(1)));
		QCOMPARE(view->nextSceneText(), QStringLiteral("Next: Intro"));
		QTest::mouseClick(view->stopAllButton(), Qt::LeftButton);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), -1, 3000);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(1), -1, 3000);
	}

	//! The keys: Down selects, Enter launches the selected scene and selects the next, a digit launches its
	//! scene, Escape stops everything, Space starts and stops the transport - even with a button focused.
	void theKeysPerform()
	{
		PerformView* view = PerformView::instance(true);
		view->show();
		QVERIFY(QTest::qWaitForWindowExposed(view));
		view->refresh();
		QTest::keyClick(view, Qt::Key_Down);
		QCOMPARE(view->selectedScene(), 1);
		QCOMPARE(view->nextSceneText(), QStringLiteral("Next: Drop"));
		QTest::keyClick(view, Qt::Key_Return);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), 1, 3000);
		QCOMPARE(view->selectedScene(), 2);
		QTest::keyClick(view, Qt::Key_Escape);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(0), -1, 3000);
		view->sceneButton(2)->setFocus();
		QTest::keyClick(view->sceneButton(2), Qt::Key_1);  // a focused button does not keep the window's keys
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(1), 0, 3000);
		QTest::keyClick(view->sceneButton(2), Qt::Key_Escape);
		QTRY_COMPARE_WITH_TIMEOUT(playingScene(1), -1, 3000);
		QVERIFY(!transportPlaying());
		QTest::keyClick(view->sceneButton(2), Qt::Key_Space);
		QTRY_VERIFY_WITH_TIMEOUT(transportPlaying(), 3000);
		QCOMPARE(playingScene(0), -1);  // Space moved the transport, it did not click the focused scene
		view->refresh();
		QTest::keyClick(view, Qt::Key_Space);
		QTRY_VERIFY_WITH_TIMEOUT(!transportPlaying(), 3000);
		view->hide();
	}

	//! The tempo is shown, and its buttons are transport.set_tempo steps that control.undo takes back.
	void theTempoButtonsAreUndoableSteps()
	{
		PerformView* view = PerformView::instance(true);
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 120}}).ok);
		view->refresh();
		QCOMPARE(view->tempoText(), QStringLiteral("120 BPM"));
		QVERIFY(view->positionText().startsWith(QStringLiteral("Bar ")));
		QTest::mouseClick(view->tempoUpButton(), Qt::LeftButton);
		QCOMPARE(run(QStringLiteral("transport.get_state")).result.value(QStringLiteral("tempo")).toInt(), 121);
		QCOMPARE(view->tempoText(), QStringLiteral("121 BPM"));
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		// The tempo's undo step is a Song checkpoint: restoring it remakes the tracks, and each track's view is made
		// from a queued call. The application's event loop runs between two presses; so does this test.
		QTest::qWait(50);
		QCOMPARE(run(QStringLiteral("transport.get_state")).result.value(QStringLiteral("tempo")).toInt(), 120);
		view->refresh();
		QTest::mouseClick(view->tempoDownButton(), Qt::LeftButton);
		QCOMPARE(run(QStringLiteral("transport.get_state")).result.value(QStringLiteral("tempo")).toInt(), 119);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QTest::qWait(50);
	}

	//! A failed transport read leaves the last valid transport display and reports the read refusal.
	void failedTransportStateReadKeepsLastKnownState()
	{
		PerformView* view = PerformView::instance(true);
		QVERIFY(run(QStringLiteral("transport.set_tempo"), {{QStringLiteral("bpm"), 120}}).ok);
		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 48}}).ok);
		view->refresh();
		const QString tempo = view->tempoText();
		const QString position = view->positionText();
		const bool playing = view->playButton()->isChecked();

		const QString failureMessage = QStringLiteral("injected transport.get_state failure");
		{
			CommandHandlerOverride failure(QStringLiteral("transport.get_state"), [failureMessage](const QJsonObject&) {
				return ControlResult::failure(ControlErrorKind::Refused, failureMessage);
			});
			view->refresh();
		}

		QCOMPARE(view->tempoText(), tempo);
		QCOMPARE(view->positionText(), position);
		QCOMPARE(view->playButton()->isChecked(), playing);
		QVERIFY2(view->statusLine().contains(failureMessage), qPrintable(view->statusLine()));
	}

	//! A failed session state read keeps the grid and reports that read's error even while siblings succeed.
	void failedSessionStateReadKeepsGridAndShowsError()
	{
		PerformView* view = PerformView::instance(true);
		view->refresh();
		const int scenes = view->sceneButtonCount();
		const QString intro = view->sceneButton(0)->text();
		const QString column = view->columnText(0);
		const QString failureMessage = QStringLiteral("injected session.get_state failure");
		{
			CommandHandlerOverride failure(QStringLiteral("session.get_state"), [failureMessage](const QJsonObject&) {
				return ControlResult::failure(ControlErrorKind::Refused, failureMessage);
			});
			view->refresh();
		}

		QCOMPARE(view->sceneButtonCount(), scenes);
		QCOMPARE(view->sceneButton(0)->text(), intro);
		QCOMPARE(view->columnText(0), column);
		QVERIFY2(view->statusLine().contains(failureMessage), qPrintable(view->statusLine()));
	}

	//! A failed direct setlist read keeps its last valid labels and navigation availability.
	void failedSetlistReadKeepsLastKnownSetlist()
	{
		PerformView* view = PerformView::instance(true);
		struct RestoreSetlist
		{
			~RestoreSetlist() { run(QStringLiteral("session.setlist"), {{QStringLiteral("items"), QJsonArray{}}}); }
		} restore;
		QVERIFY(run(QStringLiteral("session.setlist"), {{QStringLiteral("items"),
			QJsonArray{m_dir.filePath(QStringLiteral("first.mmpz")), m_dir.filePath(QStringLiteral("second.mmpz"))}}}).ok);
		view->refreshSetlist();
		const QString song = view->songText();
		const QString nextSong = view->nextSongText();
		const bool nextEnabled = view->nextSongButton()->isEnabled();
		const bool previousEnabled = view->previousSongButton()->isEnabled();
		QVERIFY(nextEnabled);
		const QString failureMessage = QStringLiteral("injected session.setlist failure");
		{
			CommandHandlerOverride failure(QStringLiteral("session.setlist"), [failureMessage](const QJsonObject&) {
				return ControlResult::failure(ControlErrorKind::Refused, failureMessage);
			});
			view->nextSongButton()->click();
			QVERIFY2(view->statusLine().contains(failureMessage), qPrintable(view->statusLine()));
			view->refreshSetlist();
		}

		QCOMPARE(view->songText(), song);
		QCOMPARE(view->nextSongText(), nextSong);
		QCOMPARE(view->nextSongButton()->isEnabled(), nextEnabled);
		QCOMPARE(view->previousSongButton()->isEnabled(), previousEnabled);
		QVERIFY2(view->statusLine().contains(failureMessage), qPrintable(view->statusLine()));
	}

	//! The largest transport tick and the one-tick boundary render a 64-bit bar number.
	void displayedBarDoesNotOverflowAtMaxTicks()
	{
		PerformView* view = PerformView::instance(true);
		QVERIFY(run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), std::numeric_limits<int>::max()}}).ok);
		struct RestoreTransport
		{
			~RestoreTransport() { run(QStringLiteral("transport.seek"), {{QStringLiteral("ticks"), 0}}); }
		} restore;
		const auto original = ControlRegistry::instance()->command(QStringLiteral("session.get_state"))->handler;
		CommandHandlerOverride ticksPerBarOverride(QStringLiteral("session.get_state"), [original](const QJsonObject& args) {
			ControlResult state = original(args);
			if (state.ok)
			{
				QJsonObject launch = state.result.value(QStringLiteral("launch")).toObject();
				launch.insert(QStringLiteral("ticks_per_bar"), 1);
				state.result.insert(QStringLiteral("launch"), launch);
			}
			return state;
		});
		view->refresh();
		QCOMPARE(view->positionText(), QStringLiteral("Bar 2147483648 · Beat 1"));
	}

	//! The setlist's song and the next one, from session.setlist; the song buttons follow where the set stands.
	void theSetlistShowsTheSongAndTheNext()
	{
		PerformView* view = PerformView::instance(true);
		view->refreshSetlist();
		QVERIFY(view->songText().startsWith(QStringLiteral("No setlist")));
		QVERIFY(!view->nextSongButton()->isEnabled());
		QVERIFY(run(QStringLiteral("session.setlist"), {{QStringLiteral("items"),
			QJsonArray{m_dir.filePath(QStringLiteral("first.mmpz")), m_dir.filePath(QStringLiteral("second.mmpz"))}}}).ok);
		view->refreshSetlist();
		QVERIFY2(view->songText().contains(QStringLiteral("2 song")), qPrintable(view->songText()));
		QCOMPARE(view->nextSongText(), QStringLiteral("Next song: first"));
		QVERIFY(view->nextSongButton()->isEnabled());
		QVERIFY(!view->previousSongButton()->isEnabled());
		QVERIFY(run(QStringLiteral("session.setlist"), {{QStringLiteral("items"), QJsonArray{}}}).ok);
	}

	//! At either end of the setlist a song step is nothing: Page Up at the first song, Page Down at the last, and the
	//! grid's Previous and Next Song. No question about unsaved work, no transport stop (mayChangeProject stops
	//! playback before it asks), and no reopening of the song that is open.
	void songStepsAtTheEndsAreNothing()
	{
		PerformView* view = PerformView::instance(true);
		const QString first = m_dir.filePath(QStringLiteral("set-first.mmp"));
		const QString second = m_dir.filePath(QStringLiteral("set-second.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(first));
		QVERIFY(Engine::getSong()->saveProjectFile(second));
		QVERIFY(run(QStringLiteral("session.setlist"), {{QStringLiteral("items"), QJsonArray{first, second}}}).ok);
		// The question a step asks would block this test; answered here at once, a step that asks shows as a transport stop.
		ControlRegistry::setQuitPromptAnswer(ControlRegistry::QuitPromptAnswer::Discard);
		// Restored however the test ends, so a failed check leaves no prompt answer, set or playing transport behind.
		struct Restore
		{
			~Restore()
			{
				run(QStringLiteral("transport.stop"));
				run(QStringLiteral("session.setlist"), {{QStringLiteral("items"), QJsonArray{}}});
				ControlRegistry::setQuitPromptAnswer(ControlRegistry::QuitPromptAnswer::Ask);
			}
		} restore;
		QMenu grid;
		SessionGridMenus::populateGrid(&grid, 2, 3);
		QAction* nextSong = findAction(&grid, QStringLiteral("Next Song"));
		QAction* previousSong = findAction(&grid, QStringLiteral("Previous Song"));
		QVERIFY(nextSong != nullptr && previousSong != nullptr);

		QVERIFY2(openUnsavedAndPlaying(1, second), "the last song did not open, unsaved, playing");
		QTRY_VERIFY_WITH_TIMEOUT(transportPlaying(), 3000);
		view->performKey(Qt::Key_PageDown);
		QVERIFY2(nothingHappened(second, 1), "Page Down at the last song stopped, asked about, or reopened the song");
		QVERIFY2(openUnsavedAndPlaying(1, second), "the last song did not open, unsaved, playing");
		QTRY_VERIFY_WITH_TIMEOUT(transportPlaying(), 3000);
		nextSong->trigger();
		QVERIFY2(nothingHappened(second, 1), "Next Song at the last song stopped, asked about, or reopened the song");
		QVERIFY2(openUnsavedAndPlaying(0, first), "the first song did not open, unsaved, playing");
		QTRY_VERIFY_WITH_TIMEOUT(transportPlaying(), 3000);
		view->performKey(Qt::Key_PageUp);
		QVERIFY2(nothingHappened(first, 0), "Page Up at the first song stopped, asked about, or reopened the song");
		QVERIFY2(openUnsavedAndPlaying(0, first), "the first song did not open, unsaved, playing");
		QTRY_VERIFY_WITH_TIMEOUT(transportPlaying(), 3000);
		previousSong->trigger();
		QVERIFY2(nothingHappened(first, 0), "Previous Song at the first song stopped, asked about, or reopened the song");
	}

	//! Learn MIDI: refused (and said) with no controller; with one, the next note binds to the pressed scene.
	void learnMidiBindsTheNextNoteToAScene()
	{
		PerformView* view = PerformView::instance(true);
		LaunchBindings::setBindings({});
		view->learnButton()->setChecked(true);
		QTest::mouseClick(view->sceneButton(1), Qt::LeftButton);
		QVERIFY2(view->statusLine().contains(QStringLiteral("no controller")), qPrintable(view->statusLine()));
		QVERIFY(!view->learnButton()->isChecked());
		QCOMPARE(playingScene(0), -1);  // a press while learning launches nothing
		LaunchBindings* bindings = LaunchBindings::connect(QString(), QString());
		view->learnButton()->setChecked(true);
		QTest::mouseClick(view->sceneButton(1), Qt::LeftButton);
		QVERIFY(view->statusLine().contains(QStringLiteral("Drop")));
		bindings->applyNote(LaunchNote{0, 64, true});
		QCOMPARE(LaunchBindings::bindings().size(), 1);
		const QJsonObject bound = LaunchBindings::bindings().first().toObject();
		QCOMPARE(bound.value(QStringLiteral("action")).toString(), QStringLiteral("scene"));
		QCOMPARE(bound.value(QStringLiteral("scene")).toInt(), 1);
		QCOMPARE(bound.value(QStringLiteral("key")).toInt(), 64);
		QVERIFY(run(QStringLiteral("control.undo")).ok);  // the binding is one undo step
		QVERIFY(LaunchBindings::bindings().isEmpty());
		LaunchBindings::disconnect();
	}

	//! Every button runs a command and says which (the agent-surface rule TakeLanesPanelTest holds panels to).
	void everyButtonDeclaresItsCommand()
	{
		PerformView* view = PerformView::instance(true);
		view->refresh();
		const QList<QToolButton*> buttons = view->findChildren<QToolButton*>();
		QVERIFY(buttons.size() >= 10);
		for (QToolButton* button : buttons)
		{
			QVERIFY2(!button->property("controlCommand").toString().isEmpty(), qPrintable(QStringLiteral("an undeclared button: ") + button->text()));
			QVERIFY2(ControlRegistry::instance()->hasCommand(button->property("controlCommand").toString()), qPrintable(button->text()));
			QVERIFY2(!button->accessibleName().isEmpty(), qPrintable(button->text()));
		}
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(PerformViewTest)
#include "PerformViewTest.moc"
