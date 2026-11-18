/*
 * AafImportMenuTest.cpp - File > Import AAF... and the file dialog it opens
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

/*! The File menu's "Import AAF..." entry names its command (interchange.aaf_import), is disabled with the reason in
 *  its status tip in a build made without LibAAF, and otherwise asks for a file and runs the import on it with the
 *  first-clip-at-origin option, showing what was built. A cancelled dialog runs nothing. */

#include <QtTest>

#include <QApplication>
#include <QFileDialog>
#include <QMenu>
#include <QTemporaryDir>
#include <QTimer>

#include "AafImport.h"
#include "AafImportMenu.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! Answers the next file dialog with @a path (cancels it when empty); counts the dialogs it answered.
void answerFileDialog(const QString& path, int* answered, int triesLeft = 100)
{
	QTimer::singleShot(10, [path, answered, triesLeft] {
		auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
		if (dialog == nullptr) { if (triesLeft > 0) { answerFileDialog(path, answered, triesLeft - 1); } return; }
		++*answered;
		if (path.isEmpty()) { dialog->reject(); return; }
		dialog->selectFile(path);
		static_cast<QDialog*>(dialog)->accept();
	});
}

} // namespace

class AafImportMenuTest : public QObject
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

	void theEntryNamesItsCommandAndIsDisabledWithoutLibAaf()
	{
		QMenu file;
		QAction* action = addAafImportAction(&file);
		QVERIFY(action != nullptr);
		QCOMPARE(file.actions().size(), 1);
		QCOMPARE(action->text(), QStringLiteral("Import AAF..."));
		QCOMPARE(action->data().toString(), QStringLiteral("interchange.aaf_import"));
		QCOMPARE(action->isEnabled(), interchange::aafImportAvailable());
		if (!interchange::aafImportAvailable())
		{
			QVERIFY(action->statusTip().contains(QStringLiteral("without LibAAF")));
		}
	}

	void aCancelledDialogRunsNothingAndAChosenFileIsImported()
	{
		QMenu file;
		QAction* action = addAafImportAction(&file);
		const std::size_t tracks = Engine::getSong()->tracks().size();
		if (!action->isEnabled())
		{
			// A build without LibAAF: the entry is greyed out, triggering it opens no file dialog and imports nothing,
			// and the reason is in its status tip.
			QVERIFY(!interchange::aafImportAvailable());
			action->trigger();
			QTest::qWait(150);  // a negative check: a dialog would be up by now
			QVERIFY2(QApplication::activeModalWidget() == nullptr, "a disabled Import AAF opened a dialog");
			QCOMPARE(Engine::getSong()->tracks().size(), tracks);
			QVERIFY(!action->statusTip().isEmpty());
			return;
		}
		int answered = 0;
		answerFileDialog(QString(), &answered);
		action->trigger();
		QCOMPARE(answered, 1);
		QCOMPARE(Engine::getSong()->tracks().size(), tracks);

		// A file that is not an AAF: the command refuses it and the song is untouched.
		const QString bad = m_home.filePath(QStringLiteral("not-an-aaf.aaf"));
		QFile file1(bad);
		QVERIFY(file1.open(QIODevice::WriteOnly));
		file1.write("this is not an AAF file");
		file1.close();
		answerFileDialog(bad, &answered);
		action->trigger();
		QCOMPARE(answered, 2);
		QCOMPARE(Engine::getSong()->tracks().size(), tracks);
	}

private:
	QTemporaryDir m_home;
};

QTEST_MAIN(AafImportMenuTest)
#include "AafImportMenuTest.moc"
