/*
 * ProjectContainerSaveAsGuiTest.cpp - Q050-007 review round 2: the VISIBLE v2 choice, driven the
 *                                     way a person drives it - the File menu entry opens the real
 *                                     Save As dialog, its checkbox is read and clicked as a widget,
 *                                     the dialog is accepted - and the result is checked on disk and
 *                                     in the registry's transaction log (the same project.save
 *                                     command the agent surface calls).
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

#include <QtTest>

#include <QAction>
#include <QFile>
#include <QJsonArray>
#include <QMenuBar>
#include <QTimer>

#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "LedCheckBox.h"
#include "MainWindow.h"
#include "ProjectContainer.h"
#include "Song.h"
#include "VersionedSaveDialog.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QByteArray readBytes(const QString& path)
{
	QFile file(path);
	return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

//! The File menu's action with this text, found by walking the real menu bar.
QAction* fileMenuAction(const QString& text)
{
	for (QAction* top : getGUI()->mainWindow()->menuBar()->actions())
	{
		if (top->menu() == nullptr) { continue; }
		for (QAction* action : top->menu()->actions())
		{
			if (action->text().remove(QLatin1Char('&')) == text) { return action; }
		}
	}
	return nullptr;
}

//! What the person sees and does in the open Save As dialog.
struct DialogScript
{
	QString file;            //!< the name they type
	bool clickBox = false;   //!< whether they click the v2 checkbox
	bool boxStartedTicked = false;
	QString boxText;
	bool boxEndedTicked = false;
	bool seen = false;
};

//! Runs while the dialog's own event loop is up: finds it, reads the checkbox, clicks it if told
//! to, types the file name and accepts.
void driveDialog(DialogScript* script)
{
	auto* dialog = qobject_cast<VersionedSaveDialog*>(QApplication::activeModalWidget());
	if (dialog == nullptr) { QTimer::singleShot(20, [script] { driveDialog(script); }); return; }
	script->seen = true;
	LedCheckBox* v2 = nullptr;
	for (LedCheckBox* box : dialog->findChildren<LedCheckBox*>())
	{
		if (box->text().contains(QStringLiteral("v2"))) { v2 = box; }
	}
	if (v2 != nullptr)
	{
		script->boxText = v2->text();
		script->boxStartedTicked = v2->model()->value();
		if (script->clickBox) { QTest::mouseClick(v2, Qt::LeftButton, Qt::NoModifier, QPoint(4, v2->height() / 2)); }
		script->boxEndedTicked = v2->model()->value();
	}
	dialog->selectFile(script->file);
	QMetaObject::invokeMethod(dialog, "accept");
}

} // namespace

class ProjectContainerSaveAsGuiTest : public QObject
{
	Q_OBJECT
private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! Save As, the box left alone: the dialog offers the box unticked and writes the legacy blob.
	void saveAsLeavesTheBoxUntickedAndWritesLegacy()
	{
		QAction* saveAs = fileMenuAction(QStringLiteral("Save As..."));
		QVERIFY(saveAs != nullptr);
		DialogScript script;
		script.file = m_home.filePath(QStringLiteral("plain.mmpz"));
		QTimer::singleShot(0, [&script] { driveDialog(&script); });
		saveAs->trigger();
		QVERIFY(script.seen);
		QVERIFY2(script.boxText.contains(QStringLiteral("larger")) && script.boxText.contains(QStringLiteral("older")),
			qPrintable(script.boxText));
		QVERIFY(!script.boxStartedTicked);
		QVERIFY(!projectcontainer::isContainer(readBytes(script.file)));
		QCOMPARE(projectcontainer::shapeOf(readBytes(script.file)), projectcontainer::Shape::LegacyCompressed);
	}

	//! Save As with the box CLICKED, on a fresh project: a container, written by the registered
	//! project.save command (its transaction is in the log), and the project is a v2 project.
	void clickingTheBoxWritesAContainerThroughProjectSave()
	{
		Engine::getSong()->clearProject();
		const int before = ControlRegistry::instance()->invoke(QStringLiteral("control.transactions"), {})
			.result.value(QStringLiteral("count")).toInt();
		QAction* saveAs = fileMenuAction(QStringLiteral("Save As..."));
		QVERIFY(saveAs != nullptr);
		DialogScript script;
		script.file = m_home.filePath(QStringLiteral("clicked.mmpz"));
		script.clickBox = true;
		QTimer::singleShot(0, [&script] { driveDialog(&script); });
		saveAs->trigger();
		QVERIFY(script.seen);
		QVERIFY(!script.boxStartedTicked);
		QVERIFY(script.boxEndedTicked);
		QVERIFY(projectcontainer::isContainer(readBytes(script.file)));
		QVERIFY(Engine::getSong()->isContainerV2Project());
		QCOMPARE(Engine::getSong()->projectFileName(), script.file);

		const QJsonObject log = ControlRegistry::instance()->invoke(QStringLiteral("control.transactions"), {}).result;
		QVERIFY2(log.value(QStringLiteral("count")).toInt() > before, "the GUI save left no transaction: it bypassed project.save");
		bool found = false;
		for (const QJsonValue& value : log.value(QStringLiteral("transactions")).toArray())
		{
			const QJsonObject tx = value.toObject();
			found = found || (tx.value(QStringLiteral("command")).toString() == QLatin1String("project.save")
				&& tx.value(QStringLiteral("before")).toObject().value(QStringLiteral("file")).toString() == script.file
				&& tx.value(QStringLiteral("class")).toString() == QLatin1String("snapshot"));
		}
		QVERIFY2(found, "no project.save transaction for the saved file in the log");
	}

	//! The "Save as v2 Container..." entry opens the same dialog with the box ALREADY ticked.
	void theMenuEntryOpensTheDialogTicked()
	{
		Engine::getSong()->clearProject();
		QAction* entry = fileMenuAction(QStringLiteral("Save as v2 Container..."));
		QVERIFY(entry != nullptr);
		DialogScript script;
		script.file = m_home.filePath(QStringLiteral("entry.mmpz"));
		QTimer::singleShot(0, [&script] { driveDialog(&script); });
		entry->trigger();
		QVERIFY(script.seen);
		QVERIFY(script.boxStartedTicked);
		QVERIFY(projectcontainer::isContainer(readBytes(script.file)));
	}

	//! A v2 project's own Save As offers the box ticked, and UNticking it by click is the way back.
	void unclickingTheBoxGoesBackToLegacy()
	{
		QVERIFY(Engine::getSong()->isContainerV2Project());
		QAction* saveAs = fileMenuAction(QStringLiteral("Save As..."));
		DialogScript script;
		script.file = m_home.filePath(QStringLiteral("back.mmpz"));
		script.clickBox = true;
		QTimer::singleShot(0, [&script] { driveDialog(&script); });
		saveAs->trigger();
		QVERIFY(script.seen);
		QVERIFY(script.boxStartedTicked);
		QVERIFY(!script.boxEndedTicked);
		QCOMPARE(projectcontainer::shapeOf(readBytes(script.file)), projectcontainer::Shape::LegacyCompressed);
		QVERIFY(!Engine::getSong()->isContainerV2Project());
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(ProjectContainerSaveAsGuiTest)
#include "ProjectContainerSaveAsGuiTest.moc"
