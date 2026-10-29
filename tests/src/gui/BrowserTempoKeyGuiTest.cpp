/*
 * BrowserTempoKeyGuiTest.cpp - 0.5.0 L11 (Q050-316.1): the Search view's tempo
 *                              and key facets, driven as a musician drives them.
 *
 * The GUI half of the tempo/key filter, kept in its own executable because
 * LibraryMenusTest is at the file-length ratchet and the browser's facets already
 * have their own engine-side proof (BrowserTempoKeyTest). This file drives the
 * view's OWN controls - the two tempo spin boxes and the key combo - and reads
 * the results tree, then does the same in Whole library mode, so the acceptance
 * claim "the GUI filters by tempo and key and both search modes answer the same
 * files" is a claim about the widgets a musician clicks, not about a handler.
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
 * You should have received a copy of the GNU General Public License along with
 * this program (see COPYING); if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QComboBox>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLineEdit>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QTreeWidget>

#include "BrowserCatalog.h"
#include "BrowserIndex.h"
#include "BrowserResults.h"
#include "BrowserSearchView.h"
#include "ConfigManager.h"
#include "ControlRegistry.h"
#include "Engine.h"
#include "GuiTestApplication.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

ControlResult run(const QString& id, const QJsonObject& args = {})
{
	return ControlRegistry::instance()->invoke(id, args);
}

//! A file the browser lists, written by us: the index store below is written by
//! hand, so the tempo/key values are exact and no detector is involved.
QString touchFile(const QDir& dir, const QString& name)
{
	const QString path = dir.filePath(name);
	QFile file(path);
	file.open(QIODevice::WriteOnly);
	file.write("x");
	return path;
}

//! One measured index record in the store's own format (BrowserIndex.cpp).
QJsonObject featureRecord(double bpm, int keyPitchClass)
{
	return {{QStringLiteral("dur"), 1.5}, {QStringLiteral("db"), -20.0}, {QStringLiteral("cent"), 1000.0},
		{QStringLiteral("zcr"), 100.0}, {QStringLiteral("bpm"), bpm}, {QStringLiteral("key"), keyPitchClass}};
}

QJsonObject indexEntry(const QString& path, bool measured, double bpm = 0.0, int keyPitchClass = -1)
{
	const QFileInfo info(path);
	QJsonObject entry{{QStringLiteral("p"), info.canonicalFilePath()}, {QStringLiteral("k"), QStringLiteral("audio")},
		{QStringLiteral("s"), static_cast<double>(info.size())},
		{QStringLiteral("m"), static_cast<double>(info.lastModified().toMSecsSinceEpoch())}};
	if (measured) { entry.insert(QStringLiteral("f"), featureRecord(bpm, keyPitchClass)); }
	return entry;
}

bool writeIndexStore(const QString& path, const QJsonArray& entries)
{
	const QJsonObject all{{QStringLiteral("version"), 1}, {QStringLiteral("built_ms"), 0.0},
		{QStringLiteral("truncated"), false}, {QStringLiteral("entries"), entries}};
	QFile file(path);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { return false; }
	const QByteArray bytes = QJsonDocument(all).toJson(QJsonDocument::Compact);
	return file.write(bytes) == bytes.size();
}

//! The file names the results tree shows, sorted.
QStringList shownNames(BrowserSearchView& view)
{
	QStringList names;
	for (int row = 0; row < view.results()->topLevelItemCount(); ++row)
	{
		names << view.results()->topLevelItem(row)->text(browserresults::Name);
	}
	names.sort();
	return names;
}

} // namespace

class BrowserTempoKeyGuiTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY2(m_dir.isValid(), "no temporary directory for the GUI browser fixtures");
		// A private home, config and working directory: the samples root, the tag
		// store, the index and the collections all live under the temporary tree.
		qputenv("ZENE_BROWSER_INDEX", m_dir.filePath(QStringLiteral("index.json")).toLocal8Bit());
		qputenv("ZENE_BROWSER_COLLECTIONS", m_dir.filePath(QStringLiteral("collections.json")).toLocal8Bit());
		guitest::startGui(m_dir);
		ConfigManager::inst()->setWorkingDir(m_dir.filePath(QStringLiteral("work")));
		QVERIFY(QDir().mkpath(ConfigManager::inst()->workingDir()));
		QVERIFY(BrowserTagStore::instance().openAt(m_dir.filePath(QStringLiteral("browser-tags.json"))));
		ControlRegistry::setReady(true);
		m_samples = QDir(ConfigManager::inst()->userSamplesDir());
		QVERIFY(QDir().mkpath(m_samples.absolutePath()));
		m_fast = touchFile(m_samples, QStringLiteral("tk-fast-120-a.wav"));
		m_slow = touchFile(m_samples, QStringLiteral("tk-slow-90-d.wav"));
		m_unknown = touchFile(m_samples, QStringLiteral("tk-unknown.wav"));
		QJsonArray entries;
		entries.append(indexEntry(m_fast, true, 120.0, 9));
		entries.append(indexEntry(m_slow, true, 90.0, 2));
		entries.append(indexEntry(m_unknown, true, 0.0, -1));
		QVERIFY2(writeIndexStore(qEnvironmentVariable("ZENE_BROWSER_INDEX"), entries),
			"the index store was not written");
		BrowserIndex::instance().reload();
	}

	void cleanupTestCase()
	{
		qunsetenv("ZENE_BROWSER_INDEX");
		qunsetenv("ZENE_BROWSER_COLLECTIONS");
		BrowserTagStore::instance().openAt(m_dir.filePath(QStringLiteral("done.json")));
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	//! The facets exist as controls beside the category filter, the key list is
	//! the shared pitch-class vocabulary, and an untouched row sends no filter.
	void theFacetsAreLiveControlsBesideTheCategoryFilter()
	{
		BrowserSearchView view(nullptr);
		QSpinBox* from = view.tempoMinFilter();
		QSpinBox* to = view.tempoMaxFilter();
		QComboBox* key = view.keyFilter();
		QVERIFY2(from != nullptr && to != nullptr && key != nullptr, "the Search view exposes no tempo/key facets");
		QCOMPARE(from->accessibleName(), QStringLiteral("Tempo from"));
		QCOMPARE(to->accessibleName(), QStringLiteral("Tempo to"));
		QCOMPARE(key->accessibleName(), QStringLiteral("Key"));
		QCOMPARE(from->objectName(), QStringLiteral("browserSearchTempoMin"));
		QCOMPARE(to->objectName(), QStringLiteral("browserSearchTempoMax"));
		QCOMPARE(key->objectName(), QStringLiteral("browserSearchKey"));
		QCOMPARE(from->value(), 0);  // 0 = open on that side, so an untouched row is no filter
		QCOMPARE(to->value(), 0);
		QCOMPARE(key->currentIndex(), 0);
		QCOMPARE(key->itemText(0), QStringLiteral("Any key"));
		QVERIFY(key->itemData(0).toString().isEmpty());
		// The list is browserKeyName's own vocabulary, in pitch-class order.
		QCOMPARE(key->count(), 13);
		for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
		{
			QCOMPARE(key->itemData(pitchClass + 1).toString(), browserKeyName(pitchClass));
		}
	}

	//! Typing a search and setting the tempo range and key narrows the list in
	//! walking mode, clearing them restores it, and Whole library answers the SAME
	//! files for the same facets - the acceptance case, through the GUI controls.
	void theFacetsNarrowBothQueryModesToTheSameFiles()
	{
		BrowserSearchView view(nullptr);
		QSpinBox* from = view.tempoMinFilter();
		QSpinBox* to = view.tempoMaxFilter();
		QComboBox* key = view.keyFilter();
		auto* field = view.findChild<QLineEdit*>(QStringLiteral("browserSearch"));
		QVERIFY(field != nullptr);
		QTest::keyClicks(field, QStringLiteral("tk-"));
		QTest::keyClick(field, Qt::Key_Return);
		QCOMPARE(view.resultCount(), 3);  // no facet: every fixture
		QCOMPARE(shownNames(view), (QStringList{QStringLiteral("tk-fast-120-a.wav"),
			QStringLiteral("tk-slow-90-d.wav"), QStringLiteral("tk-unknown.wav")}));

		from->setValue(100);
		to->setValue(130);
		QCOMPARE(shownNames(view), QStringList{QStringLiteral("tk-fast-120-a.wav")});
		from->setValue(0);
		to->setValue(0);
		QCOMPARE(view.resultCount(), 3);
		key->setCurrentIndex(key->findData(QStringLiteral("D")));
		QCOMPARE(shownNames(view), QStringList{QStringLiteral("tk-slow-90-d.wav")});
		key->setCurrentIndex(0);
		QCOMPARE(view.resultCount(), 3);

		// Whole library: the index query, driven from the same controls, answers
		// the same one file for the same tempo range.
		auto* indexed = view.findChild<QCheckBox*>(QStringLiteral("browserSearchIndexed"));
		QVERIFY(indexed != nullptr);
		indexed->setChecked(true);
		QCOMPARE(view.resultCount(), 3);
		from->setValue(100);
		to->setValue(130);
		QCOMPARE(shownNames(view), QStringList{QStringLiteral("tk-fast-120-a.wav")});
		from->setValue(0);
		to->setValue(0);
		key->setCurrentIndex(key->findData(QStringLiteral("A")));
		QCOMPARE(shownNames(view), QStringList{QStringLiteral("tk-fast-120-a.wav")});
		key->setCurrentIndex(0);
		QCOMPARE(view.resultCount(), 3);

		// An unknown tempo never matches an active range, in either mode: the third
		// fixture (measured, nothing found) is not in a 1-400 BPM answer.
		from->setValue(1);
		to->setValue(400);
		QCOMPARE(view.resultCount(), 2);
		indexed->setChecked(false);
		QCOMPARE(view.resultCount(), 2);
		from->setValue(0);
		to->setValue(0);
		QCOMPARE(view.resultCount(), 3);
	}

private:
	QTemporaryDir m_dir;
	QDir m_samples;
	QString m_fast;
	QString m_slow;
	QString m_unknown;
};

QTEST_MAIN(BrowserTempoKeyGuiTest)
#include "BrowserTempoKeyGuiTest.moc"
