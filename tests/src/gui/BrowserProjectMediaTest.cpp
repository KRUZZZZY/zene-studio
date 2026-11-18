/*
 * BrowserProjectMediaTest.cpp - Q050-121 through the Search view: Project Media
 *                               shows a usage count per file, the Unused view
 *                               keeps missing and external references visibly
 *                               distinct, and Clean Up previews the selection,
 *                               asks, and removes only the still-unused files.
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

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTreeWidget>

#include "BrowserResults.h"
#include "BrowserSearchView.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "Song.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

bool writeBytes(const QString& path, const QByteArray& bytes)
{
	QDir().mkpath(QFileInfo(path).absolutePath());
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	return file.write(bytes) == bytes.size();
}

QString quoted(const QString& path)
{
	return QString(path).replace(QLatin1Char('&'), QStringLiteral("&amp;"))
		.replace(QLatin1Char('<'), QStringLiteral("&lt;")).replace(QLatin1Char('"'), QStringLiteral("&quot;"));
}

QTreeWidgetItem* rowWithState(QTreeWidget* results, const QString& state)
{
	for (int row = 0; row < results->topLevelItemCount(); ++row)
	{
		if (results->topLevelItem(row)->text(browserresults::State) == state) { return results->topLevelItem(row); }
	}
	return nullptr;
}

QTreeWidgetItem* rowNamed(QTreeWidget* results, const QString& name)
{
	for (int row = 0; row < results->topLevelItemCount(); ++row)
	{
		if (results->topLevelItem(row)->text(browserresults::Name) == name) { return results->topLevelItem(row); }
	}
	return nullptr;
}

} // namespace

class BrowserProjectMediaTest : public QObject
{
	Q_OBJECT

private slots:

	void initTestCase()
	{
		Engine::init(true);
		ControlRegistry::setReady(true);
		QVERIFY2(m_dir.isValid(), "no temporary directory for the project-media fixtures");
		const QString media = QDir(m_dir.path()).filePath(QStringLiteral("media"));
		QVERIFY(QDir().mkpath(media));
		m_used = media + QStringLiteral("/used.wav");
		m_unused = media + QStringLiteral("/unused.wav");
		m_external = QDir(m_dir.path()).filePath(QStringLiteral("external.wav"));
		m_missing = media + QStringLiteral("/gone.wav");
		for (const QString& path : {m_used, m_unused, m_external}) { QVERIFY(writeBytes(path, QByteArray("RIFF....WAVEfmt "))); }
		m_project = QDir(m_dir.path()).filePath(QStringLiteral("song.mmp"));
		const QString xml = QStringLiteral(
			"<?xml version=\"1.0\"?>\n"
			"<zene-project creator=\"Zene Studio\" version=\"1.0\" type=\"song\" creatorversion=\"0.5.0\">\n"
			"  <song><trackcontainer type=\"song\"><track name=\"Audio\" type=\"0\">\n"
			"    <instrumenttrack><instrument name=\"audiofileprocessor\">\n"
			"      <audiofileprocessor src=\"%1\" amp=\"100\"/></instrument>\n"
			"      <sampleclip pos=\"0\" len=\"192\" src=\"%1\"/>\n"
			"      <sampleclip pos=\"192\" len=\"192\" src=\"%1\"/>\n"
			"      <sampleclip pos=\"384\" len=\"192\" src=\"%2\"/>\n"
			"      <sampleclip pos=\"576\" len=\"192\" src=\"%3\"/>\n"
			"    </instrumenttrack>\n"
			"  </track></trackcontainer></song>\n"
			"</zene-project>\n").arg(quoted(m_used), quoted(m_missing), quoted(m_external));
		QVERIFY(writeBytes(m_project, xml.toUtf8()));
	}

	void cleanupTestCase()
	{
		setBrowserMediaCleanupConfirm({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void projectMediaShowsUsageAndDistinctStates()
	{
		Song* song = Engine::getSong();
		const QString previous = song->projectFileName();
		song->noteProjectSaved(m_project);

		BrowserSearchView view(nullptr);
		view.showProjectMedia();
		QTreeWidget* results = view.results();
		QCOMPARE(view.resultCount(), 4);  // used, missing, external, unused

		QTreeWidgetItem* used = rowNamed(results, QStringLiteral("used.wav"));
		QVERIFY(used != nullptr);
		QCOMPARE(used->text(browserresults::Usage), QStringLiteral("3"));
		QCOMPARE(used->text(browserresults::State), QStringLiteral("used"));

		QVERIFY(rowWithState(results, QStringLiteral("missing")) != nullptr);
		QVERIFY(rowWithState(results, QStringLiteral("external")) != nullptr);
		QVERIFY(rowWithState(results, QStringLiteral("unused")) != nullptr);

		// The Unused view keeps the missing and external references visible so a
		// user can tell "never referenced" from "cannot find" and "elsewhere".
		auto* unusedButton = view.findChild<QPushButton*>(QStringLiteral("browserMediaUnused"));
		QVERIFY(unusedButton != nullptr);
		unusedButton->setChecked(true);
		QCOMPARE(view.resultCount(), 3);  // unused, missing, external (the used row is gone)
		QVERIFY(rowWithState(results, QStringLiteral("missing")) != nullptr);
		QVERIFY(rowWithState(results, QStringLiteral("external")) != nullptr);
		QVERIFY(rowWithState(results, QStringLiteral("used")) == nullptr);

		song->noteProjectSaved(previous);
	}

	void cleanupPreviewsTheSelectionAndCancelChangesNothing()
	{
		Song* song = Engine::getSong();
		const QString previous = song->projectFileName();
		song->noteProjectSaved(m_project);

		BrowserSearchView view(nullptr);
		view.showProjectMedia();
		QTreeWidget* results = view.results();
		QTreeWidgetItem* unused = rowWithState(results, QStringLiteral("unused"));
		QVERIFY(unused != nullptr);
		unused->setSelected(true);

		QStringList previewed;
		setBrowserMediaCleanupConfirm([&previewed](const QStringList& paths) {
			previewed = paths;
			return false;  // Cancel
		});
		auto* cleanup = view.findChild<QPushButton*>(QStringLiteral("browserMediaCleanup"));
		QVERIFY(cleanup != nullptr);
		cleanup->click();
		QCOMPARE(previewed, QStringList{QFileInfo(m_unused).absoluteFilePath()});
		QVERIFY2(QFileInfo::exists(m_unused), "Cancel removed a file");

		setBrowserMediaCleanupConfirm([](const QStringList&) { return true; });
		unused->setSelected(true);
		cleanup->click();
		QVERIFY2(!QFileInfo::exists(m_unused), "Confirm did not remove the unused file");
		QVERIFY2(QFileInfo::exists(m_used), "Confirm removed a referenced file");
		QVERIFY(QFileInfo::exists(m_external));
		QVERIFY(rowWithState(results, QStringLiteral("missing")) != nullptr);  // still a reference, not a candidate

		setBrowserMediaCleanupConfirm({});
		song->noteProjectSaved(previous);
	}

private:
	QTemporaryDir m_dir;
	QString m_used;
	QString m_unused;
	QString m_external;
	QString m_missing;
	QString m_project;
};

QTEST_MAIN(BrowserProjectMediaTest)
#include "BrowserProjectMediaTest.moc"
