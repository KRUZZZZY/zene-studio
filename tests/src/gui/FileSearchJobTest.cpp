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
		QVERIFY(QMetaObject::invokeMethod(search, "textEdited", Qt::DirectConnection,
			Q_ARG(QString, QStringLiteral("no-such-result"))));

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

		QVERIFY(QMetaObject::invokeMethod(&browser, "onSearchMatch", Qt::QueuedConnection,
			Q_ARG(quint64, quint64{0}), Q_ARG(QString, stalePath)));
		QCoreApplication::sendPostedEvents(&browser, QEvent::MetaCall);

		QCOMPARE(results->topLevelItemCount(), 0);
	}
};

QTEST_MAIN(FileSearchJobTest)
#include "FileSearchJobTest.moc"
