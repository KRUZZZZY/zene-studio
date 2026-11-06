/*
 * HybridTrackGuiTest.cpp - the Add neutral track control of the Song Editor (Q050-283.1)
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



/*! 0.5.0 L04, Q050-283.1 (R-workflow-286), in the real application: the Song Editor's
 *  "Add neutral track" control is CLICKED (not its handler called), and the same registered
 *  command an agent uses - track.add type=neutral - makes the row. The row is drawn as a neutral
 *  row (a NeutralTrackView) at its arrangement position, carries the stable id the agent surface
 *  reports for it, goes away with one control.undo and comes back with one control.redo, and
 *  is drawn again - same id, same position - after the project is saved and loaded. */

#include <QtTest>

#include <QAbstractButton>
#include <QAction>
#include <QDir>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QToolBar>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "MainWindow.h"
#include "NeutralTrack.h"
#include "NeutralTrackView.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "TrackView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

QJsonArray listed()
{
	return run(QStringLiteral("track.list")).result.value(QStringLiteral("tracks")).toArray();
}

const QList<TrackView*>& rows() { return getGUI()->songEditor()->m_editor->trackViews(); }

//! The toolbar control that adds a neutral track.
QAction* addNeutralAction()
{
	return getGUI()->songEditor()->findChild<QAction*>(QStringLiteral("addNeutralTrackAction"));
}

//! Clicks the neutral-track control the way a user does: the real button on the real toolbar.
bool clickAddNeutral()
{
	QAction* action = addNeutralAction();
	if (action == nullptr) { return false; }
	for (QToolBar* bar : getGUI()->songEditor()->findChildren<QToolBar*>())
	{
		auto* button = qobject_cast<QAbstractButton*>(bar->widgetForAction(action));
		if (button != nullptr && button->isVisible())
		{
			QTest::mouseClick(button, Qt::LeftButton);
			QCoreApplication::processEvents();
			return true;
		}
	}
	return false;
}

} // namespace

class HybridTrackGuiTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		ControlRegistry::setReady(true);
		getGUI()->mainWindow()->show();
		getGUI()->songEditor()->parentWidget()->show();
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void init()
	{
		Engine::getSong()->clearProject();
		QCoreApplication::processEvents();
	}

	//! The control exists, is visible, and clicking it creates a neutral row, drawn, with the agent's id.
	void clickingTheControlAddsADrawnNeutralRow()
	{
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("instrument")}}).ok);
		QCoreApplication::processEvents();
		QVERIFY2(addNeutralAction() != nullptr, "the Song Editor has no Add neutral track action");
		QCOMPARE(addNeutralAction()->data().toString(), QStringLiteral("track.add"));

		const int before = rows().size();
		QVERIFY2(clickAddNeutral(), "the Add neutral track button is not visible on the toolbar");
		QCOMPARE(rows().size(), before + 1);
		QCOMPARE(listed().size(), before + 1);

		TrackView* row = rows().last();
		QVERIFY2(dynamic_cast<NeutralTrackView*>(row) != nullptr, "the new row is not a neutral row");
		auto* track = dynamic_cast<NeutralTrack*>(row->getTrack());
		QVERIFY(track != nullptr);
		const QJsonObject entry = listed().last().toObject();
		QCOMPARE(entry.value(QStringLiteral("id")).toString(), control::trackIdOf(track));
		QCOMPARE(entry.value(QStringLiteral("type")).toString(), QStringLiteral("neutral"));
		QCOMPARE(entry.value(QStringLiteral("index")).toInt(), before);
		QVERIFY(row->isVisible());
	}

	//! Two clicks make two rows, in order, with distinct stable ids.
	void eachClickMakesAnotherRowWithItsOwnId()
	{
		QVERIFY(clickAddNeutral());
		QVERIFY(clickAddNeutral());
		QCOMPARE(listed().size(), 2);
		const QString a = listed().at(0).toObject().value(QStringLiteral("id")).toString();
		const QString b = listed().at(1).toObject().value(QStringLiteral("id")).toString();
		QVERIFY(a != b);
		QCOMPARE(control::trackIdOf(rows().at(0)->getTrack()), a);
		QCOMPARE(control::trackIdOf(rows().at(1)->getTrack()), b);
	}

	//! One click is one undo step: control.undo takes the row (and its view) away, control.redo draws it again.
	void aClickIsOneUndoStep()
	{
		const int depth = run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
		QVERIFY(clickAddNeutral());
		QCOMPARE(run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt(), depth + 1);
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCoreApplication::processEvents();
		QCOMPARE(listed().size(), 0);
		QCOMPARE(rows().size(), 0);
		QVERIFY(run(QStringLiteral("control.redo")).ok);
		QCoreApplication::processEvents();
		QCOMPARE(listed().size(), 1);
		QCOMPARE(rows().size(), 1);
		QVERIFY(dynamic_cast<NeutralTrackView*>(rows().first()) != nullptr);
	}

	//! After a save and a load the row is drawn again as a neutral row, with the same id and position.
	void theRowIsDrawnAgainAfterSaveAndLoad()
	{
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		QVERIFY(clickAddNeutral());
		const QJsonArray before = listed();
		QCOMPARE(before.size(), 2);

		const QString path = QDir(m_home.path()).filePath(QStringLiteral("neutral-gui.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path));
		Engine::getSong()->clearProject();
		QCoreApplication::processEvents();
		QCOMPARE(rows().size(), 0);
		Engine::getSong()->loadProject(path);
		QCoreApplication::processEvents();

		QCOMPARE(listed(), before);
		QCOMPARE(rows().size(), 2);
		QVERIFY(dynamic_cast<NeutralTrackView*>(rows().at(1)) != nullptr);
		QCOMPARE(control::trackIdOf(rows().at(1)->getTrack()),
			before.at(1).toObject().value(QStringLiteral("id")).toString());
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(HybridTrackGuiTest)
#include "HybridTrackGuiTest.moc"
