/*
 * FileSearchJobTest.cpp - stale queued matches do not enter the current search results
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
 * You should have received a copy of the GNU General Public License
 * along with this program (see COPYING); if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301 USA.
 */

#include <QtTest>

#include <QFile>
#include <QLineEdit>
#include <QTemporaryDir>
#include <QTreeWidget>

#include "FileBrowser.h"

using namespace lmms::gui;

class NewSearchAfterQueuedMatch final : public QObject
{
public:
	explicit NewSearchAfterQueuedMatch(QLineEdit* search)
		: m_search(search)
	{
	}

	bool eventFilter(QObject* watched, QEvent* event) override
	{
		Q_UNUSED(watched);
		if (event->type() == QEvent::MetaCall && ++m_metaCallCount == 2)
		{
			m_startedNewSearch = QMetaObject::invokeMethod(m_search, "textEdited", Qt::DirectConnection,
				Q_ARG(QString, QStringLiteral("no-such-result")));
		}
		return false;
	}

	bool startedNewSearch() const { return m_startedNewSearch; }

private:
	QLineEdit* m_search;
	int m_metaCallCount = 0;
	bool m_startedNewSearch = false;
};

class FileSearchJobTest : public QObject
{
	Q_OBJECT

private slots:
	void queuedResultFromAnOlderSearchIsDiscarded()
	{
		QTemporaryDir directory;
		QVERIFY(directory.isValid());

		const auto stalePath = directory.filePath(QStringLiteral("stale-result.wav"));
		QFile staleFile{stalePath};
		QVERIFY(staleFile.open(QIODevice::WriteOnly));
		QVERIFY(staleFile.write("fixture") == 7);

		FileBrowser browser{FileBrowser::Type::Normal, directory.path(), FileItem::defaultFilters(),
			QStringLiteral("Search regression"), QPixmap{}, nullptr};
		auto* search = browser.findChild<QLineEdit*>(QStringLiteral("browserSearch"));
		QVERIFY(search != nullptr);
		auto transition = NewSearchAfterQueuedMatch{search};
		browser.installEventFilter(&transition);
		QVERIFY(QMetaObject::invokeMethod(search, "textEdited", Qt::DirectConnection,
			Q_ARG(QString, QStringLiteral("stale-result"))));

		auto* results = static_cast<QTreeWidget*>(nullptr);
		for (auto* tree : browser.findChildren<QTreeWidget*>())
		{
			if (tree->property("browserResults").toBool() && !tree->isHidden())
			{
				results = tree;
				break;
			}
		}
		QVERIFY(results != nullptr);
		QCOMPARE(results->topLevelItemCount(), 0);

		// The second queued call to FileBrowser is the first search's foundMatch. Start a newer
		// search while that real queued result is waiting to reach onSearchMatch.
		QTRY_VERIFY(transition.startedNewSearch());

		QCOMPARE(results->topLevelItemCount(), 0);
	}
};

QTEST_MAIN(FileSearchJobTest)
#include "FileSearchJobTest.moc"
