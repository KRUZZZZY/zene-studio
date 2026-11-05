/*
 * RevisionsDocsTest.cpp - the documented Revisions list, against the menu and revisions.list
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
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
/*! Q050-438: docs/KNOWN-LIMITATIONS.md used to claim the revision timeline had no interface, but the
 *  tree has one: Project tools ▸ Revisions... over revisions.list. The test reads both user documents,
 *  drives the menu entry the manual names, and holds the live list's columns, actions and sources
 *  against the prose, so a changed menu, list source or action fails the documentation check. */

#include <QtTest>

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QMenu>
#include <QPushButton>
#include <QRegularExpression>
#include <QTemporaryDir>

#include "AudioDevice.h"
#include "AudioEngine.h"
#include "CommandListDialog.h"
#include "CommandLists.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiApplication.h"
#include "GuiTestApplication.h"
#include "ListTable.h"
#include "ShellCommandMenus.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

//! A minimal, real project document; the list only measures and hashes it.
QByteArray documentWithNotes(const int notes)
{
	QByteArray xml = "<?xml version=\"1.0\"?>\n<song version=\"1.2.0\">\n"
		"\t<track type=\"instrument\" name=\"lead\">\n\t\t<clip>\n";
	for (int i = 0; i < notes; ++i)
	{
		xml += "\t\t\t<note key=\"" + QByteArray::number(60 + i) + "\" pos=\"0\"/>\n";
	}
	xml += "\t\t</clip>\n\t</track>\n</song>\n";
	return xml;
}

bool writeBytes(const QString& path, const QByteArray& bytes)
{
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	return file.write(bytes) == bytes.size();
}

//! A file of the source tree (the test binary runs from <build>/tests), or empty when unreachable.
QString readDoc(const QString& relative)
{
	for (const char* up : {"/../../", "/../../../"})
	{
		QFile file(QCoreApplication::applicationDirPath() + QString::fromLatin1(up) + relative);
		if (file.open(QIODevice::ReadOnly | QIODevice::Text)) { return QString::fromUtf8(file.readAll()); }
	}
	return QString();
}

//! A document as one readable line: HTML entities decoded and every run of whitespace collapsed.
QString flattened(QString text)
{
	text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
	text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
	text.replace(QRegularExpression(QStringLiteral("\\s+")), QStringLiteral(" "));
	return text;
}

QAction* actionNamed(QMenu* menu, const QString& name)
{
	for (QAction* action : menu->actions())
	{
		if (action->text() == name) { return action; }
	}
	return nullptr;
}

QPushButton* buttonNamed(QWidget* parent, const QString& name)
{
	for (QPushButton* button : parent->findChildren<QPushButton*>())
	{
		if (button->text() == name) { return button; }
	}
	return nullptr;
}

CommandListDialog* visibleListDialog()
{
	for (QWidget* widget : QApplication::topLevelWidgets())
	{
		auto* dialog = qobject_cast<CommandListDialog*>(widget);
		if (dialog != nullptr && dialog->isVisible()) { return dialog; }
	}
	return nullptr;
}

} // namespace

class RevisionsDocsTest : public QObject
{
	Q_OBJECT

private:
	QTemporaryDir m_home;
	QTemporaryDir m_dir;
	QString m_project;

	QString path(const QString& name) const
	{
		return m_dir.path() + QLatin1Char('/') + name;
	}

	//! The artefacts revisions.list reports: a project, a .bak, a .rev0 and an autosave with its sidecar.
	void buildFixture()
	{
		m_project = path(QStringLiteral("song.mmp"));
		QVERIFY(writeBytes(m_project, documentWithNotes(3)));
		QVERIFY(writeBytes(path(QStringLiteral("song.mmp.bak")), documentWithNotes(1)));
		QVERIFY(writeBytes(path(QStringLiteral("song.mmp.rev0")), documentWithNotes(2)));
		QVERIFY(writeBytes(path(QStringLiteral("recover.mmp")), documentWithNotes(4)));
		QVERIFY(writeBytes(path(QStringLiteral("recover.mmp.info")),
			QByteArray("# zene-studio autosave identity - a side file, never part of a project\n"
				"version=1\nproject=") + m_project.toUtf8()
				+ "\nsavedUTC=2026-11-05T10:00:00Z\n"));
	}

private slots:
	void initTestCase()
	{
		QVERIFY(m_home.isValid());
		QVERIFY(m_dir.isValid());
		buildFixture();
		qputenv("ZENE_RECOVERY_FILE", path(QStringLiteral("recover.mmp")).toUtf8());
		guitest::startGui(m_home);
		QVERIFY(getGUI() != nullptr);
		Engine::audioEngine()->audioDev()->stopProcessing();
		ControlRegistry::setReady(true);
		// The list reads the session's own file; record the fixture as this session's saved project.
		Engine::getSong()->noteProjectSaved(m_project);
	}

