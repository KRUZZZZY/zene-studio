/*
 * BrowserTagMenuTest.cpp - the file browser's Tags menu adds and removes a file's tags
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

/*! The Tags submenu over a throwaway tag store (BrowserTagStore::openAt - never the user's own):
 *  "New tag..." adds one through browser.tag.add, the next menu lists it checked, unchecking it
 *  removes it through browser.tag.remove, and a tag another file carries is listed unchecked. */

#include <QtTest>

#include <QFile>
#include <QMenu>
#include <QTemporaryDir>

#include "BrowserTagMenu.h"
#include "BrowserCatalog.h"
#include "ControlRegistry.h"
#include "Engine.h"

using namespace lmms;
using namespace lmms::gui;

namespace
{

QAction* named(QMenu& menu, const QString& text)
{
	for (QAction* top : menu.actions())
	{
		if (top->menu() == nullptr) { continue; }
		for (QAction* action : top->menu()->actions())
		{
			if (action->text() == text) { return action; }
		}
	}
	return nullptr;
}

QString touch(const QTemporaryDir& dir, const char* name)
{
	const QString path = dir.filePath(QString::fromLatin1(name));
	QFile file(path);
	file.open(QIODevice::WriteOnly);
	file.write("x");
	return path;
}

} // namespace

class BrowserTagMenuTest : public QObject
{
	Q_OBJECT

private slots:
	void initTestCase()
	{
		QVERIFY(m_dir.isValid());
		BrowserTagStore::instance().openAt(m_dir.filePath(QStringLiteral("tags.json")));
		Engine::init(true);
		ControlRegistry::setReady(true);
	}

	void cleanupTestCase()
	{
		setBrowserTagPrompt({});
		ControlRegistry::setReady(false);
		Engine::destroy();
	}

	void theMenuAddsAndRemovesTags()
	{
		const QString kick = touch(m_dir, "kick.wav");
		const QString snare = touch(m_dir, "snare.wav");
		setBrowserTagPrompt([] { return std::optional<QString>(QStringLiteral("drums")); });
		{
			QMenu menu;
			addBrowserTagMenu(&menu, kick);
			named(menu, QMenu::tr("New tag..."))->trigger();
		}
		QVERIFY(BrowserTagStore::instance().tagsOf(kick).contains(QStringLiteral("drums")));

		QMenu forKick;
		addBrowserTagMenu(&forKick, kick);
		QAction* drums = named(forKick, QStringLiteral("drums"));
		QVERIFY(drums != nullptr);
		QVERIFY(drums->isChecked());

		QMenu forSnare;
		addBrowserTagMenu(&forSnare, snare);
		QVERIFY(!named(forSnare, QStringLiteral("drums"))->isChecked());
		named(forSnare, QStringLiteral("drums"))->trigger();  // check: add
		QVERIFY(BrowserTagStore::instance().tagsOf(snare).contains(QStringLiteral("drums")));

		drums->trigger();  // uncheck: remove
		QVERIFY(!BrowserTagStore::instance().tagsOf(kick).contains(QStringLiteral("drums")));
	}

private:
	QTemporaryDir m_dir;
};

QTEST_MAIN(BrowserTagMenuTest)
#include "BrowserTagMenuTest.moc"
