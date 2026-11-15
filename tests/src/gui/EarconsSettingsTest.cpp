/*
 * EarconsSettingsTest.cpp - view.earcons, View > Earcons and the earcon event hooks, in the real application
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
 * License along with this program (see COPYING); if not,
 * write to the Free Software Foundation, Inc., 51 Franklin Street,
 * Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

/*! Q050-497: every earcon category starts off; View > Earcons (objectName earconsMenu) switches one on
 *  through view.earcons and sets its level; the socket call and one undo step agree with the menu; bad
 *  arguments are typed InvalidArgs; and the refused-transition and automation-boundary hooks put a sound
 *  in the monitoring buffer only while their category is enabled. The edit earcon belongs to the
 *  registry's dispatch, so it is heard whatever route ran the command: a form (runCommand), a note
 *  drawn in the editor, an automation point moved by a key, or an in-place automation move. */

#include <QtTest>

#include <QApplication>
#include <cmath>
#include <QMenuBar>
#include <QTemporaryDir>

#include "AccessibleWidgets.h"
#include "AutomationClip.h"
#include "AutomationEditor.h"
#include "AutomationTrack.h"
#include "CommandForm.h"
#include "ControlRegistry.h"
#include "Earcons.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "InstrumentTrack.h"
#include "MainWindow.h"
#include "MidiClip.h"
#include "NoteCanvas.h"
#include "NoteEditor.h"
#include "NotePlayHandle.h"
#include "Song.h"
#include "Track.h"
#include "UiConfigStore.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult invoke(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QMenu* viewMenu()
{
	for (QAction* top : getGUI()->mainWindow()->menuBar()->actions())
	{
		if (top->menu() != nullptr && top->text().remove(QLatin1Char('&')) == QLatin1String("View")) { return top->menu(); }
	}
	return nullptr;
}

QMenu* earconsMenu(QMenu* view)
{
	for (QAction* action : view->actions())
	{
		QMenu* menu = action->menu();
		if (menu != nullptr && menu->objectName() == QLatin1String("earconsMenu")) { return menu; }
	}
	return nullptr;
}

QMenu* categoryMenu(QMenu* menu, const QString& category)
{
	for (QAction* action : menu->actions())
	{
		QMenu* sub = action->menu();
		if (sub != nullptr && sub->objectName() == QStringLiteral("earcons_%1").arg(category)) { return sub; }
	}
	return nullptr;
}

QAction* enabledAction(QMenu* category)
{
	for (QAction* action : category->actions())
	{
		if (action->data().toString() == QLatin1String("enabled")) { return action; }
	}
	return nullptr;
}

QAction* levelAction(QMenu* category, double level)
{
	for (QAction* action : category->actions())
	{
		if (action->property("earconLevel").toDouble() == level) { return action; }
	}
	return nullptr;
}

bool storedEnabled(const QString& category)
{
	return uistore::read(QStringLiteral("earcons")).value(category).toObject().value(QStringLiteral("enabled")).toBool();
}

//! Silence every voice and consume whatever was queued before the test's own play().
void flush()
{
	earcons::beginBuffer();
	for (int i = 0; i < 12000; ++i) { (void)earcons::nextSample(); }
}

//! The loudest sample of the next 12000 frames after beginBuffer().
float peak()
{
	earcons::beginBuffer();
	float loudest = 0.f;
	for (int i = 0; i < 12000; ++i)
	{
		const float value = earcons::nextSample();
		if (std::fabs(value) > std::fabs(loudest)) { loudest = value; }
	}
	return std::fabs(loudest);
}

} // namespace

class EarconsSettingsTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		// Drawing a note auditions it through the instrument: the pool main() creates must
		// exist in a test binary too (NoteEditorTest does the same).
		NotePlayHandleManager::init();
		earcons::prepare(48000);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void everyCategoryIsOffByDefault()
	{
		for (auto category = 0; category < static_cast<int>(earcons::Category::Count); ++category)
		{
			QVERIFY2(!earcons::enabled(static_cast<earcons::Category>(category)),
				qPrintable(earcons::categoryNames().value(category)));
		}
		QVERIFY(uistore::read(QStringLiteral("earcons")).isEmpty());
	}

	void theViewMenuSwitchesEditOnAndSetsItsLevel()
	{
		QMenu* view = viewMenu();
		QVERIFY(view != nullptr);
		emit view->aboutToShow();
		QMenu* menu = earconsMenu(view);
		QVERIFY2(menu != nullptr, "no Earcons entry in the View menu");
		emit menu->aboutToShow();
		QCOMPARE(menu->actions().size(), earcons::categoryNames().size());
		QCOMPARE(view->findChildren<QMenu*>(QStringLiteral("earconsMenu")).size(), 1);

		QMenu* edit = categoryMenu(menu, QStringLiteral("edit"));
		QVERIFY(edit != nullptr);
		QAction* on = enabledAction(edit);
		QVERIFY(on != nullptr);
		QVERIFY(on->isCheckable());
		QVERIFY(!on->isChecked());
		const int depth0 = undoDepth();
		on->trigger();
		QCOMPARE(undoDepth(), depth0 + 1);
		QVERIFY(earcons::enabled(earcons::Category::Edit));
		QVERIFY(storedEnabled(QStringLiteral("edit")));
		QVERIFY(on->isChecked());

		QAction* full = levelAction(edit, 1.0);
		QVERIFY(full != nullptr);
		QVERIFY(full->isCheckable());
		const float levelBefore = earcons::level(earcons::Category::Edit);
		full->trigger();
		QCOMPARE(undoDepth(), depth0 + 2);
		QCOMPARE(earcons::level(earcons::Category::Edit), 1.0f);
		QVERIFY(uistore::read(QStringLiteral("earcons")).value(QStringLiteral("edit")).toObject()
			.value(QStringLiteral("level")).toDouble() == 1.0);
		QVERIFY(full->isChecked());

		QVERIFY(invoke(QStringLiteral("control.undo")).ok);
		QCOMPARE(earcons::level(earcons::Category::Edit), levelBefore);
		QVERIFY(earcons::enabled(earcons::Category::Edit));
		QCOMPARE(undoDepth(), depth0 + 1);

		QVERIFY(invoke(QStringLiteral("control.undo")).ok);
		QVERIFY(!earcons::enabled(earcons::Category::Edit));
		QVERIFY(!storedEnabled(QStringLiteral("edit")));
		QCOMPARE(undoDepth(), depth0);
	}

	void theSocketWritesOneUndoStep()
	{
		QVERIFY(invoke(QStringLiteral("view.earcons"), {{QStringLiteral("level"), 0.5}}).errorKind == ControlErrorKind::InvalidArgs);
		const ControlResult result = invoke(QStringLiteral("view.earcons"),
			{{QStringLiteral("category"), QStringLiteral("loop")}, {QStringLiteral("enabled"), true},
				{QStringLiteral("level"), 1.0}});
		QVERIFY2(result.ok, qPrintable(result.errorMessage));
		QCOMPARE(result.result.value(QStringLiteral("category")).toString(), QStringLiteral("loop"));
		const QJsonObject previous = result.result.value(QStringLiteral("previous")).toObject();
		QCOMPARE(previous.value(QStringLiteral("enabled")).toBool(), false);
		QCOMPARE(previous.value(QStringLiteral("level")).toDouble(), 0.5);
		QVERIFY(earcons::enabled(earcons::Category::Loop));
		QCOMPARE(earcons::level(earcons::Category::Loop), 1.0f);

		QVERIFY(invoke(QStringLiteral("control.undo")).ok);
		QVERIFY(!earcons::enabled(earcons::Category::Loop));
		QCOMPARE(earcons::level(earcons::Category::Loop), 0.5f);
		QVERIFY(!storedEnabled(QStringLiteral("loop")));
	}

	void invalidArgumentsAreTyped()
	{
		using K = ControlErrorKind;
		QCOMPARE(invoke(QStringLiteral("view.earcons"), {{QStringLiteral("category"), QStringLiteral("bogus")},
			{QStringLiteral("enabled"), true}}).errorKind, K::InvalidArgs);
		QCOMPARE(invoke(QStringLiteral("view.earcons"), {{QStringLiteral("category"), QStringLiteral("record")},
			{QStringLiteral("level"), 1.5}}).errorKind, K::InvalidArgs);
		QCOMPARE(invoke(QStringLiteral("view.earcons"), {{QStringLiteral("category"), QStringLiteral("record")},
			{QStringLiteral("level"), -1.0}}).errorKind, K::InvalidArgs);
		QCOMPARE(invoke(QStringLiteral("view.earcons"), {{QStringLiteral("category"), QStringLiteral("record")}}).errorKind,
			K::InvalidArgs);
		QCOMPARE(invoke(QStringLiteral("view.earcons"), {{QStringLiteral("category"), QStringLiteral("record")},
			{QStringLiteral("enabled"), 1}}).errorKind, K::InvalidArgs);
		QVERIFY(!earcons::enabled(earcons::Category::Record));
	}

	void aRefusedTransitionIsHeardOnlyWhenEnabled()
	{
		flush();
		a11y::announceTransition(nullptr, a11y::Transition::Refused, QStringLiteral("test"));
		QCOMPARE(peak(), 0.f);  // off by default

		QVERIFY(invoke(QStringLiteral("view.earcons"),
			{{QStringLiteral("category"), QStringLiteral("refused")}, {QStringLiteral("enabled"), true}}).ok);
		flush();
		a11y::announceTransition(nullptr, a11y::Transition::Refused, QStringLiteral("test"));
		QVERIFY2(peak() > 0.f, "the refused earcon was silent while enabled");
	}

	//! The edit hook, through the form route: runCommand dispatches the command and the
	//! registry queues the sound.
	void anAppliedEditIsHeard()
	{
		QVERIFY(invoke(QStringLiteral("view.earcons"),
			{{QStringLiteral("category"), QStringLiteral("edit")}, {QStringLiteral("enabled"), true}}).ok);
		flush();
		CommandCall call;
		call.id = QStringLiteral("view.crosshair");
		call.fixed = {{QStringLiteral("enabled"), true}};
		call.ask = false;
		const ControlResult ran = runCommand(nullptr, call);
		QVERIFY2(ran.ok, qPrintable(ran.errorMessage));
		QVERIFY2(peak() > 0.f, "the edit earcon was silent for an applied edit");

		QVERIFY(invoke(QStringLiteral("view.earcons"),
			{{QStringLiteral("category"), QStringLiteral("edit")}, {QStringLiteral("enabled"), false}}).ok);
		flush();
		call.fixed = {{QStringLiteral("enabled"), false}};
		QVERIFY(runCommand(nullptr, call).ok);
		QCOMPARE(peak(), 0.f);
	}

	//! The editor route: a note drawn on the canvas runs note.add through NoteEditor::run, so
	//! the registry's EditApplied is heard while the edit category is on - and not while it is off.
	void aNoteDrawnInTheEditorIsHeard()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		QVERIFY(track != nullptr);
		auto* clip = dynamic_cast<MidiClip*>(track->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		auto* editor = new NoteEditor;
		editor->resize(1200, 640);
		editor->show();
		QVERIFY(QTest::qWaitForWindowExposed(editor));
		editor->setClip(clip);
		editor->setZoom(1.0);  // 1 px per tick: the point() arithmetic is exact
		NoteGrid& grid = editor->state().grid;
		grid.setScrollTicks(0);
		grid.setScrollY(grid.rowOfKey(62) * grid.rowHeight());
		emit editor->stateChanged();
		editor->setTool(NoteTool::Draw);

		for (const char* category : {"selection", "refused", "record", "loop"})
		{
			QVERIFY(invoke(QStringLiteral("view.earcons"),
				{{QStringLiteral("category"), QString::fromLatin1(category)}, {QStringLiteral("enabled"), false}}).ok);
		}
		QVERIFY(invoke(QStringLiteral("view.earcons"),
			{{QStringLiteral("category"), QStringLiteral("edit")}, {QStringLiteral("enabled"), true}}).ok);
		const int depth = undoDepth();
		flush();
		const QPoint first(grid.xOfTick(100) + 2, grid.yOfKey(62) + grid.rowHeight() / 2);
		QTest::mouseClick(editor->canvas(), Qt::LeftButton, Qt::NoModifier, first);
		QCOMPARE(clip->notes().size(), std::size_t{1});
		QVERIFY2(peak() > 0.f, "the edit earcon was silent for a note drawn in the editor");
		QCOMPARE(undoDepth(), depth + 1);

		QVERIFY(invoke(QStringLiteral("view.earcons"),
			{{QStringLiteral("category"), QStringLiteral("edit")}, {QStringLiteral("enabled"), false}}).ok);
		flush();
		const QPoint second(grid.xOfTick(300) + 2, grid.yOfKey(62) + grid.rowHeight() / 2);
		QTest::mouseClick(editor->canvas(), Qt::LeftButton, Qt::NoModifier, second);
		QCOMPARE(clip->notes().size(), std::size_t{2});
		QCOMPARE(peak(), 0.f);

		delete editor;
		delete clip;
	}

	//! The record hook: starting and stopping a recording are heard while the category is on.
	void recordingStartAndStopAreHeard()
	{
		QVERIFY(invoke(QStringLiteral("view.earcons"),
			{{QStringLiteral("category"), QStringLiteral("record")}, {QStringLiteral("enabled"), true}}).ok);
		flush();
		a11y::announceTransition(nullptr, a11y::Transition::RecordStarted);
		QVERIFY2(peak() > 0.f, "the record-start earcon was silent");
		flush();
		a11y::announceTransition(nullptr, a11y::Transition::RecordStopped);
		QVERIFY2(peak() > 0.f, "the record-stop earcon was silent");

		QVERIFY(invoke(QStringLiteral("view.earcons"),
			{{QStringLiteral("category"), QStringLiteral("record")}, {QStringLiteral("enabled"), false}}).ok);
		flush();
		a11y::announceTransition(nullptr, a11y::Transition::RecordStarted);
		QCOMPARE(peak(), 0.f);
		flush();
		a11y::announceTransition(nullptr, a11y::Transition::RecordStopped);
		QCOMPARE(peak(), 0.f);
	}

	//! The key route: Ctrl+Left chooses the last point, then Up moves it through
	//! automation.move_point (the volume model is addressable) - the registry queues the sound.
	void anAutomationPointMovedByKeyIsHeard()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* automation = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
		QVERIFY(track != nullptr && automation != nullptr);
		auto* clip = dynamic_cast<AutomationClip*>(automation->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->addObject(track->volumeModel(), false);  // seeds one point at 0
		clip->putValue(TimePos(48), 40.f, false);
		getGUI()->automationEditor()->open(clip);
		AutomationEditor* editor = getGUI()->automationEditor()->m_editor;
		QVERIFY(editor != nullptr);
		editor->setFocus();
		QCoreApplication::processEvents();

		QVERIFY(invoke(QStringLiteral("view.earcons"),
			{{QStringLiteral("category"), QStringLiteral("selection")}, {QStringLiteral("enabled"), false}}).ok);
		QVERIFY(invoke(QStringLiteral("view.earcons"),
			{{QStringLiteral("category"), QStringLiteral("edit")}, {QStringLiteral("enabled"), true}}).ok);
		flush();
		QTest::keyClick(editor, Qt::Key_Left, Qt::ControlModifier);  // from none, picks the last point
		flush();
		const auto& before = clip->getTimeMap()[TimePos(48)];
		const float value = before.getInValue();
		QTest::keyClick(editor, Qt::Key_Up);  // moveByArrow changes the value
		QVERIFY2(clip->getTimeMap()[TimePos(48)].getInValue() != value, "the point's value did not change");
		QVERIFY2(peak() > 0.f, "the edit earcon was silent for an automation point moved by key");
	}

	void theAutomationBoundaryIsHeard()
	{
		auto* track = dynamic_cast<InstrumentTrack*>(Track::create(Track::Type::Instrument, Engine::getSong()));
		auto* automation = dynamic_cast<AutomationTrack*>(Track::create(Track::Type::Automation, Engine::getSong()));
		QVERIFY(track != nullptr && automation != nullptr);
		auto* clip = dynamic_cast<AutomationClip*>(automation->createClip(TimePos(0)));
		QVERIFY(clip != nullptr);
		clip->addObject(track->volumeModel(), false);  // seeds one point at 0
		clip->putValue(TimePos(48), 40.f, false);
		getGUI()->automationEditor()->open(clip);
		AutomationEditor* editor = getGUI()->automationEditor()->m_editor;
		QVERIFY(editor != nullptr);
		editor->setFocus();
		QCoreApplication::processEvents();

		QVERIFY(invoke(QStringLiteral("view.earcons"),
			{{QStringLiteral("category"), QStringLiteral("selection")}, {QStringLiteral("enabled"), true}}).ok);
		flush();
		QTest::keyClick(editor, Qt::Key_Left, Qt::ControlModifier);  // from none, picks the last point
		flush();
		QTest::keyClick(editor, Qt::Key_Right, Qt::ControlModifier);  // at the end already: a boundary
		QVERIFY2(peak() > 0.f, "the selection boundary earcon was silent");
	}

private:
	int undoDepth() const
	{
		return ControlRegistry::instance()->invoke(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
	}

	QTemporaryDir m_home;
};

QTEST_MAIN(EarconsSettingsTest)
#include "EarconsSettingsTest.moc"
