/*
 * HybridTrackRoleGuiTest.cpp - a neutral row's Actions menu assigns an instrument, and back (Q050-283.2)
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

/*! 0.5.0 L04, Q050-283.2 (R-workflow-286), in the real application: the row's own Actions menu - built
 *  the way the product builds it, on aboutToShow - offers "Assign instrument" on a neutral row and
 *  "Make neutral (keep instrument)" on an instrument track. Triggering the items (not calling the
 *  handler) runs track.set_role, the command an agent runs: the row is redrawn as an instrument row at
 *  the same place with the same id, one undo step; made neutral it is drawn as a neutral row whose
 *  menu offers "Restore", which gives the instrument and its clip back - also after save and load. */

#include <QtTest>

#include <QAction>
#include <QDir>
#include <QJsonArray>
#include <QMenu>
#include <QPushButton>
#include <QTemporaryDir>

#include "ControlRegistry.h"
#include "ControlVocabulary.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Instrument.h"
#include "InstrumentTrack.h"
#include "InstrumentTrackView.h"
#include "MainWindow.h"
#include "NeutralTrack.h"
#include "NeutralTrackView.h"
#include "Song.h"
#include "SongEditor.h"
#include "Track.h"
#include "TrackContainerView.h"
#include "TrackOperationsWidget.h"
#include "TrackView.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

int undoDepth()
{
	return run(QStringLiteral("control.undo_depth")).result.value(QStringLiteral("depth")).toInt();
}

const QList<TrackView*>& rows() { return getGUI()->songEditor()->m_editor->trackViews(); }

TrackView* rowOf(const QString& id)
{
	for (TrackView* view : rows())
	{
		if (control::trackIdOf(view->getTrack()) == id) { return view; }
	}
	return nullptr;
}

int rowIndexOf(const QString& id)
{
	for (int i = 0; i < rows().size(); ++i)
	{
		if (control::trackIdOf(rows().at(i)->getTrack()) == id) { return i; }
	}
	return -1;
}

//! The action called @a name anywhere in @a menu, submenus included; nullptr when it is not offered.
QAction* findAction(QMenu* menu, const QString& name)
{
	for (QAction* action : menu->actions())
	{
		if (action->objectName() == name) { return action; }
		if (action->menu() != nullptr)
		{
			if (QAction* inner = findAction(action->menu(), name)) { return inner; }
		}
	}
	return nullptr;
}

/*! Opens the row's Actions menu the way the product does (aboutToShow builds it), triggers the item
 *  called @a name, and lets the queued command run. False when the row or the item is not there. */
bool pick(const QString& trackId, const QString& name)
{
	TrackView* view = rowOf(trackId);
	if (view == nullptr) { return false; }
	auto* operations = view->findChild<TrackOperationsWidget*>();
	if (operations == nullptr) { return false; }
	QMenu* menu = nullptr;
	for (QPushButton* candidate : operations->findChildren<QPushButton*>())
	{
		if (candidate->menu() != nullptr) { menu = candidate->menu(); }
	}
	if (menu == nullptr) { return false; }
	emit menu->aboutToShow();
	QAction* action = findAction(menu, name);
	if (action == nullptr || !action->isEnabled()) { return false; }
	action->trigger();
	QCoreApplication::processEvents();
	QCoreApplication::processEvents();
	return true;
}

QString pluginOf(const QString& id)
{
	auto* track = dynamic_cast<InstrumentTrack*>(Engine::getSong()->findTrackById(id.mid(4).toInt()));
	return track != nullptr && track->instrument() != nullptr
		? QString::fromUtf8(track->instrument()->descriptor()->name) : QString();
}

} // namespace

class HybridTrackRoleGuiTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
#ifdef LMMS_TEST_PLUGIN_DIR
		qputenv("LMMS_PLUGIN_DIR", LMMS_TEST_PLUGIN_DIR);