	void cleanupTestCase()
	{
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The stale "no interface" claim is gone, and both documents name the menu, the actions and the sources.
	void theDocumentsNameTheProjectToolsListAndItsSources()
	{
		const QString limitations = readDoc(QStringLiteral("docs/KNOWN-LIMITATIONS.md"));
		const QString manual = readDoc(QStringLiteral("data/manual/index.html"));
		QVERIFY2(!limitations.isEmpty(), "docs/KNOWN-LIMITATIONS.md is not reachable from the build directory");
		QVERIFY2(!manual.isEmpty(), "data/manual/index.html is not reachable from the build directory");
		const QString note = flattened(limitations);
		const QString page = flattened(manual);

		// The obsolete claim was present tense; it must not survive in any form.
		QVERIFY2(!note.contains(QStringLiteral("the revision timeline is drivable through the socket, not from the interface")),
			"KNOWN-LIMITATIONS still says the revision timeline is not drivable from the interface");
		QVERIFY2(!note.contains(QStringLiteral("no revision panel")),
			"KNOWN-LIMITATIONS still says there is no revision panel");
		QVERIFY2(!note.contains(QStringLiteral("UI absence — one line: the revision timeline")),
			"KNOWN-LIMITATIONS still carries the revision UI-absence claim");

		// The entry point, as the header button and the Projects menu call it - not the old route.
		QVERIFY2(page.contains(QStringLiteral("Project tools > Revisions...")),
			"the manual does not name Project tools > Revisions...");
		QVERIFY2(!page.contains(QStringLiteral("Project > Revisions")),
			"the manual still names the obsolete Project > Revisions route");
		QVERIFY2(note.contains(QStringLiteral("Project tools ▸ Revisions...")),
			"KNOWN-LIMITATIONS does not name Project tools ▸ Revisions...");
		QVERIFY2(note.contains(QStringLiteral("existing files"))
			&& note.contains(QStringLiteral("existing repository history")),
			"KNOWN-LIMITATIONS conflates Zene-written revisions with repository Git entries");
		QVERIFY2(!page.contains(QStringLiteral("writes beside the project")),
			"the manual incorrectly locates every revision source beside the project");
		QVERIFY2(!note.contains(QStringLiteral("each with its `.info` sidecar's recorded time")),
			"KNOWN-LIMITATIONS incorrectly gives recover.mmp.bak the sidecar timestamp");
		QVERIFY2(page.contains(QStringLiteral("savedUTC")), "the manual omits the current autosave's sidecar time");
		QVERIFY2(note.contains(QStringLiteral("savedUTC")), "KNOWN-LIMITATIONS omits the current autosave's sidecar time");
		QVERIFY2(page.contains(QStringLiteral("timed by its file timestamp")),
			"the manual does not distinguish recover.mmp.bak's timestamp from the sidecar time");
		QVERIFY2(note.contains(QStringLiteral("timed by its file timestamp")),
			"KNOWN-LIMITATIONS does not distinguish recover.mmp.bak's timestamp from the sidecar time");

		// The two row actions the list offers.
		for (const char* action : {"Restore", "Compare With Now"})
		{
			QVERIFY2(page.contains(QLatin1String(action)), action);
			QVERIFY2(note.contains(QLatin1String(action)), action);
		}

		// The sources revisions.list reports, and the distinction from the separate file flows.
		for (const char* source : {"rotation", "backup", "autosave", "git"})
		{
			QVERIFY2(page.contains(QLatin1String(source)), source);
			QVERIFY2(note.contains(QLatin1String(source)), source);
		}
		QVERIFY2(page.contains(QStringLiteral("Save as New Version")),
			"the manual does not keep numbered saves distinct from revisions");
		QVERIFY2(page.contains(QStringLiteral("Alternatives")),
			"the manual does not keep named alternatives distinct from revisions");

		// The row fields the list shows: id, source, UTC time and size.
		QVERIFY2(page.contains(QStringLiteral("id")) && page.contains(QStringLiteral("source"))
			&& page.contains(QStringLiteral("UTC")) && page.contains(QStringLiteral("size")),
			"the manual does not describe the list's id/source/UTC/size fields");
		QVERIFY2(note.contains(QStringLiteral("id")) && note.contains(QStringLiteral("source"))
			&& note.contains(QStringLiteral("UTC")) && note.contains(QStringLiteral("size")),
			"KNOWN-LIMITATIONS does not describe the list's id/source/UTC/size fields");

		// Compare is structural; restore changes the file but not the open session.
		QVERIFY2(page.contains(QStringLiteral("structural")), "the manual does not call Compare a structural summary");
		QVERIFY2(page.contains(QStringLiteral("not a musical diff")), "the manual does not deny that Compare is a musical diff");
		QVERIFY2(page.contains(QStringLiteral("does not reload")), "the manual does not say Restore leaves the session alone");
		QVERIFY2(note.contains(QStringLiteral("not a semantic diff")), "KNOWN-LIMITATIONS lost the structural-comparison limit");
		QVERIFY2(note.contains(QStringLiteral("does not reload")), "KNOWN-LIMITATIONS does not say Restore leaves the session alone");
	}

	//! The menu entry opens the live list, and its columns, actions and sources are the documented ones.
	void theDocumentsMatchTheMenuAndTheLiveList()
	{
		const ListSpec spec = CommandLists::revisions();
		QCOMPARE(spec.title, QStringLiteral("Revisions"));
		QCOMPARE(spec.listId, QStringLiteral("revisions.list"));
		QCOMPARE(spec.arrayKey, QStringLiteral("revisions"));
		QCOMPARE(spec.columns, QStringList({QStringLiteral("id"), QStringLiteral("source"),
			QStringLiteral("timestamp"), QStringLiteral("bytes")}));
		QVERIFY(spec.listArgs.value(QStringLiteral("include_git")).toBool());
		QCOMPARE(spec.actions.size(), 2);
		QCOMPARE(spec.actions[0].label, QStringLiteral("Restore"));
		QCOMPARE(spec.actions[0].id, QStringLiteral("revisions.restore"));
		QVERIFY(spec.actions[0].confirm);
		QCOMPARE(spec.actions[1].label, QStringLiteral("Compare With Now"));
		QCOMPARE(spec.actions[1].id, QStringLiteral("revisions.compare"));
		QVERIFY(spec.actions[1].showResult);

		// The manual's route is the real Project tools menu entry and it declares the list's own command.
		QMenu projectMenu;
		ShellCommandMenus::populateProject(&projectMenu);
		QAction* entry = actionNamed(&projectMenu, QStringLiteral("Revisions..."));
		QVERIFY2(entry != nullptr, "Project tools has no Revisions... entry");
		QCOMPARE(entry->property("controlCommand").toString(), spec.listId);
		entry->trigger();
		CommandListDialog* dialog = visibleListDialog();
		QVERIFY2(dialog != nullptr, "Revisions... opened no list dialog");

		const QJsonObject listed = dialog->listResult();
		const int count = listed.value(QStringLiteral("count")).toInt();
		QVERIFY2(count >= 3, "the fixture's rotation, backup and autosave were not all listed");
		QCOMPARE(dialog->rowCount(), count);
		const QJsonObject sources = listed.value(QStringLiteral("sources")).toObject();
		for (const char* source : {"rotation", "backup", "autosave"})
		{
			QVERIFY2(sources.contains(QLatin1String(source)), source);
		}

		// Every documented source the live list reports is named by both documents.
		const QString page = flattened(readDoc(QStringLiteral("data/manual/index.html")));
		const QString note = flattened(readDoc(QStringLiteral("docs/KNOWN-LIMITATIONS.md")));
		for (auto it = sources.begin(); it != sources.end(); ++it)
		{
			QVERIFY2(page.contains(it.key()), qPrintable(it.key()));
			QVERIFY2(note.contains(it.key()), qPrintable(it.key()));
		}

		// Every row carries the columns the list shows, and rev0 names its source.
		bool sawRotation = false;
		for (int i = 0; i < dialog->rowCount(); ++i)
		{
			const QJsonObject row = dialog->row(i);
			for (const QString& column : spec.columns)
			{
				QVERIFY2(row.contains(column), qPrintable(column));
			}
			if (row.value(QStringLiteral("id")).toString() == QStringLiteral("rev0"))
			{
				sawRotation = true;
				QCOMPARE(row.value(QStringLiteral("source")).toString(), QStringLiteral("rotation"));
				QVERIFY(!row.value(QStringLiteral("timestamp")).toString().isEmpty());
				QVERIFY(row.value(QStringLiteral("bytes")).toInt() > 0);
			}
		}
		QVERIFY2(sawRotation, "the list did not report the fixture's rev0");

		// Compare With Now is a real row button, and it runs the documented command on the selected row.
		int rotationRow = -1;
		for (int i = 0; i < dialog->rowCount(); ++i)
		{
			if (dialog->row(i).value(QStringLiteral("id")).toString() == QStringLiteral("rev0")) { rotationRow = i; }
		}
		QVERIFY(rotationRow >= 0);
		dialog->table()->selectDataRow(rotationRow);
		QPushButton* compare = buttonNamed(dialog, QStringLiteral("Compare With Now"));
		QVERIFY2(compare != nullptr, "the Revisions list has no Compare With Now button");
		QVERIFY2(compare->isEnabled(), "Compare With Now is not available for a selected row");
		QTest::mouseClick(compare, Qt::LeftButton);
		QVERIFY2(dialog->resultsText().contains(QStringLiteral("Compare With Now")),
			qPrintable(dialog->resultsText()));

		dialog->close();
		QCoreApplication::processEvents();
	}
};

QTEST_MAIN(RevisionsDocsTest)
#include "RevisionsDocsTest.moc"