#endif
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

	//! AC1 in the GUI: the neutral row's "Assign instrument" item makes it an instrument row, same id, same place.
	void assignInstrumentFromTheRowMenu()
	{
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		const QString row = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("neutral")},
			{QStringLiteral("name"), QStringLiteral("Idea")}}).result.value(QStringLiteral("track")).toString();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		QCoreApplication::processEvents();
		QVERIFY(dynamic_cast<NeutralTrackView*>(rowOf(row)) != nullptr);
		QCOMPARE(rowIndexOf(row), 1);
		const int depth = undoDepth();

		QVERIFY2(pick(row, QStringLiteral("assignInstrument_tripleoscillator")),
			"the neutral row's Actions menu offers no Assign instrument > TripleOscillator");
		QCOMPARE(undoDepth(), depth + 1);
		QVERIFY2(dynamic_cast<InstrumentTrackView*>(rowOf(row)) != nullptr, "the row is not drawn as an instrument row");
		QCOMPARE(rowIndexOf(row), 1);
		QCOMPARE(rows().size(), 3);
		QCOMPARE(pluginOf(row), QStringLiteral("tripleoscillator"));
		QCOMPARE(rowOf(row)->getTrack()->name(), QStringLiteral("Idea"));
		QCOMPARE(run(QStringLiteral("track.get_state"), {{QStringLiteral("track"), row}}).result
			.value(QStringLiteral("role")).toString(), QStringLiteral("instrument"));

		// One undo puts the neutral row back where it was.
		QVERIFY(run(QStringLiteral("control.undo")).ok);
		QCoreApplication::processEvents();
		QVERIFY(dynamic_cast<NeutralTrackView*>(rowOf(row)) != nullptr);
		QCOMPARE(rowIndexOf(row), 1);
	}

	//! AC2 in the GUI: "Make neutral" keeps the instrument role, "Restore" gives it back with its clip -
	//! also after the project is saved and loaded.
	void makeNeutralAndRestoreFromTheRowMenu()
	{
		const QString row = run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("neutral")},
			{QStringLiteral("name"), QStringLiteral("Idea")}}).result.value(QStringLiteral("track")).toString();
		QVERIFY(run(QStringLiteral("track.add"), {{QStringLiteral("type"), QStringLiteral("sample")}}).ok);
		QCoreApplication::processEvents();
		QVERIFY(pick(row, QStringLiteral("assignInstrument_tripleoscillator")));
		QVERIFY(run(QStringLiteral("clip.add"), {{QStringLiteral("track"), row}, {QStringLiteral("position"), 0},
			{QStringLiteral("length"), 192}}).ok);

		QVERIFY2(pick(row, QStringLiteral("makeNeutralAction")), "the instrument row's menu offers no Make neutral");
		QVERIFY(dynamic_cast<NeutralTrackView*>(rowOf(row)) != nullptr);
		QCOMPARE(rowIndexOf(row), 0);
		QCOMPARE(rowOf(row)->getTrack()->numOfClips(), 0);

		const QString path = QDir(m_home.path()).filePath(QStringLiteral("kept-role-gui.mmp"));
		QVERIFY(Engine::getSong()->saveProjectFile(path));
		Engine::getSong()->clearProject();
		QCoreApplication::processEvents();
		Engine::getSong()->loadProject(path);
		QCoreApplication::processEvents();
		QVERIFY(dynamic_cast<NeutralTrackView*>(rowOf(row)) != nullptr);

		const int depth = undoDepth();
		QVERIFY2(pick(row, QStringLiteral("restoreInstrumentRoleAction")), "the neutral row's menu offers no Restore");
		QCOMPARE(undoDepth(), depth + 1);
		QVERIFY(dynamic_cast<InstrumentTrackView*>(rowOf(row)) != nullptr);
		QCOMPARE(rowIndexOf(row), 0);
		QCOMPARE(pluginOf(row), QStringLiteral("tripleoscillator"));
		QCOMPARE(rowOf(row)->getTrack()->numOfClips(), 1);
		QCOMPARE(rowOf(row)->getTrack()->name(), QStringLiteral("Idea"));
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(HybridTrackRoleGuiTest)
#include "HybridTrackRoleGuiTest.moc"
